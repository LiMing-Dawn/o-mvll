#pragma once

//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include <variant>

namespace omvll {

struct BasicBlockSplitSkip {};

struct BasicBlockSplitWithProbability {
  BasicBlockSplitWithProbability(unsigned Probability = 0)
      : Probability(Probability) {}
  operator bool() const { return Probability > 0; }
  unsigned Probability;
};

using BasicBlockSplitOpt =
    std::variant<BasicBlockSplitSkip, BasicBlockSplitWithProbability>;

} // end namespace omvll
