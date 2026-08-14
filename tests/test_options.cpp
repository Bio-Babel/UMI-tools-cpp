// Slice 0: the optparse-compatible parser and help formatter.
//
// The --help cases compare against the SHIPPED GOLDENS byte for byte. That is
// legitimate here rather than golden-emulation: the baseline gate reproduced
// every one of those goldens from the current Python source, so for these
// fixtures golden == live oracle (00_baseline.md). The parity harness re-checks
// them against the live oracle directly.
#include "umi_tools/options.hpp"
#include "test_harness.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace umi_tools;

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) umi_tools_test::fail("cannot open golden: " + path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// Report the FIRST differing line, with both sides and a caret, because a
// byte-diff of a 4 KB help page is unreadable otherwise.
void compare_help(const std::string& tool, const std::string& golden_name) {
  const ToolSpec* spec = find_tool_spec(tool);
  if (spec == nullptr) umi_tools_test::fail("no ToolSpec for " + tool);

  const std::string got = format_help(*spec);
  const std::string want = read_file(std::string(UMI_TOOLS_FIXTURES_DIR) + "/" + golden_name);

  if (got == want) return;

  std::istringstream gs(got), ws(want);
  std::string gl, wl;
  int line = 0;
  while (true) {
    const bool g_ok = static_cast<bool>(std::getline(gs, gl));
    const bool w_ok = static_cast<bool>(std::getline(ws, wl));
    ++line;
    if (!g_ok && !w_ok) break;
    if (!g_ok) umi_tools_test::fail(tool + ": C++ output ended at line " +
                                   std::to_string(line) + "; golden has: [" + wl + "]");
    if (!w_ok) umi_tools_test::fail(tool + ": golden ended at line " +
                                   std::to_string(line) + "; C++ has: [" + gl + "]");
    if (gl != wl) {
      std::ostringstream os;
      os << tool << ": first difference at line " << line << "\n"
         << "  golden: [" << wl << "]\n"
         << "  cpp   : [" << gl << "]\n"
         << "  golden len=" << wl.size() << " cpp len=" << gl.size();
      umi_tools_test::fail(os.str());
    }
  }
  umi_tools_test::fail(tool + ": lines identical but bytes differ (trailing newline?)");
}

}  // namespace

// ---------------------------------------------------------------------------
// textwrap — the behaviours that change the golden if wrong
// ---------------------------------------------------------------------------
UMI_TEST_CASE(textwrap_preserves_internal_whitespace_runs) {
  // The shipped --barcode-separator help contains a DOUBLE space, and the
  // golden keeps it. textwrap splits into word/whitespace chunks and rejoins
  // them verbatim; it does not collapse runs.
  const auto lines = textwrap_wrap("separator between read id and UMI  and (optionally) the cell barcode", 54);
  CHECK_EQ(static_cast<int>(lines.size()), 2);
  CHECK_EQ(lines[0], std::string("separator between read id and UMI  and (optionally)"));
  CHECK_EQ(lines[1], std::string("the cell barcode"));
}

UMI_TEST_CASE(textwrap_drops_leading_and_trailing_whitespace) {
  const auto lines = textwrap_wrap("aaa bbb ccc", 7);
  CHECK_EQ(static_cast<int>(lines.size()), 2);
  CHECK_EQ(lines[0], std::string("aaa bbb"));
  CHECK_EQ(lines[1], std::string("ccc"));
}

UMI_TEST_CASE(textwrap_does_not_split_double_dash_options) {
  // break_on_hyphens must NOT split "--stdin/--stdout"; a hyphen run is not a
  // break point. Splitting it would corrupt many help lines.
  const auto lines = textwrap_wrap("use --stdin and --stdout for io", 12);
  for (const auto& l : lines) CHECK(l.rfind("-", 0) != 0 || l == "--stdout");
  CHECK(!lines.empty());
}

UMI_TEST_CASE(textwrap_breaks_long_word) {
  const auto lines = textwrap_wrap("aaaaaaaaaaaaaaaaaaaa", 8);
  CHECK_EQ(static_cast<int>(lines.size()), 3);
  CHECK_EQ(lines[0], std::string("aaaaaaaa"));
}

UMI_TEST_CASE(textwrap_empty_text_yields_no_lines) {
  CHECK_EQ(static_cast<int>(textwrap_wrap("", 20).size()), 0);
}

// ---------------------------------------------------------------------------
// format_option_strings — optparse's metavar rules
// ---------------------------------------------------------------------------
UMI_TEST_CASE(option_strings_metavar_from_dest_uppercased) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  CHECK(spec != nullptr);
  bool found_method = false, found_stdin = false, found_per_cell = false;
  for (const auto& g : spec->groups) {
    for (const auto& o : g.options) {
      const std::string s = format_option_strings(o, spec->short_first);
      if (o.dest != nullptr && std::string(o.dest) == "method" && !o.choices.empty()) {
        CHECK_EQ(s, std::string("--method=METHOD"));
        found_method = true;
      }
      if (s == "-I FILE, --stdin=FILE") found_stdin = true;    // explicit metavar
      if (s == "--per-cell") found_per_cell = true;            // no value
    }
  }
  CHECK(found_method);
  CHECK(found_stdin);
  CHECK(found_per_cell);
}

// ---------------------------------------------------------------------------
// --help goldens, byte for byte, for all 7 subcommands
// ---------------------------------------------------------------------------
UMI_TEST_CASE(help_golden_count_tab) { compare_help("count_tab", "count_tab_help"); }
UMI_TEST_CASE(help_golden_count) { compare_help("count", "count_help"); }
UMI_TEST_CASE(help_golden_dedup) { compare_help("dedup", "dedup_help"); }
UMI_TEST_CASE(help_golden_extract) { compare_help("extract", "extract_help"); }
UMI_TEST_CASE(help_golden_group) { compare_help("group", "group_help"); }
UMI_TEST_CASE(help_golden_whitelist) { compare_help("whitelist", "whitelist_help"); }

// ---------------------------------------------------------------------------
// parse_args
// ---------------------------------------------------------------------------
UMI_TEST_CASE(parse_applies_defaults) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto r = parse_args(*spec, {});
  CHECK_EQ(r.values.get_string("method"), std::string("directional"));
  CHECK_EQ(r.values.get_int("threshold"), 1);
  CHECK_EQ(r.values.get_string("bc_sep"), std::string("_"));
  CHECK(!r.values.get_bool("per_cell"));
}

UMI_TEST_CASE(parse_long_option_with_equals) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto r = parse_args(*spec, {"--method=unique", "--barcode-separator=:"});
  CHECK_EQ(r.values.get_string("method"), std::string("unique"));
  CHECK_EQ(r.values.get_string("bc_sep"), std::string(":"));
}

UMI_TEST_CASE(parse_long_option_separate_value) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto r = parse_args(*spec, {"--method", "cluster"});
  CHECK_EQ(r.values.get_string("method"), std::string("cluster"));
}

UMI_TEST_CASE(parse_store_true) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto r = parse_args(*spec, {"--per-cell"});
  CHECK(r.values.get_bool("per_cell"));
}

UMI_TEST_CASE(parse_short_option_attached_and_separate) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto a = parse_args(*spec, {"-L", "test.log"});
  CHECK_EQ(a.values.get_string("stdlog"), std::string("test.log"));
  const auto b = parse_args(*spec, {"-Ltest.log"});
  CHECK_EQ(b.values.get_string("stdlog"), std::string("test.log"));
}

UMI_TEST_CASE(parse_invalid_choice_message_matches_optparse) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  bool caught = false;
  try {
    parse_args(*spec, {"--method=nonesuch"});
  } catch (const std::invalid_argument& e) {
    caught = true;
    const std::string msg = e.what();
    CHECK(msg.rfind("option --method: invalid choice: 'nonesuch' (choose from ", 0) == 0);
  }
  CHECK(caught);
}

UMI_TEST_CASE(parse_unknown_option_throws) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  CHECK_THROWS_AS(parse_args(*spec, {"--no-such-flag"}), std::invalid_argument);
}

UMI_TEST_CASE(parse_help_and_version_flags) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  CHECK(parse_args(*spec, {"--help"}).wants_help);
  CHECK(parse_args(*spec, {"-h"}).wants_help);
  CHECK(parse_args(*spec, {"--version"}).wants_version);
}

UMI_TEST_CASE(parse_positional_args_collected) {
  const ToolSpec* spec = find_tool_spec("count_tab");
  const auto r = parse_args(*spec, {"--per-cell", "extra1", "extra2"});
  CHECK_EQ(static_cast<int>(r.args.size()), 2);
  CHECK_EQ(r.args[0], std::string("extra1"));
}

UMI_TEST_CASE(all_seven_tools_have_specs) {
  for (const char* t : {"whitelist", "extract", "group", "dedup", "count", "count_tab",
                        "prepare_for_em"})
    CHECK(find_tool_spec(t) != nullptr);
  CHECK(find_tool_spec("nonesuch") == nullptr);
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
