#include "rng.h"

void rng_seed(Rng *r, uint64_t seed, uint64_t seq)
{
    r->state = 0;
    r->inc = (seq << 1u) | 1u;
    rng_next(r);
    r->state += seed;
    rng_next(r);
}

uint32_t rng_next(Rng *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ULL + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    if (n == 0)
        return 0;
    /* Rechazo para evitar sesgo de módulo. */
    uint32_t threshold = (uint32_t)(-n) % n;
    for (;;) {
        uint32_t v = rng_next(r);
        if (v >= threshold)
            return v % n;
    }
}
