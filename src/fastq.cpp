#include "umi_tools/fastq.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

#include "umi_tools/logging.hpp"

namespace umi_tools {
namespace {

// Python: line.split(' ')[0]
std::string_view first_field(std::string_view identifier) {
  const std::size_t sp = identifier.find(' ');
  return sp == std::string_view::npos ? identifier : identifier.substr(0, sp);
}

// umi_methods.removeReadIDSuffix — strips a trailing /1 or /2 from the FIRST
// space-delimited field, raising if it is not there.
std::string remove_read_id_suffix(const std::string& line) {
  std::vector<std::string> components;
  std::size_t start = 0;
  while (true) {
    const std::size_t sp = line.find(' ', start);
    if (sp == std::string::npos) {
      components.push_back(line.substr(start));
      break;
    }
    components.push_back(line.substr(start, sp - start));
    start = sp + 1;
  }
  std::string& read_id = components[0];
  const std::string suffix = read_id.size() >= 2 ? read_id.substr(read_id.size() - 2) : read_id;
  if (suffix != "/1" && suffix != "/2")
    throw std::invalid_argument("read suffix must be /1 or /2. Observed: " + suffix);
  read_id.erase(read_id.size() - 2);

  std::string out;
  for (std::size_t i = 0; i < components.size(); ++i) {
    if (i) out += ' ';
    out += components[i];
  }
  return out;
}

// Python's str.rstrip() / str.strip() with no argument: strips WHITESPACE.
std::string rstrip(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}
std::string strip(std::string s) {
  s = rstrip(std::move(s));
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  return s.substr(b);
}

}  // namespace

FastqIterator::FastqIterator(LineReader& reader, bool remove_suffix)
    : reader_(reader), remove_suffix_(remove_suffix) {}

std::optional<Record> FastqIterator::next() {
  // Python:
  //   line1 = infile.readline().strip()
  //   if not line1: break
  //   if not line1.startswith('@'): U.error("parsing error: expected '@' in line %s")
  //   line2 = readline().rstrip(); line3 = readline().rstrip(); line4 = readline().rstrip()
  //   if not line3.startswith('+'): U.error("parsing error: expected '+' in line %s")
  //   if not line4: U.error("incomplete entry for %s")
  //   yield Record(line1[1:], line2, line4)
  //
  // Note the asymmetry, reproduced: line1 is .strip()ped (both ends) while
  // lines 2-4 are only .rstrip()ped, so leading whitespace on a sequence line
  // would be retained.
  std::string raw;
  if (!reader_.next(raw)) return std::nullopt;
  std::string line1 = strip(raw);
  if (line1.empty()) return std::nullopt;
  if (line1.front() != '@') error_exit("parsing error: expected '@' in line " + line1);

  std::string line2, line3, line4;
  if (!reader_.next(line2)) line2.clear();
  line2 = rstrip(line2);
  if (!reader_.next(line3)) line3.clear();
  line3 = rstrip(line3);
  if (line3.empty() || line3.front() != '+')
    error_exit("parsing error: expected '+' in line " + line3);
  if (!reader_.next(line4)) line4.clear();
  line4 = rstrip(line4);
  if (line4.empty()) error_exit("incomplete entry for " + line1);

  if (remove_suffix_) line1 = remove_read_id_suffix(line1);

  Record r;
  r.identifier = line1.substr(1);  // drop the '@'
  r.seq = line2;
  r.quals = line4;
  return r;
}

void joined_fastq_iterate(FastqIterator& it1, FastqIterator& it2, bool strict,
                          const std::function<bool(Record&, Record&)>& on_pair) {
  while (true) {
    auto read1 = it1.next();
    if (!read1) return;
    auto read2 = it2.next();
    if (!read2)
      // `read2 = next(fastq_iterator2)` (umi_methods.py:148). joinedFastqIterate
      // is a GENERATOR, so a bare StopIteration escaping its frame becomes
      // `RuntimeError: generator raised StopIteration` under PEP 479 —
      // mandatory since CPython 3.7, and the oracle runs 3.11. It is uncaught
      // at extract.py:479, so the run dies with exit 1.
      //
      // The comment that used to sit here claimed the opposite as
      // MEASURED ("the loop simply ending for a truncated file"); measuring it
      // gives `RuntimeError: generator raised StopIteration` and rc 1, where the
      // port exited 0 with a silently truncated FASTQ pair.
      throw std::runtime_error("generator raised StopIteration");

    std::string id1(first_field(read1->identifier));
    std::string id2(first_field(read2->identifier));

    if (!strict) {
      while (id2 != id1) {
        auto nxt = it2.next();
        // The strict=False advance loop (umi_methods.py:155) lets StopIteration
        // escape the same way.
        if (!nxt) throw std::runtime_error("generator raised StopIteration");
        read2 = nxt;
        id2 = std::string(first_field(read2->identifier));
      }
    }
    if (id2 != id1)
      throw std::invalid_argument("\nRead pairs do not match\n" + id1 + " != " + id2);

    if (!on_pair(*read1, *read2)) return;
  }
}

std::vector<std::string> guess_format(std::string_view quals) {
  // `mi, ma = min(c), max(c)` — min([]) raises rather than returning
  // "no format matched".
  if (quals.empty()) raise_value_error("min() arg is an empty sequence");
  int mi = 255, ma = 0;
  for (char c : quals) {
    const int v = static_cast<unsigned char>(c);
    mi = std::min(mi, v);
    ma = std::max(ma, v);
  }
  // RANGES iteration order in Python is the dict's insertion order:
  // phred33, solexa, phred64.
  const struct { const char* name; int lo, hi; } kRanges[] = {
      {"phred33", 33, 77}, {"solexa", 59, 106}, {"phred64", 64, 106}};
  std::vector<std::string> out;
  for (const auto& r : kRanges)
    if (mi >= r.lo && ma < r.hi) out.emplace_back(r.name);
  return out;
}

}  // namespace umi_tools
