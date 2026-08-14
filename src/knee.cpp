#include "umi_tools/knee.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "umi_tools/logging.hpp"
#include "umi_tools/pattern.hpp"

namespace umi_tools {
namespace {
constexpr double kPi = 3.14159265358979323846;
}  // namespace

std::vector<std::pair<std::string, std::int64_t>> most_common(const CellBarcodeCounts& c) {
  // Counter.most_common() sorts by count DESCENDING with a STABLE sort over the
  // dict's insertion order, so equal counts keep first-observed order. That tie
  // order reaches the whitelist output, so std::sort would be wrong here.
  std::vector<std::pair<std::string, std::int64_t>> v;
  v.reserve(c.size());
  for (const auto& [k, n] : c) v.emplace_back(k, n);
  std::stable_sort(v.begin(), v.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  return v;
}

std::vector<double> linspace(double start, double stop, std::int64_t num) {
  std::vector<double> out;
  if (num <= 0) return out;
  out.reserve(static_cast<std::size_t>(num));
  if (num == 1) {
    out.push_back(start);
    return out;
  }
  const double step = (stop - start) / static_cast<double>(num - 1);
  for (std::int64_t i = 0; i < num; ++i) out.push_back(start + static_cast<double>(i) * step);
  // numpy sets the last sample to `stop` exactly when endpoint=True, rather than
  // letting start + (num-1)*step drift.
  out.back() = stop;
  return out;
}

std::vector<double> gaussian_kde(const std::vector<double>& data,
                                 const std::vector<double>& points, double bw_method) {
  const std::size_t n = data.size();
  if (n == 0) throw std::invalid_argument("gaussian_kde: empty dataset");

  // np.cov(data, rowvar=1, bias=False) -> ddof = 1.
  double mean = 0.0;
  for (double x : data) mean += x;
  mean /= static_cast<double>(n);
  double var = 0.0;
  for (double x : data) var += (x - mean) * (x - mean);
  if (n > 1) var /= static_cast<double>(n - 1);

  const double covariance = bw_method * bw_method * var;
  if (!(covariance > 0.0))
    // scipy's own text, verbatim: `set_bandwidth` raises LinAlgError from the
    // Cholesky factorisation and gaussian_kde re-raises it with this message
    // (scipy/stats/_kde.py). Upstream does not catch it, so a user of
    // `whitelist --knee-method=density` on a flat count distribution sees exactly
    // this sentence at the end of a traceback. The port used to say
    // "gaussian_kde: non-positive covariance (degenerate data)" — same condition,
    // same exit code, different words.
    //
    // MEASURED across N = 8, 20, 40, 100, 400 and three degeneracy levels: the two
    // sides agree on WHEN this fires (variance exactly zero, at every N) and
    // agree on rc=0 for a variance as small as 4.3e-6. The condition here is
    // scipy's condition, not an approximation of it.
    //
    // This text is scipy-version-specific (the oracle runs 1.17.1), the same
    // exposure the ledger records for htslib messages. A reworded scipy fails the
    // check that pins this rather than silently diverging.
    throw std::invalid_argument(
        "The data appears to lie in a lower-dimensional subspace of the space in "
        "which it is expressed. This has resulted in a singular data covariance "
        "matrix, which cannot be treated using the algorithms implemented in "
        "`gaussian_kde`. Consider performing principal component analysis / "
        "dimensionality reduction and using `gaussian_kde` with the transformed "
        "data.");

  const double norm = std::sqrt(2.0 * kPi * covariance);
  std::vector<double> out;
  out.reserve(points.size());
  for (double x : points) {
    // Summed in dataset order, matching numpy's reduction over the same buffer.
    double acc = 0.0;
    for (double xj : data) {
      const double d = x - xj;
      acc += std::exp(-0.5 * d * d / covariance);
    }
    out.push_back(acc / static_cast<double>(n) / norm);
  }
  return out;
}

std::vector<std::int64_t> argrelextrema_less(const std::vector<double>& y) {
  // order=1, mode='clip': index 0 and the last index compare against themselves,
  // and `y[i] < y[i]` is false, so endpoints are never extrema. The comparison is
  // STRICT, so a plateau is never a minimum either.
  std::vector<std::int64_t> out;
  if (y.size() < 3) return out;
  for (std::size_t i = 1; i + 1 < y.size(); ++i)
    if (y[i] < y[i - 1] && y[i] < y[i + 1]) out.push_back(static_cast<std::int64_t>(i));
  return out;
}

namespace {

// whitelist_methods.getKneeEstimateDistance.getKneeDistance — the perpendicular
// distance from each point of the cumulative curve to the chord joining its
// first and last points, and the index of the maximum.
std::pair<std::vector<double>, std::int64_t> knee_distance(const std::vector<double>& values) {
  const std::size_t n = values.size();
  if (n == 0) return {{}, 0};

  // allCoord = np.vstack((range(n), values)).T
  const double first_x = 0.0, first_y = values.front();
  const double line_x = static_cast<double>(n - 1) - first_x;
  const double line_y = values.back() - first_y;
  const double line_len = std::sqrt(line_x * line_x + line_y * line_y);
  const double ux = line_len == 0.0 ? 0.0 : line_x / line_len;
  const double uy = line_len == 0.0 ? 0.0 : line_y / line_len;

  std::vector<double> dist;
  dist.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double vx = static_cast<double>(i) - first_x;
    const double vy = values[i] - first_y;
    const double sp = vx * ux + vy * uy;           // scalarProduct
    const double px = sp * ux, py = sp * uy;       // vecFromFirstParallel
    const double tx = vx - px, ty = vy - py;       // vecToLine
    dist.push_back(std::sqrt(tx * tx + ty * ty));
  }
  // np.argmax returns the FIRST maximum on ties.
  std::int64_t best = 0;
  for (std::size_t i = 1; i < n; ++i)
    if (dist[i] > dist[static_cast<std::size_t>(best)]) best = static_cast<std::int64_t>(i);
  return {dist, best};
}

}  // namespace

namespace {

// `counts[cell_number]` where cell_number may be NEGATIVE.
//
// whitelist.py's guard is
//     if options.cell_number and options.cell_number > len(cell_barcode_counts)
// which a negative value PASSES — it is truthy and not greater — so the value
// reaches `threshold = counts[cell_number]` as an ordinary Python index, and a
// negative one counts from the end. Casting to size_t wrapped it to ~2^64 and
// the run aborted instead. MEASURED on a 60-barcode ramp:
//     --set-cell-number=-1   oracle rc 0, whitelist of 59
//     --set-cell-number=-5   oracle rc 0, whitelist of 55
//     --set-cell-number=-61  oracle rc 1, IndexError: list index out of range
// Out of range in EITHER direction is that IndexError.
std::size_t py_index(std::int64_t i, std::size_t n, const char* who) {
  const std::int64_t adj = i < 0 ? i + static_cast<std::int64_t>(n) : i;
  if (adj < 0 || adj >= static_cast<std::int64_t>(n))
    throw std::out_of_range(std::string(who) + ": list index out of range");
  return static_cast<std::size_t>(adj);
}

}  // namespace

KneeResult get_knee_estimate_distance(const CellBarcodeCounts& counts,
                                      std::optional<std::int64_t> cell_number) {
  KneeResult r;
  const auto mc = most_common(counts);
  std::vector<double> values;
  values.reserve(mc.size());
  double running = 0.0;
  for (const auto& [bc, n] : mc) {
    (void)bc;
    running += static_cast<double>(n);   // np.cumsum
    values.push_back(running);
  }

  auto [dist, idx] = knee_distance(values);
  (void)dist;
  if (idx == 0)
    throw std::logic_error("Something's gone wrong here!!");   // the Python's message

  std::int64_t previous = 0;
  std::int64_t iterations = 0;
  const std::int64_t max_iterations = 100;
  while (idx - previous != 0) {
    previous = idx;
    ++iterations;
    if (iterations > max_iterations) break;
    // values[:idx*3] — Python clamps a slice past the end; so does this.
    const std::size_t take =
        std::min(values.size(), static_cast<std::size_t>(std::max<std::int64_t>(0, idx * 3)));
    auto res = knee_distance(std::vector<double>(values.begin(), values.begin() +
                                                                    static_cast<std::ptrdiff_t>(take)));
    idx = res.second;
  }
  r.idx_of_best_point = idx;

  // `knee_final_barcodes = [x[0] for x in most_common()[:idx+1]]` — a LIST, in
  // most_common order. That order is load-bearing (see KneeResult).
  std::vector<std::string> knee_final;
  for (std::size_t i = 0; i < mc.size() && static_cast<std::int64_t>(i) <= idx; ++i)
    knee_final.push_back(mc[i].first);

  if (cell_number) {
    // threshold = counts[cell_number] — a plain Python index, negatives included.
    const std::size_t k = py_index(*cell_number, mc.size(), "getKneeEstimateDistance");
    const std::int64_t threshold = mc[k].second;
    // `set([x for x, y in cell_barcode_counts.items() if y > threshold])` — the
    // comprehension iterates the Counter in INSERTION order.
    std::vector<std::string> final_barcodes;
    for (const auto& [bc, n] : counts)
      if (n > threshold) final_barcodes.push_back(bc);
    r.final_barcodes = std::move(final_barcodes);
  } else {
    r.final_barcodes = std::move(knee_final);
  }
  return r;
}

KneeResult get_knee_estimate_density(const CellBarcodeCounts& counts,
                                     std::optional<std::int64_t> expect_cells,
                                     std::optional<std::int64_t> cell_number) {
  KneeResult r;
  const auto mc = most_common(counts);
  if (mc.empty()) throw std::invalid_argument("getKneeEstimateDensity: no barcodes");

  // threshold = 0.001 * most_common(1)[0][1]
  double threshold = 0.001 * static_cast<double>(mc.front().second);

  // counts = sorted(values, reverse=True); counts_thresh = [x for x if x > threshold]
  std::vector<std::int64_t> sorted_counts;
  sorted_counts.reserve(counts.size());
  for (const auto& [bc, n] : counts) {
    (void)bc;
    sorted_counts.push_back(n);
  }
  std::sort(sorted_counts.begin(), sorted_counts.end(), std::greater<std::int64_t>());

  std::vector<double> log_counts;
  for (std::int64_t x : sorted_counts)
    if (static_cast<double>(x) > threshold) log_counts.push_back(std::log10(static_cast<double>(x)));
  if (log_counts.empty())
    throw std::invalid_argument("getKneeEstimateDensity: no counts above the 0.001 threshold");

  const std::int64_t xx_values = 10000;
  const double lo = *std::min_element(log_counts.begin(), log_counts.end());
  const double hi = *std::max_element(log_counts.begin(), log_counts.end());
  const std::vector<double> xx = linspace(lo, hi, xx_values);

  std::int64_t local_min = -1;   // Python's None

  if (cell_number) {
    const std::size_t k =
        py_index(*cell_number, sorted_counts.size(), "getKneeEstimateDensity");
    threshold = static_cast<double>(sorted_counts[k]);
  } else {
    const std::vector<double> density = gaussian_kde(log_counts, xx, 0.1);
    const std::vector<std::int64_t> local_mins = argrelextrema_less(density);

    // for poss_local_min in local_mins[::-1]  — REVERSED
    for (auto it = local_mins.rbegin(); it != local_mins.rend(); ++it) {
      const std::int64_t poss = *it;
      const double cut = std::pow(10.0, xx[static_cast<std::size_t>(poss)]);
      std::int64_t passing = 0;
      for (const auto& [bc, n] : counts) {
        (void)bc;
        if (static_cast<double>(n) > cut) ++passing;
      }
      r.local_mins_counts.push_back(passing);

      // `if not local_min:` is a TRUTHINESS test, so index 0 would not stop the
      // search — but index 0 can never be a local minimum (endpoints are
      // excluded), so this is equivalent to "not yet selected".
      if (local_min == -1) {
        if (expect_cells) {
          if (passing > static_cast<double>(*expect_cells) * 0.1 && passing <= *expect_cells)
            local_min = poss;
        } else {
          // The upstream heuristic, transcribed literally.
          if (poss >= static_cast<std::int64_t>(0.2 * static_cast<double>(xx_values)) &&
              (hi - xx[static_cast<std::size_t>(poss)] > 0.5 ||
               xx[static_cast<std::size_t>(poss)] < hi / 2.0))
            local_min = poss;
        }
      }
    }
    if (local_min != -1) threshold = std::pow(10.0, xx[static_cast<std::size_t>(local_min)]);
  }
  r.selected_local_min = local_min;

  if (cell_number || local_min != -1) {
    std::vector<std::string> final_barcodes;
    for (const auto& [bc, n] : counts)
      if (static_cast<double>(n) > threshold) final_barcodes.push_back(bc);
    r.final_barcodes = std::move(final_barcodes);
  }
  // else: final_barcodes stays nullopt — the Python's `final_barcodes = None`.
  return r;
}

std::map<std::string, std::set<std::string>> get_error_correct_mapping(
    const std::vector<std::string>& cell_barcodes,
    const std::vector<std::string>& whitelist, std::int64_t threshold) {
  // whitelist_methods' OWN hamming_distance: inf for unequal lengths, and it
  // does NOT raise (unlike the compiled kernel, which upstream deliberately
  // shadows here).
  // L16 records that the BK-tree is replaced by a linear scan — a metric INDEX,
  // not a semantic, so the answer is the same. What the ledger did NOT record is
  // that upstream brackets the tree construction with two INFO lines, and they
  // are part of every -L log. Surfaced by the Single_cell tutorial, whose
  // whitelist log was otherwise identical.
  Log::instance().info("building bktree");
  Log::instance().info("done building bktree");

  auto hamming = [](const std::string& a, const std::string& b) -> std::int64_t {
    if (a.size() != b.size()) return -1;   // stands in for np.inf: never <= threshold
    std::int64_t d = 0;
    for (std::size_t i = 0; i < a.size(); ++i)
      if (a[i] != b[i]) ++d;
    return d;
  };

  std::map<std::string, std::set<std::string>> true_to_false;
  for (const std::string& cb : cell_barcodes) {
    if (std::find(whitelist.begin(), whitelist.end(), cb) != whitelist.end())
      continue;   // already whitelisted
    // BKTree.find(cb, threshold) filtered to d > 0. A BK-tree is an INDEX, not a
    // semantic: the result is every whitelist barcode within `threshold`, which
    // a scan computes identically. At the sizes this runs on (whitelist is the
    // knee output, typically 10^2-10^3) the scan is not the bottleneck.
    std::vector<std::string> candidates;
    for (const std::string& w : whitelist) {
      const std::int64_t d = hamming(cb, w);
      if (d > 0 && d <= threshold) {
        candidates.push_back(w);
        if (candidates.size() > 1) break;   // the caller only needs 0 / 1 / >1
      }
    }
    if (candidates.size() == 1) true_to_false[candidates[0]].insert(cb);
  }
  return true_to_false;
}

CellWhitelist get_cell_whitelist(const CellBarcodeCounts& counts,
                                 const std::string& knee_method,
                                 std::optional<std::int64_t> expect_cells,
                                 std::optional<std::int64_t> cell_number,
                                 std::int64_t error_correct_threshold) {
  CellWhitelist out;
  KneeResult knee;
  if (knee_method == "distance") {
    knee = get_knee_estimate_distance(counts, cell_number);
  } else if (knee_method == "density") {
    knee = get_knee_estimate_density(counts, expect_cells, cell_number);
  } else {
    throw std::invalid_argument("knee_method must be 'distance' or 'density'");
  }
  Log::instance().info("Finished - whitelist determination");

  out.whitelist = knee.final_barcodes;
  // Pinned HERE, at the point getKneeEstimateDensity returns, because that is
  // where upstream writes <prefix>_cell_thresholds.tsv from its local
  // `final_barcodes`. whitelist.main later replaces the whitelist with
  // errorDetectAboveThreshold's survivors, and reading the size at write time
  // read the wrong list.
  out.whitelist_size_at_knee =
      out.whitelist ? static_cast<std::int64_t>(out.whitelist->size()) : -1;
  out.local_mins_counts = knee.local_mins_counts;
  out.selected_local_min = knee.selected_local_min;
  out.idx_of_best_point = knee.idx_of_best_point;

  // Python: `if cell_whitelist and error_correct_threshold > 0`. The first is a
  // truthiness test, so an EMPTY whitelist skips error correction too.
  if (out.whitelist && !out.whitelist->empty() && error_correct_threshold > 0) {
    Log::instance().info("Starting - finding putative error cell barcodes");
    std::vector<std::string> keys;
    keys.reserve(counts.size());
    for (const auto& [bc, n] : counts) {
      (void)n;
      keys.push_back(bc);
    }
    out.true_to_false_map =
        get_error_correct_mapping(keys, *out.whitelist, error_correct_threshold);
    out.has_true_to_false = true;
    Log::instance().info("Finished - finding putative error cell barcodes");
  }
  return out;
}

std::vector<std::string> check_error(const std::string& barcode,
                                     const std::vector<std::string>& whitelist,
                                     std::int64_t errors) {
  // regex.compile("(%s){e<=%i}" % (barcode, errors)).match(whitelisted)
  const Pattern p("(" + barcode + "){e<=" + std::to_string(errors) + "}");
  const auto b_length = static_cast<std::int64_t>(barcode.size());
  std::vector<std::string> near_matches;
  for (const std::string& w : whitelist) {
    const auto w_length = static_cast<std::int64_t>(w.size());
    if (barcode == w) continue;                       // don't check against itself
    if (std::max(b_length, w_length) > std::min(b_length, w_length) + errors) continue;
    if (p.match(w)) {
      near_matches.push_back(w);
      // "Assuming downstream processes are the same for (>1 -> Inf)
      // near_matches this is OK" — returns as soon as two are found.
      if (near_matches.size() > 1) return near_matches;
    }
  }
  return near_matches;
}

ErrorDetectResult error_detect_above_threshold(
    const CellBarcodeCounts& counts, const std::vector<std::string>& cell_whitelist,
    const std::map<std::string, std::set<std::string>>& true_to_false_map,
    std::int64_t errors, const std::string& resolution_method) {
  if (resolution_method != "discard" && resolution_method != "correct")
    throw std::logic_error("resolution method must be discard or correct");

  ErrorDetectResult out;
  out.true_to_false_map = true_to_false_map;   // copy.deepcopy

  std::int64_t substitution_corrected = 0, indel_discarded = 0;

  // cell_whitelist = list(cell_whitelist); cell_whitelist.sort(key=counts[x])
  //
  // ASCENDING by count with a STABLE sort, so ties keep the INCOMING order. For
  // the distance knee with no --set-cell-number — which is what every
  // ed-above-threshold fixture uses — that incoming order is most_common()'s
  // (count descending, ties by first observation), because getKneeEstimateDistance
  // returns a LIST there rather than a set. Sorting a lexicographic base order
  // instead changed which of two equal-count near-misses survived, in 3 barcodes.
  std::vector<std::string> ordered(cell_whitelist.begin(), cell_whitelist.end());
  std::stable_sort(ordered.begin(), ordered.end(),
                   [&counts](const std::string& a, const std::string& b) {
                     return counts.get(a, 0) < counts.get(b, 0);
                   });

  std::set<std::string> discard_cbs;
  for (std::size_t ix = 0; ix < ordered.size(); ++ix) {
    const std::vector<std::string> rest(ordered.begin() + static_cast<std::ptrdiff_t>(ix) + 1,
                                        ordered.end());
    const auto near_misses = check_error(ordered[ix], rest, errors);

    if (!near_misses.empty()) {
      // NOTE: upstream writes `error_counter["error_discarded_mt_1"]` with NO
      // `+= 1` (bug D7#2), so that counter is always 0 and the log line that
      // reports it always says 0. Reproduced by not counting it.
      discard_cbs.insert(ordered[ix]);
    }

    if (resolution_method == "correct" && near_misses.size() == 1) {
      // Only substitutions are correctable; an INDEL would also corrupt the UMI.
      const Pattern sub_only("(" + ordered[ix] + "){s<=" + std::to_string(errors) + "}");
      if (sub_only.match(near_misses[0])) {
        out.true_to_false_map[near_misses[0]].insert(ordered[ix]);
        ++substitution_corrected;
      } else {
        discard_cbs.insert(ordered[ix]);
        ++indel_discarded;
      }
    }
  }

  Log& log = Log::instance();
  if (resolution_method == "correct") {
    log.info("CBs above the knee corrected due to possible substitutions: " +
             std::to_string(substitution_corrected));
    log.info("CBs above the knee discarded due to possible INDELs: " +
             std::to_string(indel_discarded));
    // Always 0 upstream — see the note above.
    log.info("CBs above the knee discarded due to possible errors from multiple other CBs: 0");
  } else {
    log.info("CBs above the knee discarded due to possible errors: " +
             std::to_string(static_cast<std::int64_t>(discard_cbs.size())));
  }

  for (const std::string& cb : cell_whitelist)
    if (!discard_cbs.count(cb)) out.cell_whitelist.push_back(cb);
  return out;
}

}  // namespace umi_tools
