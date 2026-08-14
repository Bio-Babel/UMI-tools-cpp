# umi_tools-cpp

C++ port of the Python [**UMI-tools**](https://github.com/CGATOxford/UMI-tools)
package (tracks umi_tools 1.1.7.dev53+gc457eefc5).

All seven subcommands are ported — `whitelist`, `extract`, `group`, `dedup`,
`count`, `count_tab`, `prepare_for_em` — plus the top-level dispatcher, and all
121 public units are parity-validated <!-- check:units_total -->. Output is
compared against the live Python implementation rather than against shipped
goldens; the deviations that remain are enumerated in the port's validation
record.

## Build

Requires g++ 12+, CMake 3.21+, Ninja, htslib and zlib. Where htslib is not on the
default search path, point CMake at it:

```bash
export HTSLIB_ROOT=/path/to/htslib
cmake --preset default && cmake --build --preset default && ctest --preset default
```

`ctest` runs the self-contained cases and needs no Python; the harnesses that
compare against the live Python are developer-facing and not part of this tree.

```bash
cmake --install build --prefix /usr/local
```

installs the CLI, the static core library and the public headers under the
`GNUInstallDirs` locations.

## Quick Start

```bash
umi_tools dedup -I mapped.bam -S deduplicated.bam --log=dedup.log
umi_tools whitelist --bc-pattern=CCCCCCCCCCCCCCCCNNNNNNNNNN --stdin=R1.fastq.gz > whitelist.txt
```

The core library is usable without the CLI. `umi_tools_core` never links Python:

```cpp
#include <umi_tools/network.hpp>

umi_tools::UmiCounts counts;
counts[umi_tools::Bytes("ATAT")] = 10;
counts[umi_tools::Bytes("GTAT")] = 5;

umi_tools::UMIClusterer clusterer(umi_tools::ClusterMethod::Directional);
auto groups = clusterer(counts, 1);   // -> {{"ATAT", "GTAT"}}
```

Sanitizer and warnings-clean configurations are the `asan`, `asan-opt` and
`werror` presets. `-DUMI_TOOLS_BUILD_BINDINGS=ON` adds an optional pybind11
module; `examples/` builds one program per upstream tutorial.

## Documentation

```bash
doxygen Doxyfile
```

## License

MIT, as upstream. See `LICENSE`, which carries the original copyright.
