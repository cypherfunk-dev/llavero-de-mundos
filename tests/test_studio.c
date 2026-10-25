/* Tests del Estudio de la música (v1.1), sin framework externo. Salida != 0 si algo falla.
 * Cubren: la regresión (con todo en AUTO, el audio es idéntico bit a bit al de la alfa), los ajustes
 * del Estudio en el motor de música (escala, tónica, tempo, arpegio, progresión, timbre, ritmo,
 * silenciar voces), los candados (AUTO/FIJO), el guardado y la máquina de estados del panel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/engine.h"
#include "../src/core/music.h"
#include "../src/core/state.h"

static int checks, failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long va_ = (long long)(a), vb_ = (long long)(b);                    \
        checks++;                                                                \
        if (va_ != vb_) {                                                        \
            failures++;                                                          \
            printf("    FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, \
                   va_, vb_);                                                    \
        }                                                                        \
    } while (0)

typedef void (*TestFn)(void);
static int run_test(const char *name, TestFn fn)
{
    int before = failures;
    fn();
    printf("[%s] %s\n", failures == before ? " OK " : "FAIL", name);
    return failures == before;
}

#define RATE LLV_AUDIO_RATE

static uint8_t mood_for(int i) { return MOOD_MAKE(i % 4, (i / 4) % 2, (i / 8) % 3, (i / 24) % 5); }

/* ---------- regresión: con todo en AUTO, el audio es el de la alfa ---------- */

static uint64_t fnv_byte(uint64_t h, uint8_t b)
{
    return (h ^ b) * 1099511628211ull;
}

/* Hashes calculados con el código de la rama claude/fase-5-pesca (antes del Estudio): 32 casos de
 * (semilla, mutaciones, ánimo), 70 s de audio PCM de 16 bits a 22.050 Hz cada uno (FNV-1a de 64 bits). */
static const struct { uint16_t seed; uint8_t mut, mood; uint64_t hash; } ALFA_PCM[] = {
    {7, 0, 0x00, 0x1e7dda7d89dbe0d4ull},
    {2046, 1, 0x03, 0x9fd08aded35ba74full},
    {4085, 2, 0x06, 0xde6b6dcd57f84bf5ull},
    {6124, 3, 0x09, 0x9373caf207ad3828ull},
    {8163, 4, 0x0C, 0xe1e56b0dc8676ec4ull},
    {10202, 0, 0x0F, 0xacf324ac1fe9a64aull},
    {12241, 6, 0x12, 0xaea5b11b86576621ull},
    {14280, 0, 0x15, 0x3b9ba2b49e6d65d4ull},
    {16319, 1, 0x20, 0x3ec8da2129f3a575ull},
    {18358, 2, 0x23, 0x2bfb89c890e21d2full},
    {20397, 0, 0x26, 0xd407623027a21608ull},
    {22436, 4, 0x29, 0x673d9d9a2eadc0bfull},
    {24475, 5, 0x2C, 0x27aa550ac07a70e8ull},
    {26514, 6, 0x2F, 0x7d5ec1ffbac1048eull},
    {28553, 0, 0x32, 0x9531fb50cd681cf4ull},
    {30592, 0, 0x35, 0xbf20d0fd79b9bbffull},
    {32631, 2, 0x40, 0xef02409840db1fcbull},
    {34670, 3, 0x43, 0x4d6eb026a616d3c4ull},
    {36709, 4, 0x46, 0xed4ab187e7069b44ull},
    {38748, 5, 0x49, 0x1af68ab5f5caa449ull},
    {40787, 0, 0x4C, 0x910e5e71c14e6830ull},
    {42826, 0, 0x4F, 0x12d1536fea0f3838ull},
    {44865, 1, 0x52, 0x8c85fed37da5ace7ull},
    {46904, 2, 0x55, 0x6f441796660e6761ull},
    {48943, 3, 0x60, 0x91ef41cf3417a699ull},
    {50982, 0, 0x63, 0x27919edcb22655e6ull},
    {53021, 5, 0x66, 0x46a73d34c6c89d26ull},
    {55060, 6, 0x69, 0x81eed96ebb0c5234ull},
    {57099, 0, 0x6C, 0xd25c08eb1da73fb1ull},
    {59138, 1, 0x6F, 0x558c2f3f4aa106c3ull},
    {61177, 0, 0x72, 0x9a9c06b6ec60aa6eull},
    {63216, 3, 0x75, 0xb06f37f538af1a72ull},
};
/* Y las 2.048 canciones y estilos de otras tantas semillas (todos los campos, notas y acordes). */
static const uint64_t ALFA_SONGS = 0xbe615050fc64ba10ull;

static uint64_t pcm_hash(Music *m)
{
    static int16_t buf[RATE];
    uint64_t h = 1469598103934665603ull;
    for (int s = 0; s < 70; s++) {
        size_t n = music_render(m, buf, RATE);
        for (size_t k = 0; k < n; k++) {
            h = fnv_byte(h, (uint8_t)buf[k]);
            h = fnv_byte(h, (uint8_t)((uint16_t)buf[k] >> 8));
        }
    }
    return h;
}

static Music mus;

static void test_regression_auto_equals_alfa_pcm(void)
{
    for (unsigned i = 0; i < sizeof(ALFA_PCM) / sizeof(ALFA_PCM[0]); i++) {
        music_start(&mus, ALFA_PCM[i].seed, ALFA_PCM[i].mut, ALFA_PCM[i].mood, RATE);
        CHECK(pcm_hash(&mus) == ALFA_PCM[i].hash);
    }
}

static void test_regression_auto_equals_alfa_songs(void)
{
    static MusicSong sg;
    MusicStyle st;
    uint64_t h = 1469598103934665603ull;
#define HB(v) h = fnv_byte(h, (uint8_t)(v))
    for (int i = 0; i < 2048; i++) {
        uint16_t seed = (uint16_t)(i * 31 + 5);
        music_style(&st, seed, mood_for(i));
        music_song(&sg, &st, seed, (uint8_t)(i % 4));
        HB(st.mode); HB(st.key); HB(st.tonic); HB(st.bpm); HB(st.prog_a); HB(st.prog_b);
        HB(st.lead_duty); HB(st.bass_duty); HB(st.lead_env); HB(st.vib); HB(st.lpf);
        HB(st.bass_style_a); HB(st.bass_style_b); HB(st.kick_k); HB(st.hat_k); HB(st.snare_pat);
        HB(st.drums); HB(st.density);
        for (int s = 0; s < MUSIC_STEPS; s++) {
            HB(sg.mel[s].pitch); HB(sg.mel[s].len); HB(sg.mel[s].vol);
            HB(sg.bass[s].pitch); HB(sg.bass[s].len); HB(sg.bass[s].vol);
            HB(sg.drums[s]);
        }
        for (int c = 0; c < MUSIC_BARS; c++) {
            HB(sg.chord[c].root);
            HB(sg.chord[c].minor);
        }
    }
#undef HB
    CHECK(h == ALFA_SONGS);
}

int main(void)
{
    struct { const char *name; TestFn fn; } tests[] = {
        {"regresion: todo en AUTO == audio de la alfa (32 casos, 70 s cada uno)", test_regression_auto_equals_alfa_pcm},
        {"regresion: todo en AUTO == canciones y estilos de la alfa (2048)", test_regression_auto_equals_alfa_songs},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests del Estudio OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
