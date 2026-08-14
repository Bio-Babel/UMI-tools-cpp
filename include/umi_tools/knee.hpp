// knee.hpp — the two cell-barcode knee estimators, and the two scipy functions
// they rest on. This is the port's FIRST numeric work (`01_audit.md` D9).
//
// scipy's contribution here is exactly two functions
// (`05_dep_map.md` measured it), and both are reimplemented rather than
// depended on. The formulas were MEASURED against the live scipy before being
// written, not recalled:
//
//   gaussian_kde(data, bw_method=0.1)
//     covariance = factor**2 * np.cov(data, rowvar=1, bias=False)   # ddof = 1
//     density(x) = (1/n) * SUM_j exp(-0.5 (x - x_j)^2 / covariance)
//                        / sqrt(2 * pi * covariance)
//     Verified against scipy 1.17.1: max |difference| = 2.5e-16 over a 7-point
//     grid, i.e. float64 rounding.
//
//   argrelextrema(y, np.less)            # order=1, mode='clip'
//     strict local minima: y[i] < y[i-1] AND y[i] < y[i+1].
//     ENDPOINTS ARE NEVER EXTREMA — mode='clip' compares index 0 against itself,
//     and `y[0] < y[0]` is false. A PLATEAU IS NEVER A MINIMUM, because the
//     comparison is strict. Verified: [3,1,2,0.5,2,2,1,4] -> [1, 3, 6].
//
// What must match EXACTLY rather than within a tolerance, and why: the selected
// local-minimum INDEX is an integer, and the returned barcode SET is discrete.
// Only the threshold VALUE is a float. A tolerance that covered the index or the
// set would be hiding a real difference rather than accommodating one.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "umi_tools/ordered_map.hpp"

namespace umi_tools {

/// cell barcode -> count, in insertion order (the order barcodes were first
/// observed). collections.Counter is a dict, so `most_common()` breaks count
/// ties by THAT order, and the tie order reaches the output.
using CellBarcodeCounts = OrderedMap<std::string, std::int64_t>;

/// collections.Counter.most_common(): count DESCENDING, ties by first insertion.
std::vector<std::pair<std::string, std::int64_t>> most_common(const CellBarcodeCounts& c);

/// scipy.stats.gaussian_kde(data, bw_method=<scalar>) evaluated at `points`.
std::vector<double> gaussian_kde(const std::vector<double>& data,
                                 const std::vector<double>& points, double bw_method);

/// scipy.signal.argrelextrema(y, np.less) with order=1, mode='clip'.
std::vector<std::int64_t> argrelextrema_less(const std::vector<double>& y);

/// numpy.linspace(start, stop, num) — endpoint included, computed as
/// start + i*step with step = (stop-start)/(num-1), which is what numpy does.
std::vector<double> linspace(double start, double stop, std::int64_t num);

struct KneeResult {
  // Python returns None when no local minimum was accepted; the caller
  // distinguishes that from an empty result, so it is optional, not empty.
  //
  // A VECTOR, not a set, and the ORDER IS LOAD-BEARING. getKneeEstimateDistance's
  // default path returns `knee_final_barcodes`, a LIST in most_common() order
  // (count descending, ties by first observation) — NOT a set. Storing it as a
  // std::set would silently re-order it lexicographically, and
  // errorDetectAboveThreshold then sorts ASCENDING BY COUNT with a STABLE sort,
  // so that base order decides which of two equal-count near-misses survives.
  // Measured: it changed 3 barcodes in the ed_above_threshold fixtures.
  //
  // The cell_number and density paths build a Python `set` instead; there the
  // order is the comprehension's iteration over cell_barcode_counts, which is
  // that Counter's insertion order.
  std::optional<std::vector<std::string>> final_barcodes;
  // <prefix>_cell_thresholds.tsv content, when a plot prefix was requested.
  std::vector<std::int64_t> local_mins_counts;
  std::int64_t selected_local_min = -1;   // -1 == Python's None
  std::int64_t idx_of_best_point = -1;    // distance method only
};

/// whitelist_methods.getKneeEstimateDistance(counts, cell_number, plotfile_prefix)
KneeResult get_knee_estimate_distance(const CellBarcodeCounts& counts,
                                      std::optional<std::int64_t> cell_number);

/// whitelist_methods.getKneeEstimateDensity(counts, expect_cells, cell_number,
///                                          plotfile_prefix)
KneeResult get_knee_estimate_density(const CellBarcodeCounts& counts,
                                     std::optional<std::int64_t> expect_cells,
                                     std::optional<std::int64_t> cell_number);

/// whitelist_methods.getErrorCorrectMapping(cell_barcodes, whitelist, threshold)
/// -> true barcode -> {false barcodes}. Note this uses whitelist_methods' OWN
/// pure-Python `hamming_distance`, which returns inf for unequal lengths and does
/// NOT raise — upstream deliberately shadows the compiled kernel there
/// ("Unexpected results with cythonise hamming distance so redefine in python").
std::map<std::string, std::set<std::string>> get_error_correct_mapping(
    const std::vector<std::string>& cell_barcodes,
    const std::vector<std::string>& whitelist, std::int64_t threshold);

/// whitelist_methods.getCellWhitelist(...)
struct CellWhitelist {
  std::optional<std::vector<std::string>> whitelist;   // order matters — see KneeResult
  std::map<std::string, std::set<std::string>> true_to_false_map;
  bool has_true_to_false = false;
  std::vector<std::int64_t> local_mins_counts;
  std::int64_t selected_local_min = -1;
  std::int64_t idx_of_best_point = -1;

  /// `len(final_barcodes)` as it stands INSIDE getKneeEstimateDensity, which is
  /// where upstream writes `<prefix>_cell_thresholds.tsv`
  /// (whitelist_methods.py:239-248) — i.e. BEFORE errorDetectAboveThreshold
  /// runs later in whitelist.main and replaces the whitelist.
  ///
  /// Recorded here rather than read from `whitelist` at write time, because the
  /// caller legitimately overwrites `whitelist` in between: comparing against
  /// the post-correction size marked EVERY threshold "Rejected" where upstream
  /// marks exactly one "Selected" (an adversarial finding, measured).
  std::int64_t whitelist_size_at_knee = -1;
};

CellWhitelist get_cell_whitelist(const CellBarcodeCounts& counts,
                                 const std::string& knee_method,
                                 std::optional<std::int64_t> expect_cells,
                                 std::optional<std::int64_t> cell_number,
                                 std::int64_t error_correct_threshold);

/// whitelist_methods.checkError(barcode, whitelist, errors=1) — returns the
/// whitelist barcodes matching `barcode` within `errors` total edits, stopping
/// as soon as TWO are found (the caller only branches on 0 / 1 / >1).
std::vector<std::string> check_error(const std::string& barcode,
                                     const std::vector<std::string>& whitelist,
                                     std::int64_t errors);

/// whitelist_methods.errorDetectAboveThreshold(...)
struct ErrorDetectResult {
  std::vector<std::string> cell_whitelist;   // the surviving subset, order preserved
  std::map<std::string, std::set<std::string>> true_to_false_map;
};
ErrorDetectResult error_detect_above_threshold(
    const CellBarcodeCounts& counts, const std::vector<std::string>& cell_whitelist,
    const std::map<std::string, std::set<std::string>>& true_to_false_map,
    std::int64_t errors, const std::string& resolution_method);

}  // namespace umi_tools
