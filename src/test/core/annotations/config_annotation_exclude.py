#
# This file is distributed under the Apache License v2.0. See LICENSE for details.
#

import omvll
from functools import lru_cache

class MyConfig(omvll.ObfuscationConfig):
    def __init__(self):
        super().__init__()
    def obfuscate_arithmetic(self, mod: omvll.Module,
                                   fun: omvll.Function) -> omvll.ArithmeticOpt:
        # The function is force-included by name *and* probability is 100, yet
        # it carries `__attribute__((annotate("!insreplace")))`. The negated
        # annotation must win, so the pass stays disabled.
        return omvll.ArithmeticOpt(rounds=2) if omvll.ObfuscationConfig.default_config(self, mod, fun, [], [], ["memcpy_xor"], 100, "insreplace") else False

@lru_cache(maxsize=1)
def omvll_get_config() -> omvll.ObfuscationConfig:
    return MyConfig()
