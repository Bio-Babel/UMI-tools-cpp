// The umi_tools executable — port of umi_tools/umi_tools.py::main.
//
// The Python dispatcher, verbatim in behaviour:
//
//   if len(argv) < 2 or argv[1] in ("--help", "-h"):
//       print("For full UMI-tools documentation, see: <url>\n")
//       print(__doc__)
//       return 0
//   if argv[1] in ("--version", "-v"):
//       print("UMI-tools version: %s" % __version__);  return 0
//   command = argv[1]
//   if command == "prepare-for-rsem": command = "prepare-for-em"
//   try:    module = importlib.import_module("umi_tools." + command, "umi_tools")
//   except ImportError:
//       print("'%s' is not a UMI-tools command. See 'umi_tools -h'.\n" % command)
//       print("For full UMI-tools documentation, see: <url>\n")
//       print(__doc__)
//       return 1
//   del sys.argv[0]
//   module.main(sys.argv)
//
// TWO UPSTREAM BEHAVIOURS REPRODUCED DELIBERATELY (00_baseline.md F2):
//
//  1. `prepare-for-rsem` is rewritten to `prepare-for-em`, and then
//     import_module("umi_tools.prepare-for-em") is attempted. A hyphen is not
//     legal in a module name, so the import ALWAYS fails and the tool reports
//     "'prepare-for-em' is not a UMI-tools command" with exit code 1 — note it
//     reports the REWRITTEN name, not what the user typed. Only the underscore
//     spelling `prepare_for_em` works, and that is the spelling the README and
//     docs use. Measured: both hyphen forms exit 1.
//
//  2. `module.main(sys.argv)`'s return value is DISCARDED — main() falls off the
//     end returning None, and sys.exit(None) is exit code 0. So a subcommand that
//     returns 1 (e.g. count_tab invoked with no options) still exits 0 through
//     the dispatcher. Reproduced.
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "umi_tools/logging.hpp"
#include "umi_tools/tools.hpp"

namespace umi_tools {
namespace {

// umi_tools.py's module docstring, as printed by --help and by the
// unknown-command path.
constexpr const char* kDoc =
    "\n"
    "umi_tools.py - Tools for UMI analyses\n"
    "=====================================\n"
    "\n"
    ":Author: Tom Smith & Ian Sudbery, CGAT\n"
    ":Tags: Genomics UMI\n"
    "\n"
    "There are 6 tools:\n"
    "\n"
    "  - whitelist\n"
    "  - extract\n"
    "  - group\n"
    "  - dedup\n"
    "  - count\n"
    "  - count_tab\n"
    "  - prepare_for_em\n"
    "\n"
    "To get help on a specific tool, type:\n"
    "\n"
    "    umi_tools <tool> --help\n"
    "\n"
    "To use a specific tool, type::\n"
    "\n"
    "    umi_tools <tool> [tool options] [tool arguments]\n";

constexpr const char* kDocsUrl =
    "For full UMI-tools documentation, see: "
    "https://umi-tools.readthedocs.io/en/latest/\n";

// Models Python's import_module: a name resolves only if `umi_tools/<name>.py`
// exists, which is exactly why the hyphen spellings fail.
//
// The predicate is "is an importable SUBMODULE", not "is one of the 7
// tools". umi_tools.py:54-66 does `importlib.import_module("umi_tools." + cmd)`
// inside a try/except ImportError and then calls `module.main(sys.argv)`. For a
// submodule that exists but has no main(), the import SUCCEEDS, the except block
// is skipped, and the AttributeError escapes as a traceback on STDERR. The port
// classified those as unknown commands and printed a friendly message plus the
// whole docstring to STDOUT — same exit code, opposite stream, different text.
//
// The list is every submodule of the installed package, from
// `pkgutil.iter_modules(umi_tools.__path__)`; `umi_tools` itself is in it,
// because the dispatcher can dispatch to itself.
bool is_importable_module(const std::string& command) {
  static const char* kModules[] = {
      "Documentation", "Utilities",   "_dedup_umi",     "count",
      "count_tab",     "dedup",       "extract",        "extract_methods",
      "group",         "network",     "prepare_for_em", "sam_methods",
      "umi_methods",   "umi_tools",   "version",        "whitelist",
      "whitelist_methods"};
  for (const char* m : kModules)
    if (command == m) return true;
  return false;
}

// Of those submodules, the ones that actually expose `main(argv)`. MEASURED
// with hasattr: count, count_tab, dedup, extract, group, prepare_for_em,
// whitelist — and `umi_tools`, whose main() takes NO arguments, so dispatching
// to it is a TypeError rather than a run.
bool has_tool_main(const std::string& command) {
  static const char* kTools[] = {"whitelist", "extract",   "group",         "dedup",
                                 "count",     "count_tab", "prepare_for_em"};
  for (const char* m : kTools)
    if (command == m) return true;
  return false;
}

}  // namespace

int dispatch(const std::vector<std::string>& argv) {
  // argv includes the program name, as sys.argv does.
  if (argv.size() < 2 || argv[1] == "--help" || argv[1] == "-h") {
    std::cout << kDocsUrl << "\n" << kDoc << "\n";
    return 0;
  }
  if (argv[1] == "--version" || argv[1] == "-v") {
    std::cout << "UMI-tools version: 1.1.6\n";
    return 0;
  }

  std::string command = argv[1];
  if (command == "prepare-for-rsem") command = "prepare-for-em";  // note 1

  if (!is_importable_module(command)) {
    std::cout << "'" << command << "' is not a UMI-tools command. See 'umi_tools -h'.\n\n"
              << kDocsUrl << "\n" << kDoc << "\n";
    return 1;
  }
  // Importable, but not a tool: `module.main(sys.argv)` raises, and the
  // traceback goes to STDERR with nothing on stdout. Two distinct messages,
  // both measured against the live oracle.
  if (!has_tool_main(command)) {
    if (command == "umi_tools")
      std::cerr << "main() takes 0 positional arguments but 1 was given\n";
    else
      std::cerr << "module 'umi_tools." << command << "' has no attribute 'main'\n";
    return 1;
  }

  // del sys.argv[0]; the tool then sees argv starting at the subcommand name and
  // its parser skips that element.
  const std::vector<std::string> tool_argv(argv.begin() + 2, argv.end());

  if (command == "count_tab") {
    tool_count_tab(tool_argv);
    return 0;  // note 2: the tool's return value is discarded upstream
  }
  if (command == "extract") {
    tool_extract(tool_argv);
    return 0;
  }
  if (command == "whitelist") {
    tool_whitelist(tool_argv);
    return 0;
  }
  if (command == "count") {
    tool_count(tool_argv);
    return 0;
  }
  if (command == "group") {
    tool_group(tool_argv);
    return 0;
  }
  if (command == "dedup") {
    tool_dedup(tool_argv);
    return 0;
  }
  if (command == "prepare_for_em") {
    tool_prepare_for_em(tool_argv);
    return 0;
  }

  // Subcommands still to be ported (slices 3-8). This branch has no upstream
  // counterpart and must never be reachable in a released build, so it is loud
  // and uses an exit code no upstream path produces.
  std::cerr << "umi_tools (C++ port): subcommand '" << command
            << "' is not implemented yet in this build. See "
               "the README for what differs from upstream.\n";
  return 70;  // EX_SOFTWARE
}

}  // namespace umi_tools

int main(int argc, char** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
  // sys.argv[0], for the --timeit row's last column.
  umi_tools::set_program_path(argc > 0 ? argv[0] : "");
  try {
    return umi_tools::dispatch(args);
  } catch (const umi_tools::ExitRequest& e) {
    // An intended exit, thrown so that unwinding closes the gzip/BGZF writers
    // first. The message is already formatted.
    std::cerr << e.message;
    return e.code;
  } catch (const std::exception& e) {
    // Python prints a traceback and exits 1 for anything escaping module.main.
    // Without this the exception hit std::terminate -> SIGABRT (rc 134), AND
    // gcc does not unwind on the way there, so output was left truncated too.
    std::cerr << e.what() << "\n";
    return 1;
  }
}
