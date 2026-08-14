#include "umi_tools/extract_methods.hpp"

#include <algorithm>
#include <stdexcept>
#include <tuple>

#include "umi_tools/logging.hpp"

namespace umi_tools {
namespace {

// umi_methods.RANGES
struct Range { const char* name; int lo, hi; };
constexpr Range kRanges[] = {{"phred33", 33, 77}, {"solexa", 59, 106}, {"phred64", 64, 106}};

// `"".join([bc1[x] for x in self.umi_bases])` — a PLAIN str index, which raises
// IndexError past the end. This used to skip such an index silently.
//
// Measured: `__call__` guards `len(read.seq) >= len(pattern)` but never
// read.quals, and bc1_quals is `quals[:pattern_length]`, so a record whose
// quality line is shorter than the pattern reaches here with a short string.
// `extract -p NNNNNNNN` on a read with a 5-character quality line gives
// `IndexError: string index out of range` and rc 1 upstream, where the port
// exited 0 and emitted a read with a silently truncated UMI-quality string —
// which then also changes --quality-filter-threshold and --quality-filter-mask
// outcomes on that read.
std::string join_by_indices(std::string_view s, const std::vector<std::int64_t>& idx) {
  std::string out;
  out.reserve(idx.size());
  for (std::int64_t i : idx) {
    if (i < 0 || i >= static_cast<std::int64_t>(s.size()))
      throw std::out_of_range("string index out of range");
    out += s[static_cast<std::size_t>(i)];
  }
  return out;
}

}  // namespace

std::optional<int> quality_offset(std::string_view encoding) {
  for (const auto& r : kRanges)
    if (encoding == r.name) return r.lo;
  return std::nullopt;
}

std::string add_barcodes_to_identifier(const Record& read, std::string_view umi,
                                       std::string_view cell,
                                       std::string_view umi_separator) {
  // Python: read_id = read.identifier.split(" ")
  //         read_id[0] += sep + UMI            (cell == "")
  //         read_id[0] += sep + cell + sep + UMI
  //         return " ".join(read_id)
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t sp = read.identifier.find(' ', start);
    if (sp == std::string::npos) {
      parts.push_back(read.identifier.substr(start));
      break;
    }
    parts.push_back(read.identifier.substr(start, sp - start));
    start = sp + 1;
  }
  if (cell.empty())
    parts[0] += std::string(umi_separator) + std::string(umi);
  else
    parts[0] += std::string(umi_separator) + std::string(cell) +
                std::string(umi_separator) + std::string(umi);

  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += ' ';
    out += parts[i];
  }
  return out;
}

SeqQuals extract_seq_and_quals(std::string_view seq, std::string_view quals,
                               const std::set<std::int64_t>& umi_bases,
                               const std::set<std::int64_t>& cell_bases,
                               const std::set<std::int64_t>& discard_bases,
                               bool retain_umi) {
  // Python zips seq and quals, so it stops at the SHORTER of the two — a read
  // whose quality line is short silently truncates rather than erroring.
  SeqQuals out;
  const std::size_t n = std::min(seq.size(), quals.size());
  for (std::size_t ix = 0; ix < n; ++ix) {
    const auto i = static_cast<std::int64_t>(ix);
    const char base = seq[ix];
    const char qual = quals[ix];
    if (!discard_bases.count(i) && !cell_bases.count(i)) {
      if (retain_umi) {
        out.new_quals += qual;
        out.new_seq += base;
        out.umi_quals += qual;
      } else if (!umi_bases.count(i)) {
        out.new_quals += qual;
        out.new_seq += base;
      } else {
        out.umi_quals += qual;
      }
    } else if (cell_bases.count(i)) {
      out.cell_quals += qual;
    }
  }
  return out;
}

std::vector<bool> get_below_threshold(std::string_view umi_quals,
                                      std::string_view quality_encoding,
                                      std::int64_t quality_filter_threshold) {
  const auto offset = quality_offset(quality_encoding);
  if (!offset)
    throw std::invalid_argument("unknown quality encoding: " + std::string(quality_encoding));
  std::vector<bool> out;
  out.reserve(umi_quals.size());
  for (char c : umi_quals)
    out.push_back((static_cast<int>(static_cast<unsigned char>(c)) - *offset) <
                  quality_filter_threshold);
  return out;
}

bool umi_below_threshold(std::string_view umi_quals, std::string_view quality_encoding,
                         std::int64_t quality_filter_threshold) {
  for (bool b : get_below_threshold(umi_quals, quality_encoding, quality_filter_threshold))
    if (b) return true;   // Python: any(below_threshold)
  return false;
}

std::string mask_umi(std::string_view umi, std::string_view umi_quals,
                     std::string_view quality_encoding,
                     std::int64_t quality_filter_threshold) {
  const auto below = get_below_threshold(umi_quals, quality_encoding, quality_filter_threshold);
  // Python zips umi with below_threshold, so it stops at the shorter.
  std::string out;
  const std::size_t n = std::min(umi.size(), below.size());
  for (std::size_t i = 0; i < n; ++i) out += below[i] ? 'N' : umi[i];
  return out;
}

ExtractedBarcodes extract_barcodes(const Record& read, const MatchResult& match,
                                   bool extract_umi, bool extract_cell, bool discard,
                                   bool retain_umi) {
  if (!extract_cell && !extract_umi)
    error_exit("must set either extract_cell and/or extract_umi to true");

  ExtractedBarcodes out;
  std::set<std::int64_t> cell_bases, umi_bases, discard_bases;

  // Python: for k in sorted(list(groupdict)) — group NAMES in lexicographic
  // order. std::map is already sorted by key, which is the same order.
  for (const auto& [name, text] : match.groups) {
    const Span span = match.spans.at(name);
    auto add_range = [&](std::set<std::int64_t>& s) {
      for (std::int64_t i = span.start; i < span.stop; ++i) s.insert(i);
    };
    // Python iterates `sorted(list(groupdict))`, and groupdict() holds
    // EVERY named group in the pattern — value None for one that did not
    // participate — so `cell_barcode += None` / `umi += None` raises
    // `TypeError: can only concatenate str (not "NoneType") to str`. Only the
    // discard_ branch is safe, because it reads span() and gets (-1, -1).
    //
    // MEASURED: `(?P<umi_1>AAA)?(?P<umi_2>.{6})` on a read starting `CCC` gives
    // that TypeError and rc 1 upstream; the port stored the group with
    // start == -1, rendered it as "", contributed nothing and exited 0 with a
    // short UMI.
    const bool participated = span.start >= 0;
    if (extract_cell && name.rfind("cell_", 0) == 0) {
      if (!participated)
        throw std::invalid_argument(
            "can only concatenate str (not \"NoneType\") to str");
      out.cell_barcode += text;
      add_range(cell_bases);
    } else if (extract_umi && name.rfind("umi_", 0) == 0) {
      if (!participated)
        throw std::invalid_argument(
            "can only concatenate str (not \"NoneType\") to str");
      out.umi += text;
      add_range(umi_bases);
    } else if (discard && name.rfind("discard_", 0) == 0) {
      add_range(discard_bases);
    }
  }

  const SeqQuals sq =
      extract_seq_and_quals(read.seq, read.quals, umi_bases, cell_bases, discard_bases,
                            retain_umi);
  out.new_seq = sq.new_seq;
  out.new_quals = sq.new_quals;
  out.umi_quals = sq.umi_quals;
  // cell_barcode_quals is left EMPTY: upstream computes cell_quals and then
  // returns the never-assigned cell_barcode_quals (bug D7#8), so cell barcode
  // qualities are never emitted. Reproduced deliberately.
  return out;
}

// ---------------------------------------------------------------------------
// ExtractFilterAndUpdate
// ---------------------------------------------------------------------------
ExtractFilterAndUpdate::ExtractFilterAndUpdate(const ExtractFilterOptions& options,
                                               const std::string& pattern,
                                               const std::string& pattern2)
    : opt_(options) {
  auto build_string_pattern = [](const std::string& p, StringPattern& sp,
                                 const char* which) {
    if (p.empty()) return;
    // Python: if len(pattern.replace("N","").replace("X","").replace("C","")) > 0
    for (char c : p)
      if (c != 'N' && c != 'X' && c != 'C')
        throw std::invalid_argument(std::string("barcode ") + which + " (" + p +
                                    ") should only contain N/X/C characters");
    sp.present = true;
    sp.length = static_cast<std::int64_t>(p.size());
    sp.text = p;   // the too-short-read error interpolates the pattern
    for (std::int64_t i = 0; i < sp.length; ++i) {
      const char c = p[static_cast<std::size_t>(i)];
      if (c == 'N') sp.umi_bases.push_back(i);
      else if (c == 'X') sp.bc_bases.push_back(i);
      else if (c == 'C') sp.cell_bases.push_back(i);
    }
  };

  if (opt_.method == ExtractMethod::String) {
    build_string_pattern(pattern, sp1_, "pattern");
    build_string_pattern(pattern2, sp2_, "pattern2");
  } else {
    if (!pattern.empty()) rx1_.emplace(pattern);
    if (!pattern2.empty()) rx2_.emplace(pattern2);
  }
}

void ExtractFilterAndUpdate::set_cell_whitelist(std::set<std::string> w) {
  cell_whitelist_ = std::move(w);
  has_cell_whitelist_ = true;
}
void ExtractFilterAndUpdate::set_false_to_true_map(std::map<std::string, std::string> m) {
  false_to_true_map_ = std::move(m);
  has_false_to_true_ = true;
}
void ExtractFilterAndUpdate::set_cell_blacklist(std::set<std::string> b) {
  cell_blacklist_ = std::move(b);
  has_blacklist_ = true;
}
void ExtractFilterAndUpdate::set_umi_whitelist(std::set<std::string> w) {
  umi_whitelist_ = std::move(w);
  has_umi_whitelist_ = true;
}
void ExtractFilterAndUpdate::set_umi_false_to_true_map(
    std::map<std::string, std::optional<std::string>> m) {
  umi_false_to_true_map_ = std::move(m);
  has_umi_map_ = true;
}

std::optional<std::tuple<std::string, std::string, std::string, std::string, std::string,
                         std::string, std::string>>
ExtractFilterAndUpdate::get_barcodes_string(const Record& read1, const Record* read2) const {
  // _getBarcodesString: split the fixed-length prefix (or suffix for 3'), then
  // pick out the N/X/C positions by index.
  std::string cell, umi, umi_quals, new_seq, new_quals, new_seq2, new_quals2;

  auto split = [](std::string_view s, std::int64_t length, bool prime3) {
    // 5': (s[:len], s[len:])   3': (s[-len:], s[:-len])
    // Python's s[-0:] is the WHOLE string, so length 0 at the 3' end returns
    // the entire sequence as the barcode. Reproduced rather than special-cased.
    if (!prime3) {
      const auto n = static_cast<std::size_t>(std::min<std::int64_t>(length, s.size()));
      return std::pair<std::string, std::string>(std::string(s.substr(0, n)),
                                                 std::string(s.substr(n)));
    }
    if (length == 0)
      return std::pair<std::string, std::string>(std::string(s), std::string());
    const auto n = static_cast<std::size_t>(std::min<std::int64_t>(length, s.size()));
    return std::pair<std::string, std::string>(std::string(s.substr(s.size() - n)),
                                               std::string(s.substr(0, s.size() - n)));
  };
  auto joiner = [&](const std::string& sequence, const std::string& sample) {
    return opt_.prime3 ? sequence + sample : sample + sequence;
  };

  if (sp1_.present) {
    const auto [bc1, sequence1] = split(read1.seq, sp1_.length, opt_.prime3);
    const auto [bcq1, seqq1] = split(read1.quals, sp1_.length, opt_.prime3);
    umi_quals = join_by_indices(bcq1, sp1_.umi_bases);
    umi = join_by_indices(bc1, sp1_.umi_bases);
    cell = join_by_indices(bc1, sp1_.cell_bases);
    const std::string sample1 = join_by_indices(bc1, sp1_.bc_bases);
    const std::string sample_qual1 = join_by_indices(bcq1, sp1_.bc_bases);
    new_seq = joiner(sequence1, sample1);
    new_quals = joiner(seqq1, sample_qual1);
  }

  if (sp2_.present) {
    if (read2 == nullptr) return std::nullopt;
    const auto [bc2, sequence2] = split(read2->seq, sp2_.length, opt_.prime3);
    const auto [bcq2, seqq2] = split(read2->quals, sp2_.length, opt_.prime3);
    umi_quals += join_by_indices(bcq2, sp2_.umi_bases);
    umi += join_by_indices(bc2, sp2_.umi_bases);
    cell += join_by_indices(bc2, sp2_.cell_bases);
    const std::string sample2 = join_by_indices(bc2, sp2_.bc_bases);
    const std::string sample_qual2 = join_by_indices(bcq2, sp2_.bc_bases);
    new_seq2 = joiner(sequence2, sample2);
    new_quals2 = joiner(seqq2, sample_qual2);
  }

  return std::make_tuple(cell, umi, umi_quals, new_seq, new_quals, new_seq2, new_quals2);
}

std::optional<std::tuple<std::string, std::string, std::string, std::string, std::string,
                         std::string, std::string>>
ExtractFilterAndUpdate::get_barcodes_regex(const Record& read1, const Record* read2) {
  std::optional<MatchResult> match, match2;

  if (rx1_) {
    match = rx1_->match(read1.seq);
    if (!match) {
      bump("regex does not match read1");
      if (!opt_.either_read) return std::nullopt;
    } else {
      bump("regex matches read1");
    }
  }
  if (read2 != nullptr && rx2_) {
    match2 = rx2_->match(read2->seq);
    if (!match2) {
      bump("regex does not match read2");
      if (opt_.either_read) {
        if (!match) return std::nullopt;
      } else {
        return std::nullopt;
      }
    } else {
      bump("regex matches read2");
    }
  }

  if (opt_.either_read) {
    if (match && match2 && opt_.either_read_resolve == "discard") {
      bump("regex matches both. discarded");
      return std::nullopt;
    }
    ExtractedBarcodes b1, b2;
    if (match)
      b1 = extract_barcodes(read1, *match, /*extract_umi=*/true, opt_.extract_cell,
                            /*discard=*/true, opt_.retain_umi);
    if (match2)
      b2 = extract_barcodes(*read2, *match2, /*extract_umi=*/true, opt_.extract_cell,
                            /*discard=*/true, opt_.retain_umi);

    if (match && match2) {
      if (opt_.either_read_resolve != "quality")
        throw std::invalid_argument("unexpected value for either_read_resolve");
      const auto offset = quality_offset(opt_.quality_encoding);
      if (!offset) throw std::invalid_argument("unknown quality encoding");
      // `min([x - RANGES[enc][0] for x in map(ord, umi_quals)])` raises
      // ValueError on an EMPTY umi_quals, and empty is reachable:
      // validate_extract_options requires a umi_ group in only ONE of the two
      // patterns, so a pattern2 of `(?P<discard_1>.*)` yields b2.umi_quals == "".
      // Seeding a 1<<30 sentinel instead made `read1_min >= read2_min` silently
      // resolve in favour of whichever read HAS a UMI. MEASURED with
      // --either-read --either-read-resolve=quality --quality-encoding=phred33:
      // oracle rc=1 "ValueError: min() arg is an empty sequence", port rc=0
      // emitting 10 reads.
      //
      // The earlier probe of this missed it by omitting --quality-encoding,
      // which leaves it None and makes upstream fail at `RANGES[None]` with
      // KeyError: None BEFORE reaching the min() — a different error that also
      // gave rc=1 on both sides and so looked like agreement.
      auto min_qual = [&](const std::string& q) {
        if (q.empty()) throw std::invalid_argument("min() arg is an empty sequence");
        int m = 1 << 30;
        for (char c : q)
          m = std::min(m, static_cast<int>(static_cast<unsigned char>(c)) - *offset);
        return m;
      };
      if (min_qual(b1.umi_quals) >= min_qual(b2.umi_quals)) {
        bump("regex matches both. read1 used");
        return std::make_tuple(b1.cell_barcode, b1.umi, b1.umi_quals, b1.new_seq,
                               b1.new_quals, b2.new_seq, b2.new_quals);
      }
      bump("regex matches both. read2 used");
      return std::make_tuple(b2.cell_barcode, b2.umi, b2.umi_quals, b1.new_seq,
                             b1.new_quals, b2.new_seq, b2.new_quals);
    }
    if (match) {
      bump("regex only matches read1");
      return std::make_tuple(b1.cell_barcode, b1.umi, b1.umi_quals, b1.new_seq,
                             b1.new_quals, std::string(), std::string());
    }
    bump("regex only matches read2");
    return std::make_tuple(b2.cell_barcode, b2.umi, b2.umi_quals, std::string(),
                           std::string(), b2.new_seq, b2.new_quals);
  }

  // Non-either_read path.
  std::string cell, umi, umi_quals, new_seq, new_quals, new_seq2, new_quals2;
  if (rx1_) {
    const ExtractedBarcodes b =
        extract_barcodes(read1, *match, true, opt_.extract_cell, true, opt_.retain_umi);
    cell = b.cell_barcode;
    umi = b.umi;
    umi_quals = b.umi_quals;
    new_seq = b.new_seq;
    new_quals = b.new_quals;
  }
  if (read2 != nullptr && rx2_) {
    const ExtractedBarcodes b2 =
        extract_barcodes(*read2, *match2, true, opt_.extract_cell, true, opt_.retain_umi);
    cell += b2.cell_barcode;
    umi += b2.umi;
    umi_quals += b2.umi_quals;
    new_seq2 = b2.new_seq;
    new_quals2 = b2.new_quals;
  }
  return std::make_tuple(cell, umi, umi_quals, new_seq, new_quals, new_seq2, new_quals2);
}

std::optional<std::string> ExtractFilterAndUpdate::filter_cell_barcode_fn(std::string cell) {
  if (has_blacklist_ && cell_blacklist_.count(cell)) {
    bump("Cell barcode in blacklist");
    return std::nullopt;
  }
  // `if cell not in self.cell_whitelist` where cell_whitelist is None
  // raises `TypeError: argument of type 'NoneType' is not iterable`. extract.py
  // assigns it ONLY under `if options.whitelist:`, so
  // `--filter-cell-barcode` with no `--whitelist` aborts upstream on the FIRST
  // read. has_cell_whitelist_ existed to model exactly that and was assigned in
  // set_cell_whitelist but never read, so an unset whitelist behaved as an EMPTY
  // one: every read missed, was counted under "Filtered cell barcode" and
  // dropped. MEASURED: oracle rc=1 TypeError, port rc=0 with an empty FASTQ —
  // a silent total data loss where upstream fails loudly.
  if (!has_cell_whitelist_)
    throw std::invalid_argument("argument of type 'NoneType' is not iterable");
  if (!cell_whitelist_.count(cell)) {
    if (has_false_to_true_ && !false_to_true_map_.empty()) {
      auto it = false_to_true_map_.find(cell);
      if (it != false_to_true_map_.end()) {
        cell = it->second;
        bump("False cell barcode. Error-corrected");
      } else {
        bump("Filtered cell barcode. Not correctable");
        return std::nullopt;
      }
    } else {
      bump("Filtered cell barcode");
      return std::nullopt;
    }
  }
  if (has_blacklist_ && cell_blacklist_.count(cell)) {
    bump("Cell barcode corrected to barcode blacklist");
    return std::nullopt;
  }
  return cell;
}

std::optional<std::string> ExtractFilterAndUpdate::filter_umi_barcode_fn(
    const std::string& umi) {
  if (!umi_whitelist_.count(umi)) {
    std::optional<std::string> corrected;
    if (has_umi_map_ && !umi_false_to_true_map_.empty()) {
      auto it = umi_false_to_true_map_.find(umi);
      if (it != umi_false_to_true_map_.end()) {
        corrected = it->second;
        if (corrected) {
          bump("False UMI barcode. Error-corrected");
          umi_whitelist_counts_[*corrected].second += 1;  // "error"
        } else {
          bump("False UMI barcode. Not correctable - within threshold to more "
               "than one whitelisted UMI");
        }
      } else {
        bump("False UMI barcode. Not correctable - not within threshold to "
             "whitelisted UMI");
      }
    } else {
      bump("Filtered umi barcode");
    }
    return corrected;
  }
  umi_whitelist_counts_[umi].first += 1;  // "no_error"
  return umi;
}

std::optional<ExtractResult> ExtractFilterAndUpdate::operator()(Record read1,
                                                                const Record* read2) {
  // The string method checks length up front so the error is sensible rather
  // than an out-of-range slice (upstream issue #424).
  if (opt_.method == ExtractMethod::String) {
    if (sp1_.present && static_cast<std::int64_t>(read1.seq.size()) < sp1_.length)
      throw std::invalid_argument("Read sequence: " + read1.seq +
                                  " is shorter than pattern: " + sp1_.text);
    if (read2 != nullptr && sp2_.present &&
        static_cast<std::int64_t>(read2->seq.size()) < sp2_.length)
      throw std::invalid_argument("Read2 sequence: " + read2->seq +
                                  " is shorter than pattern2: " + sp2_.text);
  }

  bump("Input Reads");

  auto values = (opt_.method == ExtractMethod::String) ? get_barcodes_string(read1, read2)
                                                       : get_barcodes_regex(read1, read2);
  if (!values) return std::nullopt;
  auto [cell, umi, umi_quals, new_seq, new_quals, new_seq2, new_quals2] = *values;

  if (opt_.quality_filter_threshold) {
    if (umi_below_threshold(umi_quals, opt_.quality_encoding, *opt_.quality_filter_threshold)) {
      bump("filtered: umi quality");
      return std::nullopt;
    }
  }
  if (opt_.quality_filter_mask) {
    const std::string masked =
        mask_umi(umi, umi_quals, opt_.quality_encoding, *opt_.quality_filter_mask);
    if (masked != umi) {
      bump("UMI masked");
      umi = masked;
    }
  }
  if (opt_.filter_umi_barcode) {
    const auto filtered = filter_umi_barcode_fn(umi);
    if (!filtered) return std::nullopt;
    umi = *filtered;
  }
  if (opt_.filter_cell_barcode) {
    const auto filtered = filter_cell_barcode_fn(cell);
    if (!filtered) return std::nullopt;
    cell = *filtered;
  }

  // Python: `if self.umi_separator: umi_separator = self.umi_separator`, which
  // leaves the local UNBOUND (NameError) when the separator is falsy — upstream
  // bug D7#6. An empty separator is rejected up front instead of reproducing a
  // NameError, and the deviation is recorded in 10_validation.md.
  if (opt_.umi_separator.empty())
    throw std::invalid_argument(
        "--umi-separator='' triggers a NameError in the Python "
        "(extract_methods.py:569); refusing rather than reproducing it");
  const std::string& sep = opt_.umi_separator;

  bump("Reads output");

  ExtractResult out;
  if (opt_.either_read) {
    const std::string new_identifier = add_barcodes_to_identifier(read1, umi, cell, sep);
    read1.identifier = new_identifier;
    Record r2 = (read2 != nullptr) ? *read2 : Record{};
    // BUG D7#3, REPRODUCED: upstream assigns read1's `new_identifier` here, not
    // the `new_identifier2` it computed on the line above.
    r2.identifier = new_identifier;

    if (new_seq2.empty() && new_quals2.empty()) {   // UMI was on read 1
      read1.seq = new_seq;
      read1.quals = new_quals;
    }
    if (new_seq.empty() && new_quals.empty()) {     // UMI was on read 2
      r2.seq = new_seq2;
      r2.quals = new_quals2;
    }
    out.read1 = std::move(read1);
    out.read2 = std::move(r2);
    out.has_read2 = (read2 != nullptr);
    return out;
  }

  read1.identifier = add_barcodes_to_identifier(read1, umi, cell, sep);
  if (rx1_ || sp1_.present) {
    read1.seq = new_seq;
    read1.quals = new_quals;
  }
  out.read1 = std::move(read1);
  if (read2 != nullptr) {
    Record r2 = *read2;
    r2.identifier = add_barcodes_to_identifier(r2, umi, cell, sep);
    if (rx2_ || sp2_.present) {
      r2.seq = new_seq2;
      r2.quals = new_quals2;
    }
    out.read2 = std::move(r2);
    out.has_read2 = true;
  }
  return out;
}

std::optional<std::pair<std::string, std::string>>
ExtractFilterAndUpdate::get_barcodes_for_whitelist(const Record& read1, const Record* read2) {
  // whitelist.py calls ReadExtractor.getBarcodes(...) directly and unpacks
  // `cell, umi, _, _, _, _, _`, bypassing __call__'s filtering entirely.
  auto values = (opt_.method == ExtractMethod::String) ? get_barcodes_string(read1, read2)
                                                       : get_barcodes_regex(read1, read2);
  if (!values) return std::nullopt;
  return std::make_pair(std::get<0>(*values), std::get<1>(*values));
}

std::vector<std::pair<std::string, std::int64_t>>
ExtractFilterAndUpdate::get_read_counts_most_common() const {
  // collections.Counter.most_common(): count DESCENDING, ties broken by first
  // insertion (Counter is a dict, so it preserves insertion order and the sort
  // is stable). read_counts_ is an OrderedMap, so its iteration IS insertion
  // order and a stable_sort reproduces both halves of that contract.
  std::vector<std::pair<std::string, std::int64_t>> v;
  for (const auto& [k, n] : read_counts_) v.emplace_back(k, n);
  std::stable_sort(v.begin(), v.end(),
                   [](const auto& a, const auto& b) { return a.second > b.second; });
  return v;
}

}  // namespace umi_tools
