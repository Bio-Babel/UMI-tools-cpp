// extract_methods.hpp — port of umi_tools/extract_methods.py.
//
// The details that decide correctness:
//
//  * `ExtractBarcodes` iterates `sorted(list(groupdict))` — the group NAMES in
//    lexicographic order, not pattern order. With cell_1/cell_3/discard_2/umi_1
//    that ordering determines the concatenation order of multi-part barcodes.
//  * quality encodings offset by RANGES[encoding][0], i.e. the LOW bound:
//    phred33 -> 33, solexa -> 59, phred64 -> 64.
//  * the string method's 3' extraction uses `sequence[-length:]` and
//    `sequence[:-length]`; with length 0 Python's `[-0:]` is the WHOLE string,
//    which is a real edge case rather than an impossible one.
//  * upstream bug D7#3, reproduced: in --either-read mode read2's identifier is
//    set to read1's `new_identifier`, not to `new_identifier2`.
//  * upstream bug D7#8, reproduced: ExtractBarcodes computes cell_quals but
//    returns the always-empty cell_barcode_quals, so cell qualities never leave
//    the function.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "umi_tools/fastq.hpp"
#include "umi_tools/ordered_map.hpp"
#include "umi_tools/pattern.hpp"

namespace umi_tools {

enum class ExtractMethod { String, Regex };

/// RANGES from umi_methods: name -> (low, high). Only `low` is used for the
/// offset; `high` is used by guessFormat.
std::optional<int> quality_offset(std::string_view encoding);

/// extract_methods.addBarcodesToIdentifier(read, UMI, cell, umi_separator)
std::string add_barcodes_to_identifier(const Record& read, std::string_view umi,
                                       std::string_view cell,
                                       std::string_view umi_separator);

struct SeqQuals {
  std::string new_seq;
  std::string new_quals;
  std::string umi_quals;
  std::string cell_quals;
};

/// extract_methods.extractSeqAndQuals(seq, quals, umi_bases, cell_bases,
///                                    discard_bases, retain_umi=False)
SeqQuals extract_seq_and_quals(std::string_view seq, std::string_view quals,
                               const std::set<std::int64_t>& umi_bases,
                               const std::set<std::int64_t>& cell_bases,
                               const std::set<std::int64_t>& discard_bases,
                               bool retain_umi);

/// extract_methods.get_below_threshold / umi_below_threshold / mask_umi
std::vector<bool> get_below_threshold(std::string_view umi_quals,
                                      std::string_view quality_encoding,
                                      std::int64_t quality_filter_threshold);
bool umi_below_threshold(std::string_view umi_quals, std::string_view quality_encoding,
                         std::int64_t quality_filter_threshold);
std::string mask_umi(std::string_view umi, std::string_view umi_quals,
                     std::string_view quality_encoding,
                     std::int64_t quality_filter_threshold);

struct ExtractedBarcodes {
  std::string cell_barcode;
  std::string cell_barcode_quals;  // always empty upstream — bug D7#8
  std::string umi;
  std::string umi_quals;
  std::string new_seq;
  std::string new_quals;
};

/// extract_methods.ExtractBarcodes(read, match, extract_umi, extract_cell,
///                                 discard, retain_umi)
ExtractedBarcodes extract_barcodes(const Record& read, const MatchResult& match,
                                   bool extract_umi, bool extract_cell, bool discard,
                                   bool retain_umi);

/// Params struct carrying the ORIGINAL Python names and defaults
/// (extract_methods.ExtractFilterAndUpdate.__init__).
struct ExtractFilterOptions {
  ExtractMethod method = ExtractMethod::String;
  bool prime3 = false;
  bool extract_cell = false;
  std::string quality_encoding;                        // Python: None
  std::optional<std::int64_t> quality_filter_threshold;  // Python: False sentinel
  std::optional<std::int64_t> quality_filter_mask;       // Python: False sentinel
  bool filter_umi_barcode = false;
  bool filter_cell_barcode = false;
  bool retain_umi = false;
  bool either_read = false;
  std::string either_read_resolve = "discard";
  std::string umi_separator = "_";
};

/// The result of one call: nullopt where the Python returns None (read filtered).
struct ExtractResult {
  Record read1;
  Record read2;
  bool has_read2 = false;
};

class ExtractFilterAndUpdate {
 public:
  ExtractFilterAndUpdate(const ExtractFilterOptions& options,
                         const std::string& pattern, const std::string& pattern2);

  /// Whitelists, assigned after construction exactly as extract.py does.
  void set_cell_whitelist(std::set<std::string> whitelist);
  void set_false_to_true_map(std::map<std::string, std::string> m);
  void set_cell_blacklist(std::set<std::string> blacklist);
  void set_umi_whitelist(std::set<std::string> whitelist);
  void set_umi_false_to_true_map(std::map<std::string, std::optional<std::string>> m);

  /// ExtractFilterAndUpdate.__call__(read1, read2=None)
  std::optional<ExtractResult> operator()(Record read1, const Record* read2 = nullptr);

  /// ExtractFilterAndUpdate.getBarcodes(read1, read2) as `whitelist` calls it:
  /// the raw (cell, umi) pair with no filtering and no read update. Returns
  /// nullopt where the Python returns None (no match).
  std::optional<std::pair<std::string, std::string>> get_barcodes_for_whitelist(
      const Record& read1, const Record* read2);

  /// getReadCounts(). collections.Counter.most_common() orders by count
  /// DESCENDING, ties broken by FIRST INSERTION (Counter preserves dict order),
  /// and `extract` logs it in that order — so both are reproduced.
  std::vector<std::pair<std::string, std::int64_t>> get_read_counts_most_common() const;

  const OrderedMap<std::string, std::int64_t>& read_counts() const { return read_counts_; }
  // umi -> {"no_error": n, "error": n}, for --umi-correct-log.
  const OrderedMap<std::string, std::pair<std::int64_t, std::int64_t>>&
  umi_whitelist_counts() const { return umi_whitelist_counts_; }

 private:
  struct StringPattern {
    std::int64_t length = 0;
    std::vector<std::int64_t> umi_bases, bc_bases, cell_bases;
    bool present = false;
    /// The pattern AS WRITTEN. Upstream's too-short-read error interpolates
    /// `self.pattern` itself — `'Read sequence: %s is shorter than pattern: %s'
    /// % (read1.seq, self.pattern)` (extract_methods.py:536) — and this struct
    /// held only a length, so the port said "(length 8)" where upstream says
    /// "NNNNNNNN".
    std::string text;
  };

  void bump(const std::string& key) { read_counts_[key] += 1; }

  std::optional<std::tuple<std::string, std::string, std::string, std::string,
                           std::string, std::string, std::string>>
  get_barcodes_string(const Record& read1, const Record* read2) const;
  std::optional<std::tuple<std::string, std::string, std::string, std::string,
                           std::string, std::string, std::string>>
  get_barcodes_regex(const Record& read1, const Record* read2);

  std::optional<std::string> filter_cell_barcode_fn(std::string cell);
  std::optional<std::string> filter_umi_barcode_fn(const std::string& umi);

  ExtractFilterOptions opt_;
  StringPattern sp1_, sp2_;
  std::optional<Pattern> rx1_, rx2_;

  OrderedMap<std::string, std::int64_t> read_counts_;
  OrderedMap<std::string, std::pair<std::int64_t, std::int64_t>> umi_whitelist_counts_;

  std::set<std::string> cell_whitelist_, cell_blacklist_, umi_whitelist_;
  std::map<std::string, std::string> false_to_true_map_;
  std::map<std::string, std::optional<std::string>> umi_false_to_true_map_;
  bool has_cell_whitelist_ = false, has_blacklist_ = false;
  bool has_false_to_true_ = false, has_umi_whitelist_ = false, has_umi_map_ = false;
};

class Values;

/// Utilities.validateExtractOptions -> (extract_cell, extract_umi).
///
/// Shared because whitelist.py:346 calls it too. tool_whitelist used to sniff
/// `extract_cell` inline and skip every check, so e.g.
/// `whitelist --bc-pattern=CCCCNNNN --filtered-out2=f2.fq` (single-end) exited 0
/// having written nothing, where upstream raises.
std::pair<bool, bool> validate_extract_options(const Values& o, const std::string& pattern,
                                               const std::string& pattern2,
                                               ExtractMethod method);

/// How upstream renders a pattern inside an error message. For the REGEX method
/// validateExtractOptions has already replaced options.pattern with the COMPILED
/// object, so `%s` prints its repr; an unset pattern is None and prints "None".
std::string pattern_repr(const std::string& pattern, ExtractMethod method);

}  // namespace umi_tools
