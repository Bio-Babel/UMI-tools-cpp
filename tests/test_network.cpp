// Slice 0 + Slice 1 cases.
//
// Expectations are the LIVE ORACLE's values, captured by running the current
// Python source (00_baseline.md F1, 04_native_code.md). Where the shipped
// step1_unit_test.py disagrees with the live oracle it is stale and is NOT used
// as an expectation — that file cannot even run against the current API.
#include "umi_tools/bytes.hpp"
#include "umi_tools/edit_distance.hpp"
#include "umi_tools/network.hpp"
#include "umi_tools/ordered_map.hpp"
#include "test_harness.hpp"

#include <stdexcept>

using namespace umi_tools;
using umi_tools_test::repr;

namespace {

// The input from step1_unit_test.py, as BYTES (the type the oracle requires).
// Insertion order matters and is part of every expectation below.
UmiCounts fixture_counts() {
  UmiCounts c;
  c[Bytes("ACGT")] = 456;
  c[Bytes("AAAT")] = 90;
  c[Bytes("ACAT")] = 72;
  c[Bytes("TCGT")] = 2;
  c[Bytes("CCGT")] = 2;
  c[Bytes("ACAG")] = 1;
  return c;
}

std::vector<std::vector<std::string>> group_with(ClusterMethod m) {
  UMIClusterer clusterer(m);
  return clusterer(fixture_counts(), 1);
}

// Canonical form for the one method whose order the ORACLE does not fix.
std::vector<std::vector<std::string>> canonicalise(
    std::vector<std::vector<std::string>> groups) {
  for (auto& g : groups) std::sort(g.begin(), g.end());
  std::sort(groups.begin(), groups.end());
  return groups;
}

}  // namespace

// ---------------------------------------------------------------------------
// edit_distance — the six rows measured against the compiled oracle kernel
// ---------------------------------------------------------------------------
UMI_TEST_CASE(edit_distance_equal) { CHECK_EQ(edit_distance("ACGT", "ACGT"), 0); }
UMI_TEST_CASE(edit_distance_one_sub) { CHECK_EQ(edit_distance("ACGT", "ACGA"), 1); }
UMI_TEST_CASE(edit_distance_three_subs) { CHECK_EQ(edit_distance("ACGT", "TTTT"), 3); }
UMI_TEST_CASE(edit_distance_empty_pair) { CHECK_EQ(edit_distance("", ""), 0); }

// The oracle RAISES here (AttributeError: np.Inf removed in numpy 2.0). It does
// not return a large sentinel, and porting the .pyx text would have made it do
// exactly that. See 04_native_code.md.
UMI_TEST_CASE(edit_distance_unequal_length_throws) {
  CHECK_THROWS_AS(edit_distance("ACGT", "ACG"), std::invalid_argument);
  CHECK_THROWS_AS(edit_distance("ACG", "ACGT"), std::invalid_argument);
  CHECK_THROWS_AS(edit_distance("", "A"), std::invalid_argument);
}

// Bytes are compared unsigned, as Python compares bytes.
UMI_TEST_CASE(edit_distance_high_bytes) {
  const std::string a("\x80\x01", 2);
  const std::string b("\x01\x01", 2);
  CHECK_EQ(edit_distance(a, b), 1);
}

// ---------------------------------------------------------------------------
// bytes helpers — Python indexing/slicing semantics
// ---------------------------------------------------------------------------
UMI_TEST_CASE(bytes_negative_index) {
  CHECK_EQ(py_at("ACGT", -1), static_cast<unsigned char>('T'));
  CHECK_EQ(py_at("ACGT", 0), static_cast<unsigned char>('A'));
  CHECK_THROWS_AS(py_at("ACGT", 4), std::out_of_range);
  CHECK_THROWS_AS(py_at("ACGT", -5), std::out_of_range);
}

UMI_TEST_CASE(bytes_slice_python_semantics) {
  CHECK_EQ(py_slice("ACGTACGT", 0, 3), std::string("ACG"));
  CHECK_EQ(py_slice("ACGTACGT", -3, 8), std::string("CGT"));
  // Python clamps rather than throwing, and start >= stop yields empty.
  CHECK_EQ(py_slice("ACGT", 0, 99), std::string("ACGT"));
  CHECK_EQ(py_slice("ACGT", 3, 1), std::string(""));
  CHECK_EQ(py_slice("ACGT", -99, 2), std::string("AC"));
}

// ---------------------------------------------------------------------------
// OrderedMap — the container the whole port's ordering contract rests on
// ---------------------------------------------------------------------------
UMI_TEST_CASE(ordered_map_iterates_in_insertion_order) {
  UmiCounts c = fixture_counts();
  const std::vector<std::string> expected{"ACGT", "AAAT", "ACAT", "TCGT", "CCGT", "ACAG"};
  CHECK_EQ(repr(c.keys()), repr(expected));
}

UMI_TEST_CASE(ordered_map_at_throws_like_keyerror) {
  UmiCounts c = fixture_counts();
  CHECK_THROWS_AS(c.at(Bytes("NOPE")), std::out_of_range);
  CHECK_EQ(c.at(Bytes("ACGT")), 456);
  // Reading via at() must NOT insert, unlike std::map::operator[].
  CHECK_EQ(static_cast<int>(c.size()), 6);
}

UMI_TEST_CASE(ordered_map_reinsertion_keeps_first_position) {
  UmiCounts c;
  c[Bytes("B")] = 1;
  c[Bytes("A")] = 2;
  c[Bytes("B")] = 3;  // Python: updating a key keeps its original position
  const std::vector<std::string> expected{"B", "A"};
  CHECK_EQ(repr(c.keys()), repr(expected));
  CHECK_EQ(c.at(Bytes("B")), 3);
}

// ---------------------------------------------------------------------------
// get_substr_slices / np_median
// ---------------------------------------------------------------------------
UMI_TEST_CASE(get_substr_slices_even_split) {
  const auto s = get_substr_slices(8, 2);
  CHECK_EQ(static_cast<int>(s.size()), 2);
  CHECK_EQ(s[0].first, 0);  CHECK_EQ(s[0].second, 4);
  CHECK_EQ(s[1].first, 4);  CHECK_EQ(s[1].second, 8);
}

UMI_TEST_CASE(get_substr_slices_uneven_split_front_loaded) {
  // divmod(10, 3) = (3, 1) -> sizes [4, 3, 3]: the remainder goes to the FRONT.
  const auto s = get_substr_slices(10, 3);
  CHECK_EQ(static_cast<int>(s.size()), 3);
  CHECK_EQ(s[0].second - s[0].first, 4);
  CHECK_EQ(s[1].second - s[1].first, 3);
  CHECK_EQ(s[2].second - s[2].first, 3);
  CHECK_EQ(s[2].second, 10);
}

UMI_TEST_CASE(np_median_even_is_mean_of_middle_two) {
  // The bundle's counts: sorted [1,2,2,72,90,456] -> (2+72)/2 = 37.0, not 2.
  CHECK_EQ(np_median({456, 90, 72, 2, 2, 1}), 37.0);
}

UMI_TEST_CASE(np_median_odd) { CHECK_EQ(np_median({3, 1, 2}), 2.0); }

// ---------------------------------------------------------------------------
// UMIClusterer — the five methods, against the LIVE oracle
// ---------------------------------------------------------------------------
UMI_TEST_CASE(clusterer_unique_preserves_insertion_order) {
  // Live oracle: [['ACGT'],['AAAT'],['ACAT'],['TCGT'],['CCGT'],['ACAG']]
  // The stale step1_unit_test.py claims [['ACAG'],['ACGT'],...] — a Python-2-era
  // order. Porting to that file would have baked in the wrong order.
  const std::vector<std::vector<std::string>> expected{
      {"ACGT"}, {"AAAT"}, {"ACAT"}, {"TCGT"}, {"CCGT"}, {"ACAG"}};
  CHECK_EQ(repr(group_with(ClusterMethod::Unique)), repr(expected));
}

UMI_TEST_CASE(clusterer_percentile_preserves_insertion_order) {
  // threshold = np_median(all counts)/100 = 37.0/100 = 0.37; every count > 0.37,
  // so all six are retained, in insertion order.
  const std::vector<std::vector<std::string>> expected{
      {"ACGT"}, {"AAAT"}, {"ACAT"}, {"TCGT"}, {"CCGT"}, {"ACAG"}};
  CHECK_EQ(repr(group_with(ClusterMethod::Percentile)), repr(expected));
}

UMI_TEST_CASE(clusterer_percentile_drops_below_threshold) {
  // A bundle where the strict `>` boundary is observable: counts
  // [100, 100, 100, 1] -> median 100 -> threshold 1.0, and the count of exactly
  // 1 is NOT retained because the comparison is `>`, not `>=`.
  UmiCounts c;
  c[Bytes("AAAA")] = 100;
  c[Bytes("CCCC")] = 100;
  c[Bytes("GGGG")] = 100;
  c[Bytes("TTTT")] = 1;
  UMIClusterer clusterer(ClusterMethod::Percentile);
  const std::vector<std::vector<std::string>> expected{{"AAAA"}, {"CCCC"}, {"GGGG"}};
  CHECK_EQ(repr(clusterer(c, 1)), repr(expected));
}

UMI_TEST_CASE(clusterer_cluster_single_component_count_desc) {
  // Live oracle: [['ACGT','AAAT','ACAT','CCGT','TCGT','ACAG']]
  // CCGT precedes TCGT because the component is sorted lexicographically FIRST
  // and the count sort is stable; the two both have count 2.
  const std::vector<std::vector<std::string>> expected{
      {"ACGT", "AAAT", "ACAT", "CCGT", "TCGT", "ACAG"}};
  CHECK_EQ(repr(group_with(ClusterMethod::Cluster)), repr(expected));
}

UMI_TEST_CASE(clusterer_directional_matches_live_oracle) {
  // Live oracle: [['ACGT','ACAT','CCGT','TCGT','ACAG'], ['AAAT']]
  const std::vector<std::vector<std::string>> expected{
      {"ACGT", "ACAT", "CCGT", "TCGT", "ACAG"}, {"AAAT"}};
  CHECK_EQ(repr(group_with(ClusterMethod::Directional)), repr(expected));
}

UMI_TEST_CASE(clusterer_adjacency_partition_matches_live_oracle) {
  // The PARTITION is fixed; the within-group order is hash-randomised in the
  // oracle (01_audit.md D4), so this case compares the canonical form. The
  // order-exact form is asserted separately below.
  const auto got = canonicalise(group_with(ClusterMethod::Adjacency));
  const auto expected = canonicalise({{"ACGT", "TCGT", "CCGT"}, {"AAAT"}, {"ACAT", "ACAG"}});
  CHECK_EQ(repr(got), repr(expected));
}

UMI_TEST_CASE(clusterer_adjacency_order_is_deterministic) {
  // The port is deterministic where the oracle is not. Its order happens to
  // equal the oracle's at PYTHONHASHSEED=0 and 1 (measured); at seed 42 the
  // oracle swaps TCGT/CCGT. Asserting the port's own order here is what makes
  // the determinism a tested property rather than an accident.
  const std::vector<std::vector<std::string>> expected{
      {"ACGT", "TCGT", "CCGT"}, {"AAAT"}, {"ACAT", "ACAG"}};
  CHECK_EQ(repr(group_with(ClusterMethod::Adjacency)), repr(expected));
}

UMI_TEST_CASE(clusterer_directional_boundary_is_ge_2n_minus_1) {
  // The directional edge test is `counts[a] >= (counts[b]*2)-1`. With counts
  // (1,1): 1 >= 1 holds both ways, so the two UMIs merge. With (1,2):
  // 1 >= 3 fails and 2 >= 1 holds, so only one direction exists — but the
  // component is still connected, so they still merge. The case that separates
  // them needs an edit distance above the threshold.
  UmiCounts c;
  c[Bytes("AAAA")] = 1;
  c[Bytes("AAAC")] = 1;
  UMIClusterer clusterer(ClusterMethod::Directional);
  const std::vector<std::vector<std::string>> expected{{"AAAA", "AAAC"}};
  CHECK_EQ(repr(clusterer(c, 1)), repr(expected));
}

UMI_TEST_CASE(clusterer_threshold_zero_isolates) {
  UmiCounts c;
  c[Bytes("AAAA")] = 5;
  c[Bytes("AAAC")] = 5;
  UMIClusterer clusterer(ClusterMethod::Directional);
  const std::vector<std::vector<std::string>> expected{{"AAAA"}, {"AAAC"}};
  CHECK_EQ(repr(clusterer(c, 0)), repr(expected));
}

UMI_TEST_CASE(clusterer_stats_counters) {
  UMIClusterer clusterer(ClusterMethod::Directional);
  clusterer(fixture_counts(), 1);
  UmiCounts small;
  small[Bytes("AAAA")] = 1;
  clusterer(small, 1);
  CHECK_EQ(clusterer.positions(), 2);
  CHECK_EQ(clusterer.total_umis_per_position(), 7);
  CHECK_EQ(clusterer.max_umis_per_position(), 6);
}

UMI_TEST_CASE(clusterer_unequal_lengths_throws_with_python_message) {
  UmiCounts c;
  c[Bytes("ACGT")] = 5;
  c[Bytes("ACG")] = 1;
  UMIClusterer clusterer(ClusterMethod::Directional);
  bool caught = false;
  try {
    clusterer(c, 1);
  } catch (const std::logic_error& e) {
    caught = true;
    // The Python's assert message, verbatim (note the two spaces after ':').
    CHECK_EQ(std::string(e.what()),
             std::string("not all umis are the same length(!):  3 - 4"));
  }
  CHECK(caught);
}

UMI_TEST_CASE(clusterer_empty_bundle_throws) {
  UmiCounts c;
  UMIClusterer clusterer(ClusterMethod::Directional);
  CHECK_THROWS_AS(clusterer(c, 1), std::invalid_argument);
}

// The >25 UMI path switches to the substring index. The edge set must be
// identical to all-pairs, so both paths must produce the same grouping for an
// input that crosses the boundary.
UMI_TEST_CASE(clusterer_substring_index_path_agrees_with_all_pairs) {
  // 30 distinct 8-mers, so len(umis) > 25 and the substring index is used.
  UmiCounts c;
  const char bases[] = "ACGT";
  int made = 0;
  for (int i = 0; i < 4 && made < 30; ++i)
    for (int j = 0; j < 4 && made < 30; ++j)
      for (int k = 0; k < 4 && made < 30; ++k) {
        Bytes u = "AAAAA";
        u += bases[i];
        u += bases[j];
        u += bases[k];
        c[u] = 100 - made;
        ++made;
      }
  CHECK_EQ(static_cast<int>(c.size()), 30);

  UMIClusterer clusterer(ClusterMethod::Cluster);
  const auto via_index = clusterer(c, 1);

  // Recompute the same grouping through the all-pairs path by asking for the
  // adjacency list on a 25-UMI prefix would change the input; instead verify the
  // pigeonhole property directly: every pair at distance <= 1 must be an edge.
  const auto adj = clusterer.get_adj_list(c.keys(), c, 1);
  const auto umis = c.keys();
  for (std::size_t i = 0; i < umis.size(); ++i)
    for (std::size_t j = i + 1; j < umis.size(); ++j)
      if (edit_distance(umis[i], umis[j]) <= 1) {
        bool found = false;
        for (const auto& n : adj.at(umis[i])) if (n == umis[j]) found = true;
        CHECK(found);
      }
  CHECK(!via_index.empty());
}

// ---------------------------------------------------------------------------
// graph helpers
// ---------------------------------------------------------------------------
UMI_TEST_CASE(breadth_first_search_finds_whole_component) {
  AdjList adj;
  adj[Bytes("A")] = {Bytes("B")};
  adj[Bytes("B")] = {Bytes("A"), Bytes("C")};
  adj[Bytes("C")] = {Bytes("B")};
  adj[Bytes("D")] = {};
  CHECK_EQ(static_cast<int>(breadth_first_search(Bytes("A"), adj).size()), 3);
  CHECK_EQ(static_cast<int>(breadth_first_search(Bytes("D"), adj).size()), 1);
}

UMI_TEST_CASE(remove_umis_subtracts_node_and_neighbours) {
  AdjList adj;
  adj[Bytes("A")] = {Bytes("B")};
  adj[Bytes("B")] = {Bytes("A")};
  adj[Bytes("C")] = {};
  const std::vector<Bytes> cluster{Bytes("A"), Bytes("B"), Bytes("C")};
  CHECK_EQ(static_cast<int>(remove_umis(adj, cluster, {Bytes("A")}).size()), 1);  // C
}

UMI_TEST_CASE(breadth_first_search_recursive_matches_iterative) {
  AdjList adj;
  adj[Bytes("A")] = {Bytes("B")};
  adj[Bytes("B")] = {Bytes("A"), Bytes("C")};
  adj[Bytes("C")] = {Bytes("B")};
  CHECK_EQ(static_cast<int>(breadth_first_search_recursive(Bytes("A"), adj).size()),
           static_cast<int>(breadth_first_search(Bytes("A"), adj).size()));
}

// ---------------------------------------------------------------------------
// CellClusterer — dead upstream code, observable-behaviour parity
// ---------------------------------------------------------------------------
UMI_TEST_CASE(cell_clusterer_rejects_non_directional) {
  bool caught = false;
  try {
    CellClusterer("adjacency");
  } catch (const std::invalid_argument& e) {
    caught = true;
    CHECK_EQ(std::string(e.what()),
             std::string("CellClusterer currently only supports the directional method"));
  }
  CHECK(caught);
}

UMI_TEST_CASE(cell_clusterer_call_throws_like_upstream) {
  CellClusterer cc("directional");
  UmiCounts c;
  c[Bytes("ACGTACGT")] = 100;
  CHECK_THROWS_AS(cc({Bytes("ACGTACGT")}, c), std::logic_error);
}

UMI_TEST_CASE(parse_cluster_method_roundtrip) {
  for (const char* n : {"unique", "percentile", "cluster", "adjacency", "directional"}) {
    const auto m = parse_cluster_method(n);
    CHECK(m.has_value());
    CHECK_EQ(std::string(cluster_method_name(*m)), std::string(n));
  }
  CHECK(!parse_cluster_method("nonesuch").has_value());
}

int main(int argc, char** argv) { return umi_tools_test::main_impl(argc, argv); }
