#pragma once
#include <cstdint>

namespace pd::os {

// PCG32 XSH RR nach Melissa O'Neill (pcg-random.org). Folgt eng der Referenz
// pcg32_random_r / pcg32_srandom_r aus pcg-c-basic (Apache-2.0, (c) 2014 M.E. O'Neill).
struct Pcg32 {
  static void seed(uint64_t& state, uint64_t& inc, uint64_t initState, uint64_t seq) {
    state = 0;
    inc = (seq << 1u) | 1u;
    next(state, inc);
    state += initState;
    next(state, inc);
  }
  static uint32_t next(uint64_t& state, uint64_t inc) {
    uint64_t old = state;
    state = old * 6364136223846793005ULL + inc;
    uint32_t xorshifted = uint32_t(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = uint32_t(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
  }
};

}  // namespace pd::os
