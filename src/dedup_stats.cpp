#include "umi_tools/dedup_stats.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <set>

#include "umi_tools/edit_distance.hpp"
#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"

namespace umi_tools {

double get_average_umi_distance(const std::vector<Bytes>& umis) {
  // `if len(umis) == 1: return -1` — the int -1, which is why the lowest
  // histogram bin exists at all and gets relabelled "Single_UMI".
  if (umis.size() == 1) return -1.0;
  std::int64_t total = 0, n = 0;
  for (std::size_t i = 0; i < umis.size(); ++i)
    for (std::size_t j = i + 1; j < umis.size(); ++j) {
      total += edit_distance(umis[i], umis[j]);
      ++n;
    }
  // itertools.combinations of 0 or 1 items gives an empty list and Python would
  // raise ZeroDivisionError; the size-1 case returned above, and a bundle is
  // never empty, so n > 0 here.
  return static_cast<double>(total) / static_cast<double>(n);
}

RandomReadGenerator::RandomReadGenerator(const std::string& path,
                                         const std::string& reference,
                                         const std::string& chrom,
                                         const BundleOptions& options,
                                         PyRandom& numpy_rng)
    : rng_(numpy_rng) {
  // `inbam.fetch(reference=chrom)` when --chrom is set, else `inbam.fetch()` —
  // both exclude unplaced records (until_eof defaults to False).
  AlignmentReader reader(path, reference);
  // Measured: `dedup --output-stats` on a coordinate-sorted but
  // UNINDEXED BAM — the normal output of `samtools sort`, which builds no index
  // — exits 1 upstream with `ValueError: fetch called on bamfile without index`
  // before any bundle is processed, because BOTH `fetch()` and
  // `fetch(reference=chrom)` run with until_eof=False. The port loaded an index
  // only for the --chrom form and otherwise fell back to a linear pass, so it
  // exited 0 and wrote three stats files upstream would never produce.
  reader.require_index();
  if (!chrom.empty()) reader.set_region(chrom);

  OrderedMap<Bytes, std::int64_t> umis;   // collections.defaultdict(int)
  BamRecord read;
  while (reader.next(read)) {
    if (chrom.empty() && read.tid() < 0) continue;
    if (read.is_unmapped()) continue;
    if (read.is_read2()) continue;
    // `except KeyError: continue` — a read with no umi/cell tag is skipped.
    const auto cu = barcode_for_read(read, options);
    if (!cu) continue;
    umis[cu->umi] += 1;
  }

  std::int64_t total_umis = 0;
  for (const auto& [k, v] : umis) { (void)k; total_umis += v; }
  Log::instance().info("total_umis " + std::to_string(total_umis));
  Log::instance().info("#umis " + std::to_string(umis.size()));

  // prob[i] = float(count_i) / sum, then numpy's cumsum and the /= cdf[-1]
  // normalisation, in that order — the float64 rounding of each step is part of
  // which barcode a given draw selects.
  keys_.reserve(umis.size());
  cdf_.reserve(umis.size());
  double running = 0.0;
  for (const auto& [k, v] : umis) {
    keys_.push_back(k);
    running += static_cast<double>(v) / static_cast<double>(total_umis);
    cdf_.push_back(running);
  }
  const double last = cdf_.empty() ? 1.0 : cdf_.back();
  for (double& c : cdf_) c /= last;

  refill_random();
}

void RandomReadGenerator::refill_random() {
  // When the BAM yields no usable UMIs, keys_ and cdf_ are both empty. The old
  // guard `if (idx >= keys_.size()) idx = keys_.size() - 1;` computed 0u - 1 on
  // size_t, wrapping to SIZE_MAX, and keys_[idx] was an out-of-bounds read
  // repeated random_fill_size_ (100,000) times BEFORE any bundle was processed.
  // ASan catches that only if the wild address happens to be unmapped.
  //
  // Python raises out of np.random.choice(..., p=[]) instead. Reachable via
  // --output-stats with an absent --umi-tag, --chrom naming an empty contig, or
  // a header-only BAM.
  if (keys_.empty())
    raise_value_error("'a' cannot be empty unless no samples are taken");
  random_umis_.clear();
  random_umis_.reserve(static_cast<std::size_t>(random_fill_size_));
  for (std::int64_t i = 0; i < random_fill_size_; ++i) {
    const double u = rng_.random();
    // searchsorted(..., side='right') — the first index whose cdf value is
    // strictly greater than u.
    const auto it = std::upper_bound(cdf_.begin(), cdf_.end(), u);
    std::size_t idx = static_cast<std::size_t>(it - cdf_.begin());
    // keys_ is non-empty (checked above), so the clamp cannot underflow.
    if (idx >= keys_.size()) idx = keys_.size() - 1;   // u == 1.0 is unreachable
    random_umis_.push_back(keys_[idx]);
  }
  random_ix_ = 0;
}

std::vector<Bytes> RandomReadGenerator::get_umis(std::int64_t n) {
  if (n >= random_fill_size_ - random_ix_) {
    if (n > random_fill_size_) random_fill_size_ = n * 2;
    refill_random();
  }
  std::vector<Bytes> out;
  out.reserve(static_cast<std::size_t>(n));
  for (std::int64_t i = 0; i < n; ++i)
    out.push_back(random_umis_[static_cast<std::size_t>(random_ix_ + i)]);
  random_ix_ += n;
  return out;
}

namespace {

// pandas' median: the middle element for odd n, the MEAN of the two middle
// elements for even n — which is why an even group can produce a `.5`.
double pandas_median(std::vector<std::int64_t> v) {
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if (n % 2 == 1) return static_cast<double>(v[n / 2]);
  return (static_cast<double>(v[n / 2 - 1]) + static_cast<double>(v[n / 2])) / 2.0;
}

// np.digitize(x, bins, right=True) with bins = range(-1, max_ed+2): the index i
// such that bins[i-1] < x <= bins[i].
std::int64_t digitize_right(double x, std::int64_t lo, std::int64_t n_bins) {
  for (std::int64_t i = 0; i < n_bins; ++i)
    if (x <= static_cast<double>(lo + i)) return i;
  return n_bins;
}

std::vector<std::int64_t> tally(const std::vector<double>& values, std::int64_t max_ed) {
  const std::int64_t n_bins = max_ed + 3;          // len(range(-1, max_ed+2))
  std::vector<std::int64_t> out(static_cast<std::size_t>(n_bins), 0);
  for (double v : values) {
    const std::int64_t b = digitize_right(v, -1, n_bins);
    if (b >= 0 && b < n_bins) out[static_cast<std::size_t>(b)] += 1;
  }
  return out;
}

}  // namespace

void write_stats(const DedupStats& stats, const std::string& prefix,
                 const std::string& method_name) {
  // --- 1. <prefix>_per_umi_per_position.tsv -------------------------------
  // Counter over the COUNTS (not the UMIs), indexed by the sorted union.
  std::map<std::int64_t, std::int64_t> pre_counts, post_counts;
  for (const auto& [umi, v] : stats.pre) { (void)umi; for (std::int64_t c : v) pre_counts[c] += 1; }
  for (const auto& [umi, v] : stats.post) { (void)umi; for (std::int64_t c : v) post_counts[c] += 1; }
  std::set<std::int64_t> index;
  for (const auto& [c, n] : pre_counts) { (void)n; index.insert(c); }
  for (const auto& [c, n] : post_counts) { (void)n; index.insert(c); }
  {
    Writer out(prefix + "_per_umi_per_position.tsv");
    out.write("counts\tinstances_pre\tinstances_post\n");
    for (std::int64_t c : index) {
      auto a = pre_counts.find(c), b = post_counts.find(c);
      out.write(std::to_string(c) + "\t" +
                std::to_string(a == pre_counts.end() ? 0 : a->second) + "\t" +
                std::to_string(b == post_counts.end() ? 0 : b->second) + "\n");
    }
    out.close();
  }

  // --- 2. <prefix>_per_umi.tsv --------------------------------------------
  // MEASURED contract: rows are SORTED by the UMI
  // bytes; the merge is how='left' so the row set is the PRE index and a UMI
  // absent post-dedup becomes 0; and `.astype(int)` TRUNCATES, so an even
  // observation count whose median lands on .5 rounds DOWN.
  {
    Writer out(prefix + "_per_umi.tsv");
    out.write("UMI\tmedian_counts_pre\ttimes_observed_pre\ttotal_counts_pre\t"
              "median_counts_post\ttimes_observed_post\ttotal_counts_post\n");
    std::vector<Bytes> umis;
    for (const auto& [umi, v] : stats.pre) { (void)v; umis.push_back(umi); }
    std::sort(umis.begin(), umis.end());
    for (const Bytes& umi : umis) {
      const std::vector<std::int64_t>& pv = stats.pre.at(umi);
      const std::int64_t pre_med = static_cast<std::int64_t>(pandas_median(pv));
      const std::int64_t pre_n = static_cast<std::int64_t>(pv.size());
      const std::int64_t pre_sum = std::accumulate(pv.begin(), pv.end(), std::int64_t{0});
      std::int64_t post_med = 0, post_n = 0, post_sum = 0;
      if (stats.post.contains(umi)) {
        const std::vector<std::int64_t>& qv = stats.post.at(umi);
        post_med = static_cast<std::int64_t>(pandas_median(qv));
        post_n = static_cast<std::int64_t>(qv.size());
        post_sum = std::accumulate(qv.begin(), qv.end(), std::int64_t{0});
      }
      out.write(umi + "\t" + std::to_string(pre_med) + "\t" + std::to_string(pre_n) +
                "\t" + std::to_string(pre_sum) + "\t" + std::to_string(post_med) +
                "\t" + std::to_string(post_n) + "\t" + std::to_string(post_sum) + "\n");
    }
    out.close();
  }

  // --- 3. <prefix>_edit_distance.tsv ---------------------------------------
  // `int(max(map(max, [a, b, c, d])))` calls max() on EACH list, so an
  // EMPTY one raises `ValueError: max() arg is an empty sequence` and dedup
  // aborts (rc 1) having written only the first two TSVs. This left have==false,
  // kept max_val 0.0, and silently wrote a 3-row _edit_distance.tsv before
  // exiting 0. Reachable when no bundle reaches the stats block at all — e.g.
  // --output-stats with a --umi-whitelist whose UMIs never top a cluster, where
  // every bundle hits `if len(reads) == 0: continue`.
  double max_val = 0.0;
  bool have = false;
  for (const auto* v : {&stats.pre_cluster, &stats.post_cluster,
                        &stats.pre_cluster_null, &stats.post_cluster_null}) {
    if (v->empty()) throw std::invalid_argument("max() arg is an empty sequence");
    for (double x : *v) { max_val = have ? std::max(max_val, x) : x; have = true; }
  }
  // `int(max(...))` truncates toward zero.
  const std::int64_t max_ed = static_cast<std::int64_t>(max_val);

  const auto m  = tally(stats.post_cluster, max_ed);
  const auto mn = tally(stats.post_cluster_null, max_ed);
  // The dict literal at dedup.py:420-427 binds "unique" and then
  // `options.method`; with --method=unique those are the SAME KEY, so the later
  // (post-dedup) tally wins and columns 1-2 duplicate 3-4. Upstream's explicit
  // `columns=[...]` still names five, so the file keeps five fields — with the
  // first two carrying POST values. MEASURED on chr19.bam: the oracle's row 5 is
  // `20 8 20 8 2` while the port wrote `20 7 20 8 2`.
  //
  // Other --method values leave the keys distinct and are unaffected.
  const bool key_collision = (method_name == "unique");
  const auto u  = key_collision ? m  : tally(stats.pre_cluster, max_ed);
  const auto un = key_collision ? mn : tally(stats.pre_cluster_null, max_ed);
  {
    Writer out(prefix + "_edit_distance.tsv");
    out.write("unique\tunique_null\t" + method_name + "\t" + method_name +
              "_null\tedit_distance\n");
    for (std::size_t i = 0; i < u.size(); ++i) {
      // cluster_bins = range(-1, max_ed+2); row 0's label is overwritten.
      const std::string label =
          i == 0 ? std::string("Single_UMI")
                 : std::to_string(static_cast<std::int64_t>(i) - 1);
      out.write(std::to_string(u[i]) + "\t" + std::to_string(un[i]) + "\t" +
                std::to_string(m[i]) + "\t" + std::to_string(mn[i]) + "\t" +
                label + "\n");
    }
    out.close();
  }
}

}  // namespace umi_tools
