// pattern.hpp — the `regex`-module SUBLANGUAGE umi_tools actually uses.
//
// This is NOT a regex engine. `01_audit.md` D1 enumerated the distinct pattern
// VALUES from the source, `tests/tests.yaml` AND the docs — because the
// dependency's behaviour is selected by data, so counting its 4 call sites would
// have measured nothing. The complete grammar those values span:
//
//     (?P<name>…)   named capturing groups (names: cell_N, umi_N, discard_N)
//     .             any character
//     .{n} .{n,m}   repetition, GREEDY WITH BACKTRACKING
//     LITERAL       literal DNA runs
//     X{n}          exact repetition of a literal char, e.g. T{3}
//     .*            greedy
//     ^  $          anchors
//     {s<=N}        fuzzy: at most N substitutions
//     {e<=N}        fuzzy: at most N total errors (Levenshtein)
//     {i<=N}        fuzzy: at most N insertions   (CellClusterer only — dead code)
//
// Not used anywhere, and therefore not implemented: alternation, backreferences,
// lookaround, non-greedy quantifiers, character classes, \d/\w escapes.
//
// THE TRAP THIS FILE EXISTS TO GET RIGHT (measured, `01_audit.md` D1): a fuzzy
// quantifier binds to the PRECEDING ELEMENT, and the tests and the docs put it in
// different places, so the two spellings are NOT equivalent:
//
//     …GACGCCTT{s<=2}     (tests/tests.yaml)  -> binds to the final T only
//     (…GACGCCTT){s<=2}   (doc/regex.md)      -> binds to the whole group
//
//   input                     bare form   grouped form
//   GAGTGATTGCTTGTGACGCCTA    MATCH       MATCH        (1 sub, last base)
//   GAGAGATTGCTTGTGACGCCTT    no match    MATCH        (1 sub, position 3)
//
// So the fixture named `extract_indrop_fuzzy` is not doing 2-mismatch-anywhere
// matching at all; it tolerates substitution of one trailing base. Implementing
// the documented semantics would silently change which reads match.
//
// A second measured simplification that makes this tractable: **fuzzy is only
// ever applied to a literal** — either a single literal character (the bare form)
// or a group whose entire content is a literal run (the grouped form, and
// `checkError`'s `(BARCODE){e<=N}`). No fuzzy quantifier in this package is
// applied to `.`, to a repetition, or to a group containing anything but
// literals. That is enforced at parse time rather than assumed.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace umi_tools {

/// Where a named group matched, as `regex`'s match.span(name) reports it: byte
/// offsets [start, stop).
struct Span {
  std::int64_t start = -1;
  std::int64_t stop = -1;
};

struct MatchResult {
  std::int64_t end = 0;                    // match.end()
  // Group name -> (span, text). Ordered, so iteration is deterministic; the
  // caller (ExtractBarcodes) iterates `sorted(list(groupdict))` anyway.
  std::map<std::string, Span> spans;
  std::map<std::string, std::string> groups;
};

class Pattern {
 public:
  /// Throws std::invalid_argument on a construct outside the measured
  /// sublanguage, naming it — better a loud failure at startup than a silent
  /// mismatch on one read in a million.
  explicit Pattern(std::string_view pattern);
  ~Pattern();
  Pattern(Pattern&&) noexcept;
  Pattern& operator=(Pattern&&) noexcept;
  Pattern(const Pattern&) = delete;
  Pattern& operator=(const Pattern&) = delete;

  /// `regex`'s .match(): anchored at the START, NOT required to consume the whole
  /// string. Returns nullopt when there is no match.
  std::optional<MatchResult> match(std::string_view text) const;

  const std::string& source() const { return source_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::string source_;
};

}  // namespace umi_tools
