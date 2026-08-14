// network.hpp — the port of umi_tools/network.py, the algorithmic heart.
//
// Transcribed per unit from the Python, read AND run.
// The three semantics that decide correctness here, all measured:
//
//  1. ORDER IS OBSERVABLE. `unique` and `percentile` return the input dict's
//     insertion order; `cluster` and `directional` return a STABLE sort by count
//     descending over a lexicographically sorted component, so every tie is
//     broken by the preceding order. See ordered_map.hpp and 01_audit.md D4.
//
//  2. `adjacency`'s within-group order is NOT deterministic in Python — it comes
//     from `list(set - set)`, and CPython's bytes hashing is randomised per
//     process (measured across PYTHONHASHSEED). This port is deterministic; the
//     parity comparison canonicalises. Upstream's own harness marks exactly the
//     affected fixtures `sort: true`.
//
//  3. `sorted(..., reverse=True)` in Python is STABLE: reverse=True does not
//     reverse the relative order of equal elements. So the C++ equivalent is
//     std::stable_sort with a strict `>` comparator, never std::sort and never
//     a reversed iteration.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "umi_tools/bytes.hpp"
#include "umi_tools/ordered_map.hpp"

namespace umi_tools {

/// umis -> counts, in insertion order. This is the `bundle`/`counts` dict that
/// UMIClusterer is called with.
using UmiCounts = OrderedMap<Bytes, std::int64_t>;

/// The adjacency structure `{umi: [umi, ...]}`. Insertion-ordered because the
/// Python builds it from a dict comprehension over `umis` and then appends.
using AdjList = OrderedMap<Bytes, std::vector<Bytes>>;

/// The five clustering methods. A closed set, so an enum + switch rather than
/// virtuals or std::function (06_design.md).
enum class ClusterMethod { Unique, Percentile, Cluster, Adjacency, Directional };

/// Parse the --method string exactly as the Python does. Note that the Python's
/// __init__ has no else branch: an unrecognised method leaves get_adj_list unset
/// and fails later with AttributeError. Reproduced as an error at construction,
/// which is the same observable outcome (the tool aborts) at a better place.
std::optional<ClusterMethod> parse_cluster_method(BytesView name);
const char* cluster_method_name(ClusterMethod m);

// ---------------------------------------------------------------------------
// Module-level helpers (each a checklist unit)
// ---------------------------------------------------------------------------

/// network.breadth_first_search. Python returns the `searched` SET; its CONTENT
/// is the connected component and is traversal-order independent, and every
/// caller sorts it, so the traversal order here is free. Deterministic FIFO.
OrderedSet<Bytes> breadth_first_search(const Bytes& node, const AdjList& adj_list);

/// network.remove_umis: set(cluster) - ({adj of each node in nodes} | nodes).
/// Callers use only len(), so the content is what matters.
OrderedSet<Bytes> remove_umis(const AdjList& adj_list,
                              const std::vector<Bytes>& cluster,
                              const std::vector<Bytes>& nodes);

/// network.get_substr_slices. Both divmod operands are provably >= 1
/// (umi_length is a barcode length, idx_size = min_edit + 1), so the bare / and %
/// are correct here and py_compat is not needed.
std::vector<std::pair<std::int64_t, std::int64_t>> get_substr_slices(
    std::int64_t umi_length, std::int64_t idx_size);

/// network.build_substr_idx: slice -> substring -> {umis containing it}.
using SubstrIdx = std::vector<
    std::pair<std::pair<std::int64_t, std::int64_t>, OrderedMap<Bytes, OrderedSet<Bytes>>>>;
SubstrIdx build_substr_idx(const std::vector<Bytes>& umis, std::int64_t umi_length,
                           std::int64_t min_edit);

/// network.iter_nearest_neighbours, STREAMING — the shape the Python has.
///
/// Upstream is a generator consumed one pair at a time, so peak memory is the
/// surviving edge set, not the candidate set. Collecting first was the defect found in review: the
/// yielded-pair count is quadratic in the UMI count, not bounded by it, so a
/// deep bundle materialised orders of magnitude more pairs than it kept.
/// Emission order is identical to the generator's.
void for_each_nearest_neighbour(
    const std::vector<Bytes>& umis, const SubstrIdx& substr_idx,
    const std::function<void(const Bytes&, const Bytes&)>& emit);

/// Collecting wrapper over for_each_nearest_neighbour, for the parity driver's
/// line protocol. NOT used by the clusterer, for the reason above.
std::vector<std::pair<Bytes, Bytes>> iter_nearest_neighbours(
    const std::vector<Bytes>& umis, const SubstrIdx& substr_idx);

/// Dead upstream code, ported at observable-behaviour parity (01_audit.md D3).
/// recursive_search / breadth_first_search_recursive are referenced only from a
/// commented-out block and a TODO. They are public names, so they are checklist
/// units and cannot be silently dropped.
OrderedSet<Bytes> recursive_search(const Bytes& node, const AdjList& adj_list,
                                   OrderedSet<Bytes>& component);
OrderedSet<Bytes> breadth_first_search_recursive(const Bytes& node,
                                                 const AdjList& adj_list);

/// ---------------------------------------------------------------------------
/// UMIClusterer
/// ---------------------------------------------------------------------------
class UMIClusterer {
 public:
  explicit UMIClusterer(ClusterMethod cluster_method = ClusterMethod::Directional);

  /// network.UMIClusterer.__call__(umis, threshold), where `umis` IS the counts
  /// dict. Returns the grouping: one inner vector per group, group[0] is the
  /// representative ("top") UMI.
  ///
  /// Throws std::logic_error with the Python's own message when the UMIs are not
  /// all the same length, and std::invalid_argument on an empty input (Python's
  /// `max([])` raises ValueError there).
  std::vector<std::vector<Bytes>> operator()(const UmiCounts& umis,
                                             std::int64_t threshold);

  /// The three mutable counters group/dedup log after the run.
  std::int64_t positions() const noexcept { return positions_; }
  std::int64_t total_umis_per_position() const noexcept { return total_umis_per_position_; }
  std::int64_t max_umis_per_position() const noexcept { return max_umis_per_position_; }

  ClusterMethod method() const noexcept { return method_; }

  /// Exposed for per-unit parity tests; these are the Python's "private" methods
  /// and each is a checklist unit.
  AdjList get_adj_list(const std::vector<Bytes>& umis, const UmiCounts& counts,
                       std::int64_t threshold) const;
  std::vector<std::vector<Bytes>> get_connected_components(
      const std::vector<Bytes>& umis, const AdjList& adj_list,
      const UmiCounts& counts) const;
  std::vector<std::vector<Bytes>> get_groups(
      const std::vector<std::vector<Bytes>>& clusters, const std::vector<Bytes>& umis,
      const AdjList& adj_list, const UmiCounts& counts) const;

 private:
  ClusterMethod method_;
  std::int64_t max_umis_per_position_ = 0;
  std::int64_t total_umis_per_position_ = 0;
  std::int64_t positions_ = 0;
};

/// numpy's median: for an even-length sample, the MEAN of the two middle values
/// (not a lower-median). Used by _get_best_percentile.
double np_median(std::vector<std::int64_t> values);

/// CellClusterer — dead code that CANNOT EXECUTE upstream (01_audit.md D3).
/// Ported at observable-behaviour parity: the constructor accepts only
/// "directional" and raises otherwise; operator() raises, because the Python's
/// does (TypeError: _get_connected_components_adjacency() takes 3 positional
/// arguments but 4 were given) before it can reach its two further defects
/// (get_groups is never assigned; a set is .sort()ed).
class CellClusterer {
 public:
  explicit CellClusterer(BytesView cluster_method = "directional",
                         std::int64_t dir_threshold = 10, bool fuzzy_match = true);
  std::vector<std::vector<Bytes>> operator()(const std::vector<Bytes>& umis,
                                             const UmiCounts& counts) const;

 private:
  std::int64_t dir_threshold_;
  bool fuzzy_match_;
};

}  // namespace umi_tools
