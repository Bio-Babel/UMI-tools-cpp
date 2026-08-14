cmake_minimum_required(VERSION 3.21)
# VERSION is the PORTED PACKAGE's own version: `umi_tools.__version__` in the
# oracle environment is "1.1.6", and that is the string `umi_tools --version`
# prints on both sides. It is deliberately NOT the git-describe of the pinned
# source tree ("1.1.7.dev53+gc457eefc5", recorded in 00_baseline.md and
# 10_validation.md's pins): CMake requires a numeric major[.minor[.patch]], and a
# consumer asking "which upstream does this port reproduce" gets the answer the
# tool itself reports. The two are not interchangeable and the difference is
# recorded rather than smoothed over.
#
# `version_agrees_with_upstream` (below) fails if this and the binary's printed
# version ever drift apart, because a version that is wrong in one of two places
# is worse than one that is absent.
project(umi_tools VERSION 1.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# Exported so tooling (and the sanitizer step) can see exact compile lines.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(UMI_TOOLS_BUILD_CLI "Build the command-line executable" ON)
option(UMI_TOOLS_BUILD_BINDINGS "Build the optional pybind11 module" OFF)
option(UMI_TOOLS_WERROR "Treat warnings as errors (step 13_docs)" OFF)

# The tutorial reproductions and the whole-corpus fixture driver take tens of
# minutes each on the real data (3.26M reads; 730 MB of FASTQ), and far longer
# under a sanitizer. Registering them unconditionally made a plain `ctest` exceed
# a 30-minute budget, so anyone who did not know to pass `-LE tutorial` saw a
# timeout rather than a result.
#
# They are still the evidence steps 11 and 12 rest on — they are just not part of
# the every-slice loop:
#     cmake --preset default -DUMI_TOOLS_SLOW_TESTS=ON && ctest -L tutorial
option(UMI_TOOLS_SLOW_TESTS
       "Register the tutorial reproductions and the full-corpus driver (slow)" OFF)

# ---------------------------------------------------------------------------
# The warning contract, as ONE target rather than a block repeated per target.
#
# It used to be pasted onto umi_tools_core, the per-case test targets and the
# parity driver only — which left the SHIPPED CLI (src/main.cpp), both hand-built
# test binaries and every example compiling with no -Wall at all, and therefore
# outside the -Werror gate. Anything that links this gets the flags; forgetting
# is now the visible thing rather than the silent one.
# ---------------------------------------------------------------------------
# GCC 12.4 emits 14 -Wmaybe-uninitialized diagnostics from INSIDE libstdc++'s
# <regex> when the NFA builder is inlined at -O2: _NFA::_M_insert_subexpr_end ->
# _State's move ctor -> std::function's move ctor, in regex_automaton.h and
# std_function.h. Not one of them names a line of this port. They are ERRORS
# under the -Werror gate at the asan-opt preset and absent from the default
# preset, which does not inline deeply enough to trip them.
#
# The suppression is per-FILE rather than per-function because the diagnostic is
# raised by the optimiser with no attribution to a source location here — a
# `#pragma GCC diagnostic ignored` around the std::regex construction does not
# silence it (tried; still 14 errors). src/sam_methods_bam.cpp is the only TU
# that constructs a std::regex at this optimisation level.
#
# This was invisible for several sessions because the asan-opt "0 warnings"
# readings came from CACHED builds that compiled nothing. Warning counts mean
# something only from a clean build.
set_source_files_properties(src/sam_methods_bam.cpp PROPERTIES
  COMPILE_OPTIONS "-Wno-maybe-uninitialized")

add_library(umi_tools_warnings INTERFACE)
target_compile_options(umi_tools_warnings INTERFACE
  -Wall -Wextra -Wpedantic
  $<$<BOOL:${UMI_TOOLS_WERROR}>:-Werror>)

# ---------------------------------------------------------------------------
# Core library — the ported logic.
#
# INVARIANT: this target must never link Python or pybind11. Everything the
# port actually does lives here, so the bindings below stay droppable and the
# parity harness can exercise the same code the CLI does.
# ---------------------------------------------------------------------------
add_library(umi_tools_core
  src/umi_tools.cpp
  src/network.cpp
  src/options.cpp
  src/tool_docs.cpp
  src/option_tables.cpp      # GENERATED — see validation/generate_option_tables.py
  src/io.cpp
  src/logging.cpp
  src/sam_methods.cpp
  src/pattern.cpp
  src/fastq.cpp
  src/extract_methods.cpp
  src/whitelist_methods.cpp
  src/tool_extract.cpp
  src/knee.cpp
  src/py_random.cpp
  src/alignment.cpp
  src/sam_methods_bam.cpp
  src/sam_methods_pos.cpp
  src/alignment_write.cpp
  src/tool_whitelist.cpp
  src/tool_count.cpp
  src/sam_options.cpp
  src/tool_group.cpp
  src/tool_dedup.cpp
  src/two_pass_pair_writer.cpp
  src/dedup_stats.cpp
  src/tool_prepare_for_em.cpp
  src/tool_count_tab.cpp
)

# zlib: Utilities.openFile opens .gz/.z transparently, so gzip read+write is a
# functional requirement, not an optimisation (03_data_assets.md).
find_package(ZLIB REQUIRED)
target_link_libraries(umi_tools_core PUBLIC ZLIB::ZLIB)

# htslib — the SAM/BAM/CRAM reader. 6_deps proved it configures, LINKS and runs
# with output identical to pysam. Deliberately NOT pkg_check_modules: the
# cluster's htslib.pc declares `Requires.private: zlib liblzma` and this system
# has no liblzma.pc, so pkg-config fails at configure time even for a dynamic
# link (05_dep_map.md records the failure).
find_path(HTSLIB_INCLUDE_DIR htslib/sam.h HINTS ENV HTSLIB_ROOT
          PATHS /share/software/user/open/htslib/1.16 PATH_SUFFIXES include)
find_library(HTSLIB_LIBRARY NAMES hts HINTS ENV HTSLIB_ROOT
             PATHS /share/software/user/open/htslib/1.16 PATH_SUFFIXES lib)
if(NOT HTSLIB_INCLUDE_DIR OR NOT HTSLIB_LIBRARY)
  message(FATAL_ERROR "htslib not found: inc=${HTSLIB_INCLUDE_DIR} lib=${HTSLIB_LIBRARY}")
endif()
add_library(htslib_hts UNKNOWN IMPORTED)
set_target_properties(htslib_hts PROPERTIES
  IMPORTED_LOCATION "${HTSLIB_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${HTSLIB_INCLUDE_DIR}")
target_link_libraries(umi_tools_core PUBLIC htslib_hts)
message(STATUS "htslib: ${HTSLIB_LIBRARY}")

# The shipped fixtures double as goldens for the --help output, so the tests need
# their location. There is deliberately NO default: the fixtures live in the
# upstream Python repo, whose location is a property of the machine, and a default
# that happened to be right on one machine is a path that silently points at
# nothing everywhere else. Point it at the upstream checkout's tests/ directory:
#
#   cmake --preset default -DUMI_TOOLS_FIXTURES_DIR=/path/to/UMI-tools/tests
#
# Use a copy that stays byte-identical to the install being compared against, so
# the oracle and the fixtures cannot drift apart (see 00_baseline.md).
# The default comes from the environment rather than from this file, so the
# supervisor's `cmake --preset default` works without the preset carrying anyone's
# path. Export it in the run's env.sh next to the oracle environment.
set(UMI_TOOLS_FIXTURES_DIR "$ENV{UMI_TOOLS_FIXTURES_DIR}" CACHE PATH
    "Directory holding the upstream tests/ fixtures and goldens (required)")
if(NOT UMI_TOOLS_FIXTURES_DIR)
  message(FATAL_ERROR
    "UMI_TOOLS_FIXTURES_DIR is not set. The differential tests compare against "
    "the upstream fixtures and --help goldens, so without it they would either "
    "fail at run time or, worse, be skipped and read as passing. Set the "
    "environment variable, or configure with "
    "-DUMI_TOOLS_FIXTURES_DIR=<upstream>/tests")
elseif(NOT IS_DIRECTORY "${UMI_TOOLS_FIXTURES_DIR}")
  message(FATAL_ERROR
    "UMI_TOOLS_FIXTURES_DIR=${UMI_TOOLS_FIXTURES_DIR} is not a directory")
endif()
target_include_directories(umi_tools_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(umi_tools_core PRIVATE umi_tools_warnings)

# ---------------------------------------------------------------------------
# Tests (ctest) — the third stage of the build gate.
#
# py_compat is tested separately and permanently: it encodes Python's integer
# semantics, so its expectations are the measured Python values, not this
# port's behaviour. Keeping it out of test_umi_tools.cpp means the port's own
# cases can grow without touching it.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(umi_tools_tests tests/test_umi_tools.cpp)
target_link_libraries(umi_tools_tests PRIVATE umi_tools_core umi_tools_warnings)
add_test(NAME umi_tools_tests COMMAND umi_tools_tests)

add_executable(umi_tools_py_compat_tests tests/test_py_compat.cpp)
target_link_libraries(umi_tools_py_compat_tests PRIVATE umi_tools_core umi_tools_warnings)
add_test(NAME umi_tools_py_compat COMMAND umi_tools_py_compat_tests)

# ---------------------------------------------------------------------------
# Per-case ctest registration.
#
# 8_port requires ONE CTEST PER CASE (`add_test(NAME <case> ...)`): a single
# registration per executable reports two tests no matter how many cases exist,
# and then neither ctest output nor test_coverage_diff.py can say which case
# broke. The case list is discovered by running the test binary with --list at
# CONFIGURE time, so adding a UMI_TEST_CASE needs no CMake edit.
#
# The binary must exist for --list to work, so it is built eagerly during
# configure via try_compile-free means: a small build of the target first. If it
# is not yet built (a clean tree), fall back to registering the whole binary and
# re-run configure after the first build to expand it.
# ---------------------------------------------------------------------------
function(umi_tools_register_cases target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE umi_tools_core)
  target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
  target_compile_definitions(${target} PRIVATE
    UMI_TOOLS_FIXTURES_DIR="${UMI_TOOLS_FIXTURES_DIR}")
  target_link_libraries(${target} PRIVATE umi_tools_warnings)

  # Parse the case names out of the source directly: UMI_TEST_CASE(name).
  # Reading the source is deterministic, needs no built binary, and cannot go
  # stale relative to the file it parses.
  file(READ ${source} _src)
  string(REGEX MATCHALL "UMI_TEST_CASE\\(([A-Za-z0-9_]+)\\)" _matches "${_src}")
  set(_count 0)
  foreach(_m IN LISTS _matches)
    string(REGEX REPLACE "UMI_TEST_CASE\\(([A-Za-z0-9_]+)\\)" "\\1" _case "${_m}")
    add_test(NAME ${_case} COMMAND ${target} --case ${_case})
    math(EXPR _count "${_count} + 1")
  endforeach()
  message(STATUS "${target}: registered ${_count} individual ctest cases")
endfunction()

umi_tools_register_cases(umi_tools_network_tests tests/test_network.cpp)
umi_tools_register_cases(umi_tools_options_tests tests/test_options.cpp)
umi_tools_register_cases(umi_tools_extract_tests tests/test_extract.cpp)
umi_tools_register_cases(umi_tools_knee_tests tests/test_knee.cpp)
umi_tools_register_cases(umi_tools_readpos_tests tests/test_readpos.cpp)
umi_tools_register_cases(umi_tools_output_tests tests/test_output.cpp)

# ---------------------------------------------------------------------------
# One ctest per SHIPPED FIXTURE, registered under the fixture's own name.
#
# 8_port requires that every source test case have a target test with the same
# expectation. The upstream suite's real cases live in tests/tests.yaml (69 of
# them), so each becomes a ctest named EXACTLY as upstream names it, running the
# built C++ CLI and the live oracle on that fixture and diffing them with
# upstream's own comparison contract. Naming them identically is also what lets
# test_coverage_diff.py match them.
#
# Fixtures whose subcommand is not ported yet exit 77 and ctest reports them as
# "Skipped" — visible and counted, never silently absent.
# ---------------------------------------------------------------------------
set(UMI_TOOLS_ORACLE_PY "/scratch/users/xzchen/mamba/envs/umi-oracle/bin/python"
    CACHE FILEPATH "Python interpreter of the ORACLE environment (test-time only)")
set(UMI_TOOLS_ORACLE_CLI "/scratch/users/xzchen/mamba/envs/umi-oracle/bin/umi_tools"
    CACHE FILEPATH "The oracle's umi_tools console script (test-time only)")
set(UMI_TOOLS_PY_SOURCE "/home/users/xzchen/projects/Bio-Babel/UMI-tools"
    CACHE PATH "The upstream Python source tree (test-time only; SF-02's audit-surface check reads it)")
set(UMI_TOOLS_RESULTS_RECORDER
    "/home/users/xzchen/.claude/plugins/cache/bio-babel-toolkit/bio-babel-toolkit/0.3.0/skills/py2cpp-skills/scripts/results_recorder.py"
    CACHE FILEPATH "scripts/results_recorder.py — the canonical parity recorder")

# `validation/` holds the developer-facing parity harnesses. It is NOT part of the
# released package, so every test that drives a script from it must be registered
# CONDITIONALLY. MEASURED without it: 233 tests registered and 131 of them failed,
# because the scripts they name were absent. The 102 self-contained C++ cases are
# unaffected and stay green, which is what a release clone must see.
set(UMI_TOOLS_HAVE_VALIDATION FALSE)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/validation")
  set(UMI_TOOLS_HAVE_VALIDATION TRUE)
endif()

if(UMI_TOOLS_BUILD_CLI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/validation/fixture_manifest.json")
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/validation/fixture_manifest.json" _manifest)
  string(JSON _n_fixtures LENGTH "${_manifest}" entries)
  math(EXPR _last "${_n_fixtures} - 1")
  set(_registered 0)
  foreach(_i RANGE ${_last})
    string(JSON _fixture_name GET "${_manifest}" entries ${_i} name)
    add_test(NAME ${_fixture_name}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_cli.py
                     --bin $<TARGET_FILE:umi_tools>
                     --manifest ${CMAKE_CURRENT_SOURCE_DIR}/validation/fixture_manifest.json
                     --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_fixture_name})
    set_tests_properties(${_fixture_name} PROPERTIES SKIP_RETURN_CODE 77)
    math(EXPR _registered "${_registered} + 1")
  endforeach()
  message(STATUS "shipped fixtures: registered ${_registered} ctest cases "
                 "(unported subcommands report as Skipped)")
endif()

# ---------------------------------------------------------------------------
# Purpose-built parity harnesses, one ctest per case.
#
# These cover units the SHIPPED suite does not reach at all: prepare_for_em has
# zero fixtures (00_baseline.md F3), and the dispatcher has exactly one.
# ---------------------------------------------------------------------------
if(UMI_TOOLS_BUILD_CLI AND UMI_TOOLS_HAVE_VALIDATION)
  foreach(_case paired_coord_sorted paired_name_sorted single_end_no_read12
                tags_default tags_absent tags_multi bam_output
                bam_wide_int_tags)
    add_test(NAME prepare_for_em_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_prepare_for_em.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  foreach(_case filter_umi_top5 filter_umi_top25 filter_umi_top200)
    add_test(NAME extract_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_extract_filter_umi.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  foreach(_case dedup_paired_v0 dedup_paired_v1 dedup_paired_v2 dedup_paired_v3
                error_path_v0 error_path_v1 error_path_v2 error_path_v3)
    add_test(NAME logging_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_logging.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  # Every --method value, not just the two the corpus pairs with
  # --output-stats. See the harness docstring.
  foreach(_case unique percentile cluster adjacency directional)
    add_test(NAME dedup_stats_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_dedup_stats.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  foreach(_case n10 n0 n-1 n-61 subsetreads0 expectcells0)
    add_test(NAME cellnumber_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_whitelist_cellnumber.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_case})
  endforeach()
  foreach(_case class alt escape noncap lazy mixed)
    add_test(NAME regex_ext_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_pattern_extended.py
                     --driver $<TARGET_FILE:umi_tools_parity_driver> --case ${_case})
  endforeach()
  foreach(_case nospill spill_small spill_tiny)
    add_test(NAME sort_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_sort_spill.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  add_test(NAME parity_textwrap
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_textwrap.py
                   --driver $<TARGET_FILE:umi_tools_parity_driver>)
  foreach(_case dedup group count extract whitelist count_tab prepare_for_em)
    add_test(NAME help_extended_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_help_extended.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_case})
  endforeach()
  add_test(NAME parity_barcodes
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_barcodes.py
                   --driver $<TARGET_FILE:umi_tools_parity_driver>
                   --tests-dir ${UMI_TOOLS_FIXTURES_DIR})
  # neither example may ABORT on a library error_exit, and the gzip
  # trailer must survive. See validation/check_example_exit.py.
  add_test(NAME example_exit_QUICK_START
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/check_example_exit.py
                   $<TARGET_FILE:example_QUICK_START>
                   ${UMI_TOOLS_FIXTURES_DIR}/chr19.bam)
  foreach(_case empty_umi_separator empty_tags_is_accepted
                empty_umi_group_tag_is_rejected missing_reference_file
                missing_input_file
                ignore_umi_per_cell_missing_tag_dedup
                ignore_umi_per_cell_missing_tag_count
                per_cell_missing_tag_is_skipped
                ignore_umi_group_out_aborts
                start_opens_stdout_before_validation_dedup
                start_opens_stdout_before_validation_count
                start_opens_stdout_before_validation_group
                start_touch_does_not_clobber_output
                malformed_fastq_third_line
                filter_cell_barcode_without_whitelist
                filter_cell_barcode_with_whitelist
                prepare_for_em_empty_bam set_cell_number_overflow_message
                single_end_read2_out sort_output_write_failure
                either_read_quality_empty_umi_quals either_read_quality_both_umis
                paired_whitelist_empty_progress_log
                quals_shorter_than_seq dedup_stats_unindexed_bam
                extract_start_opens_before_validation paired_read2_shorter
                read_shorter_than_pattern gene_transcript_map_three_columns
                count_tab_one_column either_read_and_filter_umi_order
                malformed_regex_before_either_read no_umi_group_message_renders_none
                group_out_without_output_bam_skips_tag_check
                output_stats_zero_is_truthy
                absent_nh_tag_is_a_keyerror optional_group_that_did_not_participate
                ed_above_threshold_with_no_knee
                cell_thresholds_written_before_the_error
                cell_thresholds_selected_marking
                missing_tag_warning_is_the_full_record
                no_mate_warn_renders_none_contig)
    add_test(NAME hostile_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_hostile_inputs.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  # The degeneracy boundary of --knee-method=density. Straddles it deliberately:
  # extra=0 is exactly-zero log-count variance (scipy's Cholesky fails), extra>0 is
  # a variance as small as 4.3e-6 (it succeeds). The claim this replaces was that
  # scipy's check is non-monotonic in N; it is not, and the sweep now shows so.
  foreach(_case 8_0 8_1 8_2 20_0 20_1 20_2 40_0 40_1 40_2
                100_0 100_1 100_2 400_0 400_1 400_2)
    add_test(NAME knee_degenerate_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_knee_degenerate.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_case})
  endforeach()
  # SF-02: the 18 public names the checklist does not list, each resolved to
  # covered/dead by derivation from the Python source. Registered so 01_audit.md's
  # Appendix B cannot go stale silently.
  add_test(NAME audit_surface
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/check_audit_surface.py
                   --src ${UMI_TOOLS_PY_SOURCE} --cpp ${CMAKE_CURRENT_SOURCE_DIR})
  # SF-01: the per-subcommand CLI error region no tutorial reaches — `--help` at a
  # pinned COLUMNS, one invalid option value, and no arguments at all.
  foreach(_case whitelist extract group dedup count count_tab prepare_for_em)
    add_test(NAME cli_error_paths_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_cli_error_paths.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_case})
  endforeach()
  # SF-03/SF-04: pysam's five open-time checks and the fetch() index requirement.
  # `no_eof.bam` is the one that was not a message difference — the port used to
  # exit 0 with partial output on a BAM missing its BGZF EOF block — and
  # `good.bam` is a copy made WITHOUT its .bai, which is why no shipped fixture
  # could reach the index gate.
  foreach(_case empty.bam empty.sam text.bam text.sam cut_header.bam no_eof.bam
                no_header.sam hd_only.sam good.bam good.sam)
    add_test(NAME malformed_aln_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_malformed_alignment.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --tests-dir ${UMI_TOOLS_FIXTURES_DIR} --case ${_case})
  endforeach()
  # Upstream's tests/test_umi_tools.py::test_script_has_main — named identically,
  # as the 69 fixture ctests are, so test_coverage_diff.py can match it.
  if(UMI_TOOLS_SLOW_TESTS)
  # Upstream's tests/test_umi_tools.py::test_script is the PARAMETRISED DRIVER
  # for all 69 fixtures. The port covers each of those individually, but through
  # a different code path than parity_cli.py's BATCH mode -- and a stale guard in
  # that batch path silently ran 68 of 69 for several sessions before step 10
  # caught it. Registering the driver itself, named as upstream names it, is a
  # real test with real value rather than a rename for the coverage matcher.
  add_test(NAME test_script
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_cli.py
                   --bin $<TARGET_FILE:umi_tools>
                   --manifest ${CMAKE_CURRENT_SOURCE_DIR}/validation/fixture_manifest.json
                   --oracle ${UMI_TOOLS_ORACLE_CLI}
                   --results ${CMAKE_CURRENT_SOURCE_DIR}/validation/results_step10_fixtures.csv)
  set_tests_properties(test_script PROPERTIES LABELS tutorial TIMEOUT 3600)
  endif()

  add_test(NAME test_script_has_main
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/test_script_has_main.py
                   --bin $<TARGET_FILE:umi_tools>)
  add_test(NAME parity_metacontig
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_metacontig.py
                   --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                   --driver $<TARGET_FILE:umi_tools_parity_driver>
                   --tests-dir ${UMI_TOOLS_FIXTURES_DIR})
  add_test(NAME parity_bam_features
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_bam_features.py
                   --driver $<TARGET_FILE:umi_tools_parity_driver>
                   --tests-dir ${UMI_TOOLS_FIXTURES_DIR})
  add_test(NAME parity_output_naming
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_output.py
                   --driver $<TARGET_FILE:umi_tools_parity_driver>)

  # The report tree's numeric claims, verified against the live artifacts.
  # One session wrote up "a summary heading stops being read once the work moves
  # past it" and then left two headings at 155 rows while the result table said
  # 159 — in the same session, by the same hand. Resolving to be careful did not
  # work; this runs. Only claims that opt in with a `<!-- check:... -->` marker
  # are verified, so the append-only log's correctly-dated history is untouched.
  add_test(NAME report_claims_match_evidence
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/check_report_claims.py
                   --build ${CMAKE_BINARY_DIR})
  foreach(_case version_long version_short help_short no_arguments
                unknown_subcommand broken_alias_prepare_for_em
                broken_alias_prepare_for_rsem dispatch_to_subcommand
                submodule_network submodule_umi_methods submodule_Utilities
                submodule_umi_tools submodule_version
                help_then_bad_token version_then_bad_token bad_token_then_help
                count_header_after_loop_logs)
    add_test(NAME dispatch_${_case}
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_dispatch.py
                     --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                     --case ${_case})
  endforeach()
endif()

# ---------------------------------------------------------------------------
# Parity driver (step 10_parity). Library units are driven through this rather
# than through pybind11, because bindings are OFF for this port (stage A).
# Built by the `default` preset, which is the build parity is measured on.
# ---------------------------------------------------------------------------
add_executable(umi_tools_parity_driver src/parity_driver.cpp)
target_link_libraries(umi_tools_parity_driver PRIVATE umi_tools_core)
target_link_libraries(umi_tools_parity_driver PRIVATE umi_tools_warnings)

# ---------------------------------------------------------------------------
# Optional CLI executable — only if the Python source had console_scripts.
# ---------------------------------------------------------------------------
if(UMI_TOOLS_BUILD_CLI)
  add_executable(umi_tools src/main.cpp)
  target_link_libraries(umi_tools PRIVATE umi_tools_core umi_tools_warnings)
endif()

# ---------------------------------------------------------------------------
# Optional Python bindings — a SEPARATE target. Deleting this block (and
# bindings/) leaves the core library untouched.
# ---------------------------------------------------------------------------
if(UMI_TOOLS_BUILD_BINDINGS)
  find_package(pybind11 CONFIG REQUIRED)
  # umi_tools_core is a STATIC library, and linking one into a shared
  # module needs position-independent code:
  #   relocation R_X86_64_32S against `.rodata' can not be used when making a
  #   shared object; recompile with -fPIC
  # Set only in this branch, so the default and sanitizer builds keep exactly
  # the codegen every parity and UB measurement was taken against.
  set_property(TARGET umi_tools_core PROPERTY POSITION_INDEPENDENT_CODE ON)
  pybind11_add_module(umi_tools_native bindings/bindings.cpp)
  target_link_libraries(umi_tools_native PRIVATE umi_tools_core)

  # The bound API is compared against the LIVE Python. The module and
  # the oracle cannot share an interpreter here (the only pybind11 available is
  # pinned to 3.12; the oracle env is 3.11), so the harness runs under the
  # ORACLE's interpreter and shells out to 3.12 for the module — the same
  # two-process shape every CLI harness already uses.
  if(UMI_TOOLS_ORACLE_PY AND UMI_TOOLS_HAVE_VALIDATION)
    add_test(NAME parity_bindings
             COMMAND ${UMI_TOOLS_ORACLE_PY}
                     ${CMAKE_CURRENT_SOURCE_DIR}/validation/parity_bindings.py
                     --module-dir $<TARGET_FILE_DIR:umi_tools_native>)
  endif()
endif()

# ---------------------------------------------------------------------------
# Examples (step 12_examples): one runnable program per tutorial. Added here as
# they are written, so `cmake --build` proves they still compile and run.
#
# They are also the 9_sanitize CORPUS: the gate requires the sanitizers over
# "every example program and the parity inputs from 4_data", and an example that
# shelled out to the CLI would put no ported code under them.
# ---------------------------------------------------------------------------
set(UMI_TOOLS_TUTORIAL_DATA "/scratch/users/xzchen/umi-cpp-run/data"
    CACHE PATH "Where 4_data staged the tutorials' real inputs")
file(GLOB UMI_TOOLS_EXAMPLE_SOURCES CONFIGURE_DEPENDS examples/*.cpp)
# The tutorial reproductions. LABEL "tutorial" because each takes tens of
# minutes on the real data (3.26M reads / 730 MB of FASTQ) -- far too slow for
# the every-slice loop, but they are the artifacts steps 9 and 11 are ABOUT:
#     ctest -L tutorial
if(UMI_TOOLS_SLOW_TESTS AND UMI_TOOLS_BUILD_CLI AND UMI_TOOLS_HAVE_VALIDATION
   AND EXISTS "${UMI_TOOLS_TUTORIAL_DATA}/quick_start/example.bam")
  add_test(NAME tutorial_QUICK_START
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/tutorial_QUICK_START.py
                   --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                   --data ${UMI_TOOLS_TUTORIAL_DATA}/quick_start
                   --results ${CMAKE_CURRENT_SOURCE_DIR}/validation/results_QUICK_START.csv
                   --recorder ${UMI_TOOLS_RESULTS_RECORDER})
  set_tests_properties(tutorial_QUICK_START PROPERTIES LABELS tutorial TIMEOUT 7200)
endif()
if(UMI_TOOLS_SLOW_TESTS AND UMI_TOOLS_BUILD_CLI AND UMI_TOOLS_HAVE_VALIDATION
   AND EXISTS "${UMI_TOOLS_TUTORIAL_DATA}/single_cell/hgmm_100_R1.fastq.gz")
  add_test(NAME tutorial_Single_cell
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/tutorial_Single_cell_tutorial.py
                   --bin $<TARGET_FILE:umi_tools> --oracle ${UMI_TOOLS_ORACLE_CLI}
                   --data ${UMI_TOOLS_TUTORIAL_DATA}/single_cell
                   --tests-dir ${UMI_TOOLS_FIXTURES_DIR}
                   --results ${CMAKE_CURRENT_SOURCE_DIR}/validation/results_Single_cell_tutorial.csv
                   --recorder ${UMI_TOOLS_RESULTS_RECORDER})
  set_tests_properties(tutorial_Single_cell PROPERTIES LABELS tutorial TIMEOUT 14400)
endif()

foreach(example_src IN LISTS UMI_TOOLS_EXAMPLE_SOURCES)
  get_filename_component(example_name "${example_src}" NAME_WE)
  add_executable(example_${example_name} "${example_src}")
  target_link_libraries(example_${example_name} PRIVATE umi_tools_core umi_tools_warnings)
endforeach()

# ---------------------------------------------------------------------------
# Install rules.
#
# Without these, `cmake --install` exits 0 and installs NOTHING — a silent
# success, which is the failure mode this port keeps finding elsewhere. The
# package was usable only from inside its own build tree.
#
# What a consumer needs, and nothing more: the CLI on PATH, the public headers,
# and the core library the headers describe. `umi_tools_core` is static, so
# installing it without a package config is of limited use for `find_package` —
# that is stated rather than implied, and the headers plus the archive are enough
# to link by hand.
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
if(UMI_TOOLS_BUILD_CLI)
  # htslib is a MODULE on this cluster, outside any default loader path, so an
  # installed binary with no RPATH only runs where LD_LIBRARY_PATH already points
  # at it — i.e. it would appear to work here and fail for the consumer this
  # install rule exists to serve. Give it htslib's directory explicitly.
  #
  # BUILD_WITH_INSTALL_RPATH also sidesteps a real install-time failure: CMake
  # rewrites RPATH on install, and it aborted with "the current RPATH is
  # /share/.../htslib/1.16/lib, which does not contain /share/.../htslib/1.16/lib:
  # as was expected" — the recorded build RPATH carries a trailing separator the
  # linker did not write. Linking with the final RPATH from the start means there
  # is no rewrite to disagree about.
  get_filename_component(UMI_TOOLS_HTSLIB_LIBDIR "${HTSLIB_LIBRARY}" DIRECTORY)
  # htslib is not the only module-provided runtime dependency. MEASURED: with
  # LD_LIBRARY_PATH cleared, the installed binary died on
  # `/lib64/libstdc++.so.6: version GLIBCXX_3.4.20 not found` — it is built by
  # gcc 12.4.0 from a module and the system libstdc++ predates it. So the
  # compiler's own implicit link directories go in the RPATH too, minus the
  # default system ones, which the loader finds anyway and which would only make
  # the RPATH misleading.
  set(UMI_TOOLS_RPATH "${UMI_TOOLS_HTSLIB_LIBDIR}")
  foreach(_dir IN LISTS CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES)
    if(NOT _dir MATCHES "^/(usr/)?lib(64|32)?$")
      list(APPEND UMI_TOOLS_RPATH "${_dir}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES UMI_TOOLS_RPATH)
  set_target_properties(umi_tools PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "${UMI_TOOLS_RPATH}")
  install(TARGETS umi_tools RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(TARGETS umi_tools_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/umi_tools DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp")

# The two packaging properties a green suite is silent about: that the declared
# version matches the tool's own (and the oracle's), and that `cmake --install`
# installs something that runs. See validation/check_packaging.py.
if(UMI_TOOLS_BUILD_CLI AND UMI_TOOLS_HAVE_VALIDATION)
  add_test(NAME version_agrees_with_upstream
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/check_packaging.py
                   --case version --bin $<TARGET_FILE:umi_tools>
                   --oracle ${UMI_TOOLS_ORACLE_CLI} --version ${PROJECT_VERSION})
  add_test(NAME install_places_cli_and_headers
           COMMAND ${UMI_TOOLS_ORACLE_PY}
                   ${CMAKE_CURRENT_SOURCE_DIR}/validation/check_packaging.py
                   --case install --build-dir ${CMAKE_BINARY_DIR})
endif()
