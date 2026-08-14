// fastq.hpp — umi_methods' FASTQ record and iterators.
//
// Upstream's `fastqIterate` is hand-rolled (taken from CGAT.Fastq), so the port
// matches its exact structure AND its exact error strings, which are part of the
// observable contract:
//
//     "parsing error: expected '@' in line %s"
//     "parsing error: expected '+' in line %s"
//     "incomplete entry for %s"
//     "read suffix must be /1 or /2. Observed: %s"
//
// Note that the first three go through `U.error()`, which LOGS AND EXITS rather
// than raising — so a malformed FASTQ terminates the process, it does not throw.
// The fourth is a real ValueError.
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "umi_tools/io.hpp"

namespace umi_tools {

/// umi_methods.Record. `__str__` is "@%s\n%s\n+\n%s" — note the '+' line is
/// always bare, so any description after '+' in the input is DROPPED on output.
struct Record {
  std::string identifier;   // without the leading '@'
  std::string seq;
  std::string quals;

  std::string str() const {
    return "@" + identifier + "\n" + seq + "\n+\n" + quals;
  }
};

/// umi_methods.fastqIterate(infile, remove_suffix=False) — a generator.
///
/// Ported as a pull iterator rather than a callback because `extract` advances
/// two of them in lockstep (`joinedFastqIterate`), which a callback cannot
/// express. Streaming either way: one record is held at a time, never the file.
class FastqIterator {
 public:
  FastqIterator(LineReader& reader, bool remove_suffix);

  /// Returns nullopt at EOF.
  std::optional<Record> next();

 private:
  LineReader& reader_;
  bool remove_suffix_;
};

/// umi_methods.joinedFastqIterate(it1, it2, strict=True).
///
/// `strict=False` (from --reconcile-pairs) advances read2 until its id matches
/// read1's, which is how a pre-filtered read1 file is handled. `strict=True`
/// raises ValueError on the first mismatch:
///     "\nRead pairs do not match\n%s != %s"
///
/// The id compared is `identifier.split(' ')[0]` — the field before the first
/// space, NOT the whole identifier.
void joined_fastq_iterate(FastqIterator& it1, FastqIterator& it2, bool strict,
                          const std::function<bool(Record&, Record&)>& on_pair);

/// umi_methods.Record.guessFormat — returns every quality encoding whose range
/// contains all observed quality characters. RANGES is
/// {'phred33': (33,77), 'solexa': (59,106), 'phred64': (64,106)} and the test is
/// `mi >= m1 and ma < m2` — inclusive low, EXCLUSIVE high.
std::vector<std::string> guess_format(std::string_view quals);

}  // namespace umi_tools
