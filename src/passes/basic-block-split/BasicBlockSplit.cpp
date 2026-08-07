//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "omvll/ObfuscationConfig.hpp"
#include "omvll/PyConfig.hpp"
#include "omvll/log.hpp"
#include "omvll/passes/basic-block-split/BasicBlockSplit.hpp"
#include "omvll/passes/basic-block-split/BasicBlockSplitOpt.hpp"
#include "omvll/utils.hpp"

using namespace llvm;
using namespace omvll;

namespace omvll {

bool BasicBlockSplit::process(Function &F, unsigned Probability) {
  ScopedTrace TracePassFunc(F.getName(), name());

  // Snapshot the blocks to split up-front: SplitBlock inserts a new block right
  // after the one being split, so iterating over F while splitting would
  // otherwise revisit (and re-split) the freshly created halves.
  SmallVector<BasicBlock *, 32> ToSplit;
  for (BasicBlock &BB : F) {
    // Never split the entry block
    if (&BB == &F.getEntryBlock())
      continue;
    if (isEHBlock(BB) || containsSwiftErrorAlloca(BB))
      continue;
    if (RandomGenerator::checkProbability(Probability))
      ToSplit.push_back(&BB);
  }

  unsigned Count = 0;
  for (BasicBlock *BB : ToSplit) {
    // Candidate split points are the non-PHI instructions other than the
    // terminator. Splitting before a PHI is illegal, and splitting at the
    // terminator would leave an empty second half.
    SmallVector<Instruction *, 16> Candidates;
    for (Instruction &I : *BB) {
      if (isa<PHINode>(I) || I.isTerminator())
        continue;
      Candidates.push_back(&I);
    }

    // Need at least two instructions so both halves keep real work.
    if (Candidates.size() < 2)
      continue;

    // Split down the middle. SplitBlock moves the midpoint instruction and
    // everything after it into a new block, and wires an unconditional branch
    // from the original block to it.
    Instruction *MidPoint = Candidates[Candidates.size() / 2];
    SplitBlock(BB, MidPoint);
    ++Count;
  }

  SDEBUG("[{}] Basic blocks split: {}", name(), Count);
  return Count > 0;
}

PreservedAnalyses BasicBlockSplit::run(Module &M, ModuleAnalysisManager &MAM) {
  if (isModuleGloballyExcluded(&M)) {
    SINFO("Excluding module [{}]", M.getName());
    return PreservedAnalyses::all();
  }

  bool Changed = false;
  PyConfig &Config = PyConfig::instance();
  SINFO("[{}] Executing on module {}", name(), M.getName());
  ScopedTrace TracePassModule(name(), name());

  for (Function &F : M) {
    BasicBlockSplitOpt Opt = Config.getUserConfig()->basicBlockSplit(&M, &F);

    if (isCoroutine(&F))
      continue;

    auto *P = std::get_if<BasicBlockSplitWithProbability>(&Opt);
    if (P && !isFunctionGloballyExcluded(&F) && !F.isDeclaration() &&
        !F.isIntrinsic() && !F.getName().starts_with("__omvll"))
      Changed |= process(F, P->Probability);
  }

  SINFO("[{}] Changes {} applied on module {}", name(), Changed ? "" : "not",
        M.getName());

  return Changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
}

} // end namespace omvll
