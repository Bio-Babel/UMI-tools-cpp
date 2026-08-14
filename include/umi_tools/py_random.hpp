// py_random.hpp — a bit-exact replica of Python's `random` module.
//
// `01_audit.md` D6 recorded this as a CLAIM ("bit-reproducible with std::mt19937
// plus Python's genrand_res53 and init_by_array"); this file makes it code and
// validation/parity_random.py makes it measured.
//
// WHY IT MUST BE BIT-EXACT rather than merely "a Mersenne Twister": the draws
// choose WHICH READ is emitted, so a different stream is a different output file,
// not a statistically equivalent one. Two call sites, sharing ONE stream:
//
//   sam_methods.get_bundles.update_dicts:  if random.random() < 1.0/n
//       reservoir choice among equal-quality reads at the same position/UMI
//   sam_methods.get_bundles.__call__:      if random.random() >= options.subset
//       --subset read sampling
//
// Because they share a stream, the ORDER of calls is part of the contract:
// --subset draws before the reservoir draw for the same read.
//
// SEEDING — measured, and asymmetric between the two RNGs upstream uses:
//
//   Utilities.Start:1109   if global_options.random_seed is not None:
//                              random.seed(...)        # stdlib — `is not None`
//   count.py / dedup.py    if options.random_seed:
//                              np.random.seed(...)     # numpy  — TRUTHINESS
//
// So `--random-seed=0` seeds the stdlib RNG but NOT numpy's. That asymmetry is
// upstream's and is reproduced where each RNG is used.
//
// CPython's construction, read from Modules/_randommodule.c and verified against
// the live interpreter:
//   seed(int n):  key = 32-bit little-endian words of abs(n); init_by_array(key)
//   random():     a = genrand_uint32() >> 5;  b = genrand_uint32() >> 6;
//                 return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0)
//
// std::mt19937 is NOT used: its seeding is `seed_seq`/linear, not CPython's
// init_by_array, so it produces a different stream from the same integer. The
// generator core is reimplemented here so the seeding matches.
#pragma once

#include <cstdint>
#include <vector>

namespace umi_tools {

class PyRandom {
 public:
  PyRandom() { seed(0); }
  explicit PyRandom(std::int64_t s) { seed(s); }

  /// random.seed(n) for an integer n. Python takes abs(n) and splits it into
  /// 32-bit little-endian words; a seed of 0 yields the single word {0}.
  void seed(std::int64_t n);

  /// np.random.seed(n) — NUMPY'S LEGACY SEEDING, which is NOT the same as
  /// Python's. For a scalar that fits in 32 bits numpy calls init_genrand(n)
  /// directly, while CPython always goes through init_by_array. Same generator,
  /// same 53-bit double construction, DIFFERENT STREAM: measured over 6 draws at
  /// seed 123456789, numpy gives 0.53283302478975902 where random.random() gives
  /// 0.64140061618587263. dedup uses BOTH — Utilities.Start seeds the stdlib one
  /// for get_bundles, dedup.py seeds the numpy one for random_read_generator.
  void seed_numpy(std::int64_t n);

  /// random.random()
  double random();

  /// random.getrandbits(32) — one raw generator word. Exposed because it is the
  /// cheapest thing to compare against Python when diagnosing a stream drift.
  std::uint32_t getrandbits32() { return genrand_uint32(); }

 private:
  void init_genrand(std::uint32_t s);
  void init_by_array(const std::vector<std::uint32_t>& key);
  std::uint32_t genrand_uint32();

  static constexpr int kN = 624;
  static constexpr int kM = 397;
  static constexpr std::uint32_t kMatrixA = 0x9908b0dfUL;
  static constexpr std::uint32_t kUpperMask = 0x80000000UL;
  static constexpr std::uint32_t kLowerMask = 0x7fffffffUL;

  std::uint32_t mt_[kN];
  int mti_ = kN + 1;
};

/// The `random` MODULE's stream. Python's random module exposes a single hidden
/// Random instance, and --subset plus get_bundles' reservoir tie-break both draw
/// from it, so they must share one object here too.
PyRandom& global_random();

}  // namespace umi_tools
