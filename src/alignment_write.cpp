// The write half of the htslib layer: AlignmentWriter, determine_format and
// sort_output. Slice 6.
#include "umi_tools/alignment.hpp"
#include "umi_tools/logging.hpp"

#include <htslib/hts.h>
#include <htslib/kstring.h>
#include <htslib/sam.h>

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace umi_tools {
namespace {

bool ends_with_ci(std::string_view s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  for (std::size_t i = 0; i < suffix.size(); ++i) {
    const char a = static_cast<char>(std::tolower(
        static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
    if (a != suffix[i]) return false;
  }
  return true;
}

}  // namespace

std::string determine_format(std::string_view filename, bool sam,
                             std::string_view out_format) {
  // Python: `filename.lower().endswith(...)`, so the extension test is
  // case-insensitive; the sam flag and --out-format both outrank it.
  if (sam) return "sam";
  if (!out_format.empty()) return std::string(out_format);
  if (ends_with_ci(filename, ".sam")) return "sam";
  if (ends_with_ci(filename, ".cram")) return "cram";
  return "bam";
}

std::string input_mode_for_format(std::string_view format) {
  // Utilities.open_input_alignments:1658-1664. htslib sniffs the format from the
  // file regardless, so the mode never selects a parser here — it is carried
  // only because pysam interpolates it into two of its header error messages.
  if (format == "sam") return "r";
  if (format == "cram") return "rc";
  return "rb";
}

AlignmentWriter::AlignmentWriter(const std::string& path, const std::string& format,
                                 sam_hdr_t* template_header,
                                 const std::string& reference_filename,
                                 const std::string& format_options)
    : path_(path), hdr_(template_header) {
  const char* mode = format == "sam" ? "w" : (format == "cram" ? "wc" : "wb");
  fp_ = sam_open(path.c_str(), mode);
  if (fp_ == nullptr)
    throw std::runtime_error("cannot open alignment file for writing: " + path);

  // Same guard as AlignmentReader, and for the same reason. The --output-options
  // path below closed fp_ by hand before throwing; the reference and header
  // paths did not, so a bad --reference-filename or a failed header write leaked
  // the handle AND left a BAM/CRAM destination with no BGZF terminator.
  struct Guard {
    AlignmentWriter* self;
    bool armed = true;
    ~Guard() {
      if (armed && self->fp_ != nullptr) { sam_close(self->fp_); self->fp_ = nullptr; }
    }
  } guard{this};

  // --output-options: `options.output_options.split(",")` upstream, each element
  // through htslib's own parser, exactly as pysam does it.
  apply_format_options(fp_, format_options, "--output-options");
  if (!reference_filename.empty() &&
      hts_set_fai_filename(fp_, reference_filename.c_str()) < 0)
    throw std::runtime_error("cannot set reference for writing: " + reference_filename);
  if (sam_hdr_write(fp_, hdr_) < 0)
    throw std::runtime_error("cannot write header to " + path);
  guard.armed = false;
}

AlignmentWriter::~AlignmentWriter() { close_noexcept(); }

void AlignmentWriter::write(const BamRecord& rec) {
  if (closed_) throw std::logic_error("AlignmentWriter::write after close");
  if (sam_write1(fp_, hdr_, rec.raw()) < 0)
    throw std::runtime_error("sam_write1 failed for " + path_);
}

void AlignmentWriter::close() {
  if (closed_) return;
  closed_ = true;
  if (fp_ != nullptr) {
    // BGZF BUFFERS, so a full or unwritable destination does not fail
    // in sam_write1 — it fails when the last block is flushed, here. Discarding
    // sam_close's status meant `dedup -S /dev/full` exited 0 having written
    // nothing, where the oracle exits 1. The destructor below still swallows
    // it, because throwing from a destructor calls std::terminate; callers that
    // must not lose the error call close() explicitly first.
    const int rc = sam_close(fp_);
    fp_ = nullptr;
    if (rc < 0) throw std::runtime_error("sam_close failed for " + path_);
  }
  // hdr_ is BORROWED from the reader's template and must not be destroyed here.
}

void AlignmentWriter::close_noexcept() noexcept {
  if (closed_) return;
  closed_ = true;
  if (fp_ != nullptr) {
    sam_close(fp_);
    fp_ = nullptr;
  }
}

namespace {

// Upstream shells out to `pysam.sort(...)` — samtools sort — which is
// an EXTERNAL merge sort: it fills a buffer, spills a sorted run to disk, and
// k-way merges the runs at the end. Its peak RSS is therefore bounded, and
// MEASURED flat at ~139 MB across an 8x input increase. Reading the whole file
// into a std::vector<BamRecord> instead made peak memory scale with the output,
// which is an OOM on the multi-GB BAMs this tool targets.
//
// Upstream passes no `-m`, so samtools uses its default buffer of 768 MiB per
// thread; that is the bound reproduced here. UMI_TOOLS_SORT_MAX_MEM overrides it
// in BYTES, which is how the parity harness forces the spill path on a small
// input — otherwise the merge would only ever run on inputs too big to test.
std::size_t sort_buffer_bytes() {
  if (const char* env = std::getenv("UMI_TOOLS_SORT_MAX_MEM")) {
    char* end = nullptr;
    const unsigned long long v = std::strtoull(env, &end, 10);
    if (end != env && v > 0) return static_cast<std::size_t>(v);
  }
  return static_cast<std::size_t>(768) * 1024 * 1024;   // samtools' default
}

// What one record costs us: htslib's payload plus the bam1_t and the vector
// slot. Approximate on purpose — the bound only has to be the right order.
std::size_t record_bytes(const BamRecord& r) {
  return static_cast<std::size_t>(r.raw()->l_data) + sizeof(bam1_t) + 32;
}

// samtools sort's order: by (tid, pos, is_reverse) with unmapped (tid == -1)
// LAST. samtools builds a single integer key,
//
//     (uint64_t)tid << 32 | (pos + 1) << 1 | bam_is_rev(b)
//
// so the REVERSE-STRAND BIT is its least-significant component: at one
// coordinate, forward reads precede reverse reads regardless of the order they
// were written in.
//
// That third term was missing here, and the comment above this function used to
// read "a STABLE sort by (tid, pos)" — a real measurement that could not tell the
// two keys apart, because a stable sort by (tid, pos) and a sort by
// (tid, pos, is_rev) agree exactly until a forward and a reverse read share a
// position AND arrive reverse-first. No shipped fixture does that.
//
// MEASURED on `group --stdin=whitelist_umi_input.bam --output-bam --out-sam`:
// both sides write the same 10088 records in byte-identical order
// (--no-sort-output), and the sorted outputs differ in 348 positions. Replaying
// candidate keys over that shared write order, (tid, pos) misplaces 820 records
// and (tid, pos, is_reverse) reproduces the oracle's output EXACTLY.
std::int64_t tid_key_of(const BamRecord& r) {
  const std::int32_t t = r.tid();
  return t < 0 ? std::numeric_limits<std::int64_t>::max() : t;
}

bool record_less(const BamRecord& a, const BamRecord& b) {
  const std::int64_t ta = tid_key_of(a), tb = tid_key_of(b);
  if (ta != tb) return ta < tb;
  if (a.pos() != b.pos()) return a.pos() < b.pos();
  // Forward before reverse. Strictly less, so equal-strand ties stay in input
  // order under std::stable_sort and fall to the earliest run in the k-way merge.
  return static_cast<int>(a.is_reverse()) < static_cast<int>(b.is_reverse());
}

}  // namespace

void sort_output(const std::string& unsorted_path, const std::string& sorted_path,
                 const std::string& format, const std::string& reference_filename,
                 bool remove_input, const std::string& format_options) {
  const std::size_t budget = sort_buffer_bytes();

  // Spilled runs, in the order they were produced. Their names are derived from
  // the unsorted temp path, which already lives in --temp-dir.
  std::vector<std::string> runs;
  struct RunCleanup {
    std::vector<std::string>* paths;
    ~RunCleanup() { for (const auto& p : *paths) std::remove(p.c_str()); }
  } run_cleanup{&runs};

  std::vector<BamRecord> records;
  sam_hdr_t* hdr_copy = nullptr;
  {
    AlignmentReader reader(unsorted_path, reference_filename);
    hdr_copy = sam_hdr_dup(reader.header());
    if (hdr_copy == nullptr) throw std::runtime_error("sam_hdr_dup failed");
    std::unique_ptr<sam_hdr_t, void (*)(sam_hdr_t*)> guard(hdr_copy, sam_hdr_destroy);

    // A spilled run is written in the SAME format as the final output, so the
    // merge reads them back through the same code path.
    auto spill = [&]() {
      std::stable_sort(records.begin(), records.end(), record_less);
      const std::string path =
          unsorted_path + ".run" + std::to_string(runs.size()) + ".bam";
      {
        AlignmentWriter w(path, "bam", hdr_copy, reference_filename, "");
        for (const BamRecord& r : records) w.write(r);
        w.close();
      }
      runs.push_back(path);
      records.clear();
      records.shrink_to_fit();
    };

    BamRecord rec;
    std::size_t held = 0;
    while (reader.next(rec)) {
      held += record_bytes(rec);
      records.push_back(rec.clone());
      if (held >= budget) { spill(); held = 0; }
    }
    guard.release();   // ownership passes to hdr_owner below
  }
  // AlignmentWriter's constructor and every write() can throw (unopenable
  // destination, bad reference, disk full, broken pipe). The duplicated header
  // used to leak on all of those, and the unsorted temp file was left behind
  // because the std::remove at the end was skipped. Ownership makes both
  // unconditional.
  std::unique_ptr<sam_hdr_t, void (*)(sam_hdr_t*)> hdr_owner(hdr_copy, sam_hdr_destroy);
  struct TempCleanup {
    const std::string* path;
    bool remove;
    ~TempCleanup() { if (remove) std::remove(path->c_str()); }
  } temp_cleanup{&unsorted_path, remove_input};

  // MEASURED: samtools sort is a STABLE sort by (tid, pos, is_reverse) with
  // unmapped (tid == -1) LAST — see record_less for how the strand term was
  // established and why the earlier "(tid, pos)" reading held for so long.
  // std::stable_sort, not std::sort — 370 tie groups in the real corpus would
  // otherwise be free to permute.
  //
  // When nothing spilled this is the whole sort, and the behaviour (and speed)
  // is exactly what it was before the external merge sort. The merge below runs only when the
  // input exceeded the buffer.
  std::stable_sort(records.begin(), records.end(), record_less);

  // The header's @HD SO: must say `coordinate` after sorting, as samtools sets.
  //
  // MEASURED: `sam_hdr_update_hd` only UPDATES an existing @HD — when the input
  // has none it is a silent no-op, and samtools instead CREATES the line at
  // VN:1.6. `chr19_1mb.bam` (the input to all three CRAM fixtures) is exactly
  // that case, while every `group` fixture's input already carries an @HD, so
  // the gap is invisible to a green group run. An existing VN is preserved:
  // chr19.bam's `VN:1.0` survives `group_directional`, which compares the header.
  {
    kstring_t hd = KS_INITIALIZE;
    if (sam_hdr_find_hd(hdr_copy, &hd) == 0) {
      sam_hdr_update_hd(hdr_copy, "SO", "coordinate");
    } else {
      sam_hdr_add_line(hdr_copy, "HD", "VN", "1.6", "SO", "coordinate", NULL);
    }
    ks_free(&hd);
  }
  try {
    // Upstream builds samtools' -O as `format + "," + format_options`, so the
    // options land on the SORTED output too, not only the unsorted temp.
    AlignmentWriter writer(sorted_path, format, hdr_copy, reference_filename, format_options);
    if (runs.empty()) {
      for (const BamRecord& r : records) writer.write(r);
    } else {
      // The k-way merge. The last partial batch is spilled first so every
      // record lives in exactly one run and the merge is uniform.
      {
        std::stable_sort(records.begin(), records.end(), record_less);
        const std::string path =
            unsorted_path + ".run" + std::to_string(runs.size()) + ".bam";
        AlignmentWriter w(path, "bam", hdr_copy, reference_filename, "");
        for (const BamRecord& r : records) w.write(r);
        w.close();
        runs.push_back(path);
      }
      records.clear();
      records.shrink_to_fit();

      std::vector<std::unique_ptr<AlignmentReader>> in;
      std::vector<BamRecord> head(runs.size());
      std::vector<bool> live(runs.size(), false);
      for (std::size_t i = 0; i < runs.size(); ++i) {
        in.push_back(std::make_unique<AlignmentReader>(runs[i], reference_filename));
        live[i] = in[i]->next(head[i]);
      }
      // STABILITY: runs are produced in input order and each is stable
      // internally, so among equal keys the lowest run index must win. Ties
      // broken any other way would permute the 370 tie groups the corpus has.
      for (;;) {
        std::size_t best = runs.size();
        for (std::size_t i = 0; i < runs.size(); ++i) {
          if (!live[i]) continue;
          if (best == runs.size() || record_less(head[i], head[best])) best = i;
        }
        if (best == runs.size()) break;
        writer.write(head[best]);
        live[best] = in[best]->next(head[best]);
      }
    }
    writer.close();   // explicit: the flush is where a full disk shows up
  } catch (const std::exception& e) {
    // Upstream wraps the sort in
    //   try: pysam.sort(*params)
    //   except pysam.SamtoolsError as e:
    //       error("Sorting output file failed.\n\nSort command was:\n " + ...)
    // and U.error writes an ERROR record to the LOG, puts the fixed string
    // "UMI-tools failed with an error. Check the log file" on stderr, and exits
    // 1. The port let the failure escape as a bare runtime_error to main's
    // generic handler: no ERROR line in the log and different stderr text.
    //
    // os.unlink(infile) at Utilities.py:1776 is NOT reached when error() exits,
    // so upstream LEAVES the unsorted temp file behind on a failed sort. The
    // TempCleanup destructor deleted it on every path, so the one artifact a
    // user could have recovered their output from was the one thing removed.
    temp_cleanup.remove = false;
    error_exit("Sorting output file failed.\n\nSort command was:\n sort -o " +
               sorted_path + " -O " + format +
               (format_options.empty() ? std::string() : "," + format_options) +
               " " + unsorted_path + "\n\nError was:\n " + e.what());
  }
  // hdr_owner and temp_cleanup do the rest, on every exit path.
}

}  // namespace umi_tools
