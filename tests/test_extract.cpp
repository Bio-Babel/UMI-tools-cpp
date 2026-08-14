// Slice 3 cases: the regex sublanguage, FASTQ iteration, and barcode extraction.
//
// Expectations are the LIVE ORACLE's values (`regex` 2026.7.19 and the current
// umi_tools source), measured and pasted here. The exhaustive comparison lives
// in validation/parity_pattern.py, which checks 2376 (pattern, sequence) pairs
// against the live `regex` module; these cases pin the specific semantics that
// would otherwise regress silently.
#include "umi_tools/extract_methods.hpp"
#include "umi_tools/fastq.hpp"
#include "umi_tools/pattern.hpp"
#include "test_harness.hpp"

#include <set>
#include <stdexcept>

using namespace umi_tools;

namespace {
const char* kAdapter = "GAGTGATTGCTTGTGACGCCTT";

std::string span_of(const MatchResult& m, const std::string& name) {
  const Span s = m.spans.at(name);
  return std::to_string(s.start) + "," + std::to_string(s.stop);
}
}  // namespace

// ---------------------------------------------------------------------------
// The fuzzy-binding trap: the two forms are NOT equivalent (01_audit.md D1)
// ---------------------------------------------------------------------------
UMI_TEST_CASE(fuzzy_bare_binds_to_last_char_only) {
  // tests/tests.yaml's spelling: {s<=2} after a bare literal binds to the final
  // T, so a substitution at position 3 does NOT match.
  Pattern p(std::string(kAdapter) + "{s<=2}");
  CHECK(p.match("GAGTGATTGCTTGTGACGCCTT").has_value());   // exact
  CHECK(p.match("GAGTGATTGCTTGTGACGCCTA").has_value());   // 1 sub, LAST base
  CHECK(!p.match("GAGAGATTGCTTGTGACGCCTT").has_value());  // 1 sub, position 3
  CHECK(!p.match("AAGAGATTGCTTGTGACGCCTT").has_value());  // 2 subs, early
}

UMI_TEST_CASE(fuzzy_grouped_binds_to_whole_group) {
  // doc/regex.md's spelling: the same budget applies anywhere in the group.
  Pattern p("(" + std::string(kAdapter) + "){s<=2}");
  CHECK(p.match("GAGTGATTGCTTGTGACGCCTT").has_value());
  CHECK(p.match("GAGAGATTGCTTGTGACGCCTT").has_value());   // 1 sub at position 3
  CHECK(p.match("AAGAGATTGCTTGTGACGCCTT").has_value());   // 2 subs
  CHECK(!p.match("AAAAGATTGCTTGTGACGCCTT").has_value());  // 3 subs — over budget
}

// ---------------------------------------------------------------------------
// Greedy repetition with backtracking, and anchors
// ---------------------------------------------------------------------------
UMI_TEST_CASE(greedy_bounded_repeat_backtracks) {
  // .{8,12} takes as much as it can while still letting the adapter match.
  Pattern p("(?P<cell_1>.{8,12})(?P<discard_2>" + std::string(kAdapter) +
            "{s<=2})(?P<cell_3>.{8})(?P<umi_1>.{6})T{3}.*");
  const std::string s12 = std::string(12, 'A') + kAdapter + std::string(8, 'C') +
                          std::string(6, 'G') + "TTT" + "ACGT";
  const auto m12 = p.match(s12);
  CHECK(m12.has_value());
  CHECK_EQ(span_of(*m12, "cell_1"), std::string("0,12"));
  CHECK_EQ(span_of(*m12, "discard_2"), std::string("12,34"));

  const std::string s8 = std::string(8, 'A') + kAdapter + std::string(8, 'C') +
                         std::string(6, 'G') + "TTT" + "ACGT";
  const auto m8 = p.match(s8);
  CHECK(m8.has_value());
  CHECK_EQ(span_of(*m8, "cell_1"), std::string("0,8"));
  CHECK_EQ(span_of(*m8, "discard_2"), std::string("8,30"));
}

UMI_TEST_CASE(dollar_anchor_with_leading_star) {
  // The 3'-end pattern: `.*` gives back until the tail fits exactly at the end.
  Pattern p(".*(?P<umi_1>.{3}).{4}(?P<umi_2>.{2})$");
  const auto m = p.match("AAAACCCGGGGTT");
  CHECK(m.has_value());
  CHECK_EQ(m->end, 13);
  CHECK_EQ(span_of(*m, "umi_1"), std::string("4,7"));
  CHECK_EQ(span_of(*m, "umi_2"), std::string("11,13"));
}

UMI_TEST_CASE(match_is_start_anchored_but_not_end_anchored) {
  Pattern p("(?P<umi_1>.{3})");
  const auto m = p.match("ACGTACGT");
  CHECK(m.has_value());
  CHECK_EQ(m->end, 3);   // partial consumption is fine
  CHECK(!Pattern("^(?P<u>.{9})").match("ACGT").has_value());  // too short
}

UMI_TEST_CASE(pattern_supports_the_five_constructs_adc4567_added) {
  // --bc-pattern is arbitrary user input, so these five are now
  // IMPLEMENTED rather than rejected. Behaviour is compared against the live
  // `regex` module in validation/parity_pattern_extended.py; this only pins
  // that they construct and match at all, so a parser regression fails here
  // too and not only in the slower differential.
  CHECK(Pattern("a|b").match("b").has_value());
  CHECK(Pattern("[ACGT]").match("G").has_value());
  CHECK(!Pattern("[ACGT]").match("X").has_value());
  CHECK(Pattern("[^X]").match("A").has_value());
  CHECK(Pattern("\\d").match("7").has_value());
  CHECK(!Pattern("\\d").match("A").has_value());
  CHECK(Pattern("(?:AB)C").match("ABC").has_value());
  // Lazy: `.*?` stops at the FIRST T, so the group ends at 3 not 6.
  const auto lazy = Pattern("(?P<u>.*?)T").match("AAATBBT");
  CHECK(lazy.has_value());
  CHECK_EQ(lazy->end, 4);
}

UMI_TEST_CASE(pattern_still_rejects_what_changes_the_matching_model) {
  // Lookaround, inline flags, backreferences and atomic groups are NOT in
  // scope: they change the matching model rather than adding a token, and no
  // umi_tools pattern uses one. Loud failure beats a silent difference.
  CHECK_THROWS_AS(Pattern("(?=A)B"), std::invalid_argument);
  CHECK_THROWS_AS(Pattern("(?i)abc"), std::invalid_argument);
  CHECK_THROWS_AS(Pattern("(?>ab)"), std::invalid_argument);
  // A fuzzy quantifier on a non-literal would need general approximate matching.
  CHECK_THROWS_AS(Pattern(".{s<=1}"), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// FASTQ
// ---------------------------------------------------------------------------
UMI_TEST_CASE(record_str_drops_the_plus_line_description) {
  Record r{"READ1 desc", "ACGT", "IIII"};
  CHECK_EQ(r.str(), std::string("@READ1 desc\nACGT\n+\nIIII"));
}

UMI_TEST_CASE(guess_format_ranges_are_low_inclusive_high_exclusive) {
  // RANGES: phred33 (33,77), solexa (59,106), phred64 (64,106).
  const auto r = guess_format("!!!!");        // chr 33
  CHECK_EQ(static_cast<int>(r.size()), 1);
  CHECK_EQ(r[0], std::string("phred33"));
  const auto r2 = guess_format("dddd");       // chr 100: solexa + phred64
  CHECK_EQ(static_cast<int>(r2.size()), 2);
  CHECK_EQ(r2[0], std::string("solexa"));
  CHECK_EQ(r2[1], std::string("phred64"));
}

// ---------------------------------------------------------------------------
// extract_methods
// ---------------------------------------------------------------------------
UMI_TEST_CASE(add_barcodes_appends_before_the_first_space) {
  Record r{"HISEQ:87:00000000 read1", "ACGT", "IIII"};
  CHECK_EQ(add_barcodes_to_identifier(r, "GGTT", "", "_"),
           std::string("HISEQ:87:00000000_GGTT read1"));
  CHECK_EQ(add_barcodes_to_identifier(r, "GGTT", "AACC", "_"),
           std::string("HISEQ:87:00000000_AACC_GGTT read1"));
}

UMI_TEST_CASE(extract_seq_and_quals_partitions_by_index) {
  // NNNXXXXNN over "AAGGTTGCT": umi = positions 0,1,2,7,8; sample = 3,4,5,6.
  std::set<std::int64_t> umi{0, 1, 2, 7, 8}, cell{}, discard{};
  const SeqQuals sq = extract_seq_and_quals("AAGGTTGCT", "123456789", umi, cell, discard,
                                            /*retain_umi=*/false);
  CHECK_EQ(sq.new_seq, std::string("GTTG"));
  CHECK_EQ(sq.new_quals, std::string("4567"));
  CHECK_EQ(sq.umi_quals, std::string("12389"));
}

UMI_TEST_CASE(extract_seq_and_quals_retain_umi_keeps_bases_in_both) {
  std::set<std::int64_t> umi{0, 1}, cell{}, discard{};
  const SeqQuals sq = extract_seq_and_quals("ACGT", "1234", umi, cell, discard, true);
  CHECK_EQ(sq.new_seq, std::string("ACGT"));   // UMI bases retained in the read
  CHECK_EQ(sq.umi_quals, std::string("1234"));  // and reported as UMI quals
}

UMI_TEST_CASE(quality_offsets_match_RANGES_low_bounds) {
  CHECK_EQ(*quality_offset("phred33"), 33);
  CHECK_EQ(*quality_offset("solexa"), 59);
  CHECK_EQ(*quality_offset("phred64"), 64);
  CHECK(!quality_offset("nonesuch").has_value());
}

UMI_TEST_CASE(umi_below_threshold_is_any_not_all) {
  // 'I' is 40 under phred33, '!' is 0. any() -> one bad base fails the read.
  CHECK(umi_below_threshold("II!I", "phred33", 30));
  CHECK(!umi_below_threshold("IIII", "phred33", 30));
}

UMI_TEST_CASE(mask_umi_replaces_only_low_quality_bases) {
  CHECK_EQ(mask_umi("ACGT", "II!I", "phred33", 30), std::string("ACNT"));
  CHECK_EQ(mask_umi("ACGT", "IIII", "phred33", 30), std::string("ACGT"));
}

UMI_TEST_CASE(extract_barcodes_uses_sorted_group_names) {
  // cell_1 and cell_3 concatenate in NAME order, not in pattern order — which is
  // the same here, but the case pins the rule.
  Pattern p("(?P<cell_3>.{2})(?P<cell_1>.{2})(?P<umi_1>.{2})");
  const auto m = p.match("AACCGG");
  CHECK(m.has_value());
  Record r{"id", "AACCGG", "123456"};
  const ExtractedBarcodes b = extract_barcodes(r, *m, true, true, true, false);
  // sorted names -> cell_1 ("CC") then cell_3 ("AA")
  CHECK_EQ(b.cell_barcode, std::string("CCAA"));
  CHECK_EQ(b.umi, std::string("GG"));
  // Upstream bug D7#8: cell barcode qualities are never returned.
  CHECK_EQ(b.cell_barcode_quals, std::string(""));
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
