// sam_methods.hpp — the non-BAM parts of umi_tools/sam_methods.py.
//
// The BAM/htslib-dependent members of this module (get_bundles,
// TwoPassPairWriter, get_read_position, find_splice, metafetcher,
// getMetaContig2contig, and the read-based barcode getters) land in slice 5 with
// the alignment layer. What is here is what `count_tab` needs and what has no
// BAM dependency: the read-ID barcode getters and the flat-file gene counter.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <regex>
#include <string>
#include <string_view>

#include "umi_tools/alignment.hpp"
#include "umi_tools/bytes.hpp"
#include "umi_tools/ordered_map.hpp"

namespace umi_tools {

/// (cell, umi). `cell` is absent for the umi-only getter, mirroring Python's
/// `(None, umi)`.
struct CellUmi {
  std::optional<Bytes> cell;
  Bytes umi;
};

/// sam_methods.get_umi_read_string(read_id, sep="_") -> (None, id.split(sep)[-1])
/// Python's str.split returns the whole string when the separator is absent, so a
/// read id with no separator yields itself as the UMI. That is upstream's
/// behaviour, not a degenerate case to guard.
CellUmi get_umi_read_string(std::string_view read_id, std::string_view sep = "_");

/// sam_methods.get_cell_umi_read_string(read_id, sep="_")
///   -> (id.split(sep)[-2], id.split(sep)[-1])
/// With fewer than two fields Python raises IndexError, which the function
/// converts to ValueError with a message naming the read id. Reproduced as
/// std::invalid_argument carrying that message.
CellUmi get_cell_umi_read_string(std::string_view read_id, std::string_view sep = "_");

/// Per-gene counts: cell -> umi -> count. Both levels are insertion-ordered,
/// and that is OBSERVABLE: count_tab writes one row per cell in `counts.keys()`
/// order, so the reference output's cell order is the order of first appearance in
/// the input, not alphabetical. (Measured: for gene ENSG00000011304.18 the golden
/// lists GATCGATTCGAGGATA before ATAGATAGCGATAGCG, which is input order and the
/// reverse of alphabetical.)
using PerCellUmiCounts = OrderedMap<Bytes, OrderedMap<Bytes, std::int64_t>>;

/// sam_methods.get_gene_count_tab(infile, bc_getter) — a GENERATOR over genes.
/// Ported as a callback so it stays streaming: the input is a per-read flat file
/// that can be far larger than RAM, and collecting all genes into a vector would
/// pass every fixture and fail on real data.
///
/// The input must be sorted by gene; the Python yields whenever the gene column
/// changes and once more at EOF. An assert fires when a line does not have exactly
/// two tab-separated fields.
void get_gene_count_tab(
    const std::function<bool(std::string&)>& next_line,
    const std::function<CellUmi(std::string_view)>& bc_getter,
    const std::function<void(const std::string& gene, const PerCellUmiCounts&)>& on_gene);


// ---------------------------------------------------------------------------
// The BAM half of sam_methods.
// ---------------------------------------------------------------------------

/// How the UMI is extracted from a read (functools.partial over 3 functions
/// upstream; a closed set, so an enum + switch — 06_design.md).
enum class UmiMethod { ReadId, Tag, Umis };

/// sam_methods.get_barcode_umis(read, cell_barcode): parses the read NAME split
/// by ':', taking the element after "UMI_" and (optionally) after "CELL_".
/// Raises ValueError when no UMI element is present.
CellUmi get_barcode_umis(const BamRecord& read, bool cell_barcode);

/// sam_methods.get_barcode_read_id(read, cell_barcode, sep)
CellUmi get_barcode_read_id(const BamRecord& read, bool cell_barcode,
                            std::string_view sep = "_");

/// sam_methods.get_barcode_tag(...). Returns nullopt where pysam raises KeyError
/// (a missing tag), because get_bundles catches that and skips the read.
struct TagBarcodeOptions {
  std::string umi_tag = "RX";
  std::string cell_tag;
  std::string umi_tag_split;
  std::string umi_tag_delim;
  std::string cell_tag_split = "-";
  std::string cell_tag_delim;
};
std::optional<CellUmi> get_barcode_tag(const BamRecord& read, bool cell_barcode,
                                       const TagBarcodeOptions& o);

/// The `--skip-tags-regex` matcher: a MINIMAL stdlib-`re` subset, not the
/// `regex`-module sublanguage of pattern.hpp. Two forms appear in the corpus and
/// they are NOT equivalent:
///     ^(__|Unassigned)   the option DEFAULT — an alternation of two literals
///     ^[__|Unassigned]   what every count/group fixture passes — a CHARACTER
///                        CLASS, so it matches any read whose tag starts with one
///                        of { _ | U n a s i g e d }, e.g. "ambiguous".
/// Anything outside those two forms throws at construction rather than being
/// guessed at.
/// `re.search(options.skip_regex, assigned)` — an ARBITRARY user-supplied regex.
///
/// This used to accept only the two shapes the corpus contains, `^(__|Unassigned)`
/// and `^[__|Unassigned]`, and throw for anything else. That was principle 9
/// applied one step too far: the option is documented as "Ignore reads where the
/// gene-tag matches this regex", so its value is free text a user brings, and the
/// enumerated corpus is a FLOOR rather than a ceiling. MEASURED:
/// `--skip-tags-regex=__no_feature` — a plain literal, the most obvious thing to
/// type — exited 1 here and 0 upstream, as did `^__` and `__.*`.
///
/// Backed by `std::regex` with the ECMAScript grammar and `regex_search`, which
/// is `re.search`'s semantics. ECMAScript and Python `re` agree on the constructs
/// a gene-tag filter uses (literals, `^`/`$`, `[...]`, `(a|b)`, `.`, `*`, `+`,
/// `?`, `\d`, `\w`); they diverge on things like named groups and POSIX classes,
/// which is recorded rather than claimed away — see the parity row.
class SkipRegex {
 public:
  explicit SkipRegex(std::string_view pattern);
  bool search(std::string_view text) const;  ///< re.search semantics

 private:
  std::regex re_;
};

/// get_bundles' options, carrying the ORIGINAL Python dest names.
/// sam_methods.getMetaContig2contig(bamfile, gene_transcript_map).
///
/// gene -> the transcripts of that gene that are ALSO references in the BAM.
/// Upstream's value is a Python `set`; OrderedSet keeps set semantics for
/// equality (which check_output relies on) while giving the port a DETERMINISTIC
/// iteration order, which upstream does not have — see L33.
///
/// Two parsing quirks are upstream's, not tidied: a '#' line is skipped, but a
/// BLANK line STOPS parsing entirely (`break`, not `continue`).
using MetaContigMap = OrderedMap<std::string, OrderedSet<std::string>>;
MetaContigMap get_meta_contig_to_contig(AlignmentReader& reader,
                                        const std::string& gene_transcript_map);

struct BundleOptions {
  bool per_gene = false;
  bool per_contig = false;
  bool per_cell = false;
  bool whole_contig = false;
  bool ignore_umi = false;
  bool paired = false;
  bool spliced = false;
  bool read_length = false;
  bool ignore_tlen = false;
  std::string gene_tag;
  std::string assigned_tag;
  std::string skip_regex = "^(__|Unassigned)";
  std::int64_t mapping_quality = 0;
  // optparse declares --soft-clip-threshold type="float"
  // (Utilities.py:801), so options.soft_clip_threshold is a FLOAT and the
  // comparison in get_read_position is int > float. Held as int64 here, it
  // was read back with get_int, whose py_parse_int rejects the '.' in the
  // stored "8.0" and yields 0 — every soft clip then counted as spliced.
  double soft_clip_threshold = 4;
  std::string detection_method;               // NH / X0 / XT, "" = none
  // The paired-end triage options. Defaults are the option table's, verified
  // against the generated table rather than the docstrings.
  std::string unpaired_reads = "use";         // discard | use | output
  std::string unmapped_reads = "discard";     // discard | use | output
  std::string chimeric_pairs = "use";         // discard | use | output
  std::string chrom;                          // --chrom, "" = whole file
  // --gene-transcript-map. When set, reads are fetched contig by contig and
  // tagged "MC" with the gene name (sam_methods.metafetcher), and check_output
  // switches to the metacontig rule: flush a gene only once EVERY one of its
  // transcripts has been observed.
  const MetaContigMap* metacontig = nullptr;
  UmiMethod get_umi_method = UmiMethod::ReadId;
  std::string umi_sep = "_";
  TagBarcodeOptions tag_options;
};

/// One bundled entry: the count, plus the retained read(s) when the caller asked
/// for them. `count` uses only_count_reads, so `reads` stays empty there.
struct BundleEntry {
  std::int64_t count = 0;
  std::vector<BamRecord> reads;
};
using Bundle = OrderedMap<Bytes, BundleEntry>;

/// The bundle key: (gene|position, cell). For the per-gene path the position IS
/// the gene name, which is why it is a string.
struct BundleKey {
  std::string pos;                 // gene name (per-gene) or a formatted position
  std::optional<Bytes> cell;
  bool operator<(const BundleKey& o) const {
    if (pos != o.pos) return pos < o.pos;
    if (cell.has_value() != o.cell.has_value()) return !cell.has_value();
    if (cell && o.cell) return *cell < *o.cell;
    return false;
  }
};

/// sam_methods.get_bundles.__call__ — a GENERATOR upstream, ported as a callback
/// so it stays streaming over a BAM that can exceed RAM.
///
/// SCOPE THIS SESSION: the PER-GENE path (gene from a tag or contig), which is
/// what `count` uses. The positional path (get_read_position / find_splice /
/// the 4-tuple key) lands with `group` in slice 6; it is a distinct branch and is
/// not implemented here rather than half-implemented.
/// sam_methods.find_splice(cigar). Returns the offset of the first splice, or
/// Python's `False`.
///
/// D5: the Python returns an INT OR `False`, and in Python `False == 0` with the
/// same hash — so a spliced read at offset 0 and an unspliced read are THE SAME
/// dict key. Returning a plain int with 0 meaning both is therefore not a
/// simplification, it is the exact semantics. `bool(0)` and `bool(False)` are
/// both false, so the `options.spliced and is_spliced` guard agrees too.
std::int64_t find_splice(const std::vector<CigarOp>& cigar);

/// sam_methods.get_read_position(read, soft_clip_threshold) -> (start, pos, is_spliced)
///
/// D5: `pos` CAN BE NEGATIVE — a forward read soft-clipped at its 5' end gets
/// `pos = read.pos - cigar[0].len`, which goes below zero near a contig start.
/// Every field here is signed for that reason; a size_t would wrap and silently
/// reorder every bundle.
struct ReadPosition {
  std::int64_t start = 0;
  std::int64_t pos = 0;
  std::int64_t is_spliced = 0;   // 0 == Python's False (see find_splice)
};
ReadPosition get_read_position(const BamRecord& read, double soft_clip_threshold);

/// dedup.detect_bam_features(bamfile, n_entries=1000).
///
/// Returns NH/X0/XT -> 1 if EVERY mapped read among the first n_entries+1 carries
/// that tag, else 0. Insertion order (NH, X0, XT) is preserved because the error
/// message joins the surviving keys in dict order.
///
/// Note the two off-by-one-looking details, both upstream's: the loop breaks on
/// `n > n_entries` so it inspects n_entries+1 records, and it opens the file with
/// until_eof=True even though dedup's main pass does not.
OrderedMap<std::string, std::int64_t> detect_bam_features(const std::string& path,
                                                          std::int64_t n_entries = 1000);

/// The barcode-extraction step shared by get_bundles.__call__ and
/// random_read_generator.fill — upstream hands both the SAME `barcode_getter`
/// functor, so they must not be two transcriptions here either.
///
/// Returns nullopt where pysam raises KeyError (a missing umi/cell tag); every
/// caller catches that and skips the read.
std::optional<CellUmi> barcode_for_read(const BamRecord& read, const BundleOptions& options);

struct BundleReadEvents {
  OrderedMap<std::string, std::int64_t> counts;
};

/// Python's `pos`: the GENE NAME in the per-gene branch, an integer POSITION in
/// the positional branch. A single run is entirely one branch or the other, so
/// one struct with the unused half at its zero value sorts correctly in both —
/// `sorted(reads_dict.keys())` is a string sort in one mode and a numeric sort
/// in the other, and this reproduces each without a variant.
struct BundlePos {
  std::string gene;        // "" in the positional branch
  std::int64_t pos = 0;    // 0 in the per-gene branch
  bool operator<(const BundlePos& o) const {
    if (gene != o.gene) return gene < o.gene;
    return pos < o.pos;
  }
};

/// `subset` and `rng` are threaded because --subset is a real option in count's
/// table and is consumed by the SHARED read triage, not the positional branch
/// only. Hardcoding std::nullopt made `count --subset=0.5` do no subsampling at
/// all, where Python drops ~half the reads and logs "Randomly excluded".
void for_each_bundle_per_gene(
    AlignmentReader& reader, const BundleOptions& options, bool only_count_reads,
    BundleReadEvents& events,
    const std::function<void(const Bundle&, const BundleKey&)>& on_bundle,
    std::optional<double> subset = std::nullopt);

/// The POSITIONAL path (slice 6): bundles keyed by
///   (is_reverse, spliced_and_is_spliced, (not ignore_tlen)*paired*tlen, r_length, cell)
/// all of which are integers, because `False` and `0` must remain the same key.
///
/// `all_reads` retains every read per UMI (what `group` needs); otherwise a single
/// "best" read is kept, chosen by MAPQ then the multimapping tag then a RESERVOIR
/// DRAW from the shared Python `random` stream. `rng` is that stream: the same
/// object must also serve --subset, because upstream draws both from one
/// `random` module and the call ORDER is contractual.
///
/// THE KEY IS A NESTED PYTHON TUPLE: ((is_reverse, spliced, tlen, r_length), cell).
/// It is never printed, but `sorted(reads_dict[p].keys())` decides the order reads
/// come out in, so the comparison must be Python's tuple comparison exactly:
/// element-wise, left to right, with bools compared AS INTEGERS (False < True).
struct PositionalKey {
  // The per-gene branch's key is `(gene, cell)`; the positional branch's is
  // `((is_reverse, spliced, tlen, r_length), cell)`. Comparing `gene` first with
  // the four integers left at 0 in per-gene mode gives Python's ordering for
  // both, since a run never mixes the two.
  std::string gene;
  // All four are int64 because Python compares `False` and `0` as equal here —
  // see find_splice's D5 note. Storing the first two as bool would be correct
  // too, but then `spliced` could not hold `options.spliced and is_spliced`,
  // whose Python value is `False` OR an int offset.
  std::int64_t is_reverse = 0;
  std::int64_t spliced = 0;
  std::int64_t tlen = 0;          // (not ignore_tlen) * paired * read.tlen
  std::int64_t read_length = 0;
  std::optional<Bytes> cell;

  bool operator<(const PositionalKey& o) const {
    if (gene != o.gene) return gene < o.gene;
    if (is_reverse != o.is_reverse) return is_reverse < o.is_reverse;
    if (spliced != o.spliced) return spliced < o.spliced;
    if (tlen != o.tlen) return tlen < o.tlen;
    if (read_length != o.read_length) return read_length < o.read_length;
    // Python would raise TypeError comparing None < None, but it never gets
    // here: equal inner tuples + equal cells is the SAME dict key, so two
    // distinct keys always differ before this point. Ordering None first keeps
    // the container a strict weak ordering regardless.
    if (cell.has_value() != o.cell.has_value()) return !cell.has_value();
    if (cell && o.cell) return *cell < *o.cell;
    return false;
  }
};

/// Utilities.validateSamOptions. Returns the two values it MUTATES on the options
/// object (upstream edits `options` in place) plus the message it would have
/// raised. Several of its checks test the raw joined command string rather than
/// the parsed values, so `command` must be `sys.argv` AFTER the dispatcher's
/// `del sys.argv[0]` — i.e. starting at the subcommand name.
struct SamOptionsResult {
  std::string assigned_tag;
  std::string unmapped_reads;
  std::optional<std::string> error;
};
class Values;
/// `per_gene_override` exists because count.py sets `options.per_gene = True`
/// BEFORE calling validateSamOptions, so the validation there sees per_gene set
/// even though the user never passed the flag. Without it, `count --gene-tag=XF`
/// fails the "need to use --per-gene with --gene_tag" check that upstream passes.
/// Utilities.Start opens `options.stdout` with mode 'w' (Utilities.py:1112-1125)
/// BEFORE the tool's main body runs, so the file EXISTS, truncated, from that
/// moment on. Every later failure therefore leaves a zero-length output behind.
///
/// dedup, group and count all deferred creating it — dedup and
/// group until sort_output at the very end, count until after validation — so
/// an abort left NO file where upstream leaves an empty one. MEASURED on
/// `dedup --gene-tag=XF --per-contig -S out.sam`: oracle rc=1 with out.sam=0B,
/// port rc=1 with no file at all. Call this at Start's position: after the
/// header/params dump, BEFORE validate_sam_options.
///
/// A no-op for stdout ("-" or unset). The file is created and closed
/// immediately; whatever writes the real output later truncates it again.
void touch_start_output(const std::string& name);

SamOptionsResult validate_sam_options(const Values& options, bool group,
                                      const std::string& command,
                                      bool per_gene_override = false);

/// Fill a BundleOptions from parsed CLI values + validateSamOptions' results.
BundleOptions make_bundle_options(const Values& options, const SamOptionsResult& sam);

class PyRandom;

/// get_bundles.__call__ IN FULL — both branches, as upstream has them in one
/// function. Keeping them in one function here too is deliberate: the branch is
/// only the key computation and the flush rule, while the ~120 lines of read
/// triage around it are shared, and two copies of that triage would drift.
void for_each_bundle(
    AlignmentReader& reader, const BundleOptions& options, bool only_count_reads,
    bool all_reads, bool return_read2, bool return_unmapped, PyRandom& rng,
    std::optional<double> subset, BundleReadEvents& events,
    const std::function<void(const Bundle&, const BundlePos&, const PositionalKey&)>& on_bundle,
    const std::function<void(BamRecord&)>& on_single_read);

/// sam_methods.TwoPassPairWriter — the paired-end output path for `dedup`.
///
/// Owns a SECOND AlignmentReader over the same file. AlignmentReader is
/// non-copyable precisely so that this cannot accidentally become a copy of the
/// main pass's reader, which is mid-iteration.
class TwoPassPairWriter {
 public:
  TwoPassPairWriter(const std::string& path, const std::string& reference_filename,
                    AlignmentWriter& outfile);
  TwoPassPairWriter(const TwoPassPairWriter&) = delete;
  TwoPassPairWriter& operator=(const TwoPassPairWriter&) = delete;

  void write(const BamRecord& read, bool unmapped = false);
  void close();

 private:
  /// (query_name, reference_name, reference_start). A SET upstream, so only
  /// membership matters and no iteration order is observable.
  using MateKey = std::tuple<std::string, std::string, std::int64_t>;

  std::string contig_name(std::int32_t tid) const;
  void write_mates();

  AlignmentReader mate_file_;
  std::string reference_;
  AlignmentWriter& out_;
  std::set<MateKey> read1s_;
  std::optional<std::string> chrom_;
  bool closed_ = false;
};

}  // namespace umi_tools
