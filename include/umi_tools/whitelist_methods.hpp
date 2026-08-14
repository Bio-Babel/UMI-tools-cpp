// whitelist_methods.hpp — the parts of umi_tools/whitelist_methods.py that
// `extract` needs. The knee estimators, the BK-tree and errorDetectAboveThreshold
// land with the `whitelist` CLI in slice 4 (07_port_plan.md).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace umi_tools {

// whitelist_methods.getUserDefinedBarcodes(whitelist_tsv, whitelist_tsv2=None,
//     getErrorCorrection=False, deriveErrorCorrection=False, threshold=1)
//
// Returns (set(whitelist), false_to_true_map). The map's value is OPTIONAL
// because upstream stores None to mean "this error barcode is within threshold
// of more than one whitelisted barcode, so it is not correctable" — a distinct
// state from "absent", and the caller branches on it.
//
// Three modes, in the Python's own precedence order:
//   deriveErrorCorrection : generate every barcode within `threshold`
//                           substitutions of each whitelisted barcode, using
//                           base2errors = {A:[T,C,G,N], T:[A,C,G,N],
//                           C:[T,A,G,N], G:[T,C,A,N]}. NOTE there is no 'N' key,
//                           so a whitelist barcode containing N raises KeyError.
//   getErrorCorrection    : read the SECOND tab-separated field as a
//                           comma-separated list of error barcodes.
//   neither               : just the first field of each line.
//
// Lines starting with '#' are skipped in every mode. When whitelist_tsv2 is
// given, the whitelist is the itertools.product of the two files' first fields,
// concatenated w1+w2 (paired barcodes).
std::pair<std::set<std::string>, std::map<std::string, std::optional<std::string>>>
get_user_defined_barcodes(const std::string& whitelist_tsv,
                          const std::string& whitelist_tsv2 = "",
                          bool getErrorCorrection = false,
                          bool deriveErrorCorrection = false,
                          std::int64_t threshold = 1);

}  // namespace umi_tools
