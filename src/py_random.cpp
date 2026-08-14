#include "umi_tools/py_random.hpp"

#include <cstdlib>

namespace umi_tools {

// Matsumoto & Nishimura's reference MT19937, as vendored in CPython's
// Modules/_randommodule.c. Reproduced rather than delegated to std::mt19937,
// whose seeding differs (see the header).
void PyRandom::init_genrand(std::uint32_t s) {
  mt_[0] = s;
  for (mti_ = 1; mti_ < kN; ++mti_) {
    mt_[mti_] = (1812433253UL * (mt_[mti_ - 1] ^ (mt_[mti_ - 1] >> 30)) +
                 static_cast<std::uint32_t>(mti_));
  }
}

void PyRandom::init_by_array(const std::vector<std::uint32_t>& key) {
  init_genrand(19650218UL);
  int i = 1, j = 0;
  int k = (kN > static_cast<int>(key.size())) ? kN : static_cast<int>(key.size());
  for (; k; --k) {
    mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1664525UL)) +
             key[static_cast<std::size_t>(j)] + static_cast<std::uint32_t>(j);
    ++i;
    ++j;
    if (i >= kN) {
      mt_[0] = mt_[kN - 1];
      i = 1;
    }
    if (j >= static_cast<int>(key.size())) j = 0;
  }
  for (k = kN - 1; k; --k) {
    mt_[i] = (mt_[i] ^ ((mt_[i - 1] ^ (mt_[i - 1] >> 30)) * 1566083941UL)) -
             static_cast<std::uint32_t>(i);
    ++i;
    if (i >= kN) {
      mt_[0] = mt_[kN - 1];
      i = 1;
    }
  }
  mt_[0] = 0x80000000UL;   // MSB is 1, assuring a non-zero initial array
}

void PyRandom::seed_numpy(std::int64_t n) {
  init_genrand(static_cast<std::uint32_t>(static_cast<std::uint64_t>(n) & 0xFFFFFFFFu));
}

void PyRandom::seed(std::int64_t n) {
  // CPython: n = abs(arg), then the 32-bit little-endian words of n.
  // A seed of 0 gives a single zero word, not an empty key.
  std::uint64_t v = (n < 0) ? static_cast<std::uint64_t>(-(n + 1)) + 1ULL
                            : static_cast<std::uint64_t>(n);
  std::vector<std::uint32_t> key;
  if (v == 0) {
    key.push_back(0);
  } else {
    while (v != 0) {
      key.push_back(static_cast<std::uint32_t>(v & 0xffffffffULL));
      v >>= 32;
    }
  }
  init_by_array(key);
}

std::uint32_t PyRandom::genrand_uint32() {
  std::uint32_t y;
  static const std::uint32_t mag01[2] = {0x0UL, kMatrixA};

  if (mti_ >= kN) {
    int kk;
    if (mti_ == kN + 1) init_genrand(5489UL);   // never reached: seed() always runs
    for (kk = 0; kk < kN - kM; ++kk) {
      y = (mt_[kk] & kUpperMask) | (mt_[kk + 1] & kLowerMask);
      mt_[kk] = mt_[kk + kM] ^ (y >> 1) ^ mag01[y & 0x1UL];
    }
    for (; kk < kN - 1; ++kk) {
      y = (mt_[kk] & kUpperMask) | (mt_[kk + 1] & kLowerMask);
      mt_[kk] = mt_[kk + (kM - kN)] ^ (y >> 1) ^ mag01[y & 0x1UL];
    }
    y = (mt_[kN - 1] & kUpperMask) | (mt_[0] & kLowerMask);
    mt_[kN - 1] = mt_[kM - 1] ^ (y >> 1) ^ mag01[y & 0x1UL];
    mti_ = 0;
  }

  y = mt_[mti_++];
  y ^= (y >> 11);
  y ^= (y << 7) & 0x9d2c5680UL;
  y ^= (y << 15) & 0xefc60000UL;
  y ^= (y >> 18);
  return y;
}

double PyRandom::random() {
  // CPython random_random(): genrand_res53 over two words.
  const std::uint32_t a = genrand_uint32() >> 5;
  const std::uint32_t b = genrand_uint32() >> 6;
  return (static_cast<double>(a) * 67108864.0 + static_cast<double>(b)) *
         (1.0 / 9007199254740992.0);
}

}  // namespace umi_tools
