//
// This file is distributed under the Apache License v2.0. See LICENSE for
// details.
//

#include <dlfcn.h>

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/FileSystem.h"

#include "omvll/PyConfig.hpp"
#include "omvll/log.hpp"
#include "omvll/omvll_config.hpp"
#include "omvll/utils.hpp"
#include "omvll/versioning.hpp"

#include "PyObfuscationConfig.hpp"
#include "init.hpp"

namespace py = pybind11;

using namespace pybind11::literals;

namespace omvll {

static void setPythonPath(const std::string &PythonPath) {
#if PY_VERSION_HEX >= 0x030D0000
  // Py_SetPath was removed in Python 3.13. PYTHONPATH is consumed by the
  // PyConfig-based initialization used by pybind11 and keeps this compatible
  // with both the packaged standard library and its native extension modules.
  std::string ModuleSearchPath = PythonPath;
  ModuleSearchPath.append(":").append(PythonPath).append("/lib-dynload");
  setenv("PYTHONPATH", ModuleSearchPath.c_str(), true);
#else
  wchar_t *WidePath = Py_DecodeLocale(PythonPath.c_str(), nullptr);
  if (WidePath == nullptr) {
    SERR("Unable to decode Python path '{}'", PythonPath);
    return;
  }
  Py_SetPath(WidePath);
  PyMem_RawFree(WidePath);
  setenv("PYTHONHOME", PythonPath.c_str(), true);
#endif
}

void initPythonpath() {
  if (!PyConfig::YConfig.PythonPath.empty()) {
    setPythonPath(PyConfig::YConfig.PythonPath);
    return;
  }

  if (char *Config = getenv(PyConfig::PyEnv_Key)) {
    setPythonPath(Config);
    return;
  }

#if defined(__linux__) || defined(__ANDROID__)
  Dl_info Info{};
  if (dladdr((void *)&Py_Initialize, &Info) == 0 || Info.dli_fname == nullptr)
    return;

  std::string PythonPath = Info.dli_fname;
  const size_t Separator = PythonPath.find_last_of('/');
  if (Separator == std::string::npos)
    return;
  PythonPath.resize(Separator);

#if defined(__ANDROID__)
  // The Termux bundle stores libpython in lib/ and the standard library in
  // the sibling python/pythonX.Y directory.
  PythonPath.append("/../python/");
#else
  PythonPath.append("/");
#endif
  PythonPath.append("python")
      .append(std::to_string(PY_MAJOR_VERSION))
      .append(".")
      .append(std::to_string(PY_MINOR_VERSION));
  setPythonPath(PythonPath);
#endif
}

void OMVLLCtor(py::module_ &m) {
  initDefaultConfig();

  m.attr("LLVM_VERSION")  = OMVLL_LLVM_VERSION_STRING;
  m.attr("OMVLL_VERSION") = OMVLL_VERSION;
  m.attr("OMVLL_VERSION_FULL") = "OMVLL Version: " OMVLL_VERSION " / " OMVLL_LLVM_VERSION_STRING
                                 " (" OMVLL_LLVM_VERSION ")";

  // pybind11 appends an auto-generated "Members:" list to every enum docstring.
  // For Sphinx autodoc this is undesirable since it duplicates content
  {
    py::options Options;
#if PYBIND11_VERSION_MAJOR > 2 || (PYBIND11_VERSION_MAJOR == 2 && PYBIND11_VERSION_MINOR >= 11)
    Options.disable_enum_members_docstring();
#endif

    py::enum_<Phase>(m, "Phase",
                     R"delim(
    Enum representing the two LLVM optimization pipeline phases where
    obfuscation passes can be registered. Used as values in
    :py:attr:`~omvll.OMVLLConfig.pass_phases`.

    .. py:attribute:: Early

       Runs before LLVM's optimizer. Default phase for all passes.

    .. py:attribute:: Last

       Runs after LLVM's optimizer.
    )delim")
        .value("Early", Phase::Early)
        .value("Last", Phase::Last);

    py::enum_<Pass>(m, "Pass",
                    R"delim(
    Enum representing all available obfuscation passes. Used as keys in
    :py:attr:`~omvll.OMVLLConfig.pass_phases`.

    .. list-table::
       :header-rows: 1

       * - Value
         - Corresponding callback
       * - ``Pass.AntiHook``
         - :py:meth:`~omvll.ObfuscationConfig.anti_hooking`
       * - ``Pass.Arithmetic``
         - :py:meth:`~omvll.ObfuscationConfig.obfuscate_arithmetic`
       * - ``Pass.BasicBlockDuplicate``
         - :py:meth:`~omvll.ObfuscationConfig.basic_block_duplicate`
       * - ``Pass.BreakControlFlow``
         - :py:meth:`~omvll.ObfuscationConfig.break_control_flow`
       * - ``Pass.InlineJni``
         - (no callback — controlled by ``omvll.config.inline_jni_wrappers``)
       * - ``Pass.ShuffleFunctions``
         - (no callback — controlled by ``omvll.config.shuffle_functions``)
       * - ``Pass.ControlFlowFlattening``
         - :py:meth:`~omvll.ObfuscationConfig.flatten_cfg`
       * - ``Pass.FunctionOutline``
         - :py:meth:`~omvll.ObfuscationConfig.function_outline`
       * - ``Pass.IndirectBranch``
         - :py:meth:`~omvll.ObfuscationConfig.indirect_branch`
       * - ``Pass.IndirectCall``
         - :py:meth:`~omvll.ObfuscationConfig.indirect_call`
       * - ``Pass.OpaqueConstants``
         - :py:meth:`~omvll.ObfuscationConfig.obfuscate_constants`
       * - ``Pass.OpaqueFieldAccess``
         - :py:meth:`~omvll.ObfuscationConfig.obfuscate_struct_access`,
           :py:meth:`~omvll.ObfuscationConfig.obfuscate_variable_access`
       * - ``Pass.StringEncoding``
         - :py:meth:`~omvll.ObfuscationConfig.obfuscate_string`
    )delim")
        .value("AntiHook",              Pass::AntiHook)
        .value("FunctionOutline",       Pass::FunctionOutline)
        .value("StringEncoding",        Pass::StringEncoding)
        .value("OpaqueFieldAccess",     Pass::OpaqueFieldAccess)
        .value("BasicBlockDuplicate",   Pass::BasicBlockDuplicate)
        .value("ControlFlowFlattening", Pass::ControlFlowFlattening)
        .value("BreakControlFlow",      Pass::BreakControlFlow)
        .value("OpaqueConstants",       Pass::OpaqueConstants)
        .value("Arithmetic",            Pass::Arithmetic)
        .value("IndirectCall",          Pass::IndirectCall)
        .value("IndirectBranch",        Pass::IndirectBranch)
        .value("InlineJni",             Pass::InlineJni)
        .value("ShuffleFunctions",      Pass::ShuffleFunctions);
  }

  py::class_<OMVLLConfig>(m, "OMVLLConfig",
                          R"delim(
    Global configuration object controlling O-MVLL's overall behavior.
    Accessed via :py:attr:`omvll.config`.
    )delim")
      .def_readwrite("inline_jni_wrappers", &OMVLLConfig::InlineJniWrappers,
                     R"delim(
                   Force inlining of JNI C++ wrapper methods such as:

                   .. code-block:: cpp

                      const jchar* GetStringChars(jstring string, jboolean* isCopy)
                      { return functions->GetStringChars(this, string, isCopy); }

                   Default: ``True``.

                   :type: bool
                   )delim")

      .def_readwrite("shuffle_functions", &OMVLLConfig::ShuffleFunctions,
                     R"delim(
                   Randomize the order of functions within each module so that two builds
                   of the same source do not place functions at the same relative positions.

                   Default: ``True``.

                   :type: bool
                   )delim")

      .def_readwrite("global_mod_exclude", &OMVLLConfig::GlobalModuleExclude,
                     R"delim(
                   Module name substrings to exclude from all obfuscation passes. A module
                   whose path contains any of these strings is skipped entirely. For
                   example, adding ``"b/"`` would exclude a module at ``a/b/c/d.cpp``.

                   Default: ``[]``.

                   :type: list[str]
                   )delim")

      .def_readwrite("global_func_exclude", &OMVLLConfig::GlobalFunctionExclude,
                     R"delim(
                   Function name substrings to exclude from all obfuscation passes.

                   Default: ``[]``.

                   :type: list[str]
                   )delim")

      .def_readwrite("probability_seed", &OMVLLConfig::ProbabilitySeed,
                     R"delim(
                   Seed for the random number generator used by probability-based passes
                   (e.g. :py:class:`~omvll.BasicBlockDuplicateWithProbability`). A fixed
                   seed makes obfuscation output deterministic across builds.

                   Default: ``1``.

                   :type: int
                   )delim")

      .def_readwrite("output_folder", &OMVLLConfig::OutputFolder,
                     R"delim(
                   Directory where O-MVLL writes output files (e.g. log files). Created
                   automatically if it does not exist. An empty string disables file output.

                   Default: ``""``.

                   :type: str
                   )delim")

      .def_readwrite("pass_phases", &OMVLLConfig::PassPhases,
                     R"delim(
                   Maps each pass to the LLVM pipeline phase(s) it runs in. Passes absent
                   from this dictionary default to :py:attr:`~omvll.Phase.Early`.

                   .. code-block:: python

                      omvll.config.pass_phases = {
                          omvll.Pass.Arithmetic:       {omvll.Phase.Early},
                          omvll.Pass.BreakControlFlow: {omvll.Phase.Last},
                          omvll.Pass.StringEncoding:   {omvll.Phase.Early, omvll.Phase.Last},
                      }

                   :type: dict[Pass, set[Phase]]
                   )delim");

  m.attr("config") = &Config;

  py_init_obf_opt(m);
  py_init_llvm_bindings(m);
  py_init_log(m);

  py::class_<ObfuscationConfig, PyObfuscationConfig>(m, "ObfuscationConfig",
                                                     R"delim(
    Base class that must be subclassed to configure O-MVLL obfuscation passes.

    Override the callback methods below to control which passes are applied to
    each function. The configuration file must expose a top-level
    ``omvll_get_config()`` function that returns an instance of this class.
    Using :func:`functools.lru_cache` is recommended to avoid repeated
    instantiation:

    .. note::

       Most callbacks accept ``True``, ``False``, or ``None`` as convenience
       shorthands in addition to the dedicated option classes. ``None`` is
       handled explicitly because a Python method that reaches the end without
       a ``return`` statement implicitly returns ``None`` — so the following
       pattern works as intended:

       .. code-block:: python

          def break_control_flow(self, mod, func):
              if func.name == "secret_func":
                  return True
              # no return → None → pass disabled for everything else

       ``False`` and ``None`` are therefore equivalent and both disable the
       pass. The exceptions are :py:meth:`basic_block_duplicate` and
       :py:meth:`function_outline`, which require a probability-based option
       class and raise an error if a boolean is returned.

    .. code-block:: python

       @lru_cache(maxsize=1)
       def omvll_get_config() -> omvll.ObfuscationConfig:
           return MyConfig()
    )delim")
      .def(py::init<>())

      .def("obfuscate_string", &ObfuscationConfig::obfuscateString,
           R"delim(
         Callback invoked for every string literal found in *function*.

         In addition to returning a string encoding option class directly,
         the following convenience shorthands are accepted:

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``None``
              - :py:class:`~omvll.StringEncOptSkip`
            * - ``False``
              - :py:class:`~omvll.StringEncOptSkip`
            * - ``True``
              - :py:class:`~omvll.StringEncOptDefault`
            * - ``str``
              - :py:class:`~omvll.StringEncOptReplace`
            * - ``bytes``
              - :py:class:`~omvll.StringEncOptReplace`

         :param module: The LLVM module containing the function.
         :type module: :py:class:`~omvll.Module`
         :param function: The LLVM function containing the string.
         :type function: :py:class:`~omvll.Function`
         :param string: The raw bytes of the string literal.
         :type string: bytes
         )delim",
           "module"_a, "function"_a, "string"_a)

      .def("break_control_flow", &ObfuscationConfig::breakControlFlow,
           R"delim(
         Callback for the break-control-flow pass.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.BreakControlFlowOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.BreakControlFlowOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.BreakControlFlowOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("flatten_cfg", &ObfuscationConfig::controlFlowGraphFlattening,
           R"delim(
         Callback for the control-flow flattening pass.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.ControlFlowFlatteningOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.ControlFlowFlatteningOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.ControlFlowFlatteningOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("obfuscate_struct_access", &ObfuscationConfig::obfuscateStructAccess,
           R"delim(
         Callback for obfuscating structure field accesses.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.StructAccessOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.StructAccessOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.StructAccessOpt`\(``False``)

         :param struct: The LLVM struct type being accessed.
         :type struct: :py:class:`~omvll.Struct`
         )delim",
           "module"_a, "function"_a, "struct"_a)

      .def("obfuscate_variable_access",
           &ObfuscationConfig::obfuscateVariableAccess,
           R"delim(
         Callback for obfuscating global variable accesses.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.VarAccessOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.VarAccessOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.VarAccessOpt`\(``False``)

         :param variable: The global variable being accessed.
         :type variable: :py:class:`~omvll.GlobalVariable`
         )delim",
           "module"_a, "function"_a, "variable"_a)

      .def("obfuscate_constants", &ObfuscationConfig::obfuscateConstants,
           R"delim(
         Callback for the opaque constants pass.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.OpaqueConstantsBool`\(``True``)
            * - ``False``
              - :py:class:`~omvll.OpaqueConstantsBool`\(``False``)
            * - ``None``
              - :py:class:`~omvll.OpaqueConstantsBool`\(``False``)
            * - ``list[int]``
              - :py:class:`~omvll.OpaqueConstantsSet`
         )delim",
           "module"_a, "function"_a)

      .def("obfuscate_arithmetic", &ObfuscationConfig::obfuscateArithmetics,
           R"delim(
         Callback for the arithmetic obfuscation pass.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.ArithmeticOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.ArithmeticOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.ArithmeticOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("anti_hooking", &ObfuscationConfig::antiHooking,
           R"delim(
         Callback for the anti-hooking pass.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.AntiHookOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.AntiHookOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.AntiHookOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("indirect_branch", &ObfuscationConfig::indirectBranch,
           R"delim(
         Callback for the indirect branch pass. Replaces ordinary branches
         with indirect jumps.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.IndirectBranchOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.IndirectBranchOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.IndirectBranchOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("indirect_call", &ObfuscationConfig::indirectCall,
           R"delim(
         Callback for the indirect call pass. Converts direct function calls
         into indirect ones by splitting the target address into two additive
         shares.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``True``
              - :py:class:`~omvll.IndirectCallOpt`\(``True``)
            * - ``False``
              - :py:class:`~omvll.IndirectCallOpt`\(``False``)
            * - ``None``
              - :py:class:`~omvll.IndirectCallOpt`\(``False``)
         )delim",
           "module"_a, "function"_a)

      .def("basic_block_duplicate", &ObfuscationConfig::basicBlockDuplicate,
           R"delim(
         Callback for the basic block duplicate pass. Randomly selects basic
         blocks within *function* to be duplicated.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``None``
              - :py:class:`~omvll.BasicBlockDuplicateSkip`
            * - ``int`` (0–100)
              - :py:class:`~omvll.BasicBlockDuplicateWithProbability`\(``int``)
            * - ``bool``
              - fatal error
         )delim",
           "module"_a, "function"_a)

      .def("function_outline", &ObfuscationConfig::functionOutline,
           R"delim(
         Callback for the function outline pass. Randomly selects basic blocks
         within *function* to be outlined into new standalone functions.

         .. list-table::
            :header-rows: 1

            * - Return value
              - Interpretation
            * - ``None``
              - :py:class:`~omvll.FunctionOutlineSkip`
            * - ``int`` (0–100)
              - :py:class:`~omvll.FunctionOutlineWithProbability`\(``int``)
            * - ``bool``
              - fatal error
         )delim",
           "module"_a, "function"_a)

      .def("report_diff", &ObfuscationConfig::reportDiff,
           R"delim(
         Optional callback to monitor IR-level changes produced by individual
         passes. Override to inspect before/after LLVM IR.

         :param pass_name: Name of the pass that made the change.
         :type pass_name: str
         :param original: The original LLVM IR of the function.
         :type original: str
         :param obfuscated: The obfuscated LLVM IR of the function.
         :type obfuscated: str
         )delim",
           "pass_name"_a, "original"_a, "obfuscated"_a)

      .def("default_config", &ObfuscationConfig::defaultConfig,
           R"delim(
         Built-in probability-based policy helper:

         - Skips if *module* matches any pattern in *module_excludes*.
         - Skips if *function* matches any pattern in *function_excludes*.
         - Enables unconditionally if *function* matches any pattern in
           *function_includes*.
         - Otherwise enables with the given *probability* (0–100).

         Typical use as a callback fallback:

         .. code-block:: python

            def break_control_flow(self, mod, func):
                return omvll.ObfuscationConfig.default_config(
                    self, mod, func, [], [], [], 10
                )

         :param module_excludes: Module name substrings to exclude.
         :type module_excludes: list[str]
         :param function_excludes: Function name substrings to exclude.
         :type function_excludes: list[str]
         :param function_includes: Function name substrings that force the pass on.
         :type function_includes: list[str]
         :param probability: Percentage chance (0–100) to apply the pass.
         :type probability: int
         :rtype: bool
         )delim",
           "module"_a, "function"_a, "module_excludes"_a, "function_excludes"_a,
           "function_includes"_a, "probability"_a);
}

std::unique_ptr<py::module_> initOMVLLCore(py::dict Modules) {
  auto M = std::make_unique<py::module_>(
      py::module_::create_extension_module("omvll", "", new PyModuleDef()));
  OMVLLCtor(*M);
  Modules["omvll"] = *M;
  return M;
}

PyConfig::~PyConfig() = default;

PyConfig &PyConfig::instance() {
  static PyConfig Instance;
  return Instance;
}

ObfuscationConfig *PyConfig::getUserConfig() {
  try {
    py::gil_scoped_acquire gil;
    if (!py::hasattr(*Mod, "omvll_get_config"))
      fatalError("Missing omvll_get_config");

    auto PyUserConfig = Mod->attr("omvll_get_config");
    if (PyUserConfig.is_none())
      fatalError("Missing omvll_get_config");

    py::object Result = PyUserConfig();
    return Result.cast<ObfuscationConfig *>();
  } catch (const std::exception &Exc) {
    fatalError(Exc.what());
  }
}

PyConfig::PyConfig() {
  py::initialize_interpreter();
  // initialize_interpreter() already holds the GIL.

  py::module_ SysMod = py::module_::import("sys");
  py::module_ PathLib = py::module_::import("pathlib");
  py::dict Modules = SysMod.attr("modules");

  CoreMod = initOMVLLCore(Modules);

  llvm::StringRef ConfigPath;
  if (!PyConfig::YConfig.OMVLLConfig.empty())
    ConfigPath = PyConfig::YConfig.OMVLLConfig;
  else if (char *Config = getenv(EnvKey))
    ConfigPath = Config;

  SINFO("Using OMVLL_CONFIG = {}", ConfigPath);

  std::string ModName = DefaultFileName;
  if (!ConfigPath.empty()) {
    std::string Config = ConfigPath.str();
    auto PyPath = PathLib.attr("Path")(Config);
    py::list Path = SysMod.attr("path");
    Path.insert(0, PyPath.attr("parent").attr("as_posix")());
    std::string Name = PyPath.attr("stem").cast<std::string>();
    ModName = Name;
  }

  try {
    Mod = std::make_unique<py::module_>(py::module_::import(ModName.c_str()));
    ModulePath = Mod->attr("__file__").cast<std::string>();
  } catch (const std::exception &Exc) {
    fatalError(Exc.what());
  }

  // Check if configured output folder variable is not empty in order to create
  // the parents
  if (!Config.OutputFolder.empty())
    if (std::error_code EC =
            llvm::sys::fs::create_directories(Config.OutputFolder))
      fatalError("Failed to create output_folder " + Config.OutputFolder +
                 ": " + EC.message());

  // We have not manually acquired the GIL, so release it now. Subsequent
  // accesses to Python configs will manually require acquiring the GIL.
  PyEval_SaveThread();
}

std::string PyConfig::configPath() { return ModulePath; }

} // end namespace omvll

PYBIND11_MODULE(omvll, m) { omvll::OMVLLCtor(m); }
