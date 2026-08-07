Obfuscation
~~~~~~~~~~~

Config
------

.. autoclass:: omvll.ObfuscationConfig
  :members: obfuscate_string, break_control_flow, flatten_cfg, obfuscate_struct_access, obfuscate_variable_access, obfuscate_constants, obfuscate_arithmetic, anti_hooking, indirect_branch, indirect_call, basic_block_duplicate, basic_block_split, function_outline, report_diff, default_config

Driving passes from source annotations
######################################

Instead of maintaining per-pass lists of function names in the Python config,
a function can opt into (or out of) a pass directly in the source, next to the
code it protects, using ``__attribute__((annotate("...")))``:

.. code-block:: c

   __attribute__((annotate("arithmetic")))    // opt this function into arithmetic obfuscation
   __attribute__((annotate("flattening")))    // ...and control-flow flattening
   int verify_license(const char *token) { ... }

   __attribute__((annotate("!flattening")))   // opt out, even if a list includes it
   int hot_path(void) { ... }

The annotation name is an arbitrary string chosen by the config author; a
matching name passed to :py:meth:`~omvll.ObfuscationConfig.default_config`
via the ``annotation`` keyword enables the pass for annotated functions:

.. code-block:: python

   def obfuscate_arithmetic(self, mod, func):
       if omvll.ObfuscationConfig.default_config(self, mod, func,
                                                 annotation="arithmetic"):
           return omvll.ArithmeticOpt(rounds=2)
       return omvll.ArithmeticOpt(False)

Precedence, from strongest to weakest: ``module_excludes`` →
``function_excludes`` → the negated annotation ``!<annotation>`` →
``function_includes`` → the annotation ``<annotation>`` → ``probability``. A
negated annotation therefore always wins, even over an explicit include list or
a probability of 100.

Template
########

.. code-block:: python

   import omvll
   from functools import lru_cache

   class MyConfig(omvll.ObfuscationConfig):
       def __init__(self):
           super().__init__()

       def obfuscate_string(self, module: omvll.Module, func: omvll.Function,
                            string: bytes):
           if func.demangled_name == "Hello::say_hi()":
               return omvll.StringEncOptDefault()
           if "debug.cpp" in module.name:
               return omvll.StringEncOptReplace("<REMOVED>")
           return omvll.StringEncOptSkip()

       def obfuscate_arithmetic(self, mod: omvll.Module, func: omvll.Function):
           return omvll.ArithmeticOpt(True)

       def flatten_cfg(self, mod: omvll.Module, func: omvll.Function):
           return omvll.ControlFlowFlatteningOpt(True)

       def break_control_flow(self, mod: omvll.Module, func: omvll.Function):
           return omvll.ObfuscationConfig.default_config(
               self, mod, func, [], [], [], 10
           )

       def indirect_call(self, mod: omvll.Module, func: omvll.Function):
           return omvll.IndirectCallOpt(True)

       def function_outline(self, mod: omvll.Module, func: omvll.Function):
           return omvll.FunctionOutlineWithProbability(10)

       def basic_block_duplicate(self, mod: omvll.Module, func: omvll.Function):
           return omvll.BasicBlockDuplicateWithProbability(10)

       def basic_block_split(self, mod: omvll.Module, func: omvll.Function):
           return omvll.BasicBlockSplitWithProbability(10)


   @lru_cache(maxsize=1)
   def omvll_get_config() -> omvll.ObfuscationConfig:
       return MyConfig()

Options
-------

Anti-Hooking
############

.. autoclass:: omvll.AntiHookOpt

Arithmetic Obfuscation
######################

.. autoclass:: omvll.ArithmeticOpt

Basic Block Duplicate
#####################

.. autoclass:: omvll.BasicBlockDuplicateSkip

.. autoclass:: omvll.BasicBlockDuplicateWithProbability

Basic Block Split
#################

.. autoclass:: omvll.BasicBlockSplitSkip

.. autoclass:: omvll.BasicBlockSplitWithProbability

Control-Flow Breaking
#####################

.. autoclass:: omvll.BreakControlFlowOpt

Control-Flow Flattening
#######################

.. autoclass:: omvll.ControlFlowFlatteningOpt

Function Outline
################

.. autoclass:: omvll.FunctionOutlineSkip

.. autoclass:: omvll.FunctionOutlineWithProbability

Indirect Branch
###############

.. autoclass:: omvll.IndirectBranchOpt

Indirect Call
#############

.. autoclass:: omvll.IndirectCallOpt

Opaque Constants
################

.. autoclass:: omvll.OpaqueConstantsSkip

.. autoclass:: omvll.OpaqueConstantsBool

.. autoclass:: omvll.OpaqueConstantsLowerLimit

.. autoclass:: omvll.OpaqueConstantsSet

.. autoclass:: omvll.OpaqueConstantsExcludeSet

Opaque Fields Access
####################

.. autoclass:: omvll.StructAccessOpt

.. autoclass:: omvll.VarAccessOpt

Strings Encoding
################

.. autoclass:: omvll.StringEncOptSkip

.. autoclass:: omvll.StringEncOptDefault

.. autoclass:: omvll.StringEncOptGlobal

.. autoclass:: omvll.StringEncOptLocal

.. autoclass:: omvll.StringEncOptReplace
