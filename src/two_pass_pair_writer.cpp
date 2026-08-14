// sam_methods.TwoPassPairWriter — dedup --paired.
//
// Read1s are written as they are deduplicated; their MATES are found by
// re-scanning the input. That needs a SECOND, INDEPENDENT open of the same file
// (06_design.md decided this once: a second AlignmentReader, never a copy of the
// first — the main pass is mid-iteration and must not be disturbed).
//
// The scan happens per contig, at each contig BOUNDARY, so memory holds only the
// read1 keys of the contig in flight. close() then does one final until_eof pass
// for anything still outstanding (mates on a later contig than their read1).
#include "umi_tools/sam_methods.hpp"

#include "umi_tools/logging.hpp"

#include <stdexcept>

namespace umi_tools {

TwoPassPairWriter::TwoPassPairWriter(const std::string& path,
                                     const std::string& reference_filename,
                                     AlignmentWriter& outfile)
    : mate_file_(path, reference_filename), reference_(reference_filename),
      out_(outfile) {}

void TwoPassPairWriter::write(const BamRecord& read, bool unmapped) {
  // `if unmapped or read.mate_is_unmapped:` — no mate to go looking for.
  if (unmapped || read.mate_is_unmapped()) {
    out_.write(read);
    return;
  }

  const std::string ref = contig_name(read.tid());
  if (!chrom_ || *chrom_ != ref) {
    write_mates();
    chrom_ = ref;
  }

  // key = (query_name, next_reference_name, next_reference_start)
  read1s_.emplace(std::string(read.query_name()), contig_name(read.mate_tid()),
                  read.next_reference_start());
  out_.write(read);
}

std::string TwoPassPairWriter::contig_name(std::int32_t tid) const {
  // pysam yields None for tid == -1; "" stands in for it. Both sides of every
  // comparison use this same mapping, so None-vs-None still matches.
  if (tid < 0) return std::string();
  return std::string(mate_file_.target_name(tid));
}

void TwoPassPairWriter::write_mates() {
  if (chrom_) {
    Log::instance().debug("Dumping " + std::to_string(read1s_.size()) +
                          " mates for contig " + *chrom_);
  }
  // The first call has chrom_ unset and read1s_ empty, and upstream still runs a
  // full fetch(reference=None) that can match nothing. Skipping it looked
  // observably identical — it is NOT: the trailing "0 mates remaining" DEBUG
  // line disappears with it, which `-v2` compares. Measured, then reverted.

  mate_file_.set_region(chrom_ ? *chrom_ : std::string());
  BamRecord read;
  while (mate_file_.next(read)) {
    // `if any((is_unmapped, mate_is_unmapped, is_read1)): continue`
    if (read.is_unmapped() || read.mate_is_unmapped() || read.is_read1()) continue;
    MateKey key{std::string(read.query_name()), contig_name(read.tid()), read.pos()};
    auto it = read1s_.find(key);
    if (it != read1s_.end()) {
      out_.write(read);
      read1s_.erase(it);
    }
  }
  Log::instance().debug(std::to_string(read1s_.size()) + " mates remaining");
}

void TwoPassPairWriter::close() {
  if (closed_) return;
  closed_ = true;
  write_mates();
  Log::instance().info("Searching for mates for " + std::to_string(read1s_.size()) +
                       " unmatched alignments");

  // `fetch(until_eof=True, multiple_iterators=True)` — multiple_iterators makes
  // pysam OPEN THE FILE AGAIN rather than rewind the existing handle, so a fresh
  // reader is the faithful translation. It also sidesteps the ownership question
  // a rewind raises: reopening under a live index double-frees it (caught by
  // ASan when this was first written as an in-place rewind).
  AlignmentReader sweep(mate_file_.path(), reference_);
  BamRecord read;
  while (sweep.next(read)) {
    if (read.is_unmapped() || read.mate_is_unmapped() || read.is_read1()) continue;
    MateKey key{std::string(read.query_name()), contig_name(read.tid()), read.pos()};
    auto it = read1s_.find(key);
    if (it != read1s_.end()) {
      out_.write(read);
      read1s_.erase(it);
    }
  }
  Log::instance().info(std::to_string(read1s_.size()) + " mates never found");
  out_.close();
}

}  // namespace umi_tools
