// Slice 4 cases: the numeric core and the whitelist helpers.
//
// The exhaustive float comparison against live scipy is in
// validation/parity_knee.py (argrelextrema 66/66 exact; gaussian_kde max
// relative error 3.9e-14). These cases pin the DISCRETE semantics that a float
// tolerance must never be allowed to absorb.
#include "umi_tools/knee.hpp"
#include "test_harness.hpp"

#include <cmath>
#include <stdexcept>

using namespace umi_tools;

namespace {
CellBarcodeCounts counts_of(std::initializer_list<std::pair<const char*, std::int64_t>> xs) {
  CellBarcodeCounts c;
  for (const auto& [k, v] : xs) c[std::string(k)] = v;
  return c;
}
std::string join(const std::vector<std::string>& v) {
  std::string s;
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) s += ",";
    s += v[i];
  }
  return s;
}
}  // namespace

// ---------------------------------------------------------------------------
// most_common — the tie order that reaches the output
// ---------------------------------------------------------------------------
UMI_TEST_CASE(most_common_breaks_ties_by_first_insertion) {
  // Counter.most_common() is a STABLE sort by count descending, so equal counts
  // keep the order they were first observed in — NOT lexicographic order.
  const auto c = counts_of({{"ZZZ", 5}, {"AAA", 5}, {"MMM", 9}});
  const auto mc = most_common(c);
  CHECK_EQ(mc[0].first, std::string("MMM"));
  CHECK_EQ(mc[1].first, std::string("ZZZ"));   // first observed of the two 5s
  CHECK_EQ(mc[2].first, std::string("AAA"));
}

// ---------------------------------------------------------------------------
// argrelextrema — discrete, must be exact
// ---------------------------------------------------------------------------
UMI_TEST_CASE(argrelextrema_excludes_endpoints) {
  // mode='clip' compares index 0 against itself, and y[0] < y[0] is false.
  const auto m = argrelextrema_less({1.0, 5.0, 1.0});
  CHECK_EQ(static_cast<int>(m.size()), 0);
}

UMI_TEST_CASE(argrelextrema_plateau_is_not_a_minimum) {
  // The comparison is STRICT, so an equal neighbour disqualifies the point.
  const auto m = argrelextrema_less({3.0, 1.0, 2.0, 0.5, 2.0, 2.0, 1.0, 4.0});
  CHECK_EQ(static_cast<int>(m.size()), 3);
  CHECK_EQ(static_cast<int>(m[0]), 1);
  CHECK_EQ(static_cast<int>(m[1]), 3);
  CHECK_EQ(static_cast<int>(m[2]), 6);
}

UMI_TEST_CASE(argrelextrema_all_equal_has_none) {
  CHECK_EQ(static_cast<int>(argrelextrema_less({1.0, 1.0, 1.0, 1.0}).size()), 0);
}

// ---------------------------------------------------------------------------
// gaussian_kde — the one genuinely continuous unit
// ---------------------------------------------------------------------------
UMI_TEST_CASE(gaussian_kde_matches_measured_scipy_value) {
  // Measured against scipy 1.17.1 for this exact dataset and grid.
  const std::vector<double> data{1.0, 1.3, 2.7, 3.1, 3.15, 5.0, 8.2, 8.3, 9.9};
  const auto pts = linspace(1.0, 9.9, 7);
  const auto d = gaussian_kde(data, pts, 0.1);
  CHECK_EQ(static_cast<int>(d.size()), 7);
  CHECK(std::abs(d[0] - 2.2368254343680022e-01) < 1e-12);
  CHECK(std::abs(d[5] - 2.3504681137519462e-01) < 1e-12);
}

UMI_TEST_CASE(gaussian_kde_covariance_uses_ddof_1) {
  // Pins np.cov(..., bias=False), i.e. ddof=1. MEASURED against scipy 1.17.1:
  //   ddof=1 (correct) -> 0.000854568894921412
  //   ddof=0 (wrong)   -> 0.000080999109560891
  // An order of magnitude apart, so no float tolerance could ever absorb the
  // difference — which is the point of testing it separately from the tolerance.
  const std::vector<double> data{0.0, 1.0, 2.0, 3.0};
  const auto d = gaussian_kde(data, {1.5}, 0.1);
  CHECK(std::abs(d[0] - 0.000854568894921412) < 1e-15);
}

UMI_TEST_CASE(gaussian_kde_rejects_degenerate_data) {
  // Zero variance -> zero covariance -> a division by zero in the Python too.
  CHECK_THROWS_AS(gaussian_kde({2.0, 2.0, 2.0}, {2.0}, 0.1), std::invalid_argument);
}

UMI_TEST_CASE(linspace_endpoint_is_exact) {
  const auto v = linspace(0.0, 1.0, 11);
  CHECK_EQ(static_cast<int>(v.size()), 11);
  CHECK_EQ(v.front(), 0.0);
  CHECK_EQ(v.back(), 1.0);   // numpy pins the endpoint rather than accumulating
}

// ---------------------------------------------------------------------------
// the knee estimators
// ---------------------------------------------------------------------------
UMI_TEST_CASE(knee_distance_returns_most_common_order_not_sorted) {
  // The default distance path returns a LIST in most_common() order. Returning a
  // lexicographically sorted set instead changed which of two equal-count
  // near-misses survived errorDetectAboveThreshold, in 3 barcodes.
  const auto c = counts_of({{"ZZZ", 100}, {"AAA", 90}, {"MMM", 2}, {"BBB", 1}});
  const auto r = get_knee_estimate_distance(c, std::nullopt);
  CHECK(r.final_barcodes.has_value());
  CHECK(!r.final_barcodes->empty());
  CHECK_EQ((*r.final_barcodes)[0], std::string("ZZZ"));   // highest count first
}

UMI_TEST_CASE(check_error_stops_after_two_matches) {
  // "Assuming downstream processes are the same for (>1 -> Inf) near_matches"
  const std::vector<std::string> wl{"AAAAAAAA", "AAAAAAAT", "AAAAAAAC", "AAAAAAAG"};
  const auto m = check_error("AAAAAAAA", wl, 1);
  CHECK_EQ(static_cast<int>(m.size()), 2);   // returns as soon as two are found
}

UMI_TEST_CASE(check_error_skips_self_and_length_outliers) {
  const std::vector<std::string> wl{"AAAAAAAA", "AAAAAAAAAAAA"};
  // Self is skipped; the 12-mer differs in length by 4 > errors=1, so skipped.
  CHECK_EQ(static_cast<int>(check_error("AAAAAAAA", wl, 1).size()), 0);
}

UMI_TEST_CASE(error_correct_mapping_drops_ambiguous_barcodes) {
  // A barcode within threshold of TWO whitelist entries is not uniquely
  // assignable, so it is dropped rather than corrected.
  const std::vector<std::string> whitelist{"AAAA", "AAAT"};
  const auto m = get_error_correct_mapping({"AAAA", "AAAT", "AAAG"}, whitelist, 1);
  // AAAG is at distance 1 from both -> ambiguous -> absent from the map.
  CHECK(m.empty());
}

UMI_TEST_CASE(error_correct_mapping_assigns_unique_match) {
  const std::vector<std::string> whitelist{"AAAA"};
  const auto m = get_error_correct_mapping({"AAAA", "AAAT"}, whitelist, 1);
  CHECK_EQ(static_cast<int>(m.size()), 1);
  CHECK_EQ(join({m.begin()->first}), std::string("AAAA"));
  CHECK_EQ(static_cast<int>(m.begin()->second.size()), 1);
}

UMI_TEST_CASE(error_correct_mapping_ignores_unequal_lengths) {
  // whitelist_methods' own hamming_distance returns inf for unequal lengths and
  // does NOT raise (unlike the compiled kernel it deliberately shadows).
  const std::vector<std::string> whitelist{"AAAA"};
  const auto m = get_error_correct_mapping({"AAA", "AAAAA"}, whitelist, 1);
  CHECK(m.empty());
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
