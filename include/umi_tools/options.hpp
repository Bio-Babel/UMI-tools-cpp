// options.hpp — an optparse-compatible option parser and help formatter.
//
// Scope was MEASURED, not guessed (principle 9). Across the whole package there
// are 110 `add_option` calls and 20 option groups, using:
//
//     actions : store (default), store_true, callback   [3 of optparse's 8]
//     types   : string/str, choice, int, float          [4 of optparse's 6]
//     kwargs  : help, dest, default, type, action, choices, metavar, callback
//
// NOT used anywhere: store_false, append, append_const, count, nargs > 1, const,
// callback_args/kwargs, SUPPRESS_HELP, SUPPRESS_USAGE. So this is a small,
// closed subset of optparse — not a reimplementation of optparse.
//
// Two obligations shape the design:
//
//  1. `--help` output is GOLDEN-COMPARED for 7 subcommands, byte for byte, with
//     COLUMNS=80. That requires optparse's IndentedHelpFormatter geometry AND
//     umi_tools' BetterFormatter overrides AND Python's textwrap, all exactly.
//     Every geometric constant here was read off the live formatter objects
//     (width=78, max_help_position=24 -> help_position=24, help_width=54), not
//     assumed.
//
//  2. `-h/--help` is NOT optparse's built-in: umi_tools passes
//     `add_help_option=False` and registers a callback, `callbackShortHelp`,
//     which sets the parser description to None before printing. That is why the
//     goldens contain the usage block and the options but not the long module
//     docstring. Reproducing the built-in behaviour would have emitted a
//     multi-page description and failed all 7 goldens.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace umi_tools {

enum class OptAction { Store, StoreTrue, StoreFalse, Callback, Version, HelpAction };
enum class OptType { None, String, Int, Float, Choice };

/// A small inline string list, so the generated table is a plain aggregate with
/// no separate named arrays. Capacity is checked against the measured maxima
/// (short_opts 1, long_opts 2, choices 5) by a static_assert-backed ctest.
struct StrList {
  static constexpr int kCapacity = 8;
  const char* items[kCapacity];
  int count;

  const char* const* begin() const { return items; }
  const char* const* end() const { return items + count; }
  bool empty() const { return count == 0; }
  int size() const { return count; }
  const char* operator[](int i) const { return items[i]; }
};

/// One option, as extracted from the live oracle's parser objects.
struct OptionSpec {
  StrList short_opts;
  StrList long_opts;
  OptAction action;
  OptType type;
  const char* dest;
  // str(parser.defaults.get(dest)) — what optparse's expand_default substitutes
  // for "%default". `default_is_none` marks the case optparse renders as "none".
  const char* default_repr;
  bool default_is_none;
  const char* help;
  const char* metavar;
  StrList choices;
  bool takes_value;
};

struct OptionGroupSpec {
  const char* title;
  const char* description;
  std::span<const OptionSpec> options;
};

/// A dest seeded by `parser.set_defaults()` with NO corresponding add_option.
/// optparse's parse_args starts from parser.defaults in full, so these dests are
/// present on the parsed options object and are read by the tools' logic
/// (`options.filter_cell_barcodes`, `options.whitelist_tsv`,
/// `options.blacklist_tsv`). Omitting them was measured as 19 of 79 argv vectors
/// differing.
struct ExtraDefault {
  const char* dest;
  const char* default_repr;
  bool default_is_none;
};

struct ToolSpec {
  const char* name;
  const char* usage;
  const char* version;
  std::span<const ExtraDefault> extra_defaults;
  std::span<const OptionSpec> top_level;
  std::span<const OptionGroupSpec> groups;
  int width;               // 78 with COLUMNS=80
  int max_help_position;   // 24
  int indent_increment;    // 2
  bool short_first;        // true
};

/// Defined in the GENERATED src/option_tables.cpp.
std::span<const ToolSpec> all_tool_specs();
const ToolSpec* find_tool_spec(std::string_view name);

/// ---------------------------------------------------------------------------
/// Python textwrap, faithfully enough for these help strings.
///
/// The subtleties that matter, all of which change the golden if got wrong:
///   * whitespace RUNS ARE PRESERVED inside a line. textwrap splits into word and
///     whitespace chunks and rejoins them verbatim; it does not collapse runs.
///     One shipped help string contains a double space ("read id and UMI  and
///     (optionally)") and the golden keeps it.
///   * drop_whitespace=True drops a whitespace chunk at the START of any line
///     after the first, and at the END of every line.
///   * break_on_hyphens=True: a hyphenated word may split after the hyphen.
///   * replace_whitespace=True maps every whitespace character to a space FIRST
///     (after tab expansion), so an embedded newline inside a paragraph becomes a
///     space rather than a break.
/// ---------------------------------------------------------------------------
std::vector<std::string> textwrap_wrap(std::string_view text, int width);
/// textwrap's chunk split, exposed so the parity harness can compare the
/// SPLIT and not just the finished wrap: a wrong chunk boundary is invisible
/// whenever no line happens to break there.
std::vector<std::string> textwrap_chunks(const std::string& text);

/// ---------------------------------------------------------------------------
/// The help formatter: optparse.IndentedHelpFormatter + BetterFormatter.
/// ---------------------------------------------------------------------------
std::string format_option_strings(const OptionSpec& opt, bool short_first);
std::string format_help(const ToolSpec& tool, bool include_description = false);

/// The tool's module docstring, verbatim, from the GENERATED src/tool_docs.cpp.
/// Empty for an unknown tool. This is optparse's `description`.
std::string_view module_docstring(std::string_view tool);

/// ---------------------------------------------------------------------------
/// Parsed values. optparse stores everything on an object by `dest`; this keeps
/// the string form plus typed accessors, so the ported tool code reads like the
/// Python (`options.method`, `options.per_cell`).
/// ---------------------------------------------------------------------------
class Values {
 public:
  bool has(std::string_view dest) const;
  bool is_none(std::string_view dest) const;

  std::string get_string(std::string_view dest) const;
  std::int64_t get_int(std::string_view dest) const;
  double get_float(std::string_view dest) const;
  bool get_bool(std::string_view dest) const;

  /// Python's `if options.X:` for an INT-typed dest, where **0 is FALSE**.
  ///
  /// get_bool implements STRING truthiness, which is right for the
  /// string-typed dests it was written for ("0" is a non-empty str and so
  /// truthy) and WRONG for an int, where optparse has already
  /// converted the value. Four dests are int-typed and read through a
  /// truthiness test: cell_number, expect_cells, random_seed and subset_reads.
  /// MEASURED on `whitelist --set-cell-number=0`: upstream treats it as unset
  /// and runs the knee normally (30 barcodes); the port took the
  /// cell_number branch and aborted.
  bool get_int_truthy(std::string_view dest) const;

  void set(std::string_view dest, std::string value, bool is_none = false);

  /// True when the value came from argv rather than from the defaults pass.
  ///
  /// Upstream distinguishes the two by IDENTITY: `if options.stdin != sys.stdin`
  /// asks whether the default file object was replaced, not what its name is.
  /// There is no string that reproduces that — `--stdin=-` is a request to open
  /// a file literally named `-`, and upstream raises FileNotFoundError for it —
  /// so the port has to record explicitness rather than infer it from the value.
  bool was_given(std::string_view dest) const;

  /// Records that argv supplied this dest. Called by the parser, not by tools.
  void mark_given(std::string_view dest);

  const std::map<std::string, std::pair<std::string, bool>>& raw() const { return v_; }

 private:
  // dest -> (string form, is_none). A map keyed by dest is what optparse's
  // __dict__ is; iteration order is only used for `getParams`, which sorts.
  std::map<std::string, std::pair<std::string, bool>> v_;
  std::set<std::string, std::less<>> given_;
};

struct ParseResult {
  Values values;
  std::vector<std::string> args;   // positional arguments
  bool wants_help = false;         // -h/--help was seen (callbackShortHelp)
  // --help-extended is action='help', NOT callbackShortHelp, so it does NOT
  // suppress the description and prints the module docstring too.
  bool help_extended = false;
  bool wants_version = false;      // --version was seen
};

/// Parses argv[1..] against the tool spec, applying defaults first, exactly as
/// optparse does. Throws std::invalid_argument with optparse's message text for
/// an unrecognised option, a missing value, or a bad choice.
ParseResult parse_args(const ToolSpec& tool, const std::vector<std::string>& argv);

/// Utilities.OptionParser.__init__ (Utilities.py:453-471): `--no-usage` is a
/// SELF-REGISTERING option. The parser defines it only when it is ALREADY in
/// argv, and when it is, the parser is built with `usage=None`, so optparse
/// falls back to "%prog [options]" and the whole description/usage header
/// disappears. Nothing else in the package works this way, so it cannot live in
/// the generated tables.
///
/// Owns the rewritten spec's storage; the reference returned by spec() is valid
/// for this object's lifetime and aliases the base spec when --no-usage is
/// absent.
class MaybeNoUsage {
 public:
  MaybeNoUsage(const ToolSpec& base, const std::vector<std::string>& argv);
  const ToolSpec& spec() const { return *active_; }

 private:
  std::vector<OptionSpec> opts_;
  std::string usage_;
  ToolSpec rewritten_{};
  const ToolSpec* active_ = nullptr;
};

/// optparse's print_usage(): the tool's usage text, then a blank line, the docs
/// URL, and another blank line. Every caller that prints usage — the no-arguments
/// path AND parser errors — goes through this, because the two must agree and
/// the shipped suite covers neither.
std::string usage_block(const ToolSpec& spec);

// optparse's OptionParser.error(msg): print_usage() then "<prog>: error: <msg>"
// on STDERR and exit with status **2**. Measured against the oracle:
// `umi_tools extract --zzz=1` gives rc=2, where the port used to let the parse
// exception escape main and abort with SIGABRT (rc 134).
[[noreturn]] void parser_error(const ToolSpec& spec, const std::string& message);

}  // namespace umi_tools
