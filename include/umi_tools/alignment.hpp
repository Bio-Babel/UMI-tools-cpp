// alignment.hpp — htslib RAII, the C++ equivalent of the pysam surface umi_tools
// actually uses.
//
// `05_dep_map.md` measured that surface: 35 distinct `AlignedSegment` members and
// 6 `AlignmentFile` operations, every one of which maps to an htslib accessor.
// This is the case where C++ has an advantage Rust does not — the same C library
// pysam itself calls is linked directly, so format handling is not merely
// equivalent but IDENTICAL. The `6_deps` probe proved it configures, links, runs,
// and produces the same record counts and first-record fields as pysam on the
// same BAM and CRAM.
//
// OWNERSHIP (06_design.md decided this once per type, not per call site):
//   BamRecord        owns a bam1_t via unique_ptr with a custom deleter.
//                    MOVE-ONLY, with an explicit clone() — because
//                    prepare_for_em copies records, and an implicit copy of a
//                    pointer-owning type is how a double free happens.
//   AlignmentReader  owns samFile* + sam_hdr_t* + an optional index.
//                    NON-COPYABLE. TwoPassPairWriter needs a SECOND independent
//                    open of the same path, which is a second Reader, never a
//                    copy of the first.
//
// VIEW LIFETIME: accessors returning std::string_view point into the record's own
// buffer and are valid only while that BamRecord is alive and unmodified. That is
// the single rule from 06_design.md, restated where it applies.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct bam1_t;
struct sam_hdr_t;
struct htsFile;
struct hts_idx_t;
struct hts_itr_t;

namespace umi_tools {

/// One CIGAR operation. htslib packs these as (len << 4 | op).
struct CigarOp {
  std::uint32_t op;    // BAM_CMATCH=0, CINS=1, CDEL=2, CREF_SKIP=3, CSOFT_CLIP=4 …
  std::uint32_t len;
};

/// Reject a tag name that htslib would read out of bounds. Used only on the
/// WRITE side: pysam's set_tag raises `ValueError: Invalid tag: ` for a bad name
/// (rc 1) while get_tag merely raises KeyError, which copy_tags swallows — so the
/// read side treats a bad name as absent instead. Both measured on the oracle.
void check_tag_name(const std::string& tag, const char* option);

class BamRecord {
 public:
  BamRecord();
  ~BamRecord();
  BamRecord(BamRecord&&) noexcept;
  BamRecord& operator=(BamRecord&&) noexcept;
  BamRecord(const BamRecord&) = delete;
  BamRecord& operator=(const BamRecord&) = delete;

  /// A byte-for-byte deep copy (bam_copy1). NOT what prepare_for_em does —
  /// see clone_via_sam_text.
  BamRecord clone() const;

  /// prepare_for_em's copy, which is a SAM-TEXT ROUND TRIP rather than a byte
  /// copy: `pysam.AlignedSegment().from_dict(read.to_dict(), read.header)`,
  /// where to_dict is `self.to_string().split('\t')` and from_dict re-joins and
  /// calls fromstring -> sam_parse1 (libcalignedsegment.pyx:1141-1167).
  ///
  /// The round trip NORMALISES the aux representation: htslib's SAM
  /// parser stores an integer tag in the SMALLEST subtype that holds its value,
  /// whatever width the input BAM used. bam_copy1 preserves the input bytes, so
  /// the two agree on SAM text and disagree on BAM bytes. MEASURED on a
  /// name-sorted paired.bam carrying `ZW` as int32 with value 5: 6,990 records
  /// out of both, ALL SAM text identical, oracle stores ZW as 'C' and the port
  /// stored 'i'; the BAM differed by 4,858 bytes with byte-identical headers.
  BamRecord clone_via_sam_text(sam_hdr_t* hdr) const;

  bam1_t* raw() { return rec_.get(); }
  const bam1_t* raw() const { return rec_.get(); }

  /// --- the pysam members umi_tools uses -------------------------------------
  std::string_view query_name() const;  ///< read.qname / query_name
  std::int64_t pos() const;  ///< read.pos / reference_start
  std::int64_t reference_end() const;  ///< read.aend
  std::int32_t tid() const;
  std::int32_t mate_tid() const;
  std::int64_t next_reference_start() const;  ///< read.next_reference_start
  std::int64_t tlen() const;  ///< read.tlen
  std::int64_t mapq() const;  ///< read.mapq
  std::int64_t query_length() const;  ///< read.query_length
  std::uint16_t flag() const;

  bool is_unmapped() const;
  bool mate_is_unmapped() const;
  bool is_reverse() const;
  bool is_paired() const;
  bool is_read1() const;
  bool is_read2() const;
  bool is_secondary() const;

  std::vector<CigarOp> cigar() const;
  std::string cigarstring() const;

  /// read.get_tag(tag). Returns nullopt where pysam raises KeyError, because the
  /// callers all catch that (`except KeyError: continue`) — an exception here
  /// would be a control-flow difference, not just a different error type.
  std::optional<std::string> get_tag_str(const char* tag) const;
  std::optional<std::int64_t> get_tag_int(const char* tag) const;
  void set_tag_int(const char* tag, std::int64_t value);
  void set_tag_str(const char* tag, std::string_view value);

  /// copy_tags: `read2.set_tag(tag, *read1.get_tag(tag, with_value_type=True))`
  /// — the VALUE TYPE is carried across, so an integer tag stays integer rather
  /// than becoming a string. Returns false where pysam raises KeyError (absent
  /// tag), which upstream swallows with `except KeyError: pass`.
  bool copy_tag_from(const BamRecord& src, const char* tag);

  /// `str(read)`. pysam's __str__ spells the mate contig out where the raw SAM
  /// line writes "="; that difference is a deterministic per-record rewrite, so
  /// two records compare equal under one exactly when they do under the other,
  /// and this is only ever used as an equality key. See L30.
  std::string to_string(const sam_hdr_t* hdr) const;

  void set_secondary(bool value);  ///< `mate.is_secondary = True`

 private:
  struct Deleter {
    void operator()(bam1_t* p) const;
  };
  std::unique_ptr<bam1_t, Deleter> rec_;
};

/// Applies a comma-separated htslib format-option list to an open file, the way
/// `pysam.AlignmentFile(format_options=[...])` does: each element goes through
/// htslib's own parser via `hts_opt_add`, then the set is applied at once.
/// `which` names the option in the error message ("--input-options" /
/// "--output-options"). A blank list is a no-op.
void apply_format_options(struct htsFile* fp, const std::string& format_options,
                          const char* which);

class AlignmentReader {
 public:
  /// `format_options` is `--input-options` verbatim, the read-side counterpart
  /// of the writer's `--output-options`. It was parsed and then dropped, so
  /// `--input-options` had no effect at all.
  ///
  /// `mode` is the pysam mode string `open_input_alignments` would have passed
  /// (`input_mode_for_format` below). htslib sniffs the format from the file
  /// either way, so this changes NO parsing — it is consulted only where pysam
  /// interpolates the mode into an error message, which two of the checks in the
  /// constructor do. The default `"r"` is not arbitrary: it is what
  /// `pysam.AlignmentFile(filename)` with no mode resolves to (MEASURED), which
  /// is how upstream opens the mate file in `sam_methods.py:578`, so the
  /// port's own internal readers get upstream's message without passing
  /// anything.
  explicit AlignmentReader(const std::string& path,
                           const std::string& reference_filename = "",
                           const std::string& format_options = "",
                           const std::string& mode = "r");
  ~AlignmentReader();
  AlignmentReader(const AlignmentReader&) = delete;
  AlignmentReader& operator=(const AlignmentReader&) = delete;

  /// fetch(until_eof=True) — a linear pass over every record. Streaming: one
  /// record is held at a time, never the file.
  bool next(BamRecord& into);

  /// fetch(reference=<contig>) — an INDEXED region iterator. Needed by
  /// TwoPassPairWriter, which re-scans one contig at a time to find mates.
  /// Passing "" restores the plain linear pass. Loads the index on first use and
  /// throws if there is none, which is what pysam does.
  void set_region(const std::string& contig);

  /// pysam raises `ValueError: fetch called on bamfile without index` whenever
  /// fetch() runs with until_eof=False and no index exists — for the BARE
  /// `fetch()` as much as for `fetch(reference=...)`
  /// (libcalignmentfile.pyx:1101-1105). `set_region` already enforces it for the
  /// region form; this is the no-region form, which the port used to satisfy
  /// with a plain linear pass.
  void require_index();


  std::int32_t n_targets() const;
  std::string_view target_name(std::int32_t tid) const;  ///< read.reference_name
  sam_hdr_t* header() { return hdr_; }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
  std::string reference_;
  std::string mode_;
  htsFile* fp_ = nullptr;
  sam_hdr_t* hdr_ = nullptr;
  hts_idx_t* idx_ = nullptr;
  hts_itr_t* itr_ = nullptr;
};

/// Utilities.determine_format(filename, sam, out_format).
///   sam flag wins; then --out-format; then the extension (.sam / .cram);
///   otherwise BAM. Extension matching is case-INSENSITIVE (`filename.lower()`).
std::string determine_format(std::string_view filename, bool sam,
                             std::string_view out_format);

/// Utilities.open_input_alignments's mode string: "sam" -> "r", "cram" -> "rc",
/// anything else -> "rb". The READ-side counterpart of the writer's w/wc/wb.
/// Only pysam's error messages vary with it; see AlignmentReader's constructor.
std::string input_mode_for_format(std::string_view format);

/// The write half of the htslib layer. Modes follow
/// Utilities.open_output_alignments: sam -> "w", cram -> "wc", else "wb".
class AlignmentWriter {
 public:
  // `format_options` is --output-options verbatim (comma-separated, e.g.
  // "level=1"). pysam passes the same list into htslib's hts_opt API; upstream
  // instead appends it to samtools' -O argument, which is the same option list
  // reaching the same parser.
  AlignmentWriter(const std::string& path, const std::string& format,
                  sam_hdr_t* template_header,
                  const std::string& reference_filename = "",
                  const std::string& format_options = "");
  ~AlignmentWriter();
  AlignmentWriter(const AlignmentWriter&) = delete;
  AlignmentWriter& operator=(const AlignmentWriter&) = delete;

  void write(const BamRecord& rec);
  /// Flushes and closes, THROWING if the flush failed — a full or unwritable
  /// destination surfaces here, not in write(), because BGZF buffers.
  void close();

  /// The destructor's path: same close, status discarded, because throwing
  /// from a destructor calls std::terminate.
  void close_noexcept() noexcept;

 private:
  std::string path_;
  htsFile* fp_ = nullptr;
  sam_hdr_t* hdr_ = nullptr;   // borrowed from the template; not owned
  bool closed_ = false;
};

/// Utilities.sort_output — upstream shells out to `pysam.sort`, which is the
/// samtools CLI, not a libhts entry point (05_dep_map.md), so the port sorts the
/// records itself.
///
/// THE TIE ORDER WAS MEASURED, NOT ASSUMED. Sorting a shuffled copy
/// of chr19.bam and comparing against the oracle:
///     370 tie groups at the same (tid,pos); 370/370 came out in the SHUFFLED
///     INPUT's order, and the global order equalled a stable sort by (tid,pos)
///     with unmapped (tid == -1) last.
/// So this is std::STABLE_sort — std::sort is free to permute those 370 groups
/// and nothing in the output would look obviously wrong.
///
/// Upstream also passes --no-PG, so no @PG line is added; and it DELETES the
/// unsorted input when done.
void sort_output(const std::string& unsorted_path, const std::string& sorted_path,
                 const std::string& format, const std::string& reference_filename,
                 bool remove_input = true, const std::string& format_options = "");

}  // namespace umi_tools
