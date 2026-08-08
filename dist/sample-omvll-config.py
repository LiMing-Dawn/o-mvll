import omvll
from functools import lru_cache


# These functions receive the strong profile even without source annotations.
# A negated annotation such as [[clang::annotate("!flatten_cfg")]] still wins.
CRITICAL_FUNCTIONS = [r"^JNI_OnLoad$"]


class MyConfig(omvll.ObfuscationConfig):
    # Keep function layout unchanged for better instruction-cache locality.
    omvll.config.shuffle_functions = False

    def __init__(self):
        super().__init__()

    def _strong(self, mod: omvll.Module, func: omvll.Function,
                annotation: str) -> bool:
        """Enable an expensive pass only for critical or annotated functions."""
        return omvll.ObfuscationConfig.default_config(
            self,
            mod,
            func,
            [],
            [],
            CRITICAL_FUNCTIONS,
            0,
            annotation,
        )

    def obfuscate_string(self, _, __, string: bytes):
        # Decode each global string once instead of on every use.
        return omvll.StringEncOptGlobal()

    def shuffle_ops(self, mod: omvll.Module, func: omvll.Function):
        # Instruction reordering adds no instructions at runtime. Skip tiny
        # blocks to keep compile time and instruction scheduling reasonable.
        enabled = omvll.ObfuscationConfig.default_config(
            self, mod, func, [], [], [], 100, "shuffle_ops"
        )
        return omvll.ShuffleOpsOpt(min_block_size=5) if enabled else False

    def obfuscate_arithmetic(self, mod: omvll.Module,
                             func: omvll.Function):
        # Two rounds are substantially stronger than one without the code-size
        # and runtime explosion of the default three rounds on every function.
        if self._strong(mod, func, "obfuscate_arithmetic"):
            return omvll.ArithmeticOpt(rounds=2)
        return omvll.ArithmeticOpt(False)

    def obfuscate_constants(self, mod: omvll.Module,
                            func: omvll.Function):
        # Leave common small constants alone and protect meaningful constants.
        if self._strong(mod, func, "obfuscate_constants"):
            return omvll.OpaqueConstantsLowerLimit(16, arith_rounds=1)
        return False

    def flatten_cfg(self, mod: omvll.Module, func: omvll.Function):
        return omvll.ControlFlowFlatteningOpt(
            self._strong(mod, func, "flatten_cfg")
        )

    def indirect_call(self, mod: omvll.Module, func: omvll.Function):
        return omvll.IndirectCallOpt(
            self._strong(mod, func, "indirect_call")
        )

    def indirect_branch(self, mod: omvll.Module, func: omvll.Function):
        return omvll.IndirectBranchOpt(
            self._strong(mod, func, "indirect_branch")
        )

    def break_control_flow(self, mod: omvll.Module,
                           func: omvll.Function):
        return omvll.BreakControlFlowOpt(
            self._strong(mod, func, "break_control_flow")
        )

    def basic_block_split(self, mod: omvll.Module, func: omvll.Function):
        if self._strong(mod, func, "basic_block_split"):
            return omvll.BasicBlockSplitWithProbability(50)
        return omvll.BasicBlockSplitSkip()

    def basic_block_duplicate(self, mod: omvll.Module,
                              func: omvll.Function):
        if self._strong(mod, func, "basic_block_duplicate"):
            return omvll.BasicBlockDuplicateWithProbability(12)
        return omvll.BasicBlockDuplicateSkip()

    def function_outline(self, mod: omvll.Module, func: omvll.Function):
        if self._strong(mod, func, "function_outline"):
            return omvll.FunctionOutlineWithProbability(4)
        return omvll.FunctionOutlineSkip()


@lru_cache(maxsize=1)
def omvll_get_config() -> omvll.ObfuscationConfig:
    return MyConfig()
