/* PCG32: PRNG pequeño y determinista. Su estado vive dentro del GameState. */
#ifndef LLV_RNG_H
#define LLV_RNG_H

#include <stdint.h>

typedef struct {
    uint64_t state;
    uint64_t inc;
} Rng;

void rng_seed(Rng *r, uint64_t seed, uint64_t seq);
uint32_t rng_next(Rng *r);
/* Entero uniforme en [0, n). n == 0 devuelve 0. */
uint32_t rng_below(Rng *r, uint32_t n);

#endif
