//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"

#include "omvll/ObfuscationConfig.hpp"
#include "omvll/PyConfig.hpp"
#include "omvll/log.hpp"
#include "omvll/passes/shuffle-ops/ShuffleOps.hpp"
#include "omvll/utils.hpp"

using namespace llvm;

namespace omvll {

// ---------------------------------------------------------------------------
// Safety predicate
//
// Returns true when an instruction can participate in reordering.
// The exclusions below are safety invariants — weakening any of them risks
// producing semantically incorrect code:
//
//  PHINode        — must remain at the top of the block (LLVM ABI requirement).
//  Terminator     — must remain last (controls successor selection).
//  CallBase       — CallInst/InvokeInst/CallBrInst have unknown side effects
//                   and act as full barriers: nothing may cross them.
//  Volatile load/store — the C/C++ memory model requires volatile accesses to
//                        appear in source order relative to each other.
//  Atomic (isAtomic)  — covers FenceInst, AtomicRMWInst, AtomicCmpXchgInst,
//                        and atomic loads/stores; all carry ordering constraints
//                        that must be preserved.
//  EH pads        — LandingPadInst/CatchPadInst/CleanupPadInst must appear at
//                   fixed positions dictated by exception-handling unwinding.
//  mayThrow       — instructions that LLVM models as potentially throwing must
//                   keep their position relative to any instruction with side
//                   effects.
// ---------------------------------------------------------------------------
static bool isSafeToShuffle(const Instruction *I) {
  if (isa<PHINode>(I))
    return false;
  if (I->isTerminator())
    return false;
  if (isa<CallBase>(I))
    return false;
  if (const auto *LI = dyn_cast<LoadInst>(I); LI && LI->isVolatile())
    return false;
  if (const auto *SI = dyn_cast<StoreInst>(I); SI && SI->isVolatile())
    return false;
  if (I->isAtomic())
    return false;
  if (isa<LandingPadInst>(I) || isa<CatchPadInst>(I) ||
      isa<CleanupPadInst>(I))
    return false;
  if (I->mayThrow())
    return false;
  return true;
}

// Run randomized Kahn's topological sort on a slot of movable instructions.
// Edges are:
//   - Data: I_k -> I_j when I_j uses I_k's result (within the slot).
//   - Memory (conservative): I_j -> I_k (j < k) when both touch memory and
//     at least one writes. AliasAnalysis is deliberately not queried; this
//     conservative rule is sound and consistent with the project's style.
//
// Returns a valid topological order of the slot's instructions.
static SmallVector<Instruction *, 16>
kahnShuffle(ArrayRef<Instruction *> Slot) {
  DenseMap<Instruction *, unsigned> Index;
  Index.reserve(Slot.size());
  for (unsigned I = 0; I < Slot.size(); ++I)
    Index[Slot[I]] = I;

  SmallVector<SmallVector<unsigned, 4>, 16> Succs(Slot.size());
  SmallVector<unsigned, 16> InDeg(Slot.size(), 0);

  auto addEdge = [&](unsigned From, unsigned To) {
    Succs[From].push_back(To);
    ++InDeg[To];
  };

  // Data edges: I_k -> I_j when I_j uses I_k.
  for (unsigned J = 0; J < Slot.size(); ++J) {
    for (Value *Op : Slot[J]->operands()) {
      auto *OpI = dyn_cast<Instruction>(Op);
      if (!OpI)
        continue;
      auto It = Index.find(OpI);
      if (It != Index.end() && It->second != J)
        addEdge(It->second, J);
    }
  }

  // Memory edges (conservative): among instructions that touch memory,
  // preserve original relative order whenever at least one writes.
  for (unsigned J = 0; J < Slot.size(); ++J) {
    if (!Slot[J]->mayReadOrWriteMemory())
      continue;
    for (unsigned K = J + 1; K < Slot.size(); ++K) {
      if (!Slot[K]->mayReadOrWriteMemory())
        continue;
      if (Slot[J]->mayWriteToMemory() || Slot[K]->mayWriteToMemory())
        addEdge(J, K);
    }
  }

  // Kahn's algorithm with random selection from the ready set.
  SmallVector<unsigned, 16> Ready;
  for (unsigned I = 0; I < Slot.size(); ++I)
    if (InDeg[I] == 0)
      Ready.push_back(I);

  SmallVector<Instruction *, 16> Result;
  Result.reserve(Slot.size());

  while (!Ready.empty()) {
    unsigned Idx = static_cast<unsigned>(
        RandomGenerator::generateRange(0, static_cast<uint64_t>(Ready.size() - 1)));
    unsigned Node = Ready[Idx];
    Ready[Idx] = Ready.back();
    Ready.pop_back();

    Result.push_back(Slot[Node]);

    for (unsigned Succ : Succs[Node]) {
      if (--InDeg[Succ] == 0)
        Ready.push_back(Succ);
    }
  }

  return Result;
}

bool ShuffleOps::runOnBasicBlock(BasicBlock &BB, uint64_t MinBlockSize) {
  bool Changed = false;

  // Collect "slots" — maximal contiguous runs of isSafeToShuffle instructions.
  // Barriers (non-movable instructions) stay exactly where they are and delimit
  // slot boundaries.
  SmallVector<Instruction *, 16> Slot;
  unsigned SlotCount = 0;

  auto flushSlot = [&](Instruction *BarrierAfter) {
    if (Slot.size() < MinBlockSize) {
      Slot.clear();
      return;
    }

    SmallVector<Instruction *, 16> NewOrder = kahnShuffle(Slot);

    bool OrderChanged = false;
    for (unsigned I = 0; I < Slot.size(); ++I) {
      if (NewOrder[I] != Slot[I]) {
        OrderChanged = true;
        break;
      }
    }

    SDEBUG("[{}] BB '{}' slot #{}: {} instructions, order {}changed", name(),
           BB.getName(), SlotCount, Slot.size(), OrderChanged ? "" : "un");
    ++SlotCount;

    if (OrderChanged) {
      // Reinsert instructions in their new order just before the barrier.
      // Iterating in reverse ensures each moveBefore places the instruction
      // immediately before the previous insertion point.
      Instruction *Cursor = BarrierAfter;
      for (auto It = NewOrder.rbegin(); It != NewOrder.rend(); ++It) {
        (*It)->moveBeforePreserving(Cursor->getIterator());
        Cursor = *It;
      }
      Changed = true;
    }

    Slot.clear();
  };

  for (Instruction &I : BB) {
    if (isSafeToShuffle(&I)) {
      Slot.push_back(&I);
    } else {
      flushSlot(&I);
    }
  }
  // The terminator is always a barrier, so flushSlot is always triggered
  // before the loop ends.

  SDEBUG("[{}] BB '{}': {} eligible slot(s), order {}", name(), BB.getName(),
         SlotCount, Changed ? "changed" : "unchanged");

  return Changed;
}

bool ShuffleOps::runOnFunction(Function &F, const ShuffleOpsOpt &Opt) {
  ScopedTrace TracePassFunc(F.getName(), name());
  bool Changed = false;
  for (BasicBlock &BB : F)
    Changed |= runOnBasicBlock(BB, Opt.MinBlockSize);
  return Changed;
}

PreservedAnalyses ShuffleOps::run(Module &M, ModuleAnalysisManager &) {
  if (isModuleGloballyExcluded(&M)) {
    SINFO("Excluding module [{}]", M.getName());
    return PreservedAnalyses::all();
  }

  bool Changed = false;
  PyConfig &Config = PyConfig::instance();
  SINFO("[{}] Executing on module {}", name(), M.getName());
  ScopedTrace TracePassModule(name(), name());
  IRChangesMonitor ModuleChanges(M, name());

  for (Function &F : M) {
    if (isFunctionGloballyExcluded(&F) || F.isDeclaration() ||
        F.isIntrinsic())
      continue;

    ShuffleOpsOpt Opt = Config.getUserConfig()->shuffleOps(&M, &F);
    if (!Opt)
      continue;

    SINFO("[{}] Visiting function {}", name(), F.getName());
    Changed |= runOnFunction(F, Opt);
  }

  SINFO("[{}] Changes {} applied on module {}", name(), Changed ? "" : "not",
        M.getName());

  ModuleChanges.notify(Changed);
  return ModuleChanges.report();
}

} // end namespace omvll
