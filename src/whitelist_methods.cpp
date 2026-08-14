#include <cmath>
#include "umi_tools/whitelist_methods.hpp"

#include <stdexcept>
#include <vector>

#include "umi_tools/io.hpp"
#include "umi_tools/logging.hpp"

namespace umi_tools {
namespace {

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (true) {
    const std::size_t t = line.find('\t', start);
    if (t == std::string::npos) {
      out.push_back(line.substr(start));
      break;
    }
    out.push_back(line.substr(start, t - start));
    start = t + 1;
  }
  return out;
}

std::string strip(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::size_t b = 0;
  while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  return s.substr(b);
}

// Python: line.strip().split("\t") — strip FIRST, then split.
std::vector<std::string> stripped_fields(const std::string& line) {
  return split_tabs(strip(line));
}

std::vector<std::string> first_fields(const std::string& path) {
  std::vector<std::string> out;
  LineReader r(path);
  std::string line;
  while (r.next(line)) {
    if (!line.empty() && line[0] == '#') continue;   // Python: startswith('#')
    out.push_back(stripped_fields(line)[0]);
  }
  return out;
}

// base2errors: note there is NO 'N' key, so a whitelist barcode containing an N
// raises KeyError upstream. Reproduced as a thrown exception rather than
// silently skipped.
const char* errors_for(char base) {
  switch (base) {
    case 'A': return "TCGN";
    case 'T': return "ACGN";
    case 'C': return "TAGN";
    case 'G': return "TCAN";
    default: return nullptr;
  }
}

}  // namespace

std::pair<std::set<std::string>, std::map<std::string, std::optional<std::string>>>
get_user_defined_barcodes(const std::string& whitelist_tsv,
                          const std::string& whitelist_tsv2, bool getErrorCorrection,
                          bool deriveErrorCorrection, std::int64_t threshold) {
  std::vector<std::string> whitelist;
  std::map<std::string, std::optional<std::string>> false_to_true_map;

  // The generator the Python selects: paired (itertools.product of the two
  // files, concatenated) or single.
  auto barcode_list = [&]() -> std::vector<std::string> {
    if (!whitelist_tsv2.empty()) {
      const auto w1 = first_fields(whitelist_tsv);
      const auto w2 = first_fields(whitelist_tsv2);
      std::vector<std::string> out;
      out.reserve(w1.size() * w2.size());
      for (const auto& a : w1)
        for (const auto& b : w2) out.push_back(a + b);   // itertools.product order
      return out;
    }
    return first_fields(whitelist_tsv);
  };

  if (deriveErrorCorrection) {
    for (const std::string& wb : barcode_list()) {
      whitelist.push_back(wb);
      const auto n = static_cast<std::int64_t>(wb.size());
      if (n == 0) continue;

      // --correct-umi-threshold is a plain int option with no range check, and
      // both products below are unchecked int64 multiplications. A NEGATIVE
      // threshold reached the vector below as a request for ~2^64 elements
      // (std::length_error, which then aborted); a large one overflowed
      // (n^threshold passes int64 at threshold 18 for a 12-base barcode) and the
      // loop bound became meaningless. Rejected up front, naming the option.
      if (threshold < 0)
        throw std::invalid_argument(
            "--correct-umi-threshold/--error-correct-threshold must not be negative");
      {
        // n^threshold > 2^62 without evaluating it.
        double bits = static_cast<double>(threshold) * std::log2(static_cast<double>(n));
        if (bits > 62.0)
          throw std::invalid_argument(
              "--correct-umi-threshold/--error-correct-threshold is too large: "
              "generating every barcode within " + std::to_string(threshold) +
              " substitutions of a " + std::to_string(n) + "-base barcode overflows");
      }

      // Python: for positions in itertools.product(range(len(bc)), repeat=threshold)
      // — the SAME position may repeat, so the same error barcode is generated
      // more than once; the duplicate-detection branch below depends on that.
      std::vector<std::int64_t> positions(static_cast<std::size_t>(threshold), 0);
      const std::int64_t total = [&] {
        std::int64_t t = 1;
        for (std::int64_t i = 0; i < threshold; ++i) t *= n;
        return t;
      }();

      for (std::int64_t combo = 0; combo < total; ++combo) {
        std::int64_t rest = combo;
        for (std::int64_t k = threshold - 1; k >= 0; --k) {
          positions[static_cast<std::size_t>(k)] = rest % n;
          rest /= n;
        }
        // m_bases = [base2errors[bc[x]] for x in positions]
        std::vector<const char*> m_bases;
        m_bases.reserve(positions.size());
        for (std::int64_t p : positions) {
          const char* e = errors_for(wb[static_cast<std::size_t>(p)]);
          if (e == nullptr)
            throw std::invalid_argument(
                std::string("getUserDefinedBarcodes: whitelist barcode '") + wb +
                "' contains base '" + wb[static_cast<std::size_t>(p)] +
                "', which has no entry in base2errors (the Python raises KeyError)");
          m_bases.push_back(e);
        }
        // for m in itertools.product(*m_bases)
        const std::int64_t n_err = 4;   // each entry has exactly 4 alternatives
        // 4^threshold, with the same overflow argument as above; threshold is
        // already bounded so this cannot overflow, but the check is cheap and
        // keeps the bound honest if the guard above ever changes.
        if (m_bases.size() > 31)
          throw std::invalid_argument("error-correction threshold too large");
        std::int64_t err_total = 1;
        for (std::size_t i = 0; i < m_bases.size(); ++i) err_total *= n_err;
        for (std::int64_t ec = 0; ec < err_total; ++ec) {
          std::string error_barcode = wb;
          std::int64_t r = ec;
          for (std::size_t k = m_bases.size(); k-- > 0;) {
            const std::int64_t pick = r % n_err;
            r /= n_err;
            error_barcode[static_cast<std::size_t>(positions[k])] = m_bases[k][pick];
          }
          auto it = false_to_true_map.find(error_barcode);
          if (it != false_to_true_map.end()) {
            // Already seen: within `threshold` of more than one whitelisted
            // barcode, so it is not correctable. The Python logs only when the
            // existing value is still set (so it reports each collision once).
            if (it->second)
              Log::instance().info("Error barcode " + error_barcode +
                                   " can be assigned to more than one possible true "
                                   "barcode: " + *it->second + " or " + wb);
            it->second = std::nullopt;
          } else {
            false_to_true_map[error_barcode] = wb;
          }
        }
      }
    }
  } else if (getErrorCorrection) {
    if (!whitelist_tsv2.empty())
      throw std::logic_error(
          "Can only extract errors from the whitelist if a single whitelist is given");
    LineReader r(whitelist_tsv);
    std::string line;
    while (r.next(line)) {
      if (!line.empty() && line[0] == '#') continue;
      const auto fields = stripped_fields(line);
      whitelist.push_back(fields[0]);
      // Python indexes line[1] unconditionally here, so a whitelist without a
      // second column raises IndexError under --error-correct-cell.
      if (fields.size() < 2)
        throw std::out_of_range(
            "whitelist line has no second field for error correction: " + line);
      std::size_t start = 0;
      const std::string& errs = fields[1];
      while (true) {
        const std::size_t c = errs.find(',', start);
        const std::string eb =
            c == std::string::npos ? errs.substr(start) : errs.substr(start, c - start);
        false_to_true_map[eb] = fields[0];
        if (c == std::string::npos) break;
        start = c + 1;
      }
    }
  } else {
    whitelist = barcode_list();
  }

  return {std::set<std::string>(whitelist.begin(), whitelist.end()), false_to_true_map};
}

}  // namespace umi_tools
