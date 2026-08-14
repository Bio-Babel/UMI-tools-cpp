#include "umi_tools/umi_tools.hpp"

namespace umi_tools {

// this returned 0 — the scaffold's placeholder, which survived into
// the shipped public header and the pybind11 surface. The package reports
// __version__ = "1.1.6" (and main.cpp prints that same string), so the major
// version is 1. `umi_tools_native.version_major()` disagreed with
// `umi_tools.__version__.split('.')[0]`, and tests/test_umi_tools.cpp PINNED
// the wrong answer.
int version_major() { return 1; }

}  // namespace umi_tools
