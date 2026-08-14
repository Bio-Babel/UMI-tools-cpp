// OrderedMap / OrderedSet — Python dict and set semantics for the parts that
// are OBSERVABLE in this package's output.
//
// Why this file exists (01_audit.md D4, 00_baseline.md F1). Measured, not
// assumed:
//
//   * Python dict iterates in INSERTION ORDER, and that order reaches
//     umi_tools' output directly: UMIClusterer.__call__ does
//     `umis = list(umis.keys())`, and for the `unique` and `percentile` methods
//     the returned grouping IS that order. The stale step1_unit_test.py encodes
//     a different (Python-2-era) order, which is how the divergence was found.
//   * std::unordered_map's iteration order is unspecified and was measured
//     REVERSED on this toolchain. Using it for a UMI collection would silently
//     reverse every `unique`/`percentile` result.
//
// So the ordered container is mandatory, not stylistic.
//
// THE INVARIANT: the internal unordered_map is an index for LOOKUP ONLY and is
// never iterated. All iteration goes through items_, which is in insertion
// order. Any change that iterates index_ reintroduces exactly the bug this file
// exists to prevent.
//
// Also deliberate: `at()` throws like Python's KeyError, and `operator[]` is the
// only inserting accessor, mirroring Python's `d[k] = v`. The porting principles
// name `m[k]` silently inserting a default as a measured trap; here reads use
// at() and every operator[] call site is deliberate.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace umi_tools {

template <class K, class V, class Hash = std::hash<K>>
class OrderedMap {
 public:
  using value_type = std::pair<K, V>;
  using container = std::vector<value_type>;
  using iterator = typename container::iterator;
  using const_iterator = typename container::const_iterator;

  /// --- iteration: insertion order, always ---
  iterator begin() { return items_.begin(); }
  iterator end() { return items_.end(); }
  const_iterator begin() const { return items_.begin(); }
  const_iterator end() const { return items_.end(); }

  std::size_t size() const noexcept { return items_.size(); }
  bool empty() const noexcept { return items_.empty(); }

  bool contains(const K& k) const { return index_.find(k) != index_.end(); }

  /// Python d[k] for READING: throws when the key is absent, where Python
  /// raises KeyError.
  ///
  /// The type thrown is std::out_of_range, and the old wording here
  /// ("raises KeyError") was misleading in one specific place: if this ever
  /// crosses a pybind11 boundary, pybind maps std::out_of_range to **IndexError**,
  /// not KeyError. Nothing binds it today — bindings/bindings.cpp exposes a
  /// single zero-argument version_major and no py::register_exception_translator
  /// — so no divergence is reachable, but the comment should not imply the
  /// mapping is already right. Binding the real API would need a translator for
  /// this AND for ExitRequest, which deliberately does not derive from
  /// std::exception (logging.hpp) and so would not be translated at all.
  const V& at(const K& k) const {
    auto it = index_.find(k);
    if (it == index_.end()) throw std::out_of_range("OrderedMap::at: key not found");
    return items_[it->second].second;
  }
  V& at(const K& k) {
    auto it = index_.find(k);
    if (it == index_.end()) throw std::out_of_range("OrderedMap::at: key not found");
    return items_[it->second].second;
  }

  // Python d[k] = v / collections.defaultdict: inserts a default when absent.
  // Only for sites where the Python assigns or uses a defaultdict.
  V& operator[](const K& k) {
    auto it = index_.find(k);
    if (it != index_.end()) return items_[it->second].second;
    index_.emplace(k, items_.size());
    items_.emplace_back(k, V{});
    return items_.back().second;
  }

  /// Python dict.get(k, default) — no insertion.
  V get(const K& k, V fallback) const {
    auto it = index_.find(k);
    return it == index_.end() ? fallback : items_[it->second].second;
  }

  /// Python list(d.keys()) — insertion order.
  std::vector<K> keys() const {
    std::vector<K> out;
    out.reserve(items_.size());
    for (const auto& kv : items_) out.push_back(kv.first);
    return out;
  }

  std::vector<V> values() const {
    std::vector<V> out;
    out.reserve(items_.size());
    for (const auto& kv : items_) out.push_back(kv.second);
    return out;
  }

  // Python del d[k]. Erasing from the middle would invalidate every stored
  // index, so it is not offered: no umi_tools code path deletes a single UMI
  // key (get_bundles deletes whole position buckets, which is a different
  // container). Left unimplemented on purpose rather than implemented wrongly.

  void clear() {
    items_.clear();
    index_.clear();
  }
  void reserve(std::size_t n) {
    items_.reserve(n);
    index_.reserve(n);
  }

 private:
  container items_;                              // ITERATED — insertion order
  std::unordered_map<K, std::size_t, Hash> index_;  // LOOKUP ONLY — never iterated
};

/// Insertion-ordered set: Python `set` used for membership, made deterministic.
///
/// Note what this does NOT claim: Python's own set iteration order is hash-based
/// and, for bytes, randomised per process (measured across PYTHONHASHSEED in
/// 01_audit.md D4). Where that order leaks into umi_tools' output — only
/// _group_adjacency's `list(connected_nodes - observed)` — the oracle is itself
/// unstable, so the port is deterministic and the parity comparison
/// canonicalises. This container is the port's deterministic answer; it is not an
/// emulation of CPython's set order, because there is no stable order to emulate.
template <class K, class Hash = std::hash<K>>
class OrderedSet {
 public:
  using container = std::vector<K>;
  using const_iterator = typename container::const_iterator;

  const_iterator begin() const { return items_.begin(); }
  const_iterator end() const { return items_.end(); }
  std::size_t size() const noexcept { return items_.size(); }
  bool empty() const noexcept { return items_.empty(); }

  bool contains(const K& k) const { return index_.find(k) != index_.end(); }

  /// Python set.add — returns true if newly inserted.
  bool insert(const K& k) {
    if (index_.find(k) != index_.end()) return false;
    index_.emplace(k, items_.size());
    items_.push_back(k);
    return true;
  }

  /// Python set.update(iterable)
  template <class It>
  void update(It first, It last) {
    for (; first != last; ++first) insert(*first);
  }

  const std::vector<K>& as_vector() const { return items_; }

  void clear() {
    items_.clear();
    index_.clear();
  }
  void reserve(std::size_t n) {
    items_.reserve(n);
    index_.reserve(n);
  }

 private:
  container items_;                              // ITERATED
  std::unordered_map<K, std::size_t, Hash> index_;  // LOOKUP ONLY
};

}  // namespace umi_tools
