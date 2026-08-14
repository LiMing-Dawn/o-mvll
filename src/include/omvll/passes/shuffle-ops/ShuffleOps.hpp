#pragma once

//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "llvm/IR/PassManager.h"

#include "omvll/passes/shuffle-ops/ShuffleOpsOpt.hpp"

namespace omvll {

// Reorders instructions within each basic block using a randomized topological
// sort (Kahn's algorithm). The resulting order is semantically equivalent to
// the original but harder to follow statically, adding obfuscation depth on
// top of the other passes.
struct ShuffleOps : llvm::PassInfoMixin<ShuffleOps> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);
  bool runOnFunction(llvm::Function &F, const ShuffleOpsOpt &Opt);
  bool runOnBasicBlock(llvm::BasicBlock &BB, uint64_t MinBlockSize);
};

} // end namespace omvll
