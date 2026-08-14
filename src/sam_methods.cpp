#include "umi_tools/sam_methods.hpp"

#include <stdexcept>
#include <vector>

namespace umi_tools {
namespace {

// Python str.split(sep) with an explicit separator: NO collapsing of runs, and a
// missing separator yields a single-element list containing the whole string.
// (str.split() with no argument behaves differently — it splits on whitespace
// runs — but every call site here passes an explicit separator.)
std::vector<std::string_view> py_split(std::string_view s, std::string_view sep) {
  std::vector<std::string_view> out;
  if (sep.empty()) throw std::invalid_argument("empty separator");
  std::size_t start = 0;
  while (true) {
    const std::size_t p = s.find(sep, start);
    if (p == std::string_view::npos) {
      out.push_back(s.substr(start));
      break;
    }
    out.push_back(s.substr(start, p - start));
    start = p + sep.size();
  }
  return out;
}

}  // namespace

CellUmi get_umi_read_string(std::string_view read_id, std::string_view sep) {
  const auto parts = py_split(read_id, sep);
  // Python: return (None, read_id.split(sep)[-1].encode('utf-8'))
  return CellUmi{std::nullopt, Bytes(parts.back())};
}

CellUmi get_cell_umi_read_string(std::string_view read_id, std::string_view sep) {
  const auto parts = py_split(read_id, sep);
  if (parts.size() < 2) {
    // The Python catches IndexError and raises ValueError with this message
    // (note the missing spaces in the original's concatenated string literals,
    // reproduced verbatim).
    throw std::invalid_argument(
        std::string("Could not extract UMI or CB from the read ID, please"
                    "check UMI and CB are encoded in the read name:") +
        std::string(read_id));
  }
  return CellUmi{Bytes(parts[parts.size() - 2]), Bytes(parts.back())};
}

void get_gene_count_tab(
    const std::function<bool(std::string&)>& next_line,
    const std::function<CellUmi(std::string_view)>& bc_getter,
    const std::function<void(const std::string&, const PerCellUmiCounts&)>& on_gene) {
  // Python:
  //   gene = None
  //   counts = collections.Counter()          # replaced by a defaultdict below
  //   for line in infile:
  //       values = line.strip().split("\t")
  //       assert len(values) == 2, "line: %s does not contain 2 columns" % line
  //       read_id, assigned_gene = values
  //       if assigned_gene != gene:
  //           if gene: yield gene, counts
  //           gene = assigned_gene
  //           counts = defaultdict(Counter)
  //       cell, umi = bc_getter(read_id)
  //       counts[cell][umi] += 1
  //   yield gene, counts
  //
  // Two faithful details:
  //  * `if gene:` is a TRUTHINESS test, so a gene literally named "" would not
  //    be yielded. Reproduced with an explicit empty check rather than a
  //    has-value check.
  //  * the final yield happens unconditionally, so an empty input yields
  //    (None, {}) — count_tab then writes nothing because the map is empty.
  std::string gene;
  bool have_gene = false;
  PerCellUmiCounts counts;

  std::string line;
  while (next_line(line)) {
    // Python's str.strip() removes whitespace from BOTH ends before splitting,
    // so a trailing \r or spaces do not become part of the gene name.
    std::size_t b = 0, e = line.size();
    while (b < e && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r' ||
                     line[b] == '\n'))
      ++b;
    while (e > b && (line[e - 1] == ' ' || line[e - 1] == '\t' ||
                     line[e - 1] == '\r' || line[e - 1] == '\n'))
      --e;
    const std::string_view stripped(line.data() + b, e - b);

    const auto values = py_split(stripped, "\t");
    if (values.size() != 2)
      // `assert len(values) == 2, "line: %s does not contain 2 columns"
      // % line` interpolates the RAW line, and iterating a text file leaves the
      // TRAILING NEWLINE on it, so upstream's message breaks across two lines:
      //     AssertionError: line: ONECOLUMN
      //      does not contain 2 columns
      // This used `stripped`, giving one line. The reader hands `line` already
      // stripped of its terminator, so the "\n" is put back explicitly.
      //
      // Residual, stated rather than hidden: a FINAL line with no terminator
      // has no newline upstream either, and this adds one. Reproducing that
      // needs the reader to report whether a terminator was present, which it
      // does not; the common case (any line but the last) is exact.
      throw std::logic_error("line: " + std::string(line) + "\n" +
                             " does not contain 2 columns");

    const std::string_view read_id = values[0];
    const std::string_view assigned_gene = values[1];

    if (!have_gene || assigned_gene != gene) {
      if (have_gene && !gene.empty()) on_gene(gene, counts);
      gene.assign(assigned_gene);
      have_gene = true;
      counts.clear();
    }

    const CellUmi cu = bc_getter(read_id);
    // Python's defaultdict(Counter): counts[cell][umi] += 1. The cell key for the
    // umi-only getter is None; a single fixed key stands in for it, because the
    // Python really does use None as a dict key there.
    const Bytes cell_key = cu.cell ? *cu.cell : Bytes();
    counts[cell_key][cu.umi] += 1;
  }

  // The unconditional final yield.
  on_gene(gene, counts);
}

}  // namespace umi_tools
