// tools.hpp — one entry point per subcommand, plus the dispatcher.
//
// Mirrors umi_tools/umi_tools.py::main and each tool module's main(argv).
// Signatures take argv WITHOUT the program name, matching the Python dispatcher,
// which does `del sys.argv[0]` before calling `module.main(sys.argv)`.
#pragma once

#include <string>
#include <vector>

namespace umi_tools {

/// Implemented subcommands (slice order — 07_port_plan.md).
int tool_count_tab(const std::vector<std::string>& argv);
int tool_extract(const std::vector<std::string>& argv);
int tool_whitelist(const std::vector<std::string>& argv);
int tool_count(const std::vector<std::string>& argv);
int tool_group(const std::vector<std::string>& argv);
int tool_dedup(const std::vector<std::string>& argv);
int tool_prepare_for_em(const std::vector<std::string>& argv);

/// umi_tools.py::main. Returns the process exit code.
int dispatch(const std::vector<std::string>& argv);

}  // namespace umi_tools
