#pragma once

//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include "llvm/IR/PassManager.h"

namespace omvll {

struct BasicBlockSplit : llvm::PassInfoMixin<BasicBlockSplit> {
  llvm::PreservedAnalyses run(llvm::Module &M,
                              llvm::ModuleAnalysisManager &MAM);

  bool process(llvm::Function &F, unsigned Probability);
};

} // end namespace omvll
