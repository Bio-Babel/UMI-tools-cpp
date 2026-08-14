#include "umi_tools/pattern.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>

namespace umi_tools {
namespace {

constexpr std::int64_t kInf = std::numeric_limits<std::int64_t>::max();

enum class FuzzyKind { None, Sub, Err, Ins, Del };

// --bc-pattern is arbitrary user input handed to regex.compile, so the
// five constructs the parser used to reject at construction are implemented
// here: character classes, alternation, escapes, non-capturing groups and lazy
// quantifiers. Each is validated against the LIVE `regex` module in
// validation/parity_pattern.py rather than reasoned about.
struct CharClass {
  bool negated = false;
  std::vector<std::pair<unsigned char, unsigned char>> ranges;   // inclusive
  bool matches(unsigned char c) const {
    bool in = false;
    for (const auto& [lo, hi] : ranges)
      if (c >= lo && c <= hi) { in = true; break; }
    return in != negated;
  }
};

struct Atom {
  enum class Kind { Char, Any, Group, AnchorStart, AnchorEnd, Class, Alt } kind = Kind::Char;

  char ch = '\0';                 // Kind::Char
  std::vector<Atom> sub;          // Kind::Group
  std::string name;               // Kind::Group; empty = non-capturing
  CharClass cls;                  // Kind::Class
  std::vector<std::vector<Atom>> alts;   // Kind::Alt — tried in source order
  // `??`, `*?`, `+?`, `{m,n}?`: prefer the SHORTEST match. Python's lazy
  // quantifiers, which the parser used to reject.
  bool lazy = false;

  // Quantifier. Defaults to exactly once.
  std::int64_t rep_min = 1;
  std::int64_t rep_max = 1;

  // Fuzzy quantifier, applied to THIS atom (the preceding element).
  FuzzyKind fuzzy = FuzzyKind::None;
  std::int64_t fuzzy_max = 0;
  // Set when the fuzzy quantifier applies to a pure-literal atom; holds that
  // literal. Fuzzy is only ever applied to a literal in this package (see the
  // header), and that is checked at parse time.
  std::string fuzzy_literal;
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
class Parser {
 public:
  explicit Parser(std::string_view p) : p_(p) {}

  std::vector<Atom> parse_sequence(bool inside_group) {
    std::vector<Atom> out;
    while (i_ < p_.size()) {
      const char c = p_[i_];
      if (c == ')') {
        if (!inside_group) throw std::invalid_argument("pattern: unbalanced ')'");
        return finish_alts(std::move(out));
      }
      // Alternation splits the sequence being built. Collected here and folded
      // into a single Alt atom when the branch ends, so `a|b|c` is one atom
      // with three alternatives, tried left to right as Python does.
      if (c == '|') {
        ++i_;
        alt_branches_.push_back(std::move(out));
        out.clear();
        continue;
      }

      if (c == '{') {
        // A quantifier must follow an atom.
        if (out.empty()) throw std::invalid_argument("pattern: quantifier with no atom");
        apply_brace(out.back());
        continue;
      }
      if (c == '*' || c == '+' || c == '?') {
        if (out.empty()) throw std::invalid_argument("pattern: quantifier with no atom");
        ++i_;
        Atom& a = out.back();
        if (c == '*') { a.rep_min = 0; a.rep_max = kInf; }
        else if (c == '+') { a.rep_min = 1; a.rep_max = kInf; }
        else { a.rep_min = 0; a.rep_max = 1; }
        // A '?' directly after a quantifier would mean non-greedy; none of the
        // package's patterns use it, so it is rejected rather than silently
        // treated as greedy.
        if (i_ < p_.size() && p_[i_] == '?') { a.lazy = true; ++i_; }
        continue;
      }

      Atom a;
      if (c == '^') { a.kind = Atom::Kind::AnchorStart; ++i_; }
      else if (c == '$') { a.kind = Atom::Kind::AnchorEnd; ++i_; }
      else if (c == '.') { a.kind = Atom::Kind::Any; ++i_; }
      else if (c == '(') { a = parse_group(); }
      else if (c == '[') { a = parse_class(); }
      else if (c == '\\') { a = parse_escape(); }
      else { a.kind = Atom::Kind::Char; a.ch = c; ++i_; }
      out.push_back(std::move(a));
    }
    if (inside_group) throw std::invalid_argument("pattern: unterminated group");
    return finish_alts(std::move(out));
  }

  // `a|b` at this nesting level becomes ONE Alt atom. Each branch keeps its own
  // sequence, and Python tries them left to right, so the vector order is the
  // match order.
  std::vector<Atom> finish_alts(std::vector<Atom> tail) {
    if (alt_branches_.empty()) return tail;
    alt_branches_.push_back(std::move(tail));
    Atom a;
    a.kind = Atom::Kind::Alt;
    a.alts = std::move(alt_branches_);
    alt_branches_.clear();
    return std::vector<Atom>{std::move(a)};
  }

 private:
  // Branches collected at THIS nesting level; parse_group recurses with its
  // own Parser state via save/restore below, so branches never leak across a
  // group boundary.
  std::vector<std::vector<Atom>> alt_branches_;

  // `[ACGT]`, `[^ACGT]`, `[A-Za-z0-9_]`. Python's rules for the awkward
  // positions are reproduced: a `]` FIRST is a literal, and a `-` first or last
  // is a literal.
  Atom parse_class() {
    ++i_;  // '['
    Atom a;
    a.kind = Atom::Kind::Class;
    if (i_ < p_.size() && p_[i_] == '^') { a.cls.negated = true; ++i_; }
    bool first = true;
    while (i_ < p_.size() && (p_[i_] != ']' || first)) {
      unsigned char lo;
      if (p_[i_] == '\\') {
        ++i_;
        if (i_ >= p_.size()) throw std::invalid_argument("pattern: trailing backslash");
        const char e = p_[i_];
        ++i_;
        const EscRanges esc = escape_class(e);
        if (esc.n > 0) {
          // A CLASS escape (\d \w \s and their negations) contributes its whole
          // set, never a single character. Taking only ranges[0].first turned
          // `[\dA]` into `[0A]`, which matched '0' and rejected '1'..'9'; and a
          // negated escape has to be expanded to its COMPLEMENT here, because
          // this class carries one `negated` flag for the whole set and cannot
          // represent a per-member negation.
          std::vector<std::pair<unsigned char, unsigned char>> base(esc.r, esc.r + esc.n);
          const auto add = esc.negated ? complement(base) : base;
          for (const auto& r : add) a.cls.ranges.push_back(r);
          first = false;
          continue;
        }
        // Not a class escape: an escaped literal, e.g. `[\.\-]`.
        switch (e) {
          case 'n': lo = static_cast<unsigned char>('\n'); break;
          case 't': lo = static_cast<unsigned char>('\t'); break;
          case 'r': lo = static_cast<unsigned char>('\r'); break;
          default:  lo = static_cast<unsigned char>(e);     break;
        }
      } else {
        lo = static_cast<unsigned char>(p_[i_]);
        ++i_;
      }
      // A range, unless the '-' is the last character before ']'.
      if (i_ + 1 < p_.size() && p_[i_] == '-' && p_[i_ + 1] != ']') {
        ++i_;
        unsigned char hi = static_cast<unsigned char>(p_[i_]);
        if (p_[i_] == '\\') {
          ++i_;
          if (i_ >= p_.size()) throw std::invalid_argument("pattern: trailing backslash");
          hi = static_cast<unsigned char>(p_[i_]);
        }
        ++i_;
        if (hi < lo) throw std::invalid_argument("pattern: bad character range in []");
        a.cls.ranges.emplace_back(lo, hi);
      } else {
        a.cls.ranges.emplace_back(lo, lo);
      }
      first = false;
    }
    if (i_ >= p_.size()) throw std::invalid_argument("pattern: unterminated character class");
    ++i_;  // ']'
    return a;
  }

  static std::vector<std::pair<unsigned char, unsigned char>> complement(
      const std::vector<std::pair<unsigned char, unsigned char>>& in) {
    std::vector<std::pair<unsigned char, unsigned char>> out;
    int c = 0;
    while (c <= 255) {
      bool inside = false;
      for (const auto& [lo, hi] : in)
        if (c >= lo && c <= hi) { inside = true; c = hi + 1; break; }
      if (inside) continue;
      const int start = c;
      while (c <= 255) {
        bool hit = false;
        for (const auto& [lo, hi] : in) if (c >= lo && c <= hi) { hit = true; break; }
        if (hit) break;
        ++c;
      }
      out.emplace_back(static_cast<unsigned char>(start), static_cast<unsigned char>(c - 1));
    }
    return out;
  }

  // The class an escape denotes. ASCII semantics, which is what these patterns
  // are: read sequences and quality strings.
  //
  // Filled through an OUT-PARAMETER rather than returned by value. Returning a
  // CharClass here made GCC 12.4 emit a -Wfree-nonheap-object error at -O2 from
  // inside std::vector's deallocate — a false positive about the returned
  // vector under inlining, but an ERROR under the step-13 -Werror gate. Writing
  // into the caller's object avoids the temporary and the diagnostic, and is
  // not worse code.
  // A class escape has at most SIX ranges (\s), so it is returned in a fixed
  // array with a count and no heap allocation at all. A std::vector here — by
  // value OR through an out-parameter — made GCC 12.4 emit a
  // -Wfree-nonheap-object error at -O2 from the loop-local's destructor inside
  // parse_class: a false positive about a vector whose size it cannot bound,
  // but an ERROR under the step-13 -Werror gate. No allocation, no diagnostic,
  // and the bound is a fact about the escapes rather than a workaround.
  struct EscRanges {
    std::pair<unsigned char, unsigned char> r[6];
    int n = 0;
    bool negated = false;
    void add(unsigned char lo, unsigned char hi) { r[n++] = {lo, hi}; }
  };

  static EscRanges escape_class(char e) {
    EscRanges c;
    switch (e) {
      case 'D': c.negated = true; [[fallthrough]];
      case 'd': c.add('0', '9'); return c;
      case 'W': c.negated = true; [[fallthrough]];
      case 'w': c.add('0','9'); c.add('A','Z'); c.add('a','z'); c.add('_','_'); return c;
      case 'S': c.negated = true; [[fallthrough]];
      case 's': c.add(' ',' '); c.add('\t','\t'); c.add('\n','\n');
                c.add('\v','\v'); c.add('\f','\f'); c.add('\r','\r'); return c;
      default: return c;   // n == 0 means "a literal", see callers
    }
  }


  // `\d`, `\w`, `\s` and their negations become a Class; anything else is the
  // ESCAPED LITERAL, which is how `\.` and `\+` work.
  Atom parse_escape() {
    ++i_;  // backslash
    if (i_ >= p_.size()) throw std::invalid_argument("pattern: trailing backslash");
    const char e = p_[i_];
    ++i_;
    Atom a;
    const EscRanges c = escape_class(e);
    if (c.n > 0) {
      a.kind = Atom::Kind::Class;
      a.cls.negated = c.negated;
      a.cls.ranges.assign(c.r, c.r + c.n);
      return a;
    }
    a.kind = Atom::Kind::Char;
    switch (e) {
      case 'n': a.ch = '\n'; break;
      case 't': a.ch = '\t'; break;
      case 'r': a.ch = '\r'; break;
      default:  a.ch = e;     break;   // \. \+ \( \\ ...
    }
    return a;
  }

  Atom parse_group() {
    ++i_;  // '('
    Atom g;
    g.kind = Atom::Kind::Group;
    if (p_.compare(i_, 3, "?P<") == 0) {
      i_ += 3;
      const std::size_t close = p_.find('>', i_);
      if (close == std::string_view::npos)
        throw std::invalid_argument("pattern: unterminated group name");
      g.name = std::string(p_.substr(i_, close - i_));
      i_ = close + 1;
    } else if (p_.compare(i_, 2, "?:") == 0) {
      // A plain non-capturing group. The Atom already models "no name" as
      // non-capturing, so this only had to be PARSED.
      i_ += 2;
    } else if (i_ < p_.size() && p_[i_] == '?') {
      // Lookaround, flags, backreferences, atomic groups: still out of scope,
      // and still rejected loudly rather than mis-parsed. No umi_tools pattern
      // uses one, and unlike the five constructs above they change the matching
      // model rather than adding a token.
      throw std::invalid_argument(
          "pattern: only (?P<name>...) and (?:...) group extensions are "
          "supported");
    }
    // A group is its own alternation scope: `(a|b)c` must not fold the `c`.
    auto saved = std::move(alt_branches_);
    alt_branches_.clear();
    g.sub = parse_sequence(/*inside_group=*/true);
    alt_branches_ = std::move(saved);
    if (i_ >= p_.size() || p_[i_] != ')')
      throw std::invalid_argument("pattern: unterminated group");
    ++i_;  // ')'
    return g;
  }

  // `{...}` is either a repetition ({n} / {n,m}) or a fuzzy quantifier
  // ({s<=N}, {e<=N}, {i<=N}, {d<=N}). They are told apart by the `<=`.
  void apply_brace(Atom& target) {
    const std::size_t close = p_.find('}', i_);
    if (close == std::string_view::npos)
      throw std::invalid_argument("pattern: unterminated '{'");
    const std::string body(p_.substr(i_ + 1, close - i_ - 1));
    i_ = close + 1;
    // `{m,n}?` is LAZY, exactly like `*?`/`+?`/`??`. Without consuming
    // the '?' here it was left in the stream as a `?` quantifier applied to the
    // same atom, which overwrote rep_min/rep_max with 0..1 — measured as
    // `(?P<umi_1>.{2,4}?)(?P<discard_1>Z.*)` matching one character where the
    // minimum is two.
    bool lazy_brace = false;
    if (i_ < p_.size() && p_[i_] == '?') { lazy_brace = true; ++i_; }

    const std::size_t le = body.find("<=");
    if (le != std::string::npos) {
      const std::string kind = trim(body.substr(0, le));
      const std::int64_t n = std::stoll(trim(body.substr(le + 2)));
      FuzzyKind fk;
      if (kind == "s") fk = FuzzyKind::Sub;
      else if (kind == "e") fk = FuzzyKind::Err;
      else if (kind == "i") fk = FuzzyKind::Ins;
      else if (kind == "d") fk = FuzzyKind::Del;
      else
        throw std::invalid_argument("pattern: unknown fuzzy kind '" + kind + "'");

      // Fuzzy is only ever applied to a literal in this package. Enforce it
      // rather than assume it: a fuzzy quantifier on `.` or on a repetition
      // would need general approximate matching, and if one ever appears this
      // must fail loudly instead of quietly matching something else.
      std::string lit;
      if (!literal_of(target, lit))
        throw std::invalid_argument(
            "pattern: a fuzzy quantifier is applied to a non-literal element; "
            "01_audit.md D1 measured that umi_tools only ever applies fuzzy to a "
            "literal character or to a group containing a pure literal");
      target.fuzzy = fk;
      target.fuzzy_max = n;
      target.fuzzy_literal = lit;
      return;
    }

    const std::size_t comma = body.find(',');
    if (comma == std::string::npos) {
      const std::int64_t n = std::stoll(trim(body));
      target.rep_min = target.rep_max = n;
    } else {
      const std::string lo = trim(body.substr(0, comma));
      const std::string hi = trim(body.substr(comma + 1));
      target.rep_min = lo.empty() ? 0 : std::stoll(lo);
      target.rep_max = hi.empty() ? kInf : std::stoll(hi);
    }
    if (lazy_brace) target.lazy = true;
  }

  // The literal text of an atom, if it is one: a single Char with no
  // repetition, or a Group whose contents are all such Chars.
  static bool literal_of(const Atom& a, std::string& out) {
    if (a.rep_min != 1 || a.rep_max != 1) return false;
    if (a.kind == Atom::Kind::Char) { out = std::string(1, a.ch); return true; }
    if (a.kind != Atom::Kind::Group) return false;
    std::string acc;
    for (const Atom& s : a.sub) {
      if (s.kind != Atom::Kind::Char || s.rep_min != 1 || s.rep_max != 1) return false;
      acc += s.ch;
    }
    out = acc;
    return true;
  }

  static std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
  }

  std::string_view p_;
  std::size_t i_ = 0;
};

// ---------------------------------------------------------------------------
// Matcher — backtracking with continuations.
// ---------------------------------------------------------------------------
struct Capture {
  std::int64_t start = -1;
  std::int64_t stop = -1;
};

using Captures = std::map<std::string, Capture>;
using Cont = std::function<bool(std::int64_t)>;

// Levenshtein distance, used only for `{e<=N}`'s boolean test.
std::int64_t levenshtein(std::string_view a, std::string_view b) {
  std::vector<std::int64_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<std::int64_t>(j);
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = static_cast<std::int64_t>(i);
    for (std::size_t j = 1; j <= b.size(); ++j) {
      const std::int64_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev = cur;
  }
  return prev[b.size()];
}

class Matcher {
 public:
  Matcher(const std::vector<Atom>& atoms, std::string_view text)
      : atoms_(atoms), text_(text) {}

  bool run(Captures& caps, std::int64_t& end_out) {
    return seq(atoms_, 0, 0, caps, [&](std::int64_t e) {
      end_out = e;
      return true;
    });
  }

 private:
  std::int64_t len() const { return static_cast<std::int64_t>(text_.size()); }

  // Match atoms[idx..] starting at pos; call k(end) on success.
  bool seq(const std::vector<Atom>& atoms, std::size_t idx, std::int64_t pos,
           Captures& caps, const Cont& k) {
    if (idx == atoms.size()) return k(pos);
    const Atom& a = atoms[idx];
    return repeat(a, 0, pos, caps, [&](std::int64_t p) {
      return seq(atoms, idx + 1, p, caps, k);
    });
  }

  // Greedy repetition with backtracking: try as many as possible, then give
  // back one at a time. `.{8,12}` and `.*` both rely on this, and the measured
  // spans confirm greedy-then-backtrack is what `regex` does.
  // Does one occurrence of this atom always consume EXACTLY one character and
  // capture nothing? `.` and a literal char do; a fuzzy literal, a group or an
  // anchor do not.
  static bool is_single_char_atom(const Atom& a) {
    // Class was added later: it also consumes exactly one character and captures
    // nothing, so `[ACGT]*` gets the same stack-safe path as `.*`. Alt does NOT
    // qualify — a branch can consume any width.
    return (a.kind == Atom::Kind::Any || a.kind == Atom::Kind::Char ||
            a.kind == Atom::Kind::Class) &&
           a.fuzzy == FuzzyKind::None;
  }

  bool repeat(const Atom& a, std::int64_t done, std::int64_t pos, Captures& caps,
              const Cont& k) {
    // FIXED-WIDTH FAST PATH, and it is a correctness fix not an optimisation.
    //
    // The general path below recurses once per occurrence CONSUMED, so for the
    // unbounded `.*` that ends every documented indrop pattern the depth equals
    // the read length. MEASURED: a 20,000-base read is fine, a 60,000-base read
    // (ONT/PacBio territory) overflows the 8 MB stack and the process dies with
    // SIGSEGV where the oracle returns 0.
    //
    // When one occurrence always consumes exactly one character and captures
    // nothing, greedy-then-backtrack is a plain loop over end positions: scan
    // forward to the furthest reachable point, then hand back one character at a
    // time. Same order of attempts, same first match, no stack growth.
    if (is_single_char_atom(a)) {
      // A LAZY single-char repetition walks the SAME positions in the
      // opposite order: shortest first. Handled here rather than falling
      // through to the general lazy path below, because `.*?` needs the
      // stack-safety this fast path exists to provide just as much as `.*`.
      if (a.lazy) {
        std::int64_t n = done, p = pos;
        while (n < a.rep_min) {                 // satisfy the minimum first
          std::int64_t next = -1;
          if (!one(a, p, caps, [&](std::int64_t q) { next = q; return true; })) return false;
          p = next;
          ++n;
        }
        for (;;) {
          if (k(p)) return true;
          if (n >= a.rep_max) return false;
          std::int64_t next = -1;
          if (!one(a, p, caps, [&](std::int64_t q) { next = q; return true; })) return false;
          p = next;
          ++n;
        }
      }
      std::int64_t n = done;
      std::int64_t p = pos;
      while (n < a.rep_max) {
        std::int64_t next = -1;
        // one() with a continuation that just records the end position: for a
        // single-char atom it is called at most once and never recurses.
        if (!one(a, p, caps, [&](std::int64_t q) { next = q; return true; })) break;
        p = next;
        ++n;
      }
      for (; n >= a.rep_min; --n, --p) {
        if (k(p)) return true;
        if (n == 0) break;
      }
      return false;
    }

    // A LAZY quantifier tries the shortest match first: satisfy the
    // minimum, hand control to the continuation, and only consume more if that
    // fails. Greedy is the mirror image and is below.
    if (a.lazy) {
      if (done >= a.rep_min && k(pos)) return true;
      if (done >= a.rep_max) return false;
      return one(a, pos, caps, [&](std::int64_t p) {
        if (p == pos && a.rep_max == kInf) return false;   // zero-width guard
        return repeat(a, done + 1, p, caps, k);
      });
    }

    if (done < a.rep_max) {
      // Try consuming one more occurrence first (greedy).
      const bool more = one(a, pos, caps, [&](std::int64_t p) {
        // Guard against a zero-width atom repeating forever.
        if (p == pos && a.rep_max == kInf) return done + 1 >= a.rep_min ? k(p) : false;
        return repeat(a, done + 1, p, caps, k);
      });
      if (more) return true;
    }
    if (done >= a.rep_min) return k(pos);
    return false;
  }

  // One occurrence of an atom.
  bool one(const Atom& a, std::int64_t pos, Captures& caps, const Cont& k) {
    switch (a.kind) {
      case Atom::Kind::AnchorStart:
        return pos == 0 ? k(pos) : false;
      case Atom::Kind::AnchorEnd:
        return pos == len() ? k(pos) : false;
      case Atom::Kind::Any:
        // `.` does not match a newline in Python without re.DOTALL, and no
        // umi_tools pattern sets that flag. Read sequences never contain one,
        // but the semantics are reproduced rather than assumed away.
        if (pos >= len() || text_[static_cast<std::size_t>(pos)] == '\n') return false;
        return k(pos + 1);
      case Atom::Kind::Char:
        if (a.fuzzy != FuzzyKind::None) return fuzzy_literal(a, pos, k);
        if (pos >= len() || text_[static_cast<std::size_t>(pos)] != a.ch) return false;
        return k(pos + 1);
      case Atom::Kind::Class:
        // Like Any, a class never matches past the end. Negated
        // classes DO match '\n' in Python (only `.` is special), so there is no
        // newline exclusion here.
        if (pos >= len()) return false;
        if (!a.cls.matches(static_cast<unsigned char>(text_[static_cast<std::size_t>(pos)])))
          return false;
        return k(pos + 1);
      case Atom::Kind::Alt:
        // Branches in SOURCE ORDER, each backtracking independently — Python
        // returns the first branch that lets the rest of the pattern match, not
        // the longest.
        for (const auto& branch : a.alts)
          if (seq(branch, 0, pos, caps, k)) return true;
        return false;
      case Atom::Kind::Group: {
        if (a.fuzzy != FuzzyKind::None) {
          // Fuzzy on a group: the group is a pure literal (checked at parse
          // time), so record the span over whatever it consumes.
          return fuzzy_literal(a, pos, [&](std::int64_t p) {
            return with_capture(a, pos, p, caps, k);
          });
        }
        const std::int64_t start = pos;
        return seq(a.sub, 0, pos, caps, [&](std::int64_t p) {
          return with_capture(a, start, p, caps, k);
        });
      }
    }
    return false;
  }

  bool with_capture(const Atom& a, std::int64_t start, std::int64_t stop, Captures& caps,
                    const Cont& k) {
    if (a.name.empty()) return k(stop);
    const Capture saved = caps.count(a.name) ? caps[a.name] : Capture{};
    caps[a.name] = Capture{start, stop};
    if (k(stop)) return true;
    caps[a.name] = saved;  // undo on backtrack
    return false;
  }

  // `{s<=N}` / `{e<=N}` / `{i<=N}` over a literal.
  bool fuzzy_literal(const Atom& a, std::int64_t pos, const Cont& k) {
    const std::string& lit = a.fuzzy_literal;
    const std::int64_t L = static_cast<std::int64_t>(lit.size());

    if (a.fuzzy == FuzzyKind::Sub) {
      // Substitutions only: consumes exactly L characters, at most N of which
      // may differ.
      if (pos + L > len()) return false;
      std::int64_t diffs = 0;
      for (std::int64_t j = 0; j < L; ++j) {
        if (text_[static_cast<std::size_t>(pos + j)] != lit[static_cast<std::size_t>(j)]) {
          if (++diffs > a.fuzzy_max) return false;
        }
      }
      return k(pos + L);
    }

    // `{e<=N}` (and `{i<=N}`, which is a strict subset): the consumed length may
    // vary by up to N. Prefer the longest consumption first, matching the
    // engine's preference for a longer match, then give back.
    //
    // NOTE ON SCOPE: the only live caller of `{e<=N}` is whitelist_methods'
    // checkError, which uses the BOOLEAN result only and never reads a span.
    // The consumption-length preference here is therefore validated for that
    // boolean use; if a future pattern reads a span from an `{e<=N}` element,
    // that ordering needs its own measurement. Recorded in 10_validation.md.
    const std::int64_t n = a.fuzzy_max;
    for (std::int64_t take = std::min(L + n, len() - pos); take >= std::max<std::int64_t>(0, L - n);
         --take) {
      const std::string_view window = text_.substr(static_cast<std::size_t>(pos),
                                                   static_cast<std::size_t>(take));
      if (levenshtein(window, lit) <= n) {
        if (k(pos + take)) return true;
      }
    }
    return false;
  }

  const std::vector<Atom>& atoms_;
  std::string_view text_;
};

}  // namespace

struct Pattern::Impl {
  std::vector<Atom> atoms;
  // Every NAMED group the pattern declares, in the order declared. Python's
  // groupdict() reports all of them, including any that did not participate in
  // the match; without this list `match()` could only report the ones the
  // matcher actually reached.
  std::vector<std::string> declared_names;
};

namespace {
// Walks the parsed atoms and collects declared group names, descending into
// nested groups so a name inside an optional group is still declared.
void collect_names(const std::vector<Atom>& atoms, std::vector<std::string>& out) {
  for (const Atom& a : atoms) {
    if (!a.name.empty() &&
        std::find(out.begin(), out.end(), a.name) == out.end())
      out.push_back(a.name);
    collect_names(a.sub, out);
  }
}
}  // namespace

Pattern::Pattern(std::string_view pattern)
    : impl_(std::make_unique<Impl>()), source_(pattern) {
  Parser parser(pattern);
  impl_->atoms = parser.parse_sequence(/*inside_group=*/false);
  collect_names(impl_->atoms, impl_->declared_names);
}

Pattern::~Pattern() = default;
Pattern::Pattern(Pattern&&) noexcept = default;
Pattern& Pattern::operator=(Pattern&&) noexcept = default;

std::optional<MatchResult> Pattern::match(std::string_view text) const {
  Captures caps;
  std::int64_t end = 0;
  Matcher m(impl_->atoms, text);
  if (!m.run(caps, end)) return std::nullopt;

  MatchResult r;
  r.end = end;
  // groupdict() reports EVERY declared name. A group that backtracked out never
  // reaches the loop below, so it is seeded here with Python's (-1, -1) span and
  // an empty text; ExtractBarcodes distinguishes it by span.start < 0 and raises
  // the TypeError Python raises on `umi += None`.
  for (const std::string& name : impl_->declared_names) {
    r.spans[name] = Span{-1, -1};
    r.groups[name] = std::string();
  }
  for (const auto& [name, cap] : caps) {
    r.spans[name] = Span{cap.start, cap.stop};
    if (cap.start >= 0)
      r.groups[name] = std::string(text.substr(static_cast<std::size_t>(cap.start),
                                               static_cast<std::size_t>(cap.stop - cap.start)));
    else
      r.groups[name] = std::string();
  }
  return r;
}

}  // namespace umi_tools
