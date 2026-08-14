#include "umi_tools/alignment.hpp"

#include "umi_tools/logging.hpp"

#include <htslib/bgzf.h>
#include <htslib/hts.h>
#include <htslib/sam.h>
#include <htslib/kstring.h>

#include <cctype>
#include <cstring>

#include <stdexcept>
#include <vector>

namespace umi_tools {

void check_tag_name(const std::string& tag, const char* option) {
  // pysam's SET path is stricter than its GET path, measured on the oracle:
  //   get_tag("")  -> KeyError   (copy_tags swallows it; rc 0)
  //   set_tag("")  -> ValueError: Invalid tag:   (rc 1)
  // so only the writers validate, and with upstream's message.
  (void)option;
  if (tag.size() != 2) raise_value_error("Invalid tag: " + tag);
}

void BamRecord::Deleter::operator()(bam1_t* p) const {
  if (p != nullptr) bam_destroy1(p);
}

BamRecord::BamRecord() : rec_(bam_init1()) {
  if (!rec_) throw std::runtime_error("bam_init1 failed");
}
BamRecord::~BamRecord() = default;
BamRecord::BamRecord(BamRecord&&) noexcept = default;
BamRecord& BamRecord::operator=(BamRecord&&) noexcept = default;

BamRecord BamRecord::clone() const {
  BamRecord out;
  if (bam_copy1(out.rec_.get(), rec_.get()) == nullptr)
    throw std::runtime_error("bam_copy1 failed");
  return out;
}

BamRecord BamRecord::clone_via_sam_text(sam_hdr_t* hdr) const {
  // sam_format1 then sam_parse1 — exactly the pair pysam's to_dict/from_dict
  // reduce to. sam_parse1 needs a MUTABLE, NUL-terminated buffer: it writes
  // into the string while tokenising, so to_string()'s value is copied into a
  // vector rather than passed through c_str().
  const std::string text = to_string(hdr);
  std::vector<char> buf(text.begin(), text.end());
  buf.push_back('\0');
  kstring_t ks;
  ks.s = buf.data();
  ks.l = text.size();
  ks.m = buf.size();

  BamRecord out;
  if (sam_parse1(&ks, hdr, out.rec_.get()) < 0)
    throw std::runtime_error("sam_parse1 failed re-parsing: " + text);
  return out;
}

std::string_view BamRecord::query_name() const { return bam_get_qname(rec_.get()); }
std::int64_t BamRecord::pos() const { return rec_->core.pos; }
std::int64_t BamRecord::reference_end() const { return bam_endpos(rec_.get()); }
std::int32_t BamRecord::tid() const { return rec_->core.tid; }
std::int32_t BamRecord::mate_tid() const { return rec_->core.mtid; }
std::int64_t BamRecord::next_reference_start() const { return rec_->core.mpos; }
std::int64_t BamRecord::tlen() const { return rec_->core.isize; }
std::int64_t BamRecord::mapq() const { return rec_->core.qual; }
std::int64_t BamRecord::query_length() const { return rec_->core.l_qseq; }
std::uint16_t BamRecord::flag() const { return rec_->core.flag; }

bool BamRecord::is_unmapped() const { return (rec_->core.flag & BAM_FUNMAP) != 0; }
bool BamRecord::mate_is_unmapped() const { return (rec_->core.flag & BAM_FMUNMAP) != 0; }
bool BamRecord::is_reverse() const { return (rec_->core.flag & BAM_FREVERSE) != 0; }
bool BamRecord::is_paired() const { return (rec_->core.flag & BAM_FPAIRED) != 0; }
bool BamRecord::is_read1() const { return (rec_->core.flag & BAM_FREAD1) != 0; }
bool BamRecord::is_read2() const { return (rec_->core.flag & BAM_FREAD2) != 0; }
bool BamRecord::is_secondary() const { return (rec_->core.flag & BAM_FSECONDARY) != 0; }

std::vector<CigarOp> BamRecord::cigar() const {
  std::vector<CigarOp> out;
  const std::uint32_t* c = bam_get_cigar(rec_.get());
  out.reserve(rec_->core.n_cigar);
  for (std::uint32_t i = 0; i < rec_->core.n_cigar; ++i)
    out.push_back(CigarOp{bam_cigar_op(c[i]), bam_cigar_oplen(c[i])});
  return out;
}

std::string BamRecord::cigarstring() const {
  // pysam's cigarstring is None for an absent CIGAR; the only use in umi_tools
  // is `'N' in read.cigarstring`, so an empty string is the faithful stand-in.
  std::string out;
  const std::uint32_t* c = bam_get_cigar(rec_.get());
  for (std::uint32_t i = 0; i < rec_->core.n_cigar; ++i) {
    out += std::to_string(bam_cigar_oplen(c[i]));
    out += bam_cigar_opchr(c[i]);
  }
  return out;
}

std::optional<std::string> BamRecord::get_tag_str(const char* tag) const {
  std::uint8_t* aux = bam_aux_get(rec_.get(), tag);
  if (aux == nullptr) return std::nullopt;   // pysam raises KeyError; callers catch it
  const char type = static_cast<char>(*aux);
  if (type == 'Z' || type == 'H') {
    const char* s = bam_aux2Z(aux);
    return s == nullptr ? std::nullopt : std::optional<std::string>(s);
  }
  if (type == 'A') return std::string(1, static_cast<char>(bam_aux2A(aux)));
  // Integer and float tags stringify the way Python's str() would for the
  // values umi_tools reads (gene/cell tags are Z in practice).
  if (type == 'f' || type == 'd') return std::to_string(bam_aux2f(aux));
  return std::to_string(bam_aux2i(aux));
}

std::optional<std::int64_t> BamRecord::get_tag_int(const char* tag) const {
  std::uint8_t* aux = bam_aux_get(rec_.get(), tag);
  if (aux == nullptr) return std::nullopt;
  return bam_aux2i(aux);
}

void BamRecord::set_tag_int(const char* tag, std::int64_t value) {
  bam_aux_update_int(rec_.get(), tag, value);
}

void BamRecord::set_tag_str(const char* tag, std::string_view value) {
  const std::string v(value);
  bam_aux_update_str(rec_.get(), tag, static_cast<int>(v.size() + 1), v.c_str());
}

// --------------------------------------------------------------------------
bool BamRecord::copy_tag_from(const BamRecord& src, const char* tag) {
  // htslib declares these `const char tag[2]` and reads BOTH bytes
  // unconditionally, so a shorter name is a one-byte out-of-bounds read (and
  // ASan misses it: the SSO buffer is 16 bytes). MEASURED against the oracle:
  // `prepare_for_em --tags=` exits 0 there, because get_tag("") raises KeyError
  // and copy_tags swallows it. So a bad name is "absent", not an error.
  if (tag == nullptr || std::strlen(tag) != 2) return false;
  std::uint8_t* aux = bam_aux_get(src.rec_.get(), tag);
  if (aux == nullptr) return false;          // KeyError upstream
  const char type = static_cast<char>(*aux);
  const std::uint8_t* data = aux + 1;

  // Length of the VALUE bytes that follow the type char.
  int len = 0;
  switch (type) {
    case 'A': case 'c': case 'C': len = 1; break;
    case 's': case 'S':           len = 2; break;
    case 'i': case 'I': case 'f': len = 4; break;
    case 'd':                     len = 8; break;
    case 'Z': case 'H':
      len = static_cast<int>(std::strlen(reinterpret_cast<const char*>(data))) + 1;
      break;
    case 'B': {
      const char sub = static_cast<char>(data[0]);
      std::uint32_t n = 0;
      std::memcpy(&n, data + 1, 4);
      int esz = 0;
      switch (sub) {
        case 'c': case 'C': esz = 1; break;
        case 's': case 'S': esz = 2; break;
        case 'i': case 'I': case 'f': esz = 4; break;
        default: return false;
      }
      len = 5 + static_cast<int>(n) * esz;
      break;
    }
    default:
      return false;
  }

  // pysam's set_tag REMOVES an existing tag and APPENDS the new value at the
  // END of the aux list; bam_aux_update_* instead rewrites it IN PLACE. The
  // values are identical either way and SAM text hides the integer subtype, so
  // the only observable difference is TAG ORDER — measured on --tags=NM,MD,ZZ,
  // where the oracle moves NM and MD to the end and an in-place update leaves
  // them where they were. Delete-then-append reproduces the oracle's order.
  std::uint8_t* existing = bam_aux_get(rec_.get(), tag);
  if (existing != nullptr) bam_aux_del(rec_.get(), existing);

  // The raw bytes and the ORIGINAL type char are carried across, which is what
  // `set_tag(..., value_type=<the type get_tag reported>)` does.
  return bam_aux_append(rec_.get(), tag, type, len, data) == 0;
}

std::string BamRecord::to_string(const sam_hdr_t* hdr) const {
  kstring_t ks = KS_INITIALIZE;
  if (sam_format1(hdr, rec_.get(), &ks) < 0) {
    ks_free(&ks);
    throw std::runtime_error("sam_format1 failed");
  }
  std::string out(ks.s, ks.l);
  ks_free(&ks);
  return out;
}

void BamRecord::set_secondary(bool value) {
  if (value) rec_->core.flag |= BAM_FSECONDARY;
  else rec_->core.flag &= ~static_cast<std::uint16_t>(BAM_FSECONDARY);
}

void apply_format_options(htsFile* fp, const std::string& format_options,
                          const char* which) {
  if (format_options.empty()) return;
  hts_opt* opts = nullptr;
  std::size_t start = 0;
  bool bad = false;
  while (start <= format_options.size()) {
    const std::size_t comma = format_options.find(',', start);
    const std::string one = format_options.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!one.empty() && hts_opt_add(&opts, one.c_str()) < 0) { bad = true; break; }
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  if (!bad && opts != nullptr && hts_opt_apply(fp, opts) < 0) bad = true;
  if (opts != nullptr) hts_opt_free(opts);
  if (bad) throw std::runtime_error(std::string("bad ") + which + ": " + format_options);
}

AlignmentReader::AlignmentReader(const std::string& path,
                                 const std::string& reference_filename,
                                 const std::string& format_options,
                                 const std::string& mode)
    : path_(path), reference_(reference_filename), mode_(mode) {
  fp_ = sam_open(path.c_str(), "r");
  if (fp_ == nullptr) throw std::runtime_error("cannot open alignment file: " + path);

  // A throwing CONSTRUCTOR means the destructor never runs, so anything already
  // acquired leaks. Rather than remembering to close at each throw site — which
  // is exactly what was forgotten on both paths below — a scope guard closes
  // unless construction reaches the end. `dedup --reference-filename=/nope.fa`
  // and any unparseable header both used to leak the htsFile.
  struct Guard {
    AlignmentReader* self;
    bool armed = true;
    ~Guard() {
      if (!armed) return;
      if (self->hdr_ != nullptr) { sam_hdr_destroy(self->hdr_); self->hdr_ = nullptr; }
      if (self->fp_ != nullptr) { sam_close(self->fp_); self->fp_ = nullptr; }
    }
  } guard{this};

  if (!reference_filename.empty()) {
    // CRAM needs a reference; htslib resolves the header's UR field otherwise,
    // which is what the shipped chr19_1mb.cram relies on and what the 6_deps
    // probe exercised.
    if (hts_set_fai_filename(fp_, reference_filename.c_str()) < 0)
      throw std::runtime_error("cannot set reference: " + reference_filename);
  }
  // pysam checks FOUR more things between the open and the first record, each
  // with its own message (libcalignmentfile.pyx:946-1004 + libchtslib.pyx:346).
  // The port reached none of them: every case below produced a different
  // sentence, and one produced NO ERROR AT ALL. Measured against the live oracle
  // over nine malformed inputs.
  //
  // 1. The format CATEGORY, tested before the header. An empty file, a text
  //    file, a BAM cut mid-header: htslib OPENS all three, and only the category
  //    says they are not alignment data. This was the case SF-01 found —
  //    `prepare_for_em` on an empty stdin said "cannot read header: -".
  const htsFormat* fmt = hts_get_format(fp_);
  if (fmt == nullptr || fmt->category != sequence_data)
    throw std::runtime_error("file does not contain alignment data");

  // --input-options, applied BEFORE the header is read: several htslib options
  // (decode_md, required_fields, the CRAM ones) only take effect if they are
  // set before decoding starts. The guard above covers a throw from here.
  apply_format_options(fp_, format_options, "--input-options");

  // 2. check_truncation(ignore_truncation=False) — the only one of these that
  //    was not merely a sentence. A BAM whose BGZF EOF block is missing is a
  //    file htslib reads happily to the end of whatever is there, so the port
  //    exited 0 with PARTIAL output where upstream refuses to start (measured:
  //    oracle rc 1, port rc 0). Silent truncation of the caller's data.
  //
  //    Gated on `compression == bgzf` and then on `is_bgzf`, which is what
  //    pysam's check plus hts_get_bgzfp amount to. hts.h:245 warns that
  //    `is_bgzf` alone does not mean BGZF-compressed, so it is not the test.
  if (fmt->compression == bgzf && fp_->is_bgzf && fp_->fp.bgzf != nullptr) {
    const int eof = bgzf_check_EOF(fp_->fp.bgzf);
    // pysam raises `IOError(errno, ...)`, whose text carries an errno the port
    // cannot reproduce — the L39 case, and unreachable without a real I/O error.
    if (eof < 0) throw std::runtime_error("error checking for EOF marker");
    // pysam calls `.format(self.filename)` on a string containing no field, so
    // the filename never appears in it. Reproduced verbatim, quirk included.
    if (eof == 0) throw std::runtime_error("no BGZF EOF marker; file may be truncated");
  }

  hdr_ = sam_hdr_read(fp_);
  if (hdr_ == nullptr) {
    // 3./4. Two different sentences, chosen by the DETECTED format rather than
    //       by the mode. The SAM arm is upstream's own bug: its `%s` has no `%`
    //       operator applied, so the literal `mode='%s'` reaches the user.
    //       Reproduced, because parity is against what upstream DOES.
    const htsExactFormat ef = fmt->format;
    if (ef == bam || ef == cram)
      throw std::runtime_error("file does not have a valid header (mode='" + mode_ +
                               "') - is it BAM/CRAM format?");
    throw std::runtime_error("SAM? file does not have a valid header (mode='%s'), "
                             "please provide reference_names and reference_lengths");
  }

  // 5. check_sq. Upstream never passes check_sq=False, so a header with zero @SQ
  //    lines fails before any record is read. The port had no such check and got
  //    the right EXIT CODE by accident: the first record naming an unknown
  //    reference failed to parse, reported as "truncated alignment file". A
  //    headerless SAM whose records happened to parse would have been processed
  //    silently.
  if (sam_hdr_nref(hdr_) == 0)
    throw std::runtime_error("file has no sequences defined (mode='" + mode_ +
                             "') - is it SAM/BAM format? Consider opening with "
                             "check_sq=False");
  guard.armed = false;
}

AlignmentReader::~AlignmentReader() {
  // Order matters: the iterator borrows from the index, so it goes first.
  if (itr_ != nullptr) hts_itr_destroy(itr_);
  if (idx_ != nullptr) hts_idx_destroy(idx_);
  if (hdr_ != nullptr) sam_hdr_destroy(hdr_);
  if (fp_ != nullptr) sam_close(fp_);
}

bool AlignmentReader::next(BamRecord& into) {
  const int r = itr_ != nullptr ? sam_itr_next(fp_, itr_, into.raw())
                                : sam_read1(fp_, hdr_, into.raw());
  if (r >= 0) return true;
  if (r < -1) throw std::runtime_error("truncated alignment file: " + path_);
  return false;
}

namespace {

// pysam's `self.is_remote`, which is htslib's `hisremote()`: a path carrying a
// URL scheme. fetch() skips the index requirement for one, so a remote BAM must
// not be refused here.
bool is_remote_path(const std::string& path) {
  const std::size_t sep = path.find("://");
  if (sep == std::string::npos || sep == 0) return false;
  for (std::size_t i = 0; i < sep; ++i) {
    const char c = path[i];
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.')
      return false;
  }
  return true;
}

// pysam loads the index at OPEN time with SAVE_REMOTE|SILENT_FAIL
// (libcalignmentfile.pyx:1013-1014) and only later asks `has_index()`. The
// SILENT_FAIL is why upstream prints no htslib diagnostic for a missing index:
// without it htslib writes "[E::idx_find_and_load] Could not retrieve index file
// for ..." to stderr, and the port's stderr then diverges from the oracle's on a
// line NEITHER tool authored.
hts_idx_t* load_index_quietly(htsFile* fp, const std::string& path) {
  return sam_index_load3(fp, path.c_str(), nullptr,
                         HTS_IDX_SAVE_REMOTE | HTS_IDX_SILENT_FAIL);
}

}  // namespace

void AlignmentReader::require_index() {
  // pysam.fetch()'s gate, libcalignmentfile.pyx:1105-1110:
  //   if (is_bam or is_cram) and not until_eof and not is_remote:
  //       if not has_index(): raise ValueError(...)
  //
  // The BAM/CRAM gate is load-bearing: a SAM has no index and needs none, and
  // `dedup` on a SAM exits 0 upstream (MEASURED). Requiring one unconditionally
  // refuses a file upstream accepts, which is the same class of error as
  // accepting one it refuses — it just fails in the safer direction.
  const htsExactFormat ef = hts_get_format(fp_)->format;
  if (ef != bam && ef != cram) return;
  if (is_remote_path(path_)) return;
  if (idx_ != nullptr) return;
  idx_ = load_index_quietly(fp_, path_);
  if (idx_ == nullptr)
    throw std::runtime_error("fetch called on bamfile without index");
}

void AlignmentReader::set_region(const std::string& contig) {
  if (itr_ != nullptr) { hts_itr_destroy(itr_); itr_ = nullptr; }
  if (contig.empty()) return;                 // back to the linear pass
  if (idx_ == nullptr) {
    // pysam raises the SAME ValueError for both fetch forms — the region form is
    // not a special case (MEASURED: `dedup --chrom=chr19` on an unindexed BAM
    // reports "fetch called on bamfile without index", identical to the bare
    // `fetch()`). This used to invent its own sentence naming the path, and to
    // load the index loudly; see load_index_quietly.
    idx_ = load_index_quietly(fp_, path_);
    if (idx_ == nullptr)
      throw std::runtime_error("fetch called on bamfile without index");
  }
  itr_ = sam_itr_querys(idx_, hdr_, contig.c_str());
  if (itr_ == nullptr)
    throw std::runtime_error("cannot query region '" + contig + "' in " + path_);
}

std::int32_t AlignmentReader::n_targets() const { return hdr_->n_targets; }

std::string_view AlignmentReader::target_name(std::int32_t tid) const {
  // read.reference_name is None for an unmapped read (tid -1); the callers that
  // reach it always have a mapped read, and an empty view is the faithful stand-in.
  if (tid < 0 || tid >= hdr_->n_targets) return {};
  return sam_hdr_tid2name(hdr_, tid);
}

}  // namespace umi_tools
