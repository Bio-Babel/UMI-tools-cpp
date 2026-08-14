// dedup --output-stats: the three TSVs, plus the two units that feed them.
//
// This is the only place the port needs NUMPY's random stream rather than
// CPython's (see PyRandom::seed_numpy). Everything here is bit-reproducible:
// np.random.choice with a `p=` argument is not a black box but a documented
// three-step recipe, MEASURED here rather than assumed —
//     cdf = p.cumsum(); cdf /= cdf[-1]
//     idx = cdf.searchsorted(random_sample(n), side='right')
// which is std::partial_sum + std::upper_bound over the same float64 values in
// the same order.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "umi_tools/alignment.hpp"
#include "umi_tools/bytes.hpp"
#include "umi_tools/ordered_map.hpp"
#include "umi_tools/py_random.hpp"
#include "umi_tools/sam_methods.hpp"

namespace umi_tools {

/// umi_methods.get_average_umi_distance(umis).
///
/// Returns -1 for a single UMI (Python returns the INT -1, which then flows into
/// a float list and lands in the lowest histogram bin, relabelled "Single_UMI").
double get_average_umi_distance(const std::vector<Bytes>& umis);

/// umi_methods.random_read_generator — draws UMIs with replacement, weighted by
/// how often each was observed in the input BAM.
class RandomReadGenerator {
 public:
  // `barcode_getter` is the same functor get_bundles uses, so the UMIs counted
  // here are exactly the ones the bundles were keyed on.
  RandomReadGenerator(const std::string& path, const std::string& reference,
                      const std::string& chrom, const BundleOptions& options,
                      PyRandom& numpy_rng);

  /// getUmis(n). Returns a VIEW-sized copy of the next n draws.
  std::vector<Bytes> get_umis(std::int64_t n);

 private:
  void refill_random();

  PyRandom& rng_;
  std::vector<Bytes> keys_;      // list(self.umis.keys()) — INSERTION order
  std::vector<double> cdf_;
  std::vector<Bytes> random_umis_;
  std::int64_t random_ix_ = 0;
  std::int64_t random_fill_size_ = 100000;
};

/// The accumulated per-bundle statistics, written out by write_stats().
struct DedupStats {
  OrderedMap<Bytes, std::vector<std::int64_t>> pre;    // UMI -> counts observed
  OrderedMap<Bytes, std::vector<std::int64_t>> post;
  std::vector<double> pre_cluster;
  std::vector<double> post_cluster;
  std::vector<double> pre_cluster_null;
  std::vector<double> post_cluster_null;
};

void write_stats(const DedupStats& stats, const std::string& prefix,
                 const std::string& method_name);

}  // namespace umi_tools
