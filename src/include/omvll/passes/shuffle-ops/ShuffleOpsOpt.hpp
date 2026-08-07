#pragma once

//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include <cstdint>

namespace omvll {

struct ShuffleOpsOpt {
  static constexpr uint64_t DefaultMinBlockSize = 4;

  ShuffleOpsOpt() = default;
  ShuffleOpsOpt(bool Value) : MinBlockSize(Value ? DefaultMinBlockSize : 0) {}
  ShuffleOpsOpt(uint64_t MinBlockSize) : MinBlockSize(MinBlockSize) {}
  operator bool() const { return MinBlockSize > 0; }

  uint64_t MinBlockSize = DefaultMinBlockSize;
};

} // end namespace omvll
