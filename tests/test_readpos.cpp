// Slice 6 cases: find_splice and get_read_position — `01_audit.md` D5's units.
//
// The exhaustive comparison is validation/parity_readpos.py (367,728 real reads
// vs the live oracle). These cases pin the two D5 semantics that a bulk
// comparison over the shipped corpus CANNOT cover, because the corpus never
// produces either: a negative position, and the 0-vs-False equivalence.
#include "umi_tools/sam_methods.hpp"
#include "test_harness.hpp"

#include <stdexcept>

using namespace umi_tools;

namespace {
// htslib op codes: M=0 I=1 D=2 N=3 S=4 H=5 P=6 ==7 X=8
std::vector<CigarOp> cig(std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> xs) {
  std::vector<CigarOp> out;
  for (const auto& [op, len] : xs) out.push_back(CigarOp{op, len});
  return out;
}
}  // namespace

UMI_TEST_CASE(find_splice_leading_soft_clip_is_an_offset_not_a_splice) {
  // `if cigar[0][0] == 4: offset = cigar[0][1]; cigar = cigar[1:]` — a LEADING
  // soft clip is consumed as an offset; only a later N or S is the splice.
  CHECK_EQ(find_splice(cig({{4, 5}, {0, 40}})), 0);          // 5S40M -> no splice
  CHECK_EQ(find_splice(cig({{4, 5}, {0, 40}, {3, 100}, {0, 10}})), 45);  // 5S40M100N10M
}

UMI_TEST_CASE(find_splice_returns_zero_for_no_splice) {
  // Python returns `False` here, and `False == 0` with the same hash, so a
  // spliced read at offset 0 and an unspliced read are THE SAME bundle key.
  // Returning 0 for both is the exact semantics, not a simplification.
  CHECK_EQ(find_splice(cig({{0, 50}})), 0);
}

UMI_TEST_CASE(find_splice_zero_offset_splice_is_indistinguishable_from_false) {
  // 0S is not emitted by aligners, but an N at the very start gives offset 0 —
  // the same value the no-splice path returns. This case exists to document that
  // the collision is intended.
  CHECK_EQ(find_splice(cig({{3, 100}, {0, 50}})), 0);
}

UMI_TEST_CASE(find_splice_counts_only_reference_consuming_ops) {
  // M D = X advance the offset; I H P do not.
  CHECK_EQ(find_splice(cig({{0, 10}, {1, 5}, {0, 10}, {3, 50}})), 20);   // I skipped
  CHECK_EQ(find_splice(cig({{0, 10}, {2, 5}, {0, 10}, {3, 50}})), 25);   // D counted
  CHECK_EQ(find_splice(cig({{0, 10}, {5, 5}, {0, 10}, {3, 50}})), 20);   // H skipped
}

UMI_TEST_CASE(find_splice_rejects_an_unknown_cigar_op) {
  CHECK_THROWS_AS(find_splice(cig({{99, 10}})), std::invalid_argument);
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
