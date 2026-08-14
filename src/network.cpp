#include "umi_tools/network.hpp"

#include <algorithm>
#include <functional>
#include <deque>
#include <stdexcept>
#include <string>

#include "umi_tools/edit_distance.hpp"

namespace umi_tools {
namespace {

// Python: sorted(seq, key=lambda x: counts[x], reverse=True)
//
// STABLE, and descending. Python's sort is stable and `reverse=True` does NOT
// reverse the order of equal elements, so the C++ equivalent is stable_sort with
// a strict `>` on the key. std::sort would be free to permute ties, and
// sorting ascending then reversing would invert them — both change the output of
// `cluster` and `directional`, whose within-group order is compared exactly.
std::vector<Bytes> sorted_by_count_desc(const std::vector<Bytes>& in,
                                        const UmiCounts& counts) {
  std::vector<Bytes> out = in;
  std::stable_sort(out.begin(), out.end(), [&counts](const Bytes& a, const Bytes& b) {
    return counts.at(a) > counts.at(b);
  });
  return out;
}

}  // namespace

std::optional<ClusterMethod> parse_cluster_method(BytesView name) {
  if (name == "unique") return ClusterMethod::Unique;
  if (name == "percentile") return ClusterMethod::Percentile;
  if (name == "cluster") return ClusterMethod::Cluster;
  if (name == "adjacency") return ClusterMethod::Adjacency;
  if (name == "directional") return ClusterMethod::Directional;
  return std::nullopt;
}

const char* cluster_method_name(ClusterMethod m) {
  switch (m) {
    case ClusterMethod::Unique: return "unique";
    case ClusterMethod::Percentile: return "percentile";
    case ClusterMethod::Cluster: return "cluster";
    case ClusterMethod::Adjacency: return "adjacency";
    case ClusterMethod::Directional: return "directional";
  }
  return "";
}

// --------------------------------------------------------------------------
// breadth_first_search
//
// Python:
//   searched = set(); queue = set()
//   queue.update((node,)); searched.update((node,))
//   while len(queue) > 0:
//       node = queue.pop()
//       for next_node in adj_list[node]:
//           if next_node not in searched:
//               queue.update((next_node,)); searched.update((next_node,))
//   return searched
//
// `queue.pop()` takes an arbitrary set element, so the Python's traversal order
// is unspecified — but the RESULT is the full connected component either way,
// and every caller sorts it. A FIFO deque is used here for determinism.
// --------------------------------------------------------------------------
OrderedSet<Bytes> breadth_first_search(const Bytes& node, const AdjList& adj_list) {
  OrderedSet<Bytes> searched;
  std::deque<Bytes> queue;
  queue.push_back(node);
  searched.insert(node);

  while (!queue.empty()) {
    const Bytes current = queue.front();
    queue.pop_front();
    // Python indexes adj_list[node] directly: a node reached by an edge is
    // always a key, because the adjacency dict is built over all umis first.
    for (const Bytes& next_node : adj_list.at(current)) {
      if (searched.insert(next_node)) queue.push_back(next_node);
    }
  }
  return searched;
}

// --------------------------------------------------------------------------
// remove_umis
//   nodes_to_remove = set([node for x in nodes for node in adj_list[x]] + nodes)
//   return set(cluster) - nodes_to_remove
// --------------------------------------------------------------------------
OrderedSet<Bytes> remove_umis(const AdjList& adj_list, const std::vector<Bytes>& cluster,
                              const std::vector<Bytes>& nodes) {
  OrderedSet<Bytes> nodes_to_remove;
  for (const Bytes& x : nodes)
    for (const Bytes& n : adj_list.at(x)) nodes_to_remove.insert(n);
  for (const Bytes& n : nodes) nodes_to_remove.insert(n);

  OrderedSet<Bytes> out;
  for (const Bytes& c : cluster)
    if (!nodes_to_remove.contains(c)) out.insert(c);
  return out;
}

// --------------------------------------------------------------------------
// get_substr_slices
//   cs, r = divmod(umi_length, idx_size)
//   sub_sizes = [cs + 1] * r + [cs] * (idx_size - r)
//
// divmod operands: umi_length is a barcode length (>= 1 on every reachable
// path — UMIClusterer asserts equal lengths and an empty bundle raises earlier)
// and idx_size = min_edit + 1 with min_edit = threshold >= 0, so idx_size >= 1.
// Both non-negative, so bare / and % agree with Python's // and % and
// py_compat is not required. Guarded anyway: a zero divisor here would be UB.
// --------------------------------------------------------------------------
std::vector<std::pair<std::int64_t, std::int64_t>> get_substr_slices(
    std::int64_t umi_length, std::int64_t idx_size) {
  // Python does NOT reject a negative idx_size: divmod(L, -1) is
  // (-L, 0), so r == 0 and sub_sizes == [cs+1]*0 + [cs]*(-1) == [] — negative
  // list repetition is empty — hence slices == []. build_substr_idx then yields
  // an empty index, no candidate pairs, and the bundle clusters as singletons.
  // MEASURED: get_substr_slices(10, -1) and (10, -2) are both [] upstream where
  // this threw, reachable via `--edit-distance-threshold=-2` on a >25-UMI bundle.
  //
  // idx_size == 0 stays an error: Python raises ZeroDivisionError there, so
  // failing is the faithful answer.
  if (idx_size < 0) return {};
  if (idx_size == 0)
    throw std::invalid_argument("get_substr_slices: idx_size must be positive");
  if (umi_length < 0)
    throw std::invalid_argument("get_substr_slices: umi_length must be non-negative");

  const std::int64_t cs = umi_length / idx_size;
  const std::int64_t r = umi_length % idx_size;

  std::vector<std::int64_t> sub_sizes;
  sub_sizes.reserve(static_cast<std::size_t>(idx_size));
  for (std::int64_t i = 0; i < r; ++i) sub_sizes.push_back(cs + 1);
  for (std::int64_t i = 0; i < idx_size - r; ++i) sub_sizes.push_back(cs);

  std::vector<std::pair<std::int64_t, std::int64_t>> slices;
  slices.reserve(sub_sizes.size());
  std::int64_t offset = 0;
  for (std::int64_t s : sub_sizes) {
    slices.emplace_back(offset, offset + s);
    offset += s;
  }
  return slices;
}

// --------------------------------------------------------------------------
// build_substr_idx
// --------------------------------------------------------------------------
SubstrIdx build_substr_idx(const std::vector<Bytes>& umis, std::int64_t umi_length,
                           std::int64_t min_edit) {
  SubstrIdx substr_idx;
  for (const auto& idx : get_substr_slices(umi_length, min_edit + 1)) {
    OrderedMap<Bytes, OrderedSet<Bytes>> by_substring;
    for (const Bytes& u : umis) {
      // Python u[slice(*idx)] on bytes; the slice bounds come from
      // get_substr_slices and are within [0, umi_length].
      by_substring[py_slice(u, idx.first, idx.second)].insert(u);
    }
    substr_idx.emplace_back(idx, std::move(by_substring));
  }
  return substr_idx;
}

// --------------------------------------------------------------------------
// iter_nearest_neighbours
//   for i, u in enumerate(umis, 1):           # i is 1-BASED
//       neighbours = set()
//       for idx, substr_map in substr_idx.items():
//           neighbours = neighbours.union(substr_map[u[slice(*idx)]])
//       neighbours.difference_update(umis[:i])   # drops u AND all earlier umis
//       for nbr in neighbours: yield u, nbr
//
// The 1-based enumerate is load-bearing: umis[:i] for the i-th element excludes
// u itself and everything before it, so each unordered pair is visited once.
// Getting the off-by-one wrong here either yields self-pairs (edit_distance 0,
// so every UMI becomes its own neighbour) or drops one real pair per UMI.
// --------------------------------------------------------------------------
void for_each_nearest_neighbour(
    const std::vector<Bytes>& umis, const SubstrIdx& substr_idx,
    const std::function<void(const Bytes&, const Bytes&)>& emit) {
  // The Python is a GENERATOR, consumed one pair at a time by
  // _get_adj_list_adjacency / _get_adj_list_directional, so only the pairs that
  // survive `edit_distance <= threshold` are ever retained. Collecting them all
  // first made peak memory the CANDIDATE set instead of the edge set —
  // an adversarial finding, which also corrected the header's claim that
  // the count is bounded by the bundle's UMI count. It is not: the yielded
  // count is quadratic in n, so a 1e5-UMI bundle materialised ~1e7-1e8 pairs.
  //
  // Streaming here keeps the emission ORDER identical, so the adjacency lists
  // are built in the same sequence as before.
  OrderedSet<Bytes> seen;   // membership test for `umis[:i]`, grown as i advances

  for (std::size_t z = 0; z < umis.size(); ++z) {
    const Bytes& u = umis[z];
    seen.insert(u);  // now `seen` == set(umis[:z+1]) == umis[:i] for i = z+1

    OrderedSet<Bytes> neighbours;
    for (const auto& [idx, substr_map] : substr_idx) {
      const Bytes u_sub = py_slice(u, idx.first, idx.second);
      if (substr_map.contains(u_sub))
        for (const Bytes& n : substr_map.at(u_sub)) neighbours.insert(n);
    }
    for (const Bytes& nbr : neighbours)
      if (!seen.contains(nbr)) emit(u, nbr);
  }
}

std::vector<std::pair<Bytes, Bytes>> iter_nearest_neighbours(
    const std::vector<Bytes>& umis, const SubstrIdx& substr_idx) {
  // Collecting wrapper, for the parity driver's line protocol. Deliberately NOT
  // used by the clusterer: see for_each_nearest_neighbour.
  std::vector<std::pair<Bytes, Bytes>> pairs;
  for_each_nearest_neighbour(umis, substr_idx,
                             [&pairs](const Bytes& a, const Bytes& b) {
                               pairs.emplace_back(a, b);
                             });
  return pairs;
}

// --------------------------------------------------------------------------
// Dead upstream code (01_audit.md D3), at observable-behaviour parity.
// recursive_search uses a function ATTRIBUTE as shared mutable state
// (recursive_search.component); that is a module-level global in Python, so the
// component is threaded explicitly here rather than reproducing the global.
// --------------------------------------------------------------------------
OrderedSet<Bytes> recursive_search(const Bytes& node, const AdjList& adj_list,
                                   OrderedSet<Bytes>& component) {
  // Explicit frame stack, not self-recursion. Python bounds its depth twice —
  // `sys.setrecursionlimit(10000)` at network.py:18, and
  // breadth_first_search_recursive CATCHES RecursionError and falls back to the
  // iterative BFS (network.py:47-54) — so a deep chain degrades there with a
  // logged 'Recursion Error'. C++ has no catchable stack overflow, so the same
  // chain was a SIGSEGV here: an adversarial finding.
  //
  // This is the mechanical de-recursion, so it is equivalent BY CONSTRUCTION
  // rather than by argument. Each frame keeps the SNAPSHOT of `children` taken
  // before the loop, which is load-bearing: a child added to `component` by an
  // earlier sibling's subtree is still in the snapshot and is still descended
  // into (the insert becomes a no-op, the descent does not). Filtering lazily
  // instead would skip it and explore fewer nodes.
  struct Frame {
    std::vector<Bytes> children;
    std::size_t next = 0;
  };
  auto snapshot = [&](const Bytes& n) {
    std::vector<Bytes> kids;
    for (const Bytes& c : adj_list.at(n))
      if (!component.contains(c)) kids.push_back(c);
    return kids;
  };

  std::vector<Frame> stack;
  stack.push_back(Frame{snapshot(node), 0});
  while (!stack.empty()) {
    Frame& f = stack.back();
    if (f.next == f.children.size()) {
      stack.pop_back();
      continue;
    }
    const Bytes child = f.children[f.next++];
    component.insert(child);
    stack.push_back(Frame{snapshot(child), 0});
  }
  return component;
}

OrderedSet<Bytes> breadth_first_search_recursive(const Bytes& node,
                                                 const AdjList& adj_list) {
  // Python catches RecursionError and falls back to breadth_first_search.
  // C++ has no catchable stack-overflow, so the fallback cannot be reproduced
  // faithfully; both branches return the same component, so the iterative one
  // is used. Recorded in the deviation ledger.
  return breadth_first_search(node, adj_list);
}

// --------------------------------------------------------------------------
// np.median: even length -> MEAN of the two middle values.
// --------------------------------------------------------------------------
double np_median(std::vector<std::int64_t> values) {
  if (values.empty())
    throw std::invalid_argument("np_median: empty input (numpy returns nan with a warning)");
  std::sort(values.begin(), values.end());
  const std::size_t n = values.size();
  if (n % 2 == 1) return static_cast<double>(values[n / 2]);
  return (static_cast<double>(values[n / 2 - 1]) + static_cast<double>(values[n / 2])) / 2.0;
}

// --------------------------------------------------------------------------
// UMIClusterer
// --------------------------------------------------------------------------
UMIClusterer::UMIClusterer(ClusterMethod cluster_method) : method_(cluster_method) {}

AdjList UMIClusterer::get_adj_list(const std::vector<Bytes>& umis, const UmiCounts& counts,
                                   std::int64_t threshold) const {
  // _get_adj_list_null: percentile and unique do not use an adjacency list.
  if (method_ == ClusterMethod::Unique || method_ == ClusterMethod::Percentile)
    return AdjList{};

  AdjList adj_list;
  for (const Bytes& umi : umis) adj_list[umi];  // {umi: [] for umi in umis}

  // The >25 switch to a substring index is an OPTIMISATION in the Python, but it
  // is only approximately equivalent to all-pairs: iter_nearest_neighbours finds
  // pairs sharing a substring block, which is exact for Hamming distance <=
  // threshold given threshold+1 blocks (pigeonhole), so the edge set matches.
  // The threshold value 25 is reproduced exactly because the two paths can
  // differ if that reasoning ever fails.
  const bool directional = (method_ == ClusterMethod::Directional);

  // Both candidate sources are STREAMED into this, rather than collected into a
  // `pairs` vector first: upstream consumes a generator / itertools.combinations
  // lazily and only ever retains the surviving edges. The visit ORDER
  // is unchanged, so the adjacency lists come out identical.
  auto consider = [&](const Bytes& umi1, const Bytes& umi2) {
    if (edit_distance(umi1, umi2) <= threshold) {
      if (!directional) {
        // _get_adj_list_adjacency: undirected edge.
        adj_list.at(umi1).push_back(umi2);
        adj_list.at(umi2).push_back(umi1);
      } else {
        // _get_adj_list_directional: counts[a] >= (counts[b]*2)-1.
        // NOTE the exact boundary: `>=` and `*2 - 1`, not `>` and not `*2`.
        // With counts (1,1) this is 1 >= 1 -> both edges added.
        const std::int64_t c1 = counts.at(umi1);
        const std::int64_t c2 = counts.at(umi2);
        if (c1 >= (c2 * 2) - 1) adj_list.at(umi1).push_back(umi2);
        if (c2 >= (c1 * 2) - 1) adj_list.at(umi2).push_back(umi1);
      }
    }
  };

  if (umis.size() > 25) {
    const std::int64_t umi_length = py_len(umis[0]);
    for_each_nearest_neighbour(umis, build_substr_idx(umis, umi_length, threshold),
                               consider);
  } else {
    // itertools.combinations(umis, 2) — also lazy upstream.
    for (std::size_t i = 0; i < umis.size(); ++i)
      for (std::size_t j = i + 1; j < umis.size(); ++j) consider(umis[i], umis[j]);
  }
  return adj_list;
}

std::vector<std::vector<Bytes>> UMIClusterer::get_connected_components(
    const std::vector<Bytes>& umis, const AdjList& adj_list,
    const UmiCounts& counts) const {
  // _get_connected_components_null: returns `umis` unchanged. Represented by the
  // caller, which handles unique/percentile on the flat list (as the Python
  // does), so this is only ever reached for the three graph methods.
  if (method_ == ClusterMethod::Unique || method_ == ClusterMethod::Percentile) {
    std::vector<std::vector<Bytes>> flat;
    flat.reserve(umis.size());
    for (const Bytes& u : umis) flat.push_back({u});
    return flat;
  }

  OrderedSet<Bytes> found;
  std::vector<std::vector<Bytes>> components;

  // sorted(graph, key=counts, reverse=True) — graph is a dict, so this iterates
  // its keys in INSERTION order and the stable sort breaks ties by that order.
  for (const Bytes& node : sorted_by_count_desc(adj_list.keys(), counts)) {
    if (!found.contains(node)) {
      std::vector<Bytes> component = breadth_first_search(node, adj_list).as_vector();
      // Python: component.sort() — a plain lexicographic bytes sort, NOT by
      // count. This is what makes the component order deterministic despite the
      // set-based BFS.
      std::sort(component.begin(), component.end());
      for (const Bytes& c : component) found.insert(c);
      components.push_back(std::move(component));
    }
  }
  return components;
}

namespace {

// _get_best_min_account: the minimum set of top-count UMIs that accounts for the
// whole cluster. Returns nullopt where the Python falls off the loop and returns
// None (upstream bug D7#5 — the caller then does observed.update(None) and
// raises TypeError).
std::optional<std::vector<Bytes>> get_best_min_account(const std::vector<Bytes>& cluster,
                                                       const AdjList& adj_list,
                                                       const UmiCounts& counts) {
  if (cluster.size() == 1) return cluster;

  const std::vector<Bytes> sorted_nodes = sorted_by_count_desc(cluster, counts);
  for (std::size_t i = 0; i + 1 < sorted_nodes.size(); ++i) {
    const std::vector<Bytes> prefix(sorted_nodes.begin(),
                                    sorted_nodes.begin() + static_cast<std::ptrdiff_t>(i) + 1);
    if (remove_umis(adj_list, cluster, prefix).empty()) return prefix;
  }
  return std::nullopt;  // Python returns None here
}

}  // namespace

std::vector<std::vector<Bytes>> UMIClusterer::get_groups(
    const std::vector<std::vector<Bytes>>& clusters, const std::vector<Bytes>& umis,
    const AdjList& adj_list, const UmiCounts& counts) const {
  std::vector<std::vector<Bytes>> groups;

  switch (method_) {
    case ClusterMethod::Unique: {
      // _group_unique. Python: `groups = [clusters]` when len(clusters)==1 else
      // `[[x] for x in clusters]`, where `clusters` is the flat umis LIST. Both
      // branches produce one singleton group per UMI, in INSERTION ORDER.
      groups.reserve(umis.size());
      for (const Bytes& u : umis) groups.push_back({u});
      return groups;
    }

    case ClusterMethod::Percentile: {
      // _group_percentile -> _get_best_percentile(clusters, counts) where
      // `clusters` is the flat umis list.
      //   if len(cluster) == 1: return list(cluster)
      //   threshold = np.median(list(counts.values())) / 100
      //   return [read for read in cluster if counts[read] > threshold]
      // Note: the median is over ALL counts in the bundle, not the cluster, and
      // the comparison is STRICT >. Order is the umis list's insertion order.
      if (umis.size() == 1) {
        groups.push_back({umis[0]});
        return groups;
      }
      const double threshold = np_median(counts.values()) / 100.0;
      for (const Bytes& u : umis)
        if (static_cast<double>(counts.at(u)) > threshold) groups.push_back({u});
      return groups;
    }

    case ClusterMethod::Cluster: {
      // _group_cluster: every component, sorted by count descending (stable).
      for (const auto& cluster : clusters)
        groups.push_back(sorted_by_count_desc(cluster, counts));
      return groups;
    }

    case ClusterMethod::Directional: {
      // _group_directional. `observed` persists ACROSS clusters (unlike
      // _group_adjacency, where it is per-cluster) — that difference is why the
      // two methods produce different groupings from the same components.
      OrderedSet<Bytes> observed;
      for (const auto& cluster : clusters) {
        if (cluster.size() == 1) {
          groups.push_back(cluster);
          observed.insert(cluster[0]);
        } else {
          std::vector<Bytes> temp_cluster;
          for (const Bytes& node : sorted_by_count_desc(cluster, counts)) {
            if (!observed.contains(node)) {
              temp_cluster.push_back(node);
              observed.insert(node);
            }
          }
          groups.push_back(std::move(temp_cluster));
        }
      }
      return groups;
    }

    case ClusterMethod::Adjacency: {
      // _group_adjacency. `observed` is reset per cluster.
      for (const auto& cluster : clusters) {
        if (cluster.size() == 1) {
          groups.push_back(cluster);
          continue;
        }
        OrderedSet<Bytes> observed;
        const auto lead_umis = get_best_min_account(cluster, adj_list, counts);
        if (!lead_umis) {
          // Python: observed.update(None) -> TypeError. Reproduced as an error
          // rather than inventing a fallback (deviation ledger, D7#5).
          throw std::logic_error(
              "_group_adjacency: no prefix of the cluster accounts for it; the "
              "Python raises TypeError here (observed.update(None))");
        }
        for (const Bytes& u : *lead_umis) observed.insert(u);

        for (const Bytes& lead_umi : *lead_umis) {
          // set(adj_list[lead_umi]) - observed, then observed |= connected.
          // This `list(set - set)` is the one place upstream's output order is
          // hash-randomised (01_audit.md D4); the port is deterministic in
          // adjacency-list order and the parity check canonicalises.
          std::vector<Bytes> group{lead_umi};
          OrderedSet<Bytes> connected_nodes;
          for (const Bytes& n : adj_list.at(lead_umi)) connected_nodes.insert(n);
          for (const Bytes& n : connected_nodes)
            if (!observed.contains(n)) group.push_back(n);
          for (const Bytes& n : connected_nodes) observed.insert(n);
          groups.push_back(std::move(group));
        }
      }
      return groups;
    }
  }
  throw std::logic_error("UMIClusterer: unhandled cluster method");
}

std::vector<std::vector<Bytes>> UMIClusterer::operator()(const UmiCounts& umis_counts,
                                                          std::int64_t threshold) {
  // Python: counts = umis; umis = list(umis.keys())
  const std::vector<Bytes> umis = umis_counts.keys();

  ++positions_;
  const std::int64_t number_of_umis = static_cast<std::int64_t>(umis.size());
  total_umis_per_position_ += number_of_umis;
  if (number_of_umis > max_umis_per_position_) max_umis_per_position_ = number_of_umis;

  // Python: len_umis = [len(x) for x in umis]; max(len_umis) == min(len_umis).
  // On an empty bundle max([]) raises ValueError before the assert is reached.
  if (umis.empty())
    throw std::invalid_argument(
        "UMIClusterer: empty bundle (the Python raises ValueError from max([]))");

  std::int64_t min_len = py_len(umis[0]);
  std::int64_t max_len = min_len;
  for (const Bytes& u : umis) {
    const std::int64_t l = py_len(u);
    if (l < min_len) min_len = l;
    if (l > max_len) max_len = l;
  }
  if (max_len != min_len) {
    // The Python's assert message, verbatim (two spaces after the colon).
    throw std::logic_error("not all umis are the same length(!):  " +
                           std::to_string(min_len) + " - " + std::to_string(max_len));
  }

  const AdjList adj_list = get_adj_list(umis, umis_counts, threshold);
  const auto clusters = get_connected_components(umis, adj_list, umis_counts);
  return get_groups(clusters, umis, adj_list, umis_counts);
}

// --------------------------------------------------------------------------
// CellClusterer — dead code, observable-behaviour parity only.
// --------------------------------------------------------------------------
CellClusterer::CellClusterer(BytesView cluster_method, std::int64_t dir_threshold,
                             bool fuzzy_match)
    : dir_threshold_(dir_threshold), fuzzy_match_(fuzzy_match) {
  if (cluster_method != "directional") {
    // The Python's own message, verbatim.
    throw std::invalid_argument(
        "CellClusterer currently only supports the directional method");
  }
}

std::vector<std::vector<Bytes>> CellClusterer::operator()(const std::vector<Bytes>&,
                                                          const UmiCounts&) const {
  // Upstream cannot get past this point: __call__ invokes
  // self.get_connected_components(umis, adj_list, counts) with three arguments
  // while _get_connected_components_adjacency(self, graph, counts) accepts two,
  // so it raises TypeError. Two further defects sit behind it (get_groups is
  // never assigned; breadth_first_search's set is .sort()ed). Measured in
  // 01_audit.md D3. There is no semantics to port, so this reproduces the error.
  (void)dir_threshold_;
  (void)fuzzy_match_;
  throw std::logic_error(
      "CellClusterer.__call__ is not callable upstream: the Python raises "
      "TypeError (_get_connected_components_adjacency() takes 3 positional "
      "arguments but 4 were given)");
}

}  // namespace umi_tools
