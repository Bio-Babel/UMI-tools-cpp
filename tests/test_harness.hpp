// Minimal case-addressable test harness.
//
// It exists so that each case can be registered as its OWN ctest
// (`add_test(NAME <case> COMMAND <bin> --case <case>)`). The scaffold registers
// one ctest per executable; leaving it that way would report two tests no matter
// how many cases exist, and neither ctest output nor test_coverage_diff.py could
// say which case broke.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace umi_tools_test {

using CaseFn = std::function<void()>;

inline std::map<std::string, CaseFn>& registry() {
  static std::map<std::string, CaseFn> r;
  return r;
}

struct Registrar {
  Registrar(const std::string& name, CaseFn fn) { registry()[name] = std::move(fn); }
};

#define UMI_TEST_CASE(name)                                            \
  static void name();                                                  \
  static ::umi_tools_test::Registrar registrar_##name(#name, name);    \
  static void name()

struct Failure {
  std::string what;
};

inline void fail(const std::string& msg) { throw Failure{msg}; }

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond))                                                                \
      ::umi_tools_test::fail(std::string(__FILE__) + ":" +                       \
                             std::to_string(__LINE__) + ": CHECK(" #cond ")");   \
  } while (0)

template <class A, class B>
void check_eq_impl(const A& a, const B& b, const char* sa, const char* sb,
                   const char* file, int line) {
  if (!(a == b)) {
    std::ostringstream os;
    os << file << ":" << line << ": CHECK_EQ(" << sa << ", " << sb << ")\n"
       << "  lhs = " << a << "\n  rhs = " << b;
    fail(os.str());
  }
}

#define CHECK_EQ(a, b) ::umi_tools_test::check_eq_impl((a), (b), #a, #b, __FILE__, __LINE__)

// Renders nested vectors of byte strings the way the Python prints them, so a
// mismatch can be pasted straight into a comparison with the oracle's output.
inline std::string repr(const std::vector<std::string>& v) {
  std::string s = "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) s += ", ";
    s += "'" + v[i] + "'";
  }
  return s + "]";
}
inline std::string repr(const std::vector<std::vector<std::string>>& v) {
  std::string s = "[";
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (i) s += ", ";
    s += repr(v[i]);
  }
  return s + "]";
}

// Throws-with-the-right-type checks. Python's exception TYPE is part of the
// contract (06_design.md), so a case that only asserts "something threw" would
// pass for the wrong reason.
#define CHECK_THROWS_AS(expr, ExcType)                                          \
  do {                                                                          \
    bool caught = false;                                                        \
    try {                                                                       \
      (void)(expr);                                                             \
    } catch (const ExcType&) {                                                   \
      caught = true;                                                             \
    } catch (...) {                                                              \
      ::umi_tools_test::fail(std::string(__FILE__) + ":" +                       \
                             std::to_string(__LINE__) +                          \
                             ": threw the WRONG type, expected " #ExcType);      \
    }                                                                            \
    if (!caught)                                                                 \
      ::umi_tools_test::fail(std::string(__FILE__) + ":" +                       \
                             std::to_string(__LINE__) +                          \
                             ": did not throw " #ExcType);                       \
  } while (0)

inline int main_impl(int argc, char** argv) {
  std::string requested;
  bool list = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--list") list = true;
    else if (a == "--case" && i + 1 < argc) requested = argv[++i];
    else if (a.rfind("--case=", 0) == 0) requested = a.substr(7);
  }

  if (list) {
    for (const auto& [name, _] : registry()) std::cout << name << "\n";
    return 0;
  }

  auto run_one = [](const std::string& name, const CaseFn& fn) -> bool {
    try {
      fn();
      return true;
    } catch (const Failure& f) {
      std::cerr << "FAIL " << name << "\n" << f.what << "\n";
    } catch (const std::exception& e) {
      std::cerr << "FAIL " << name << "\n  unexpected exception: " << e.what() << "\n";
    } catch (...) {
      std::cerr << "FAIL " << name << "\n  unexpected non-std exception\n";
    }
    return false;
  };

  if (!requested.empty()) {
    auto it = registry().find(requested);
    if (it == registry().end()) {
      std::cerr << "unknown case: " << requested << "\n";
      return 2;
    }
    return run_one(it->first, it->second) ? 0 : 1;
  }

  int failed = 0;
  for (const auto& [name, fn] : registry())
    if (!run_one(name, fn)) ++failed;
  std::cout << registry().size() - static_cast<std::size_t>(failed) << "/"
            << registry().size() << " cases passed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace umi_tools_test
