//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

#include "omvll/ObfuscationConfig.hpp"
#include "omvll/PyConfig.hpp"
#include "omvll/log.hpp"
#include "omvll/passes/basic-block-duplicate/BasicBlockDuplicate.hpp"
#include "omvll/passes/basic-block-duplicate/BasicBlockDuplicateOpt.hpp"
#include "omvll/utils.hpp"

using namespace llvm;
using namespace omvll;

namespace omvll {

static constexpr auto LRand48FunctionName = "lrand48";
static constexpr unsigned MaxDuplicatedBlocksPerFunction = 500;

// A duplicated block is split in three: `Head` keeps the PHI nodes and hosts
// the coinflip, while the rest of the original block lives in two twins (the
// original tail and its clone)
static bool canBypassHead(const BasicBlock &Head, const BasicBlock &Twin) {
  if (!Head.phis().empty())
    return false;

  for (const Instruction &I : Twin)
    for (const Value *Op : I.operands())
      if (const auto *OpI = dyn_cast<Instruction>(Op))
        if (OpI->getParent() == &Head)
          return false;

  return true;
}

// Chain the cloned blocks together
static void redirectClonesToClonedSuccessors(
    ArrayRef<BasicBlock *> Clones,
    const DenseMap<BasicBlock *, BasicBlock *> &CloneOf) {
  for (BasicBlock *Clone : Clones) {
    Instruction *Term = Clone->getTerminator();

    // Successors of these terminators are tied to blockaddress operands or to
    // EH constraints, leave them alone
    if (isa<IndirectBrInst>(Term) || isa<CallBrInst>(Term))
      continue;

    for (unsigned Idx = 0, E = Term->getNumSuccessors(); Idx != E; ++Idx) {
      BasicBlock *Succ = Term->getSuccessor(Idx);
      if (Succ->isEHPad())
        continue;

      auto It = CloneOf.find(Succ);
      if (It == CloneOf.end())
        continue;

      BasicBlock *SuccClone = It->second;
      if (SuccClone == Clone || !canBypassHead(*Succ, *SuccClone))
        continue;

      // `Clone` becomes a predecessor of `SuccClone`, taking over the incoming
      // values that used to flow in through `Succ`.
      bool HasIncomingFromHead =
          llvm::all_of(SuccClone->phis(), [&](const PHINode &PN) {
            return PN.getBasicBlockIndex(Succ) >= 0;
          });
      if (!HasIncomingFromHead)
        continue;

      for (PHINode &PN : SuccClone->phis())
        PN.addIncoming(PN.getIncomingValueForBlock(Succ), Clone);

      Term->setSuccessor(Idx, SuccClone);
    }
  }
}

static Value *buildCoinflip(IRBuilder<> &Builder, Module &M) {
    LLVMContext &Ctx = M.getContext();

    AttributeList AL;
    AL = AL.addFnAttribute(Ctx, Attribute::NoUnwind);
    auto *I64Ty = Builder.getInt64Ty();

    FunctionCallee LRand48 = M.getOrInsertFunction(LRand48FunctionName, AL, I64Ty);

    Value *Rand = Builder.CreateCall(LRand48);
    Value *LSB  = Builder.CreateAnd(Rand, ConstantInt::get(I64Ty, 1));
    return Builder.CreateICmpNE(LSB, ConstantInt::get(I64Ty, 0));
}

bool BasicBlockDuplicate::process(Function &F, LLVMContext &Ctx,
                                  unsigned Probability) {
  ScopedTrace TracePassFunc(F.getName(), name());
  SmallVector<BasicBlock *, 64> ToDup;
  ToDup.reserve(F.size());

  // Collect basic blocks to be duplicated, up to MaxDuplicatedBlocksPerFunction.
  for (BasicBlock &BB : F) {
    if (ToDup.size() >= MaxDuplicatedBlocksPerFunction)
      break;
    if (&BB == &F.getEntryBlock())
      continue;
    if (isEHBlock(BB) || containsSwiftErrorAlloca(BB))
      continue;
    if (RandomGenerator::checkProbability(Probability))
      ToDup.push_back(&BB);
  }

  if (ToDup.empty())
    return false;

  // Maps a duplicated block onto the clone of its tail, so that cloned blocks
  // can be chained together once every block has been processed
  DenseMap<BasicBlock *, BasicBlock *> CloneOf;
  SmallVector<BasicBlock *, 64> Clones;

  IRBuilder<> Builder(Ctx);
  for (BasicBlock *BB : ToDup) {
    BasicBlock::iterator SplitIt = BB->getFirstNonPHIIt();
    if (SplitIt == BB->end())
      continue;

    // Duplicate the basic block and remap its instruction operands via VMap.
    ValueToValueMapTy VMap;
    BasicBlock *OldBB = SplitBlock(BB, SplitIt);
    BasicBlock *NewBB = CloneBasicBlock(OldBB, VMap, ".clone", BB->getParent());
    SmallVector<BasicBlock *, 1> Blocks{NewBB};
    remapInstructionsInBlocks(Blocks, VMap);

    // Drop original basic block terminator.
    BB->getTerminator()->eraseFromParent();

    // Branch on coinflip result.
    Builder.SetInsertPoint(BB);
    Value *Cond = buildCoinflip(Builder, *F.getParent());
    Builder.CreateCondBr(Cond, NewBB, OldBB);

    // Ensure existing PNs have an incoming entry for the newly-cloned basic
    // block.
    for (BasicBlock *Succ : successors(NewBB)) {
      for (PHINode &PN : Succ->phis()) {
        Value *Incoming = PN.getIncomingValueForBlock(OldBB);
        if (auto *IncomingI = dyn_cast_or_null<Instruction>(Incoming))
          if (Value *Mapped = VMap.lookup(IncomingI))
            Incoming = Mapped;
        PN.addIncoming(Incoming, NewBB);
      }
    }

    CloneOf[BB] = NewBB;
    Clones.push_back(NewBB);

    // Rebuild SSA form by inserting missing phi nodes at merge points.
    SmallVector<PHINode *, 8> NewPHIs;
    SSAUpdater Updater(&NewPHIs);
    for (Instruction &I : *OldBB) {
      Value *ClonedVal = VMap.lookup(&I);
      if (!ClonedVal)
        continue;

      bool HasOutsideUsers = llvm::any_of(I.users(), [&](User *U) {
        if (auto *I = dyn_cast<Instruction>(U)) {
          BasicBlock *BB = I->getParent();
          return BB != OldBB && BB != NewBB;
        }
        return false;
      });

      if (!HasOutsideUsers)
        continue;

      Updater.Initialize(I.getType(), I.getName());
      Updater.AddAvailableValue(OldBB, &I);
      Updater.AddAvailableValue(NewBB, ClonedVal);

      for (Use &U : make_early_inc_range(I.uses())) {
        auto *UserI = dyn_cast<Instruction>(U.getUser());
        if (!UserI)
          continue;

        BasicBlock *BB = UserI->getParent();
        if (!isa<PHINode>(UserI) && (BB == OldBB || BB == NewBB))
          continue;

        Updater.RewriteUse(U);
      }
    }
  }

  // Done once SSA form has been rebuilt everywhere: the PHI nodes inserted
  // above decide whether a given head can be bypassed or not.
  redirectClonesToClonedSuccessors(Clones, CloneOf);

  SDEBUG("[{}] Basic blocks duplicated: {}", name(), ToDup.size());
  return true;
}

PreservedAnalyses BasicBlockDuplicate::run(Module &M,
                                           ModuleAnalysisManager &MAM) {
  if (isModuleGloballyExcluded(&M)) {
    SINFO("Excluding module [{}]", M.getName());
    return PreservedAnalyses::all();
  }

  bool Changed = false;
  PyConfig &Config = PyConfig::instance();
  LLVMContext &Ctx = M.getContext();
  SINFO("[{}] Executing on module {}", name(), M.getName());
  ScopedTrace TracePassModule(name(), name());

  for (Function &F : M) {
    BasicBlockDuplicateOpt Opt =
        Config.getUserConfig()->basicBlockDuplicate(&M, &F);

    if (isCoroutine(&F))
      continue;

    auto *P = std::get_if<BasicBlockDuplicateWithProbability>(&Opt);
    if (P && !isFunctionGloballyExcluded(&F) && !F.isDeclaration() &&
        !F.isIntrinsic() && !F.getName().starts_with("__omvll"))
      Changed |= process(F, Ctx, P->Probability);
  }

  SINFO("[{}] Changes {} applied on module {}", name(), Changed ? "" : "not",
        M.getName());

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // end namespace omvll