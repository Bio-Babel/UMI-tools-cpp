#include "umi_tools/sam_methods.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>

#include "umi_tools/logging.hpp"

#include <set>

#include "umi_tools/io.hpp"
#include "umi_tools/py_random.hpp"

namespace umi_tools {
namespace {

std::vector<std::string_view> split_view(std::string_view s, std::string_view sep) {
  // `--umi-separator=` reaches here as "": find("", start) returns start, so the
  // loop never advanced and the vector grew until the process died. Python
  // raises ValueError: empty separator. The sibling py_split already threw for
  // this; only this copy was unguarded.
  if (sep.empty()) throw std::invalid_argument("empty separator");
  std::vector<std::string_view> out;
  std::size_t start = 0;
  while (true) {
    const std::size_t p = s.find(sep, start);
    if (p == std::string_view::npos) { out.push_back(s.substr(start)); break; }
    out.push_back(s.substr(start, p - start));
    start = p + sep.size();
  }
  return out;
}

}  // namespace

CellUmi get_barcode_umis(const BamRecord& read, bool cell_barcode) {
  // Python: for element in read.qname.split(":"):
  //             if element.startswith("UMI_"):  umi  = element[4:]
  //             elif element.startswith("CELL_") and cell_barcode: cell = element[5:]
  //         if umi is None: raise ValueError(...)
  // Note the LAST matching element wins — the loop does not break.
  std::optional<Bytes> umi, cell;
  for (std::string_view e : split_view(read.query_name(), ":")) {
    if (e.rfind("UMI_", 0) == 0) umi = Bytes(e.substr(4));
    else if (cell_barcode && e.rfind("CELL_", 0) == 0) cell = Bytes(e.substr(5));
  }
  if (!umi)
    throw std::invalid_argument("Could not extract UMI +/- cell barcode from the read tag");
  return CellUmi{cell, *umi};
}

CellUmi get_barcode_read_id(const BamRecord& read, bool cell_barcode,
                            std::string_view sep) {
  // Upstream wraps the WHOLE body in `try: ... except: raise ValueError(...)` —
  // a BARE except, so every failure inside, including the empty-separator
  // ValueError from str.split, is replaced by this one message. Measured:
  // `--umi-separator=` reports the read-ID message, not "empty separator".
  std::vector<std::string_view> parts;
  try {
    parts = split_view(read.query_name(), sep);
  } catch (const std::exception&) {
    throw std::invalid_argument(
        "Could not extract UMI +/- cell barcode from the read"
        "ID, please check UMI is encoded in the read name");
  }
  if (cell_barcode) {
    if (parts.size() < 2)
      throw std::invalid_argument(
          "Could not extract UMI +/- cell barcode from the read"
          "ID, please check UMI is encoded in the read name");
    return CellUmi{Bytes(parts[parts.size() - 2]), Bytes(parts.back())};
  }
  return CellUmi{std::nullopt, Bytes(parts.back())};
}

std::optional<CellUmi> get_barcode_tag(const BamRecord& read, bool cell_barcode,
                                       const TagBarcodeOptions& o) {
  auto umi_opt = read.get_tag_str(o.umi_tag.c_str());
  if (!umi_opt) return std::nullopt;          // pysam KeyError -> caller skips
  std::string umi = *umi_opt;
  std::optional<std::string> cell;
  if (cell_barcode) {
    if (o.cell_tag.empty()) return std::nullopt;
    auto c = read.get_tag_str(o.cell_tag.c_str());
    if (!c) return std::nullopt;
    cell = *c;
  }
  if (!o.umi_tag_split.empty()) umi = std::string(split_view(umi, o.umi_tag_split)[0]);
  if (!o.umi_tag_delim.empty()) {
    std::string joined;
    for (auto part : split_view(umi, o.umi_tag_delim)) joined += std::string(part);
    umi = joined;
  }
  // 10X appends a GEM tag to the CELL barcode, e.g. GATAGATACCTAGATA-1, hence
  // the default cell_tag_split of "-".
  if (cell && !o.cell_tag_split.empty())
    cell = std::string(split_view(*cell, o.cell_tag_split)[0]);
  if (cell && !o.cell_tag_delim.empty()) {
    std::string joined;
    for (auto part : split_view(*cell, o.cell_tag_delim)) joined += std::string(part);
    cell = joined;
  }
  return CellUmi{cell ? std::optional<Bytes>(Bytes(*cell)) : std::nullopt, Bytes(umi)};
}

// --------------------------------------------------------------------------
// NOTE: this TU is compiled with -Wno-maybe-uninitialized; see the comment on
// sam_methods_bam.cpp in CMakeLists.txt for why, and why the suppression could
// not be scoped any tighter than the file.
SkipRegex::SkipRegex(std::string_view pattern) {
  // validateSamOptions accepts whatever re.compile accepts (Utilities.py:1296-1301)
  // and turns a failure into "skip-regex '%s' is not a valid regex". The throw
  // here is converted to that same message by validate_sam_options.
  try {
    re_ = std::regex(std::string(pattern), std::regex::ECMAScript);
  } catch (const std::regex_error&) {
    throw std::invalid_argument("SkipRegex: '" + std::string(pattern) +
                                "' is not a valid regex");
  }
}

bool SkipRegex::search(std::string_view text) const {
  // re.search: unanchored, anywhere in the string.
  return std::regex_search(std::string(text), re_);
}

// --------------------------------------------------------------------------
MetaContigMap get_meta_contig_to_contig(AlignmentReader& reader,
                                        const std::string& gene_transcript_map) {
  std::set<std::string> references;
  for (std::int32_t i = 0; i < reader.n_targets(); ++i)
    references.insert(std::string(reader.target_name(i)));

  MetaContigMap out;
  LineReader in(gene_transcript_map);
  std::string line;
  while (in.next(line)) {
    if (!line.empty() && line[0] == '#') continue;
    // `if len(line.strip()) == 0: break` — a blank line ENDS the file, it does
    // not skip a record. Reproduced.
    std::string stripped = line;
    const std::size_t b = stripped.find_first_not_of(" \t\r\n");
    const std::size_t e = stripped.find_last_not_of(" \t\r\n");
    stripped = (b == std::string::npos) ? std::string() : stripped.substr(b, e - b + 1);
    if (stripped.empty()) break;

    // `gene, transcript = line.strip().split("\t")` is a 2-TUPLE UNPACK, so a
    // line with three or more fields raises
    //   ValueError: too many values to unpack (expected 2)
    // and aborts the run. Measured: this took everything after the
    // FIRST tab as the transcript, so a 3-column line yielded "chr19\tEXTRA",
    // which failed the references test and was silently dropped — a malformed
    // map produced a quietly SMALLER metacontig map (fewer fetched contigs,
    // smaller output) where upstream exits 1.
    // `split("\t")` yields exactly 2 fields iff the line holds exactly one tab
    // (an empty field still counts, so counting tabs is equivalent, not an
    // approximation).
    const std::size_t n_tabs =
        static_cast<std::size_t>(std::count(stripped.begin(), stripped.end(), '\t'));
    if (n_tabs != 1)
      throw std::invalid_argument(
          n_tabs < 1 ? "not enough values to unpack (expected 2, got 1)"
                     : "too many values to unpack (expected 2)");
    const std::size_t tab = stripped.find('\t');
    const std::string gene = stripped.substr(0, tab);
    const std::string transcript = stripped.substr(tab + 1);
    if (references.count(transcript)) out[gene].insert(transcript);
  }
  return out;
}

OrderedMap<std::string, std::int64_t> detect_bam_features(const std::string& path,
                                                          std::int64_t n_entries) {
  OrderedMap<std::string, std::int64_t> available;
  const char* tags[] = {"NH", "X0", "XT"};
  for (const char* t : tags) available[t] = 1;

  AlignmentReader reader(path);
  BamRecord read;
  std::int64_t n = 0;
  while (reader.next(read)) {
    if (n > n_entries) break;          // `if n > n_entries: break` — n_entries+1 records
    ++n;
    if (read.is_unmapped()) continue;
    for (const char* t : tags)
      if (!read.get_tag_str(t).has_value()) available[t] = 0;
  }
  return available;
}

// The per-gene entry point `count` uses. Now an ADAPTER over for_each_bundle
// rather than a second implementation: the read triage (read2, unmapped, paired,
// chimeric, subset, MAPQ, barcode) is identical between the branches upstream,
// and keeping two copies of it here is how the two would quietly diverge.
void for_each_bundle_per_gene(
    AlignmentReader& reader, const BundleOptions& options, bool only_count_reads,
    BundleReadEvents& events,
    const std::function<void(const Bundle&, const BundleKey&)>& on_bundle,
    std::optional<double> subset) {
  PyRandom& rng = global_random();
  for_each_bundle(
      reader, options, only_count_reads, /*all_reads=*/false, /*return_read2=*/false,
      /*return_unmapped=*/false, rng, subset, events,
      [&on_bundle](const Bundle& bundle, const BundlePos& pos, const PositionalKey& key) {
        BundleKey bk;
        bk.pos = pos.gene;
        bk.cell = key.cell;
        on_bundle(bundle, bk);
      },
      [](BamRecord&) {});
}

}  // namespace umi_tools
