// ctest entry point. Grows one case per ported unit; the R/Python-vs-C++
// comparison itself is recorded by validation/ (step 10_parity), not here.
#include "umi_tools/umi_tools.hpp"

#include <cstdio>

int main() {
  // Upstream's __version__ is "1.1.6"; the major version is 1, not the
  // scaffold's 0.
  if (umi_tools::version_major() != 1) {
    std::fprintf(stderr, "version_major mismatch\n");
    return 1;
  }
  return 0;
}
