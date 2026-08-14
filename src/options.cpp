#include <iostream>
#include "umi_tools/logging.hpp"
#include "umi_tools/options.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <optional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace umi_tools {
namespace {
// optparse._parse_num (optparse.py:405-418) followed by Python's int(str, base).
//
//     if val[:2].lower() == "0x":   radix = 16
//     elif val[:2].lower() == "0b": radix = 2; val = val[2:] or "0"
//     elif val[:1] == "0":          radix = 8
//     else:                         radix = 10
//     return int(val, radix)
//
// The radix sniff indexes the RAW string. Stripping whitespace or a sign first
// changes the answer, and MEASURED it does:
//     "010"    -> 8      but " 010 "  -> 10   (the space defeats val[:1]=="0")
//     "-010"   -> -10                         (the sign does too)
//     "09"     -> rejected                    (invalid octal)
//     " 0b101" -> rejected                    (val[:2] is " 0", so base 10)
// Only int() itself tolerates the padding, and it does so AFTER the radix is
// already fixed. An earlier version of this function stripped first and got
// " 010 " wrong by exactly that ordering.
// Python's `str(float)` / `repr(float)`, which is what optparse stores for a
// `type='float'` option and what the parameter dump then prints. MEASURED:
// `--soft-clip-threshold 4` shows **4.0** upstream and showed `4` here, because
// the port kept the user's text instead of the parsed value.
//
// Two rules, both different from printf's:
//   * SHORTEST round-trip digits, not a fixed precision (`0.1`, not
//     `0.10000000000000001`).
//   * fixed notation when the decimal exponent is in [-4, 16), scientific
//     otherwise — where `%g` switches at the precision instead, so `1e15` would
//     come out `1e+15` where Python gives `1000000000000000.0`.
// An integral value always keeps a `.0`; that is the whole visible difference
// for the values anyone passes to these options.
std::string py_float_repr(double v) {
  if (std::isnan(v)) return std::signbit(v) ? "-nan" : "nan";
  if (std::isinf(v)) return v < 0 ? "-inf" : "inf";

  char buf[64];
  int prec = 1;
  for (; prec < 17; ++prec) {
    std::snprintf(buf, sizeof buf, "%.*e", prec - 1, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  std::snprintf(buf, sizeof buf, "%.*e", prec - 1, v);

  std::string sci(buf);                       // "-4.7000e+00"
  const std::size_t epos = sci.find('e');
  const int exp = std::atoi(sci.c_str() + epos + 1);
  std::string mant = sci.substr(0, epos);
  std::string sign;
  if (!mant.empty() && (mant[0] == '-' || mant[0] == '+')) {
    if (mant[0] == '-') sign = "-";
    mant.erase(0, 1);
  }
  mant.erase(std::remove(mant.begin(), mant.end(), '.'), mant.end());
  while (mant.size() > 1 && mant.back() == '0') mant.pop_back();

  std::string out;
  if (exp >= -4 && exp < 16) {
    if (exp >= 0) {
      const std::size_t ip = static_cast<std::size_t>(exp) + 1;
      out = mant.size() >= ip ? mant.substr(0, ip) : mant + std::string(ip - mant.size(), '0');
      const std::string frac = mant.size() > ip ? mant.substr(ip) : "";
      out += "." + (frac.empty() ? "0" : frac);
    } else {
      out = "0." + std::string(static_cast<std::size_t>(-exp) - 1, '0') + mant;
    }
  } else {
    out = mant.substr(0, 1);
    if (mant.size() > 1) out += "." + mant.substr(1);
    char eb[16];
    std::snprintf(eb, sizeof eb, "e%c%02d", exp < 0 ? '-' : '+', exp < 0 ? -exp : exp);
    out += eb;
  }
  return sign + out;
}

std::optional<std::int64_t> py_parse_int(const std::string& raw) {
  int radix = 10;
  std::string v = raw;
  std::string head2 = raw.substr(0, std::min<std::size_t>(2, raw.size()));
  for (char& c : head2) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (head2 == "0x") {
    radix = 16;
  } else if (head2 == "0b") {
    radix = 2;
    v = raw.substr(2);
    if (v.empty()) v = "0";
  } else if (!raw.empty() && raw[0] == '0') {
    radix = 8;
  }

  // int(v, radix): surrounding whitespace, an optional sign, the radix's own
  // prefix when it matches (int("0x1f", 16) == 31), and `_` between digits.
  const auto b = v.find_first_not_of(" \t\n\r\f\v");
  if (b == std::string::npos) return std::nullopt;
  v = v.substr(b, v.find_last_not_of(" \t\n\r\f\v") - b + 1);

  bool neg = false;
  if (v[0] == '+' || v[0] == '-') {
    neg = v[0] == '-';
    v.erase(0, 1);
  }
  const char pfx = radix == 16 ? 'x' : radix == 2 ? 'b' : radix == 8 ? 'o' : '\0';
  if (pfx != '\0' && v.size() > 2 && v[0] == '0' &&
      std::tolower(static_cast<unsigned char>(v[1])) == pfx)
    v.erase(0, 2);

  std::string digits;
  for (std::size_t k = 0; k < v.size(); ++k) {
    if (v[k] != '_') {
      digits += v[k];
    } else if (k == 0 || k + 1 == v.size() || v[k - 1] == '_') {
      return std::nullopt;              // `_` is a separator, only BETWEEN digits
    }
  }
  if (digits.empty()) return std::nullopt;

  errno = 0;
  char* end = nullptr;
  const long long n = std::strtoll(digits.c_str(), &end, radix);
  if (end == digits.c_str() || *end != '\0' || errno == ERANGE) return std::nullopt;
  return static_cast<std::int64_t>(neg ? -n : n);
}
}  // namespace

namespace {

// --------------------------------------------------------------------------
// Python textwrap
//
// Ported from CPython's Lib/textwrap.py with the defaults umi_tools uses:
// expand_tabs=True (tabsize 8), replace_whitespace=True, drop_whitespace=True,
// break_long_words=True, break_on_hyphens=True, initial/subsequent_indent="".
// --------------------------------------------------------------------------

// textwrap._munge_whitespace: expand tabs, then map every whitespace char to ' '.
std::string munge_whitespace(std::string_view text) {
  std::string expanded;
  int col = 0;
  for (char ch : text) {
    if (ch == '\t') {
      const int n = 8 - (col % 8);
      expanded.append(static_cast<std::size_t>(n), ' ');
      col += n;
    } else {
      expanded.push_back(ch);
      if (ch == '\n') col = 0; else ++col;
    }
  }
  // _whitespace = '\t\n\x0b\x0c\r ' -> all become ' '.
  for (char& ch : expanded) {
    switch (ch) {
      case '\n': case '\v': case '\f': case '\r': case ' ':
        ch = ' ';
        break;
      default:
        break;
    }
  }
  return expanded;
}

bool is_ws(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

// textwrap's wordsep_re, reduced to what it actually does for this corpus:
// split into runs of whitespace and runs of non-whitespace, then further split a
// non-whitespace run after a hyphen when break_on_hyphens applies.
//
// CPython's rule (wordsep_re) breaks after a hyphen only when the hyphen sits
// between two letters and is not part of a run of hyphens — i.e. "en-dash" style
// compound words split, while "--option" and "3-4" do not. That distinction
// matters here because help strings are full of "--flag" spellings which must
// NOT be split.
// textwrap's `wordsep_re.split`, minus the empty pieces.
//
// The pattern's "hyphenated word" branch is three LOOKBEHIND assertions:
//     -(?:(?<=[^\d\W]{2}-)|(?<=[^\d\W]-[^\d\W]-))(?=[^\d\W]-?[^\d\W])
// ECMAScript, the grammar std::regex implements, has no lookbehind, so this is
// procedural. It reproduces the alternation in the regex's own order.
//
// The previous version approximated: it required a single alphabetic character
// before the hyphen where the regex requires TWO (or an alpha-hyphen-alpha
// run), and it had no em-dash branch at all. That is why `` ``--method`` ``
// chunked as one piece instead of '``--' + 'method``'. Validated exactly, at
// both the chunk and the wrapped-line level, by validation/parity_textwrap.py.
std::vector<std::string> split_chunks(const std::string& text) {
  // [^\d\W] -- a word char that is not a digit: ASCII letters and underscore.
  // Every string this sees is ASCII (measured: the seven tool docstrings and
  // every shipped help string).
  auto alpha_ = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
  };
  auto word_c = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  };
  // The class guarding both em-dash branches: [\w!"'&.,?]
  auto emdash_lhs = [&](char c) {
    return word_c(c) || c == '!' || c == '"' || c == '\'' || c == '&' ||
           c == '.' || c == ',' || c == '?';
  };
  // An em-dash run at i:  (?<=[\w!"'&.,?]) -{2,} (?=\w)
  auto emdash_at = [&](std::size_t i) {
    if (i == 0 || !emdash_lhs(text[i - 1])) return false;
    std::size_t j = i;
    while (j < text.size() && text[j] == '-') ++j;
    return (j - i) >= 2 && j < text.size() && word_c(text[j]);
  };
  // The hyphenated-word break, asked at the index of a '-' inside a word.
  auto hyphen_break_at = [&](std::size_t j) {
    const bool two_alpha = j >= 2 && alpha_(text[j - 1]) && alpha_(text[j - 2]);
    const bool a_h_a = j >= 3 && alpha_(text[j - 1]) && text[j - 2] == '-' &&
                       alpha_(text[j - 3]);
    if (!two_alpha && !a_h_a) return false;
    const std::size_t k = j + 1;
    if (k >= text.size() || !alpha_(text[k])) return false;
    std::size_t m = k + 1;
    if (m < text.size() && text[m] == '-') ++m;
    return m < text.size() && alpha_(text[m]);
  };

  std::vector<std::string> chunks;
  std::size_t i = 0;
  while (i < text.size()) {
    if (is_ws(text[i])) {                       // a whitespace run
      std::size_t j = i;
      while (j < text.size() && is_ws(text[j])) ++j;
      chunks.emplace_back(text, i, j - i);
      i = j;
      continue;
    }
    if (text[i] == '-' && emdash_at(i)) {       // an em-dash run
      std::size_t j = i;
      while (j < text.size() && text[j] == '-') ++j;
      chunks.emplace_back(text, i, j - i);
      i = j;
      continue;
    }
    // `[^\s]+?` with the shortest terminator, tested in the regex's order:
    // hyphenated break, end of word, then the start of an em-dash.
    std::size_t j = i + 1;
    for (;; ++j) {
      if (j < text.size() && text[j] == '-' && hyphen_break_at(j)) { ++j; break; }
      if (j >= text.size() || is_ws(text[j])) break;
      if (emdash_lhs(text[j - 1])) {
        std::size_t k = j;
        while (k < text.size() && text[k] == '-') ++k;
        if ((k - j) >= 2 && k < text.size() && word_c(text[k])) break;
      }
    }
    chunks.emplace_back(text, i, j - i);
    i = j;
  }
  return chunks;
}

std::string strip_copy(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && is_ws(s[b])) ++b;
  while (e > b && is_ws(s[e - 1])) --e;
  return s.substr(b, e - b);
}

}  // namespace

// textwrap._wrap_chunks
std::vector<std::string> textwrap_chunks(const std::string& text) {
  return split_chunks(munge_whitespace(text));
}

std::vector<std::string> textwrap_wrap(std::string_view text, int width) {
  if (width <= 0) throw std::invalid_argument("textwrap: invalid width");
  std::vector<std::string> chunks = split_chunks(munge_whitespace(text));
  std::vector<std::string> lines;

  // The Python reverses and pops from the back; an index walking forward is the
  // same thing and avoids the copy.
  std::size_t pos = 0;
  while (pos < chunks.size()) {
    std::vector<std::string> cur_line;
    std::size_t cur_len = 0;

    // drop_whitespace: skip a leading whitespace chunk on any line but the first.
    if (!lines.empty() && pos < chunks.size() && strip_copy(chunks[pos]).empty()) ++pos;

    while (pos < chunks.size()) {
      const std::size_t l = chunks[pos].size();
      if (cur_len + l <= static_cast<std::size_t>(width)) {
        cur_line.push_back(chunks[pos]);
        cur_len += l;
        ++pos;
      } else {
        break;
      }
    }

    // _handle_long_word with break_long_words=True: put as much of the
    // oversized chunk on this line as fits.
    if (pos < chunks.size() && chunks[pos].size() > static_cast<std::size_t>(width)) {
      // space_left is `width - cur_len`, NOT clamped to 1 -- only a width below
      // 1 clamps, and width is validated positive above. A zero space_left
      // appends the empty string, which the trailing-whitespace drop removes;
      // that is how a wrapped line legitimately keeps a TRAILING SPACE.
      const std::size_t space_left = (cur_len < static_cast<std::size_t>(width))
                                         ? static_cast<std::size_t>(width) - cur_len
                                         : 0;
      const std::string chunk = chunks[pos];
      std::size_t end = space_left;
      // break_on_hyphens: break after the last hyphen inside space_left, but
      // only if a non-hyphen precedes it. This splits
      // `` ``--multimapping-detection-method`` `` as '``--' rather than
      // mid-word.
      if (chunk.size() > space_left) {
        std::size_t hyphen = std::string::npos;
        for (std::size_t k = 0; k < space_left && k < chunk.size(); ++k)
          if (chunk[k] == '-') hyphen = k;
        if (hyphen != std::string::npos && hyphen > 0) {
          bool any_non_hyphen = false;
          for (std::size_t k = 0; k < hyphen; ++k)
            if (chunk[k] != '-') { any_non_hyphen = true; break; }
          if (any_non_hyphen) end = hyphen + 1;
        }
      }
      cur_line.push_back(chunk.substr(0, end));
      chunks[pos] = chunk.substr(end);
      cur_len = 0;
      for (const auto& c : cur_line) cur_len += c.size();
    }

    // drop_whitespace: drop a trailing whitespace chunk.
    if (!cur_line.empty() && strip_copy(cur_line.back()).empty()) {
      cur_len -= cur_line.back().size();
      cur_line.pop_back();
    }

    if (!cur_line.empty()) {
      std::string joined;
      for (const auto& c : cur_line) joined += c;
      lines.push_back(std::move(joined));
    }
  }
  return lines;
}

const ToolSpec* find_tool_spec(std::string_view name) {
  for (const auto& t : all_tool_specs())
    if (name == t.name) return &t;
  return nullptr;
}

// --------------------------------------------------------------------------
// optparse.HelpFormatter.format_option_strings
//
//   if option.takes_value():
//       metavar = option.metavar or option.dest.upper()
//       short_opts = ["%s %s" % (sopt, metavar) for sopt in option._short_opts]
//       long_opts  = ["%s=%s" % (lopt, metavar) for lopt in option._long_opts]
//   else:
//       short_opts = option._short_opts; long_opts = option._long_opts
//   if self.short_first: opts = short_opts + long_opts
//   return ", ".join(opts)
// --------------------------------------------------------------------------
std::string format_option_strings(const OptionSpec& opt, bool short_first) {
  std::vector<std::string> short_parts, long_parts;
  if (opt.takes_value) {
    std::string metavar;
    if (opt.metavar != nullptr) {
      metavar = opt.metavar;
    } else if (opt.dest != nullptr) {
      metavar = opt.dest;
      for (char& c : metavar) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    for (const char* s : opt.short_opts) short_parts.push_back(std::string(s) + " " + metavar);
    for (const char* l : opt.long_opts) long_parts.push_back(std::string(l) + "=" + metavar);
  } else {
    for (const char* s : opt.short_opts) short_parts.emplace_back(s);
    for (const char* l : opt.long_opts) long_parts.emplace_back(l);
  }
  std::vector<std::string> parts;
  if (short_first) {
    parts = short_parts;
    parts.insert(parts.end(), long_parts.begin(), long_parts.end());
  } else {
    parts = long_parts;
    parts.insert(parts.end(), short_parts.begin(), short_parts.end());
  }
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += ", ";
    out += parts[i];
    }
  return out;
}

namespace {

// optparse.HelpFormatter.expand_default:
//   default_value = parser.defaults.get(option.dest)
//   if default_value is NO_DEFAULT or default_value is None:
//       default_value = self.NO_DEFAULT_VALUE      # "none"
//   return option.help.replace("%default", str(default_value))
std::string expand_default(const OptionSpec& opt) {
  if (opt.help == nullptr) return {};
  std::string help(opt.help);
  const std::string tag = "%default";
  const std::string value = opt.default_is_none ? "none" : std::string(opt.default_repr);
  std::string out;
  std::size_t i = 0;
  while (true) {
    const std::size_t p = help.find(tag, i);
    if (p == std::string::npos) {
      out += help.substr(i);
      break;
    }
    out += help.substr(i, p - i);
    out += value;
    i = p + tag.size();
  }
  return out;
}

std::string spaces(int n) { return std::string(n > 0 ? static_cast<std::size_t>(n) : 0, ' '); }

// optparse.SUPPRESS_HELP == "SUPPRESSHELP". OptionContainer.format_option_help
// skips such options entirely:
//     if not option.help is SUPPRESS_HELP: result.append(...)
// but they still PARSE normally, and — a detail that is easy to miss —
// store_option_strings computes max_len over ALL options including these, so a
// suppressed option can still widen help_position. 12 options use it (7 in
// extract.py, 5 in Utilities.py).
bool is_suppressed(const OptionSpec& o) {
  return o.help != nullptr && std::string_view(o.help) == "SUPPRESSHELP";
}

// BetterFormatter.format_option — a modified copy of optparse's, with the
// help text wrapped per '\n'-separated paragraph (an empty paragraph yields an
// empty line, which optparse's own version would drop).
std::string format_option(const OptionSpec& opt, int current_indent, int help_position,
                          int help_width, bool short_first) {
  std::string result;
  std::string opts = format_option_strings(opt, short_first);
  const int opt_width = help_position - current_indent - 2;
  int indent_first = 0;
  if (static_cast<int>(opts.size()) > opt_width) {
    opts = spaces(current_indent) + opts + "\n";
    indent_first = help_position;
  } else {
    // "%*s%-*s  " % (current_indent, "", opt_width, opts)
    std::string padded = opts;
    if (static_cast<int>(padded.size()) < opt_width)
      padded += spaces(opt_width - static_cast<int>(padded.size()));
    opts = spaces(current_indent) + padded + "  ";
    indent_first = 0;
  }
  result += opts;

  if (opt.help != nullptr && *opt.help != '\0') {
    const std::string help_text = expand_default(opt);
    std::vector<std::string> help_lines;
    // help_text.split('\n'), wrapping each part; an empty part -> ''.
    std::size_t start = 0;
    while (true) {
      const std::size_t nl = help_text.find('\n', start);
      const std::string part = help_text.substr(
          start, nl == std::string::npos ? std::string::npos : nl - start);
      const auto wrapped = textwrap_wrap(part, help_width);
      if (!wrapped.empty())
        help_lines.insert(help_lines.end(), wrapped.begin(), wrapped.end());
      else
        help_lines.emplace_back();
      if (nl == std::string::npos) break;
      start = nl + 1;
    }
    result += spaces(indent_first) + help_lines[0] + "\n";
    for (std::size_t i = 1; i < help_lines.size(); ++i)
      result += spaces(help_position) + help_lines[i] + "\n";
  } else if (!opts.empty() && opts.back() != '\n') {
    result += "\n";
  }
  return result;
}

// BetterFormatter.format_usage:
//     return optparse._(usage) + msg
// where msg is a fixed two-newline prefix and the docs URL. Note there is no
// "Usage: " prefix — the tools' own usage strings already carry it.
constexpr const char* kUsageMsg =
    "\n\nFor full UMI-tools documentation, see "
    "https://umi-tools.readthedocs.io/en/latest/\n";

}  // namespace

// --------------------------------------------------------------------------
// OptionParser.format_help, with callbackShortHelp's description suppression.
// --------------------------------------------------------------------------
std::string format_help(const ToolSpec& tool, bool include_description) {
  // HelpFormatter.store_option_strings: help_position = min(max_len + 2,
  // max_help_position), where max_len includes the option's indent level
  // (2 for top-level, 4 inside a group). help_width = max(width - hp, 11).
  int max_len = 0;
  for (const auto& o : tool.top_level)
    max_len = std::max(max_len,
                       static_cast<int>(format_option_strings(o, tool.short_first).size()) +
                           tool.indent_increment);
  for (const auto& g : tool.groups)
    for (const auto& o : g.options)
      max_len = std::max(max_len,
                         static_cast<int>(format_option_strings(o, tool.short_first).size()) +
                             2 * tool.indent_increment);
  const int help_position = std::min(max_len + 2, tool.max_help_position);
  const int help_width = std::max(tool.width - help_position, 11);

  std::string out;

  // if self.usage: result.append(self.get_usage() + "\n")
  if (tool.usage != nullptr && *tool.usage != '\0')
    out += std::string(tool.usage) + kUsageMsg + "\n";

  // callbackShortHelp does parser.set_description(None) before print_help(), so
  // the long module docstring is absent from every golden.
  // optparse.format_help:  if self.description:
  //                              result.append(self.format_description() + "\n")
  // BetterFormatter.format_description(d) is `_formatter(d) + '\n'`, and
  // _formatter wraps EACH LINE of the text separately at self.width and rejoins
  // with '\n' -- so blank lines survive as blank lines rather than collapsing.
  if (include_description) {
    const std::string_view desc = module_docstring(tool.name);
    if (!desc.empty()) {
      std::string body;
      std::size_t i = 0;
      bool first = true;
      while (i <= desc.size()) {
        const std::size_t nl = desc.find('\n', i);
        const std::string_view line =
            desc.substr(i, nl == std::string_view::npos ? std::string_view::npos : nl - i);
        if (!first) body += '\n';
        first = false;
        const auto wrapped = textwrap_wrap(line, tool.width);
        for (std::size_t k = 0; k < wrapped.size(); ++k) {
          if (k) body += '\n';
          body += wrapped[k];
        }
        if (nl == std::string_view::npos) break;
        i = nl + 1;
      }
      out += body + "\n" + "\n";
    }
  }

  // format_option_help: heading at indent 0, then indent().
  out += "Options:\n";
  int current_indent = tool.indent_increment;

  std::vector<std::string> pieces;
  if (!tool.top_level.empty()) {
    std::string s;
    for (const auto& o : tool.top_level) {
      if (is_suppressed(o)) continue;
      s += format_option(o, current_indent, help_position, help_width, tool.short_first);
    }
    pieces.push_back(s);
    pieces.emplace_back("\n");
  }
  for (const auto& g : tool.groups) {
    // OptionGroup.format_help: heading at the current indent, then indent().
    // The heading is emitted even when the group contributes no option lines —
    // because every option is SUPPRESS_HELP, or because the group is genuinely
    // empty (count.py's "count-specific options").
    std::string s = spaces(current_indent) + g.title + ":\n";
    const int inner = current_indent + tool.indent_increment;
    std::string body;
    for (const auto& o : g.options) {
      if (is_suppressed(o)) continue;
      body += format_option(o, inner, help_position, help_width, tool.short_first);
    }
    // OptionContainer.format_help joins [description, options] with "\n"; with
    // no description that is just the option help.
    s += body;
    pieces.push_back(s);
    pieces.emplace_back("\n");
  }
  // "".join(result[:-1]) — the final separator is dropped.
  if (!pieces.empty()) pieces.pop_back();
  for (const auto& p : pieces) out += p;

  return out;
}

// --------------------------------------------------------------------------
// Values
// --------------------------------------------------------------------------
bool Values::has(std::string_view dest) const { return v_.find(std::string(dest)) != v_.end(); }

bool Values::is_none(std::string_view dest) const {
  auto it = v_.find(std::string(dest));
  return it == v_.end() || it->second.second;
}

std::string Values::get_string(std::string_view dest) const {
  auto it = v_.find(std::string(dest));
  if (it == v_.end()) throw std::out_of_range("Values: no such dest: " + std::string(dest));
  return it->second.first;
}

std::int64_t Values::get_int(std::string_view dest) const {
  const std::string s = get_string(dest);
  // Same routine store() validates with, so a value that parsed as octal is
  // RETRIEVED as octal. Using strtoll base 10 here made `--mapping-quality 010`
  // validate as 8 and then come back as 10.
  const auto n = py_parse_int(s);
  return n.value_or(0);
}

double Values::get_float(std::string_view dest) const {
  return std::strtod(get_string(dest).c_str(), nullptr);
}

bool Values::get_int_truthy(std::string_view dest) const {
  auto it = v_.find(std::string(dest));
  if (it == v_.end()) return false;
  if (it->second.second) return false;              // None is falsy
  const std::string& s = it->second.first;
  if (s.empty()) return false;
  const auto n = py_parse_int(s);
  // A value optparse could not have produced for an int dest: fall back to the
  // string rule rather than silently reading it as 0.
  if (!n) return !(s == "False" || s == "None");
  return *n != 0;
}

bool Values::get_bool(std::string_view dest) const {
  auto it = v_.find(std::string(dest));
  if (it == v_.end()) return false;
  if (it->second.second) return false;              // None is falsy
  const std::string& s = it->second.first;
  // Python truthiness for the values these dests can hold. "" and "False" model
  // the empty-string and flag defaults; a None default is handled above.
  //
  // "0" is NOT in that list, and used to be: a non-empty Python str is ALWAYS
  // truthy, so `if options.stats:` is TRUE for --output-stats=0. MEASURED
  //: upstream writes 0_per_umi.tsv / 0_per_umi_per_position.tsv /
  // 0_edit_distance.tsv and the port wrote nothing. `--timeit 0` was the same
  // shape, and `--output-stats=0 --ignore-umi` skipped the "cannot be used
  // together" error too. The only genuinely falsy string here is "".
  return !(s.empty() || s == "False" || s == "None");
}

void Values::set(std::string_view dest, std::string value, bool is_none) {
  v_[std::string(dest)] = {std::move(value), is_none};
}

void Values::mark_given(std::string_view dest) { given_.emplace(dest); }

bool Values::was_given(std::string_view dest) const {
  return given_.find(dest) != given_.end();
}

// --------------------------------------------------------------------------
// parse_args — optparse's _process_args, restricted to the measured subset.
// --------------------------------------------------------------------------
namespace {

struct Lookup {
  std::map<std::string, const OptionSpec*> by_opt;
};

Lookup build_lookup(const ToolSpec& tool) {
  Lookup lk;
  auto add = [&lk](const OptionSpec& o) {
    for (const char* s : o.short_opts) lk.by_opt[s] = &o;
    for (const char* l : o.long_opts) lk.by_opt[l] = &o;
  };
  for (const auto& o : tool.top_level) add(o);
  for (const auto& g : tool.groups)
    for (const auto& o : g.options) add(o);
  return lk;
}

void check_choice(const OptionSpec& o, const std::string& value, const std::string& opt_str) {
  if (o.type != OptType::Choice) return;
  for (const char* c : o.choices)
    if (value == c) return;
  // optparse's message: option %s: invalid choice: %r (choose from %s)
  std::string choices;
  for (int i = 0; i < o.choices.size(); ++i) {
    if (i) choices += ", ";
    choices += std::string("'") + o.choices[i] + "'";
  }
  throw std::invalid_argument("option " + opt_str + ": invalid choice: '" + value +
                              "' (choose from " + choices + ")");
}

void store(Values& v, const OptionSpec& o, const std::string& value, const std::string& opt_str) {
  check_choice(o, value, opt_str);
  std::string stored = value;
  if (o.type == OptType::Int) {
    // optparse: option %s: invalid integer value: %r
    const auto n = py_parse_int(value);
    if (!n)
      throw std::invalid_argument("option " + opt_str + ": invalid integer value: '" + value + "'");
    // Python stores the PARSED int, so the params block prints 8 for "010".
    // Storing the raw text made every int option echo back what the user typed
    // rather than what was parsed.
    stored = std::to_string(*n);
  } else if (o.type == OptType::Float) {
    const char* p = value.c_str();
    char* end = nullptr;
    const double d = std::strtod(p, &end);
    if (end == p || *end != '\0')
      throw std::invalid_argument("option " + opt_str + ": invalid floating-point value: '" +
                                  value + "'");
    stored = py_float_repr(d);        // Python stores the PARSED float
  }
  v.set(o.dest, stored, false);
  v.mark_given(o.dest);
}

}  // namespace

ParseResult parse_args(const ToolSpec& tool, const std::vector<std::string>& argv) {
  ParseResult res;
  const Lookup lk = build_lookup(tool);

  // optparse's get_default_values() copies parser.defaults IN FULL, so the
  // set_defaults-only dests come first and can then be overridden by an option's
  // own default if one exists for the same dest.
  for (const auto& e : tool.extra_defaults) {
    if (e.dest == nullptr) continue;
    res.values.set(e.dest, e.default_is_none ? "" : e.default_repr, e.default_is_none);
  }

  // optparse applies every default first (get_default_values).
  auto apply_defaults = [&res](const OptionSpec& o) {
    if (o.dest == nullptr || *o.dest == '\0') return;
    res.values.set(o.dest, o.default_is_none ? "" : o.default_repr, o.default_is_none);
  };
  for (const auto& o : tool.top_level) apply_defaults(o);
  for (const auto& g : tool.groups)
    for (const auto& o : g.options) apply_defaults(o);

  // argv[0] is the program name; the tools pass sys.argv with it removed by the
  // dispatcher, so the caller hands us the list starting at the subcommand.
  std::size_t i = 0;
  bool only_positional = false;
  // In optparse the -h callback (callbackShortHelp -> print_help();
  // parser.exit()) and the version action run DURING parsing and TERMINATE
  // there, so a bad token LATER in argv is never reached. Parsing all of argv
  // first and only then inspecting wants_help turned
  // `dedup --help --zzz` into `error: no such option: --zzz`, exit 2, where
  // upstream prints the help and exits 0. Order still matters and is
  // preserved: `dedup --zzz --help` is exit 2 on BOTH sides, because optparse
  // reaches the bad token first.
  bool stop_parsing = false;
  while (i < argv.size()) {
    if (stop_parsing) break;      // optparse exited inside the option's action
    const std::string& tok = argv[i];

    if (only_positional || tok == "-" || tok.size() < 2 || tok[0] != '-') {
      res.args.push_back(tok);
      ++i;
      continue;
    }
    if (tok == "--") {
      only_positional = true;
      ++i;
      continue;
    }

    // Applies one option once its value is resolved. Split out from argv
    // consumption because optparse's short-option path applies SEVERAL options
    // from a single token, so the two cannot stay fused.
    auto apply_option = [&](const OptionSpec& o, const std::string& opt_str,
                            const std::optional<std::string>& value) {
      switch (o.action) {
        case OptAction::StoreTrue:  res.values.set(o.dest, "True", false);  break;
        case OptAction::StoreFalse: res.values.set(o.dest, "False", false); break;
        case OptAction::Version:
          res.wants_version = true;
          stop_parsing = true;
          break;
        case OptAction::Callback:
          // The only callback in the package is callbackShortHelp on -h/--help,
          // and it does parser.set_description(None) before print_help().
          res.wants_help = true;
          stop_parsing = true;
          break;
        case OptAction::HelpAction:
          // --help-extended: optparse's own help action, which keeps the
          // description and so prints the module docstring in full.
          res.wants_help = true;
          res.help_extended = true;
          stop_parsing = true;
          break;
        case OptAction::Store:
          store(res.values, o, *value, opt_str);
          break;
      }
    };

    if (tok.rfind("--", 0) == 0) {
      std::string opt_str = tok;
      std::optional<std::string> inline_value;
      const std::size_t eq = tok.find('=');
      if (eq != std::string::npos) {
        opt_str = tok.substr(0, eq);
        inline_value = tok.substr(eq + 1);
      }

      // optparse._match_long_opt: a long option may be given by any UNIQUE
      // PREFIX. An exact match always wins; otherwise the prefix must select
      // exactly one. Measured against the live oracle:
      //   --unmapped=discard -> --unmapped-reads   (the shipped fixtures rely on this)
      //   --un    -> "ambiguous option: --un (--unmapped-reads, --unpaired-reads?)"
      //   --zzz   -> "no such option: --zzz"
      auto it = lk.by_opt.find(opt_str);
      if (it == lk.by_opt.end()) {
        std::vector<std::string> possibilities;
        for (const auto& [name, _] : lk.by_opt) {
          (void)_;
          if (name.rfind("--", 0) == 0 && name.rfind(opt_str, 0) == 0)
            possibilities.push_back(name);
        }
        if (possibilities.size() == 1) {
          it = lk.by_opt.find(possibilities.front());
        } else if (possibilities.size() > 1) {
          std::sort(possibilities.begin(), possibilities.end());
          std::string joined;
          for (std::size_t k = 0; k < possibilities.size(); ++k) {
            if (k) joined += ", ";
            joined += possibilities[k];
          }
          throw std::invalid_argument("ambiguous option: " + opt_str + " (" + joined + "?)");
        }
      }
      if (it == lk.by_opt.end())
        throw std::invalid_argument("no such option: " + opt_str);
      // optparse reports the CANONICAL name, not what the user typed: an
      // abbreviated `--mapping-q=abc` is rejected as `option --mapping-quality:
      // invalid integer value`. _match_long_opt returns the resolved name and
      // everything downstream uses it.
      opt_str = it->first;
      const OptionSpec& o = *it->second;

      ++i;
      // optparse.Option.takes_value(): a flag given `=value` is an ERROR, not a
      // flag. MEASURED: `dedup --paired=1` exits 2 upstream; here it set the
      // flag and ran a full dedup, writing a BAM to stdout. Silent.
      if (inline_value && o.action != OptAction::Store)
        throw std::invalid_argument(opt_str + " option does not take a value");
      if (o.action == OptAction::Store) {
        std::string value;
        if (inline_value) {
          value = *inline_value;
        } else {
          if (i >= argv.size())
            throw std::invalid_argument(opt_str + " option requires 1 argument");
          value = argv[i++];
        }
        apply_option(o, opt_str, value);
      } else {
        apply_option(o, opt_str, std::nullopt);
      }
      continue;
    }

    // optparse._process_short_opts: a short token is a CLUSTER of characters,
    // each its own option. MEASURED: the oracle parses `dedup -io` as in_sam
    // AND out_sam; treating the token as a single option silently dropped every
    // character after the first, and `-ix` (x undefined) exited 0 instead of 2.
    // The first value-taking option consumes the token remainder, or the next
    // argv element when the remainder is empty, and ends the cluster.
    ++i;
    for (std::size_t c = 1; c < tok.size(); ++c) {
      const std::string opt_str = std::string("-") + tok[c];
      const auto sit = lk.by_opt.find(opt_str);
      if (sit == lk.by_opt.end())
        throw std::invalid_argument("no such option: " + opt_str);
      const OptionSpec& o = *sit->second;
      if (o.action != OptAction::Store) {
        apply_option(o, opt_str, std::nullopt);
        continue;
      }
      std::string value;
      if (c + 1 < tok.size()) {
        value = tok.substr(c + 1);       // -Sout.tsv
      } else {
        if (i >= argv.size())
          throw std::invalid_argument(opt_str + " option requires 1 argument");
        value = argv[i++];
      }
      apply_option(o, opt_str, value);
      break;                             // optparse: stop = True
    }
  }
  return res;
}

namespace {

constexpr const char* kDocsLine =
    "For full UMI-tools documentation, see https://umi-tools.readthedocs.io/en/latest/";
}  // namespace

MaybeNoUsage::MaybeNoUsage(const ToolSpec& base, const std::vector<std::string>& argv)
    : active_(&base) {
  if (std::find(argv.begin(), argv.end(), "--no-usage") == argv.end()) return;

  opts_.assign(base.top_level.begin(), base.top_level.end());

  OptionSpec nu{};
  nu.long_opts = StrList{{"--no-usage"}, 1};
  nu.action = OptAction::StoreTrue;
  nu.type = OptType::String;
  nu.dest = "help_no_usage";
  nu.default_repr = "None";
  nu.default_is_none = true;
  nu.help = "output help without usage information";

  // optparse adds --version in _populate_option_list, before the tool's own
  // options; Utilities adds --no-usage at the END of __init__, so it lands
  // directly after it. MEASURED: it is line 7 of `dedup --no-usage --help`,
  // between --version and the first option group.
  auto at = opts_.begin();
  for (auto it = opts_.begin(); it != opts_.end(); ++it) {
    for (const char* l : it->long_opts) {
      if (std::string_view(l) == "--version") at = it + 1;
    }
  }
  opts_.insert(at, nu);

  usage_ = std::string(base.name) + " [options]";
  rewritten_ = base;
  rewritten_.usage = usage_.c_str();
  rewritten_.top_level = opts_;
  active_ = &rewritten_;
}

std::string usage_block(const ToolSpec& spec) {
  return std::string(spec.usage) + "\n\n" + kDocsLine + "\n\n";
}

void parser_error(const ToolSpec& spec, const std::string& message) {
  throw ExitRequest{2, usage_block(spec) + spec.name + ": error: " + message + "\n"};
}

}  // namespace umi_tools
