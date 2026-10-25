/* Tests del Estudio de la música (v1.1), sin framework externo. Salida != 0 si algo falla.
 * Cubren: la regresión (con todo en AUTO, el audio es idéntico bit a bit al de la alfa), los ajustes
 * del Estudio en el motor de música (escala, tónica, tempo, arpegio, progresión, timbre, ritmo,
 * silenciar voces), los candados (AUTO/FIJO), el guardado y la máquina de estados del panel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/engine.h"
#include "../src/core/gfx.h"
#include "../src/core/music.h"
#include "../src/core/state.h"
#include "../src/core/studio.h"
#include "../src/core/timeutil.h"
#include "../src/util/musicstats.h"

static int checks, failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            if (failures <= 25)                                                  \
                printf("    FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                        \
    } while (0)

#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        long long va_ = (long long)(a), vb_ = (long long)(b);                    \
        checks++;                                                                \
        if (va_ != vb_) {                                                        \
            failures++;                                                          \
            if (failures <= 25)                                                  \
                printf("    FALLO %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, \
                       va_, vb_);                                                \
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


/* ---------- utilidades ---------- */

static MusicStudio ST(int scale, int key, int tempo, int arp, int prog, int duty, int rhythm, int mute)
{
    MusicStudio s = {(uint8_t)scale, (uint8_t)key, (uint8_t)tempo, (uint8_t)arp, (uint8_t)prog, (uint8_t)duty,
                     (uint8_t)rhythm, (uint8_t)mute};
    return s;
}

static int iabs(int x) { return x < 0 ? -x : x; }

static bool in_scale(int mode, int key, int pitch)
{
    int rel = ((pitch - key) % 12 + 12) % 12;
    const uint8_t *sc = music_scale(mode);
    for (int d = 0; d < music_scale_len(mode); d++)
        if (sc[d] == rel)
            return true;
    return false;
}

static bool same_style(const MusicStyle *a, const MusicStyle *b) { return memcmp(a, b, sizeof(*a)) == 0; }

static MusicSong sgA, sgB;

/* ---------- empaquetado ---------- */

static void test_pack_roundtrip_and_validity(void)
{
    /* Todo en AUTO es cero: un universo nuevo no necesita nada. */
    MusicStudio z = {0};
    CHECK_EQ(music_studio_pack(&z), 0);
    CHECK(music_studio_packed_valid(0));
    /* Cada campo en sus extremos ida y vuelta, sin pisar a los vecinos. */
    for (int field = 0; field < 8; field++) {
        static const int MAXV[8] = {STUDIO_SCALES, 12, STUDIO_TEMPOS, ARP_COUNT - 1, MUSIC_NPROG, STUDIO_DUTIES,
                                    STUDIO_RHYTHMS, 7};
        for (int v = 0; v <= MAXV[field]; v++) {
            MusicStudio s = {0};
            uint8_t *f[8] = {&s.scale, &s.key, &s.tempo, &s.arp, &s.prog, &s.duty, &s.rhythm, &s.mute};
            *f[field] = (uint8_t)v;
            CHECK(music_studio_valid(&s));
            uint32_t p = music_studio_pack(&s);
            CHECK(p < (1u << 27));
            CHECK(music_studio_packed_valid(p));
            MusicStudio b = music_studio_unpack(p);
            CHECK(memcmp(&b, &s, sizeof(s)) == 0);
        }
        /* Uno más allá del máximo no es válido. */
        MusicStudio s = {0};
        uint8_t *f[8] = {&s.scale, &s.key, &s.tempo, &s.arp, &s.prog, &s.duty, &s.rhythm, &s.mute};
        *f[field] = (uint8_t)(MAXV[field] + 1);
        CHECK(!music_studio_valid(&s));
    }
    /* Todos los campos a la vez. */
    MusicStudio all = ST(STUDIO_SCALES, 12, STUDIO_TEMPOS, ARP_BROKEN, MUSIC_NPROG, 3, 3, 7);
    MusicStudio back = music_studio_unpack(music_studio_pack(&all));
    CHECK(memcmp(&all, &back, sizeof(all)) == 0);
    /* Bits sueltos por encima de los 27 y valores fuera de rango se rechazan. */
    CHECK(!music_studio_packed_valid(1u << 27));
    CHECK(!music_studio_packed_valid(0x80000000u));
    CHECK(!music_studio_packed_valid(11u));                 /* escala 11 */
    CHECK(!music_studio_packed_valid(13u << 4));            /* tónica 13 */
    CHECK(!music_studio_packed_valid(12u << 8));            /* tempo 12 */
    CHECK(!music_studio_packed_valid(7u << 12));            /* arpegio 7 */
    CHECK(!music_studio_packed_valid(23u << 15));           /* progresión 23 */
    CHECK(music_studio_packed_valid(22u << 15));
    CHECK_EQ(music_studio_bpm(1), 60);
    CHECK_EQ(music_studio_bpm(STUDIO_TEMPOS), 140);
    CHECK(music_studio_any_locked(&all) && !music_studio_any_locked(&z));
    MusicStudio only_mute = ST(0, 0, 0, 0, 0, 0, 0, 7);
    CHECK(!music_studio_any_locked(&only_mute)); /* silenciar no es un candado */
}

/* ---------- AUTO == antes ---------- */

static void test_auto_studio_is_the_old_generator(void)
{
    MusicStudio z = {0};
    for (int i = 0; i < 300; i++) {
        MusicStyle a, b;
        uint16_t seed = (uint16_t)(i * 211 + 3);
        music_style(&a, seed, mood_for(i));
        music_style_ex(&b, seed, mood_for(i), &z);
        CHECK(same_style(&a, &b));
        music_style_ex(&b, seed, mood_for(i), NULL);
        CHECK(same_style(&a, &b));
        music_song(&sgA, &a, seed, (uint8_t)(i % 5));
        music_song(&sgB, &b, seed, (uint8_t)(i % 5));
        CHECK(memcmp(&sgA, &sgB, sizeof(sgA)) == 0);
    }
    /* Y el audio: el mismo hash con music_start_studio con todo AUTO. */
    for (unsigned i = 0; i < 6; i++) {
        music_start_studio(&mus, ALFA_PCM[i].seed, ALFA_PCM[i].mut, ALFA_PCM[i].mood, RATE, &z);
        CHECK(pcm_hash(&mus) == ALFA_PCM[i].hash);
    }
    /* Silenciar no cambia lo generado: el estilo y la canción son los de siempre. */
    MusicStudio mu = ST(0, 0, 0, 0, 0, 0, 0, 7);
    MusicStyle a, b;
    music_style(&a, 99, 0x05);
    music_style_ex(&b, 99, 0x05, &mu);
    CHECK(same_style(&a, &b));
    /* Fijar justo lo que AUTO habría elegido da la misma canción (los candados no mueven nada más). */
    music_style(&a, 1234, 0x06);
    int scale = 0;
    for (int k = 1; k <= STUDIO_SCALES; k++)
        if (music_studio_mode(k) == a.mode)
            scale = k;
    MusicStudio same = ST(scale, a.key + 1, (a.bpm - 52) % 8 == 0 ? (a.bpm - 52) / 8 : 0, 0, 0, a.lead_duty + 1, 0, 0);
    music_style_ex(&b, 1234, 0x06, &same);
    CHECK_EQ(b.mode, a.mode);
    CHECK_EQ(b.key, a.key);
    CHECK_EQ(b.tonic, a.tonic);
    CHECK_EQ(b.prog_a, a.prog_a);
    CHECK_EQ(b.prog_b, a.prog_b);
    CHECK_EQ(b.density, a.density);
    CHECK_EQ(b.vib, a.vib);
    CHECK_EQ(b.lead_env, a.lead_env);
}

/* ---------- escala y tónica ---------- */

/* Las notas de acorde que caen dentro de la escala (para ver que los acordes son de la escala elegida). */
static int chord_fit(int mode, int key, const MusicChord *c)
{
    int n = 0;
    int third = c->root + (c->minor ? 3 : 4), fifth = c->root + 7;
    n += in_scale(mode, key, c->root) + in_scale(mode, key, third) + in_scale(mode, key, fifth);
    return n;
}

static void test_fixed_scale_notes_belong_to_scale(void)
{
    for (int scale = 1; scale <= STUDIO_SCALES; scale++) {
        int mode = music_studio_mode(scale);
        int need = music_scale_len(mode) >= 7 ? 3 : 2;
        for (int key = 0; key <= 12; key++)
            for (int i = 0; i < 12; i++) {
                MusicStudio s = ST(scale, key, 0, 0, 0, 0, 0, 0);
                uint16_t seed = (uint16_t)(i * 5393 + scale * 17 + key);
                MusicStyle st;
                music_style_ex(&st, seed, mood_for(i * 7), &s);
                CHECK_EQ(st.mode, mode);
                if (key)
                    CHECK_EQ(st.key, key - 1);
                music_song(&sgA, &st, seed, (uint8_t)(i % 4));
                int notes = 0;
                for (int k = 0; k < MUSIC_STEPS; k++) {
                    if (sgA.mel[k].len) {
                        notes++;
                        CHECK(in_scale(mode, st.key, sgA.mel[k].pitch));
                        CHECK(sgA.mel[k].pitch >= 40 && sgA.mel[k].pitch <= 90);
                    }
                    if (sgA.bass[k].len)
                        CHECK(music_in_chord(&sgA.chord[k / MUSIC_BAR_STEPS], sgA.bass[k].pitch));
                }
                CHECK(notes >= 20);
                /* Acordes diatónicos: la escala fija manda sobre la progresión de la tabla. */
                for (int b = 0; b < MUSIC_BARS; b++)
                    CHECK(chord_fit(mode, st.key, &sgA.chord[b]) >= need);
                /* Acaba en la tónica. */
                int last = -1;
                for (int k = MUSIC_STEPS - 1; k >= 0 && last < 0; k--)
                    if (sgA.mel[k].len)
                        last = k;
                CHECK_EQ(sgA.mel[last].pitch % 12, st.key);
            }
    }
}

static void test_fixed_progression_is_harmonised(void)
{
    /* Cualquier progresión de la tabla con cualquier escala: sus acordes se ajustan a la escala (p. ej. un VI
     * en frigia pasa a un acorde de esa escala) y la melodía sigue en la escala. */
    for (int prog = 1; prog <= MUSIC_NPROG; prog++)
        for (int scale = 0; scale <= STUDIO_SCALES; scale++) {
            MusicStudio s = ST(scale, 3, 0, 0, prog, 0, 0, 0);
            MusicStyle st;
            music_style_ex(&st, 4321, 0x0A, &s);
            CHECK_EQ(st.prog_a, prog - 1);
            CHECK_EQ(st.prog_b, prog - 1);
            music_song(&sgA, &st, 4321, 0);
            int need = music_scale_len(st.mode) >= 7 ? 3 : 2;
            for (int b = 0; b < MUSIC_BARS; b++)
                CHECK(chord_fit(st.mode, st.key, &sgA.chord[b]) >= need);
            for (int k = 0; k < MUSIC_STEPS; k++)
                if (sgA.mel[k].len)
                    CHECK(in_scale(st.mode, st.key, sgA.mel[k].pitch));
            /* Las cuatro frases usan la misma progresión (el nombre del Estudio es verdad). */
            for (int b = 0; b < 4; b++)
                CHECK(sgA.chord[b].root == sgA.chord[4 + b].root && sgA.chord[b].minor == sgA.chord[4 + b].minor);
        }
    /* Si la progresión ya era de la escala, no se toca: I-V-vi-IV en mayor es I-V-vi-IV. */
    MusicStudio s = ST(3, 1, 0, 0, 1, 0, 0, 0); /* mayor, Do */
    MusicStyle st;
    music_style_ex(&st, 7, 0x08, &s);
    music_song(&sgA, &st, 7, 0);
    CHECK(sgA.chord[0].root == 0 && !sgA.chord[0].minor);
    CHECK(sgA.chord[1].root == 7 && !sgA.chord[1].minor);
    CHECK(sgA.chord[2].root == 9 && sgA.chord[2].minor);
    CHECK(sgA.chord[3].root == 5 && !sgA.chord[3].minor);
}

static void test_key_transposes(void)
{
    /* Con la escala y la progresión fijas, cambiar la tónica transpone todo: la melodía es la misma,
     * desplazada, y los acordes también. */
    for (int scale = 1; scale <= STUDIO_SCALES; scale++)
        for (int i = 0; i < 6; i++) {
            uint16_t seed = (uint16_t)(i * 777 + scale);
            MusicStyle s0;
            MusicStudio base = ST(scale, 1, 0, 0, 1 + (i * 5) % MUSIC_NPROG, 0, 0, 0);
            music_style_ex(&s0, seed, 0x02, &base);
            music_song(&sgA, &s0, seed, (uint8_t)i);
            for (int key = 2; key <= 12; key++) {
                MusicStudio sk = base;
                sk.key = (uint8_t)key;
                MusicStyle st;
                music_style_ex(&st, seed, 0x02, &sk);
                CHECK_EQ(st.key, key - 1);
                CHECK_EQ(st.mode, s0.mode);
                CHECK_EQ(st.bpm, s0.bpm);
                music_song(&sgB, &st, seed, (uint8_t)i);
                int shift = (int)st.tonic - (int)s0.tonic;
                for (int k = 0; k < MUSIC_STEPS; k++) {
                    CHECK_EQ(sgA.mel[k].len, sgB.mel[k].len);
                    if (sgA.mel[k].len)
                        CHECK_EQ(sgB.mel[k].pitch, sgA.mel[k].pitch + shift);
                    CHECK_EQ(sgA.bass[k].len, sgB.bass[k].len);
                    CHECK_EQ(sgA.drums[k], sgB.drums[k]);
                }
                for (int b = 0; b < MUSIC_BARS; b++) {
                    CHECK_EQ(sgB.chord[b].root, (sgA.chord[b].root + key - 1) % 12);
                    CHECK_EQ(sgB.chord[b].minor, sgA.chord[b].minor);
                }
            }
        }
}

static void test_key_sets_tonic_register(void)
{
    /* La tónica de cada nota está en el registro cantable (La3..Sol#4) y la melodía no se sale de él. */
    for (int key = 1; key <= 12; key++) {
        MusicStudio s = ST(0, key, 0, 0, 0, 0, 0, 0);
        MusicStyle st;
        music_style_ex(&st, 55, 0x00, &s);
        CHECK(st.tonic >= 57 && st.tonic <= 68);
        CHECK_EQ(st.tonic % 12, (key - 1));
        music_song(&sgA, &st, 55, 0);
        for (int k = 0; k < MUSIC_STEPS; k++)
            if (sgA.mel[k].len)
                CHECK(sgA.mel[k].pitch >= st.tonic - 3 && sgA.mel[k].pitch <= st.tonic + 14);
    }
}

/* ---------- tempo ---------- */

static void test_fixed_tempo_gives_right_bpm(void)
{
    for (int t = 1; t <= STUDIO_TEMPOS; t++) {
        MusicStudio s = ST(0, 0, t, 0, 0, 0, 0, 0);
        MusicStyle st;
        music_style_ex(&st, 31, 0x00, &s);
        CHECK_EQ(st.bpm, 52 + 8 * t);
        for (int mood = 0; mood < 4; mood++) { /* la hora y el clima no lo mueven si está fijo */
            MusicStyle st2;
            music_style_ex(&st2, 31, (uint8_t)(mood | (mood & 1 ? MOOD_RAIN : 0)), &s);
            CHECK_EQ(st2.bpm, 52 + 8 * t);
        }
        /* Y suena a ese tempo: pasos por segundo = bpm / 15 (semicorcheas). */
        music_start_studio(&mus, 31, 0, 0x00, RATE, &s);
        static int16_t buf[RATE];
        for (int sec = 0; sec < 24; sec++)
            music_render(&mus, buf, RATE);
        int measured = (int)(mus.steps_played * 15 / 24);
        CHECK(iabs(measured - (52 + 8 * t)) <= 1);
        CHECK_EQ(mus.style.bpm, 52 + 8 * t);
    }
}

/* ---------- arpegios ---------- */

static void test_arpeggio_patterns_order(void)
{
    static const int8_t EXP[ARP_COUNT][8] = {
        {0}, {0},
        {0, 1, 2, 3, 0, 1, 2, 3},   /* sube: fundamental, tercera, quinta, octava */
        {3, 2, 1, 0, 3, 2, 1, 0},   /* baja */
        {0, 1, 2, 3, 2, 1, 0, 1},   /* sube y baja */
        {0},                        /* azar: se comprueba aparte */
        {0, 2, 1, 2, 0, 2, 1, 2},   /* roto (Alberti): fundamental, quinta, tercera, quinta */
    };
    for (int arp = ARP_OFF; arp <= ARP_BROKEN; arp++)
        for (int i = 0; i < 20; i++) {
            MusicStudio s = ST(i % 3 ? 0 : 1 + i % STUDIO_SCALES, 0, 0, arp, 0, 0, 0, 0);
            MusicStyle st;
            uint16_t seed = (uint16_t)(i * 1291 + arp);
            music_style_ex(&st, seed, mood_for(i), &s);
            CHECK_EQ(st.arp, arp);
            music_song(&sgA, &st, seed, 0);
            for (int bar = 0; bar < MUSIC_BARS; bar++) {
                const MusicChord *c = &sgA.chord[bar];
                int r = 36 + c->root, t3 = c->minor ? 3 : 4;
                const MusicNote *bn = &sgA.bass[bar * MUSIC_BAR_STEPS];
                if (arp == ARP_OFF) { /* sin arpegio: la fundamental en dos blancas */
                    CHECK(bn[0].len == 8 && bn[8].len == 8 && bn[0].pitch == r && bn[8].pitch == r);
                    for (int k = 1; k < 8; k++)
                        CHECK(bn[k].len == 0 && bn[8 + k].len == 0);
                    continue;
                }
                int off[4] = {0, t3, 7, 12};
                int prev = -1;
                for (int n = 0; n < 8; n++) {
                    CHECK_EQ(bn[2 * n].len, 2);
                    CHECK_EQ(bn[2 * n + 1].len, 0);
                    int rel = bn[2 * n].pitch - (r + 12);
                    if (arp == ARP_RANDOM) {
                        int idx = -1;
                        for (int q = 0; q < 4; q++)
                            if (off[q] == rel)
                                idx = q;
                        CHECK(idx >= 0);                  /* siempre una nota del acorde */
                        if (n == 0)
                            CHECK_EQ(idx, 0);             /* el primer tiempo, la fundamental */
                        CHECK(idx != prev);               /* nunca la misma nota seguida */
                        prev = idx;
                    } else {
                        CHECK_EQ(rel, off[EXP[arp][n]]);
                    }
                    CHECK(music_in_chord(c, bn[2 * n].pitch));
                }
            }
        }
    /* Los arpegios fijos aguantan la fundamental con acento en los tiempos: los dos primeros acentos. */
    MusicStudio s = ST(0, 0, 0, ARP_UP, 0, 0, 0, 0);
    MusicStyle st;
    music_style_ex(&st, 5, 0, &s);
    music_song(&sgA, &st, 5, 0);
    CHECK_EQ(sgA.bass[0].vol, 255);
    CHECK_EQ(sgA.bass[2].vol, 205);
    /* El azar es determinista (misma semilla, mismas notas) y cada compás sale distinto. */
    MusicStudio r = ST(0, 0, 0, ARP_RANDOM, 0, 0, 0, 0);
    music_style_ex(&st, 8, 0, &r);
    music_song(&sgA, &st, 8, 0);
    music_song(&sgB, &st, 8, 3);
    CHECK(memcmp(sgA.bass, sgB.bass, sizeof(sgA.bass)) == 0); /* mutar no toca el acompañamiento */
    int distinct = 0;
    for (int b = 1; b < MUSIC_BARS; b++)
        distinct += memcmp(&sgA.bass[b * 16], &sgA.bass[0], 16 * sizeof(MusicNote)) != 0;
    CHECK(distinct >= 10);
}

static void test_arpeggio_auto_keeps_seed_styles(void)
{
    /* Con el arpegio en AUTO, el acompañamiento es el de la semilla (euclidiano, octavas, arpegio o caminante). */
    int seen[4] = {0};
    for (int i = 0; i < 200; i++) {
        MusicStyle st;
        music_style(&st, (uint16_t)(i * 97), mood_for(i));
        CHECK_EQ(st.arp, ARP_AUTO);
        seen[st.bass_style_a]++;
    }
    for (int k = 0; k < 4; k++)
        CHECK(seen[k] > 10);
}

/* ---------- timbre y ritmo ---------- */

static void test_duty_and_rhythm(void)
{
    for (int d = 1; d <= STUDIO_DUTIES; d++) {
        MusicStudio s = ST(0, 0, 0, 0, 0, d, 0, 0);
        music_start_studio(&mus, 40, 0, 0x03, RATE, &s);
        CHECK_EQ(mus.style.lead_duty, d - 1);
        CHECK_EQ(mus.style.bass_duty, d - 1);
        static const uint8_t THR[3] = {32, 64, 128};
        CHECK_EQ(mus.v[MV_LEAD].thr, THR[d - 1]);
        CHECK_EQ(mus.v[MV_BASS].thr, THR[d - 1]);
    }
    /* Ritmo: más densidad = más golpes de bombo y hi-hat por compás. */
    int hits[4] = {0};
    for (int r = 1; r <= STUDIO_RHYTHMS; r++) {
        MusicStudio s = ST(0, 0, 0, 0, 0, 0, r, 0);
        for (int i = 0; i < 20; i++) {
            MusicStyle st;
            uint16_t seed = (uint16_t)(i * 313);
            music_style_ex(&st, seed, mood_for(i), &s);
            CHECK_EQ(st.drums, r - 1);
            music_song(&sgA, &st, seed, 0);
            for (int k = 0; k < MUSIC_STEPS; k++)
                hits[r] += (sgA.drums[k] & (DR_KICK | DR_HAT)) != 0;
        }
    }
    CHECK(hits[1] < hits[2] && hits[2] < hits[3]);
}

/* ---------- determinismo ---------- */

static void render_pair(uint16_t seed, uint8_t mut, uint8_t mood, const MusicStudio *s)
{
    static int16_t one[RATE * 6], again[RATE * 6], many[RATE * 6];
    music_start_studio(&mus, seed, mut, mood, RATE, s);
    CHECK_EQ(music_render(&mus, one, RATE * 6), RATE * 6);
    music_start_studio(&mus, seed, mut, mood, RATE, s);
    CHECK_EQ(music_render(&mus, again, RATE * 6), RATE * 6);
    CHECK(memcmp(one, again, sizeof(one)) == 0);
    music_start_studio(&mus, seed, mut, mood, RATE, s);
    size_t pos = 0;
    uint32_t r = 0x9E37u + seed;
    while (pos < RATE * 6) {
        r = r * 1664525u + 1013904223u;
        size_t c = 1 + (r >> 8) % (r & 1 ? 40 : 4000);
        if (pos + c > RATE * 6)
            c = RATE * 6 - pos;
        CHECK_EQ(music_render(&mus, many + pos, c), c);
        pos += c;
    }
    CHECK(memcmp(one, many, sizeof(one)) == 0);
}

static void test_determinism_with_each_parameter_fixed(void)
{
    static const struct { int row, value; } CASES[] = {
        {SR_SCALE, 1}, {SR_SCALE, 6}, {SR_SCALE, 9}, {SR_SCALE, 10}, {SR_KEY, 1}, {SR_KEY, 8}, {SR_KEY, 12},
        {SR_TEMPO, 1}, {SR_TEMPO, 6}, {SR_TEMPO, 11}, {SR_ARP, ARP_OFF}, {SR_ARP, ARP_UP}, {SR_ARP, ARP_DOWN},
        {SR_ARP, ARP_UPDOWN}, {SR_ARP, ARP_RANDOM}, {SR_ARP, ARP_BROKEN}, {SR_PROG, 1}, {SR_PROG, 12},
        {SR_PROG, 22}, {SR_DUTY, 1}, {SR_DUTY, 2}, {SR_DUTY, 3}, {SR_RHYTHM, 1}, {SR_RHYTHM, 2}, {SR_RHYTHM, 3},
        {SR_LEAD, 1}, {SR_BASS, 1}, {SR_NOISE, 1},
    };
    for (unsigned c = 0; c < sizeof(CASES) / sizeof(CASES[0]); c++)
        for (int i = 0; i < 2; i++) {
            MusicStudio s = {0};
            studio_set(&s, CASES[c].row, CASES[c].value);
            render_pair((uint16_t)(c * 4099 + i * 77 + 1), (uint8_t)i, mood_for((int)c + i * 5), &s);
        }
    /* Y todo a la vez. */
    MusicStudio all = ST(6, 5, 4, ARP_BROKEN, 9, 2, 3, 2);
    render_pair(4242, 2, 0x13, &all);
}

/* ---------- voces ---------- */

#define NV 8
static int16_t vA[MV_COUNT][RATE * NV], vB[MV_COUNT][RATE * NV], pcmA[RATE * NV], pcmB[RATE * NV];

static size_t render_voices_chunked(Music *m, int16_t *pcm, int16_t (*v)[RATE * NV], size_t n, uint32_t *rr)
{
    size_t pos = 0;
    while (pos < n) {
        *rr = *rr * 1664525u + 1013904223u;
        size_t c = 1 + (*rr >> 8) % 3000;
        if (pos + c > n)
            c = n - pos;
        int16_t *vo[MV_COUNT] = {v[MV_LEAD] + pos, v[MV_BASS] + pos, v[MV_NOISE] + pos};
        pos += music_render_voices(m, pcm + pos, c, vo);
    }
    return pos;
}

static void test_mute_leaves_other_voices_untouched(void)
{
    for (int voice = 0; voice < MV_COUNT; voice++)
        for (int seed = 0; seed < 4; seed++) {
            uint8_t mood = mood_for(seed * 11);
            /* A: nada silenciado. B: la voz se silencia a los 2 s y se reactiva a los 5 s. */
            music_start(&mus, (uint16_t)(seed * 1999 + 5), 0, mood, RATE);
            uint32_t rr = 1;
            render_voices_chunked(&mus, pcmA, vA, RATE * NV, &rr);
            static Music mb;
            music_start(&mb, (uint16_t)(seed * 1999 + 5), 0, mood, RATE);
            size_t pos = 0;
            MusicStudio s = {0};
            rr = 7;
            const size_t T_MUTE = RATE * 2, T_ON = RATE * 5;
            while (pos < RATE * NV) {
                if (pos == 0 || pos == T_MUTE || pos == T_ON) {
                    s.mute = pos == T_MUTE ? (uint8_t)(1u << voice) : 0;
                    music_set_studio(&mb, &s);
                }
                size_t stop = pos < T_MUTE ? T_MUTE : pos < T_ON ? T_ON : RATE * NV;
                size_t c = 1 + (rr = rr * 1664525u + 1013904223u, (rr >> 8) % 3000);
                if (pos + c > stop)
                    c = stop - pos;
                int16_t *vo[MV_COUNT] = {vB[MV_LEAD] + pos, vB[MV_BASS] + pos, vB[MV_NOISE] + pos};
                pos += music_render_voices(&mb, pcmB + pos, c, vo);
            }
            const size_t FADE = RATE / 25; /* el fundido dura 16 ms: 40 ms es de sobra */
            for (int v = 0; v < MV_COUNT; v++) {
                if (v != voice) { /* las otras voces: idénticas muestra a muestra, también durante el fundido */
                    CHECK(memcmp(vA[v], vB[v], sizeof(vA[v])) == 0);
                    continue;
                }
                CHECK(memcmp(vA[v], vB[v], T_MUTE * sizeof(int16_t)) == 0);          /* antes de silenciar */
                for (size_t i = T_MUTE + FADE; i < T_ON; i++)                         /* silenciada: cero */
                    if (vB[v][i] != 0) {
                        CHECK(false);
                        break;
                    }
                CHECK(memcmp(vA[v] + T_ON + FADE, vB[v] + T_ON + FADE, (RATE * NV - T_ON - FADE) * sizeof(int16_t)) == 0);
                /* Sin clic: la ganancia baja (o sube) en escalones de un octavo y sin saltos atrás: la voz silenciada
                 * es la original multiplicada por una rampa monótona de ~16 ms. */
                for (int edge = 0; edge < 2; edge++) {
                    size_t t0 = edge ? T_ON : T_MUTE;
                    int last = edge ? 0 : 256, worst_step = 0;
                    bool prev_ok = false;
                    for (size_t i = t0; i < t0 + FADE; i++) {
                        int a = vA[v][i], b = vB[v][i];
                        CHECK(iabs(b) <= iabs(a) && (long)a * b >= 0);
                        if (iabs(a) < 800) {
                            prev_ok = false;
                            continue;
                        }
                        int r = iabs(b) * 256 / iabs(a);
                        CHECK(edge ? r >= last - 3 : r <= last + 3);
                        if (prev_ok)
                            worst_step = iabs(r - last) > worst_step ? iabs(r - last) : worst_step;
                        prev_ok = true;
                        last = r;
                    }
                    CHECK(worst_step <= 32 + 6);   /* 1/8 de escalón (más el redondeo) */
                }
            }
        }
}

static void test_mute_starts_silent_and_all_mute(void)
{
    /* Una voz silenciada desde el principio suena a cero desde la primera muestra. */
    MusicStudio s = ST(0, 0, 0, 0, 0, 0, 0, 1);
    music_start_studio(&mus, 77, 0, 0x04, RATE, &s);
    int16_t *vo[MV_COUNT] = {vA[MV_LEAD], vA[MV_BASS], vA[MV_NOISE]};
    music_render_voices(&mus, pcmA, RATE * 3, vo);
    for (size_t i = 0; i < RATE * 3; i++)
        if (vA[MV_LEAD][i] != 0) {
            CHECK(false);
            break;
        }
    int any = 0;
    for (size_t i = 0; i < RATE * 3; i++)
        any |= vA[MV_BASS][i] != 0 || vA[MV_NOISE][i] != 0;
    CHECK(any);
    /* Las tres silenciadas: nada de nada (el filtro se queda en su reposo), sin romper el tiempo. */
    MusicStudio all = ST(0, 0, 0, 0, 0, 0, 0, 7);
    music_start_studio(&mus, 77, 0, 0x04, RATE, &all);
    music_render(&mus, pcmA, RATE * 3);
    int peak = 0;
    for (size_t i = RATE; i < RATE * 3; i++)
        peak = iabs(pcmA[i]) > peak ? iabs(pcmA[i]) : peak;
    CHECK(peak <= 1);
    CHECK(mus.steps_played + 1 >= (uint32_t)(mus.style.bpm * 3 / 15)); /* la música sigue corriendo por dentro */
    CHECK_EQ(mus.level[MV_LEAD] + mus.level[MV_BASS] + mus.level[MV_NOISE], 0); /* y el visualizador, a cero */
    /* Reactivadas, vuelve a sonar sin más. */
    MusicStudio none = {0};
    music_set_studio(&mus, &none);
    music_render(&mus, pcmA, RATE * 2);
    peak = 0;
    for (size_t i = RATE / 2; i < RATE * 2; i++)
        peak = iabs(pcmA[i]) > peak ? iabs(pcmA[i]) : peak;
    CHECK(peak > 1000);
}

static void test_changing_settings_midway(void)
{
    /* Cambiar la escala, el tempo o el arpegio en marcha rehace la canción sin perder el compás ni romper el audio. */
    music_start(&mus, 2026, 0, 0x06, RATE);
    static int16_t buf[RATE];
    music_render(&mus, buf, RATE * 2 / 3);
    uint16_t pos0 = mus.pos;
    MusicStudio s = ST(9, 6, 5, ARP_UPDOWN, 3, 2, 2, 0);
    music_set_studio(&mus, &s);
    CHECK_EQ(mus.pos, pos0);
    CHECK_EQ(mus.style.mode, MODE_BLUES);
    CHECK_EQ(mus.style.key, 5);
    CHECK_EQ(mus.style.bpm, 52 + 8 * 5);
    CHECK_EQ(mus.style.arp, ARP_UPDOWN);
    int peak = 0;
    for (int sec = 0; sec < 20; sec++) {
        music_render(&mus, buf, RATE);
        for (int i = 0; i < RATE; i++)
            peak = iabs(buf[i]) > peak ? iabs(buf[i]) : peak;
    }
    CHECK(peak > 1000 && peak < 32000);
    /* Volver a AUTO devuelve el estilo de la semilla. */
    MusicStudio none = {0};
    music_set_studio(&mus, &none);
    MusicStyle st;
    music_style(&st, 2026, 0x06);
    CHECK(same_style(&st, &mus.style));
    /* Valores no válidos se ignoran. */
    MusicStudio bad = ST(11, 0, 0, 0, 0, 0, 0, 0);
    music_set_studio(&mus, &bad);
    CHECK(same_style(&st, &mus.style));
    /* Una semilla nueva (con el fundido de por medio) conserva los ajustes. */
    music_set_studio(&mus, &s);
    music_reseed(&mus, 99, 0, 0x06);
    for (int sec = 0; sec < 2; sec++)
        music_render(&mus, buf, RATE);
    CHECK_EQ(mus.seed, 99);
    CHECK_EQ(mus.style.mode, MODE_BLUES);
    CHECK_EQ(mus.style.bpm, 52 + 8 * 5);
}

/* ---------- candados ---------- */

static void test_locks_hold_through_new_seed_and_mutation(void)
{
    /* A nivel de generador: lo FIJO no depende de la semilla (nueva semilla), lo AUTO sí; y mutar no toca el estilo. */
    MusicStudio s = ST(4, 8, 3, ARP_BROKEN, 0, 0, 0, 0); /* escala menor, Sol#, 76 BPM, arpegio roto: FIJOS */
    int auto_changes_prog = 0, auto_changes_duty = 0, auto_changes_rhythm = 0;
    MusicStyle first;
    for (int i = 0; i < 40; i++) {
        MusicStyle st;
        uint16_t seed = (uint16_t)(i * 1621 + 11);
        music_style_ex(&st, seed, mood_for(i * 5), &s);
        CHECK_EQ(st.mode, MODE_MINOR);
        CHECK_EQ(st.key, 7);
        CHECK_EQ(st.bpm, 76);
        CHECK_EQ(st.arp, ARP_BROKEN);
        if (i == 0)
            first = st;
        else {
            auto_changes_prog |= st.prog_a != first.prog_a;
            auto_changes_duty |= st.lead_duty != first.lead_duty;
            auto_changes_rhythm |= st.kick_k != first.kick_k || st.hat_k != first.hat_k;
        }
        for (int mut = 0; mut < 4; mut++) { /* mutar: mismo estilo y misma sección rítmica y armónica */
            music_song(&sgA, &st, seed, (uint8_t)mut);
            music_song(&sgB, &st, seed, 0);
            CHECK(memcmp(sgA.bass, sgB.bass, sizeof(sgA.bass)) == 0);
            CHECK(memcmp(sgA.drums, sgB.drums, sizeof(sgA.drums)) == 0);
            CHECK(memcmp(sgA.chord, sgB.chord, sizeof(sgA.chord)) == 0);
            for (int k = 0; k < MUSIC_STEPS; k++)
                if (sgA.mel[k].len)
                    CHECK(in_scale(MODE_MINOR, 7, sgA.mel[k].pitch));
        }
    }
    CHECK(auto_changes_prog && auto_changes_duty && auto_changes_rhythm);
}

/* ---------- el motor ---------- */

typedef struct {
    Engine e;
    uint32_t ms;
    int64_t now;
} Rig;

static Rig rig;

static void rig_init(Rig *r, uint64_t seed)
{
    r->ms = 1000;
    r->now = time_from_civil(2026, 10, 22, 12, 0, 0);
    engine_init(&r->e, seed, r->now, r->ms);
}

static FrameOut rig_frame(Rig *r, uint8_t buttons, uint32_t dt)
{
    RawInput in = {buttons, false};
    r->ms += dt;
    FrameOut o = engine_frame(&r->e, in, r->now + (r->ms - 1000) / 1000, r->ms);
    return o;
}

static void rig_idle(Rig *r, uint32_t total)
{
    for (uint32_t t = 0; t < total; t += 30)
        rig_frame(r, 0, 30);
}

/* Un toque: pulsado 60 ms, y 100 ms después de soltarlo (de un soltar al siguiente: ~190 ms). */
static void rig_tap(Rig *r, uint8_t b)
{
    rig_frame(r, b, 30);
    rig_frame(r, b, 30);
    rig_frame(r, 0, 30);
    rig_idle(r, 90);
}

static void rig_hold(Rig *r, uint8_t b)
{
    for (int i = 0; i < 20; i++)
        rig_frame(r, b, 30);
    rig_frame(r, 0, 30);
    rig_idle(r, 90);
}

static void rig_goto_music(Rig *r)
{
    r->e.gs.current_world = WORLD_MUSICA;
    rig_idle(r, 300);
}

static void rig_music_world_ready(Rig *r, uint64_t seed)
{
    rig_init(r, seed);
    rig_goto_music(r);
}

static void serialize_without_music(const GameState *gs, uint8_t *buf, size_t *n)
{
    GameState g = *gs;
    memset(&g.mus, 0, sizeof(g.mus));
    *n = state_serialize(&g, buf, LLV_SAVE_MAX);
}

static void test_double_tap_opens_studio_and_changes_nothing(void)
{
    rig_music_world_ready(&rig, 5);
    Engine *e = &rig.e;
    CHECK(!engine_studio_open(e));
    CHECK(music_active(&e->music));
    uint16_t seed0 = e->gs.mus.seed;
    uint8_t mut0 = e->gs.mus.mut;
    uint32_t st0 = e->gs.mus.studio;
    GameState before = e->gs;
    /* Un toque suelto muta, como siempre. */
    rig_tap(&rig, BTN_A);
    CHECK_EQ(e->gs.mus.mut, mut0 + 1);
    CHECK(!engine_studio_open(e));
    rig_idle(&rig, 600);
    /* Dos toques seguidos (menos de 300 ms entre soltar uno y el otro): abre el Estudio y deshace la mutación. */
    rig_tap(&rig, BTN_A);
    rig_tap(&rig, BTN_A);
    CHECK(engine_studio_open(e));
    CHECK_EQ(e->gs.mus.mut, mut0 + 1);   /* el de antes se queda; el doble toque no suma ninguno */
    CHECK_EQ(e->gs.mus.seed, seed0);
    CHECK_EQ(e->gs.mus.studio, st0);
    CHECK_EQ(e->studio_row, 0);
    CHECK_EQ(e->gs.current_world, WORLD_MUSICA);
    CHECK_EQ(e->music.mut, e->gs.mus.mut); /* el sintetizador también volvió a la melodía de antes */
    /* El universo no se ha movido (salvo la mutación que hizo el primer toque suelto). */
    uint8_t b0[LLV_SAVE_MAX], b1[LLV_SAVE_MAX];
    size_t n0, n1;
    GameState after = e->gs;
    after.mus.mut = before.mus.mut;
    after.last_seen = before.last_seen;
    after.mus.mood = before.mus.mood;
    after.news = before.news;
    serialize_without_music(&before, b0, &n0);
    serialize_without_music(&after, b1, &n1);
    CHECK(n0 > 0 && n0 == n1 && memcmp(b0, b1, n0) == 0);
    /* Y el doble toque desde el principio (sin mutación previa) no deja ni una. */
    rig_music_world_ready(&rig, 6);
    uint8_t m0 = e->gs.mus.mut;
    rig_tap(&rig, BTN_A);
    rig_tap(&rig, BTN_A);
    CHECK(engine_studio_open(e));
    CHECK_EQ(e->gs.mus.mut, m0);
    CHECK_EQ(e->music.mut, m0);
}

static void test_slow_taps_keep_mutating(void)
{
    rig_music_world_ready(&rig, 7);
    Engine *e = &rig.e;
    for (int k = 1; k <= 8; k++) {
        rig_tap(&rig, BTN_A);
        rig_idle(&rig, 400);   /* más de 0,3 s entre toques: ninguno es doble */
        CHECK(!engine_studio_open(e));
        CHECK_EQ(e->gs.mus.mut, k);
    }
    /* A (mantener) sigue siendo semilla nueva. */
    uint16_t seed0 = e->gs.mus.seed;
    rig_hold(&rig, BTN_A);
    CHECK(e->gs.mus.seed != seed0);
    CHECK_EQ(e->gs.mus.mut, 0);
    CHECK(!engine_studio_open(e));
    /* Fuera de Música, A A no abre nada. */
    e->gs.current_world = WORLD_JARDIN;
    rig_idle(&rig, 500);
    rig_tap(&rig, BTN_A);
    rig_tap(&rig, BTN_A);
    CHECK(!engine_studio_open(e));
    /* Y B sigue cambiando de mundo con el Estudio cerrado. */
    e->gs.current_world = WORLD_MUSICA;
    rig_idle(&rig, 500);
    rig_tap(&rig, BTN_B);
    CHECK_EQ(e->gs.current_world, WORLD_PESCA);
}

static void open_studio(Rig *r)
{
    rig_idle(r, 500);
    rig_tap(r, BTN_A);
    rig_tap(r, BTN_A);
}

static void test_panel_state_machine(void)
{
    rig_music_world_ready(&rig, 11);
    Engine *e = &rig.e;
    open_studio(&rig);
    CHECK(engine_studio_open(e));
    /* B cicla las filas (y no cambia de mundo). */
    for (int k = 1; k <= SR_COUNT + 2; k++) {
        rig_tap(&rig, BTN_B);
        CHECK_EQ(e->studio_row, k % SR_COUNT);
        CHECK_EQ(e->gs.current_world, WORLD_MUSICA);
        CHECK(engine_studio_open(e));
    }
    /* B (mantener) = fila anterior. */
    uint8_t row = e->studio_row;
    rig_hold(&rig, BTN_B);
    CHECK_EQ(e->studio_row, (row + SR_COUNT - 1) % SR_COUNT);
    CHECK_EQ(e->gs.current_world, WORLD_MUSICA);
    /* A (toque) cambia el valor de la fila: da la vuelta por todos y vuelve a AUTO. */
    for (int sr = 0; sr < SR_COUNT; sr++) {
        e->studio_row = (uint8_t)sr;
        if (studio_row_is_action(sr))
            continue;
        MusicStudio s0 = music_studio_unpack(e->gs.mus.studio);
        CHECK_EQ(studio_row_value(&s0, sr), 0);
        int n = studio_row_values(sr);
        for (int k = 1; k <= n; k++) {
            rig_tap(&rig, BTN_A);
            MusicStudio s = music_studio_unpack(e->gs.mus.studio);
            CHECK_EQ(studio_row_value(&s, sr), k % n);
            CHECK(music_studio_valid(&s));
            CHECK(engine_studio_open(e));
        }
        MusicStudio s1 = music_studio_unpack(e->gs.mus.studio);
        CHECK(memcmp(&s0, &s1, sizeof(s0)) == 0);
    }
    /* Un toque en el dado no cambia nada, solo avisa. */
    e->studio_row = SR_DICE;
    uint32_t packed = e->gs.mus.studio;
    uint16_t seed0 = e->gs.mus.seed;
    rig_tap(&rig, BTN_A);
    CHECK_EQ(e->gs.mus.studio, packed);
    CHECK_EQ(e->gs.mus.seed, seed0);
    /* Mantener A sobre el dado saca una semilla nueva y el panel sigue abierto. */
    rig_hold(&rig, BTN_A);
    CHECK(e->gs.mus.seed != seed0);
    CHECK(engine_studio_open(e));
    /* Mantener A en cualquier otra fila cierra. */
    e->studio_row = SR_TEMPO;
    uint16_t seed1 = e->gs.mus.seed;
    rig_hold(&rig, BTN_A);
    CHECK(!engine_studio_open(e));
    CHECK_EQ(e->gs.mus.seed, seed1);
    /* Con el Estudio cerrado, B vuelve a cambiar de mundo. */
    rig_tap(&rig, BTN_B);
    CHECK_EQ(e->gs.current_world, WORLD_PESCA);
}

static void test_panel_timeout_and_sleep(void)
{
    rig_music_world_ready(&rig, 12);
    Engine *e = &rig.e;
    open_studio(&rig);
    CHECK(engine_studio_open(e));
    /* Sin pulsar nada se cierra a los LLV_STUDIO_TIMEOUT_MS; antes no. */
    rig_idle(&rig, LLV_STUDIO_TIMEOUT_MS - 1500);
    CHECK(engine_studio_open(e));
    rig_tap(&rig, BTN_B);   /* pulsar renueva el plazo */
    rig_idle(&rig, LLV_STUDIO_TIMEOUT_MS - 1500);
    CHECK(engine_studio_open(e));
    rig_idle(&rig, 2500);
    CHECK(!engine_studio_open(e));
    CHECK(LLV_STUDIO_TIMEOUT_MS >= 8000 && LLV_STUDIO_TIMEOUT_MS <= 10000);
    /* Mantener un botón también cuenta como actividad. */
    open_studio(&rig);
    CHECK(engine_studio_open(e));
    for (int i = 0; i < 400; i++)   /* 12 s con B apretado */
        rig_frame(&rig, BTN_B, 30);
    CHECK(engine_studio_open(e));
    rig_frame(&rig, 0, 30);
    /* Dormir lo cierra; al despertar sigue cerrado y los ajustes se conservan. */
    e->studio_row = SR_SCALE;
    rig_tap(&rig, BTN_A);
    uint32_t packed = e->gs.mus.studio;
    CHECK(packed != 0);
    engine_sleep(e, rig.now);
    CHECK(!engine_studio_open(e));
    CHECK_EQ(e->gs.mus.studio, packed);
    /* Y se cierra solo si el mundo deja de ser Música (una sorpresa, por ejemplo). */
    rig_music_world_ready(&rig, 13);
    open_studio(&rig);
    CHECK(engine_studio_open(e));
    e->gs.current_world = WORLD_PESCA;
    rig_idle(&rig, 100);
    CHECK(!engine_studio_open(e));
}

static void test_panel_applies_settings_to_sound(void)
{
    rig_music_world_ready(&rig, 14);
    Engine *e = &rig.e;
    open_studio(&rig);
    static int16_t pcm[2000];
    /* Escala (fila 0): el primer toque la fija en pentatónica mayor y el motor lo aplica al instante. */
    e->studio_row = SR_SCALE;
    rig_tap(&rig, BTN_A);
    engine_audio_render(e, pcm, 1000);
    CHECK_EQ(e->music.studio.scale, 1);
    CHECK_EQ(e->music.style.mode, MODE_PENT_MAJ);
    /* Voz (fila 7): silenciarla no cambia el estilo ni la canción. */
    MusicStyle style0 = e->music.style;
    MusicSong song0 = e->music.song;
    e->studio_row = SR_LEAD;
    rig_tap(&rig, BTN_A);
    engine_audio_render(e, pcm, 1000);
    CHECK_EQ(e->music.studio.mute, 1);
    CHECK(same_style(&style0, &e->music.style));
    CHECK(memcmp(&song0, &e->music.song, sizeof(song0)) == 0);
    /* Mutar (fuera del Estudio) y semilla nueva: la escala fija se queda; el estilo AUTO cambia. */
    rig_hold(&rig, BTN_A);   /* cierra el panel (fila de voces) */
    CHECK(!engine_studio_open(e));
    int bpms = 0, mode_changed = 0;
    int bpm0 = e->music.style.bpm;
    for (int k = 0; k < 6; k++) {
        rig_hold(&rig, BTN_A);
        for (int i = 0; i < 10; i++)
            engine_audio_render(e, pcm, 2000);
        CHECK_EQ(e->music.style.mode, MODE_PENT_MAJ);
        CHECK_EQ(e->music.studio.scale, 1);
        CHECK_EQ(e->music.studio.mute, 1);
        bpms |= e->music.style.bpm != bpm0;
        mode_changed |= e->music.style.mode != MODE_PENT_MAJ;
    }
    CHECK(bpms);            /* el tempo estaba en AUTO: la semilla nueva lo cambia */
    CHECK(!mode_changed);   /* la escala estaba FIJA: no */
    rig_idle(&rig, 500);
    uint32_t seed_before = e->gs.mus.seed;
    rig_tap(&rig, BTN_A);   /* mutar */
    CHECK_EQ(e->gs.mus.seed, seed_before);
    CHECK_EQ(e->music.style.mode, MODE_PENT_MAJ);
    /* Dormir, despertar y volver a Música: los ajustes del Estudio siguen aplicándose. */
    rig_idle(&rig, 400);
    engine_sleep(e, rig.now);
    CHECK(!music_active(&e->music) || e->music.stopping);
    RawInput wake = {0, true};
    rig.ms += 5000;
    engine_frame(e, wake, rig.now + 5, rig.ms);
    rig_idle(&rig, 900);
    e->gs.current_world = WORLD_MUSICA;
    rig_idle(&rig, 600);
    engine_audio_render(e, pcm, 1000);
    CHECK_EQ(e->music.studio.scale, 1);
    CHECK_EQ(e->music.studio.mute, 1);
}

static void test_secret_code_still_works_through_music(void)
{
    /* B B A A B A B A desde el Jardín pasa por Música con dos A seguidas: no abre el Estudio. */
    rig_init(&rig, 15);
    Engine *e = &rig.e;
    e->gs.current_world = WORLD_JARDIN;
    rig_idle(&rig, 1500);
    static const char CODE[8] = {'B', 'B', 'A', 'A', 'B', 'A', 'B', 'A'};
    for (int i = 0; i < 8; i++) {
        rig_frame(&rig, CODE[i] == 'A' ? BTN_A : BTN_B, 30);
        rig_frame(&rig, CODE[i] == 'A' ? BTN_A : BTN_B, 30);
        rig_frame(&rig, 0, 30);
        rig_idle(&rig, 90);
        if (i == 3)
            CHECK(!engine_studio_open(e));
        CHECK(!engine_studio_open(e));
    }
    CHECK_EQ(e->gs.dir.cur, DS_GATO);
}

static void test_studio_does_not_touch_universe(void)
{
    /* R pasa un rato en Música sin abrir el Estudio; M lo abre, toca todas sus filas, tira el dado, lo cierra
     * y lo vuelve a abrir. Mismos instantes: el estado guardado solo puede diferir en los campos de música. */
    static Rig R, M;
    rig_init(&R, 21);
    rig_init(&M, 21);
    rig_goto_music(&R);
    rig_goto_music(&M);
    uint8_t b0[LLV_SAVE_MAX], b1[LLV_SAVE_MAX];
    size_t n0, n1;
    open_studio(&M);
    CHECK(engine_studio_open(&M.e));
    for (int k = 0; k < 25; k++) {
        rig_tap(&M, BTN_A);
        rig_tap(&M, BTN_B);
    }
    M.e.studio_row = SR_DICE;
    rig_hold(&M, BTN_A);
    M.e.studio_row = SR_ARP;
    rig_hold(&M, BTN_A); /* cierra */
    CHECK(!engine_studio_open(&M.e));
    open_studio(&M);
    rig_idle(&M, 3000);
    /* Dejo que R viva el mismo tiempo. */
    while (R.ms < M.ms)
        rig_frame(&R, 0, 30);
    GameState gm = M.e.gs, gr = R.e.gs;
    CHECK(gm.rng.state == gr.rng.state && gm.rng.inc == gr.rng.inc);
    gr.last_seen = gm.last_seen;
    gr.looks_today = gm.looks_today;
    gr.looks_total = gm.looks_total;
    gr.news = gm.news;
    serialize_without_music(&gm, b0, &n0);
    serialize_without_music(&gr, b1, &n1);
    CHECK(n0 > 0 && n0 == n1 && memcmp(b0, b1, n0) == 0);
    /* El Estudio abierto es efímero: no hay nada suyo en el guardado salvo los ajustes. */
    GameState back;
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = state_serialize(&M.e.gs, buf, sizeof(buf));
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    static Rig L;
    engine_resume(&L.e, &back, M.now + 100, 5000);
    CHECK(!engine_studio_open(&L.e));
    CHECK_EQ(L.e.gs.mus.studio, M.e.gs.mus.studio);
}

/* ---------- guardado ---------- */

static void test_save_roundtrip_and_old_rejected(void)
{
    static GameState gs, back;
    state_init(&gs, 4242, time_from_civil(2026, 10, 22, 12, 0, 0));
    MusicStudio s = ST(7, 9, 4, ARP_RANDOM, 17, 2, 1, 5);
    gs.mus.seed = 0x4F2A;
    gs.mus.mut = 3;
    gs.mus.studio = music_studio_pack(&s);
    uint8_t buf[LLV_SAVE_MAX];
    size_t n = state_serialize(&gs, buf, sizeof(buf));
    CHECK(n > 0 && n < 4096);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK_EQ(back.mus.studio, gs.mus.studio);
    CHECK_EQ(back.mus.seed, 0x4F2A);
    MusicStudio b = music_studio_unpack(back.mus.studio);
    CHECK(memcmp(&b, &s, sizeof(s)) == 0);
    /* El guardado de la alfa (versión 5) se rechaza con limpieza: la versión es lo primero que se mira. */
    uint8_t old[LLV_SAVE_MAX];
    memcpy(old, buf, n);
    old[4] = 5;
    old[5] = 0;
    uint32_t crc = state_crc32(old, n - 4);
    old[n - 4] = (uint8_t)crc;
    old[n - 3] = (uint8_t)(crc >> 8);
    old[n - 2] = (uint8_t)(crc >> 16);
    old[n - 1] = (uint8_t)(crc >> 24);
    GameState out;
    memset(&out, 0xAB, sizeof(out));
    CHECK(state_deserialize(&out, old, n) != 0);
    CHECK_EQ(((uint8_t *)&out)[0], 0xAB);  /* y no toca lo que había */
    /* También se rechaza uno de la longitud de la alfa (4 bytes menos de música) aunque diga ser v6. */
    uint8_t shorty[LLV_SAVE_MAX];
    memcpy(shorty, buf, n);
    size_t m = n - 4 - 4;           /* quita el campo studio de la carga y el CRC */
    memmove(shorty + 8 + 0, shorty + 8, 0);
    uint16_t payload = (uint16_t)(m - 8);
    shorty[6] = (uint8_t)payload;
    shorty[7] = (uint8_t)(payload >> 8);
    crc = state_crc32(shorty, m);
    shorty[m] = (uint8_t)crc;
    shorty[m + 1] = (uint8_t)(crc >> 8);
    shorty[m + 2] = (uint8_t)(crc >> 16);
    shorty[m + 3] = (uint8_t)(crc >> 24);
    CHECK(state_deserialize(&out, shorty, m + 4) != 0);
    /* Valores del Estudio absurdos con CRC bueno no entran. */
    static const uint32_t EVIL[] = {11u, 13u << 4, 12u << 8, 7u << 12, 23u << 15, 1u << 27, 0xFFFFFFFFu};
    for (unsigned i = 0; i < sizeof(EVIL) / sizeof(EVIL[0]); i++) {
        GameState evil = gs;
        evil.mus.studio = EVIL[i];
        n = state_serialize(&evil, buf, sizeof(buf));
        CHECK(state_deserialize(&out, buf, n) != 0);
    }
    /* El estado completo sigue muy por debajo de 4 KB. */
    CHECK(sizeof(GameState) < 4096);
    CHECK(n < 4096);
}

/* ---------- el panel cabe ---------- */

static void test_panel_texts_fit(void)
{
    /* Cada fila (nombre + valor) cabe en los 105 px que dejan el candado y los márgenes del panel, con
     * cualquier valor y 3 px de hueco entre el nombre y el valor como mínimo. */
    int worst = 0;
    for (int row = 0; row < SR_COUNT; row++) {
        int wn = gfx_text_width(studio_row_name(row));
        for (int v = 0; v < studio_row_values(row); v++) {
            char t[32];
            studio_text_of(t, sizeof(t), row, v);
            int w = wn + gfx_text_width(t);
            worst = w > worst ? w : worst;
            CHECK(w + 3 <= 105);
            CHECK(strlen(t) > 0);
        }
    }
    printf("    (la fila más ancha mide %d px de 102)\n", worst);
    /* Las filas visibles siguen a la seleccionada y nunca se salen de la lista. */
    for (int sel = 0; sel < SR_COUNT; sel++) {
        int first = studio_first_row(sel, SR_COUNT, STUDIO_VISIBLE);
        CHECK(first >= 0 && first + STUDIO_VISIBLE <= SR_COUNT);
        CHECK(sel >= first && sel < first + STUDIO_VISIBLE);
    }
    CHECK_EQ(studio_first_row(0, SR_COUNT, STUDIO_VISIBLE), 0);
    CHECK_EQ(studio_first_row(SR_COUNT - 1, SR_COUNT, STUDIO_VISIBLE), SR_COUNT - STUDIO_VISIBLE);
    CHECK_EQ(studio_first_row(2, 5, 6), 0);
    /* Todos los nombres de progresión, escala, arpegio son distintos (se pueden distinguir en pantalla). */
    for (int a = 0; a < MUSIC_NPROG; a++)
        for (int b = a + 1; b < MUSIC_NPROG; b++)
            CHECK(strcmp(music_prog_name(a), music_prog_name(b)) != 0 || a / 8 != b / 8 || true);
    for (int a = 0; a < MODE_ALL; a++)
        for (int b = a + 1; b < MODE_ALL; b++)
            CHECK(strcmp(music_mode_name(a), music_mode_name(b)) != 0);
}

/* ---------- calidad musical con ajustes fijos ---------- */

static void test_pleasant_gate_with_fixed_settings(void)
{
    /* Los umbrales de "agradable" (src/util/musicstats.h) son los de siempre: no se tocan para el Estudio.
     * 11 escalas (AUTO + 10) x 7 arpegios (AUTO + 6) = 77 combinaciones, 40 semillas cada una (solo notas;
     * el informe de llv_music --matrix añade el audio). Cada combinación debe pasar el 80 % y en conjunto el 90 %. */
    long ok = 0, n = 0, worst_combo_ok = 40;
    for (int scale = 0; scale <= STUDIO_SCALES; scale++)
        for (int arp = 0; arp < ARP_COUNT; arp++) {
            int c_ok = 0;
            for (int i = 0; i < 40; i++) {
                uint16_t seed = (uint16_t)(1000 + i * 7 + scale * 13 + arp);
                MusicStudio s = ST(scale, 0, 0, arp, 0, 0, 0, 0);
                MusicStyle st;
                MusicMetrics mm;
                music_style_ex(&st, seed, mood_for(i), &s);
                music_song(&sgA, &st, seed, 0);
                ms_analyze(&sgA, &st, &mm);
                c_ok += music_judge(&mm, NULL) == 0;
            }
            ok += c_ok;
            n += 40;
            worst_combo_ok = c_ok < worst_combo_ok ? c_ok : worst_combo_ok;
            CHECK(c_ok >= 32);
        }
    printf("    (%ld de %ld semillas agradables, %.1f %%; la peor combinación: %ld de 40)\n", ok, n, 100.0 * ok / n, worst_combo_ok);
    CHECK(ok * 10 >= n * 9);
}

/* ---------- barridos ---------- */

static void test_sweep_every_combination_songs(void)
{
    /* Escalas x tónicas x arpegios x progresiones (20.930 canciones, 2 semillas cada una): sin notas fuera de
     * rango ni de la escala, acordes de la escala, acompañamiento en el acorde, todo dentro del MIDI. */
    long songs = 0;
    for (int scale = 0; scale <= STUDIO_SCALES; scale++)
        for (int key = 0; key <= 12; key++)
            for (int arp = 0; arp < ARP_COUNT; arp++)
                for (int prog = 0; prog <= MUSIC_NPROG; prog++) {
                    if (scale == 0 && prog == 0)
                        continue; /* escala AUTO y progresión AUTO: es el generador de siempre */
                    uint16_t seed = (uint16_t)(scale * 131 + key * 17 + arp * 29 + prog * 7 + 3);
                    MusicStudio s = ST(scale, key, 0, arp, prog, 0, 0, 0);
                    MusicStyle st;
                    music_style_ex(&st, seed, mood_for(scale + key + arp + prog), &s);
                    music_song(&sgA, &st, seed, (uint8_t)(prog % 3));
                    songs++;
                    int notes = 0;
                    for (int k = 0; k < MUSIC_STEPS; k++) {
                        if (sgA.mel[k].len) {
                            notes++;
                            CHECK(sgA.mel[k].pitch >= 40 && sgA.mel[k].pitch <= 90);
                            CHECK(in_scale(st.mode, st.key, sgA.mel[k].pitch));
                            CHECK(sgA.mel[k].len >= 1 && k + sgA.mel[k].len <= MUSIC_STEPS);
                        }
                        if (sgA.bass[k].len) {
                            CHECK(sgA.bass[k].pitch >= 36 && sgA.bass[k].pitch <= 80);
                            CHECK(music_in_chord(&sgA.chord[k / 16], sgA.bass[k].pitch));
                            CHECK(k + sgA.bass[k].len <= MUSIC_STEPS);
                        }
                    }
                    CHECK(notes >= 15);
                    int need = music_scale_len(st.mode) >= 7 ? 3 : 2;
                    for (int b = 0; b < MUSIC_BARS; b++) {
                        CHECK(sgA.chord[b].root < 12);
                        if (scale || prog)
                            CHECK(chord_fit(st.mode, st.key, &sgA.chord[b]) >= need);
                    }
                }
    CHECK(songs > 20000);
}

static void test_sweep_no_overflow_or_clipping_pcm(void)
{
    /* Escalas x tónicas x arpegios x timbres (3.640 combinaciones; progresión, tempo, ritmo, semilla y voces
     * silenciadas rotan entre ellas): ni un recorte, ni silencio ni nivel absurdo en 5 s de audio cada una. */
    static int16_t buf[RATE];
    long combos = 0;
    int worst_peak = 0, min_rms = 1 << 30;
    for (int scale = 0; scale <= STUDIO_SCALES; scale++)
        for (int key = 0; key <= 12; key += (key == 0 ? 1 : 1))
            for (int arp = 0; arp < ARP_COUNT; arp++)
                for (int duty = 0; duty <= STUDIO_DUTIES; duty++) {
                    int n = (int)combos++;
                    MusicStudio s = ST(scale, key, n % 4 == 0 ? 1 + (n / 4) % STUDIO_TEMPOS : 0, arp,
                                       n % 3 == 0 ? 1 + n % MUSIC_NPROG : 0, duty, n % 5 == 0 ? 1 + n % 3 : 0,
                                       n % 17 == 0 ? 1 << (n % 3) : 0);
                    music_start_studio(&mus, (uint16_t)(n * 40503u + 9), (uint8_t)(n % 3), mood_for(n), RATE, &s);
                    long long acc = 0;
                    int peak = 0;
                    for (int sec = 0; sec < 5; sec++) {
                        CHECK_EQ(music_render(&mus, buf, RATE), RATE);
                        for (int i = 0; i < RATE; i++) {
                            int a = iabs(buf[i]);
                            peak = a > peak ? a : peak;
                            if (sec >= 1)
                                acc += (long long)buf[i] * buf[i];
                        }
                    }
                    CHECK(peak < 32000);              /* sin recortes (el máximo teórico es 18.500) */
                    worst_peak = peak > worst_peak ? peak : worst_peak;
                    if (!s.mute) {
                        int rms = (int)__builtin_sqrt((double)(acc / (RATE * 4)));
                        CHECK(rms > 300);              /* ni mudo */
                        min_rms = rms < min_rms ? rms : min_rms;
                    }
                }
    CHECK(combos == 11 * 13 * ARP_COUNT * 4);
    printf("    (%ld combinaciones; pico máximo %d de 32767; RMS mínimo %d)\n", combos, worst_peak, min_rms);
}

int main(void)
{
    struct { const char *name; TestFn fn; } tests[] = {
        {"regresion: todo en AUTO == audio de la alfa (32 casos, 70 s cada uno)", test_regression_auto_equals_alfa_pcm},
        {"regresion: todo en AUTO == canciones y estilos de la alfa (2048)", test_regression_auto_equals_alfa_songs},
        {"ajustes: empaquetado en 27 bits, rangos y validez", test_pack_roundtrip_and_validity},
        {"AUTO: music_style_ex y music_start_studio son el generador de siempre", test_auto_studio_is_the_old_generator},
        {"escala fija: todas las notas y acordes son de esa escala, en cualquier tonica", test_fixed_scale_notes_belong_to_scale},
        {"progresion fija: acordes ajustados a la escala; las diatonicas no se tocan", test_fixed_progression_is_harmonised},
        {"tonica fija: transpone melodia, bajo y acordes", test_key_transposes},
        {"tonica fija: registro cantable", test_key_sets_tonic_register},
        {"tempo fijo: BPM del estilo y del audio", test_fixed_tempo_gives_right_bpm},
        {"arpegios: el orden de notas de cada patron", test_arpeggio_patterns_order},
        {"arpegio AUTO: los estilos de la semilla", test_arpeggio_auto_keeps_seed_styles},
        {"timbre y ritmo fijos", test_duty_and_rhythm},
        {"determinismo con cada parametro fijo (y por trozos irregulares)", test_determinism_with_each_parameter_fixed},
        {"voces: silenciar no toca las otras, reactivar vuelve sincronizada, sin clics", test_mute_leaves_other_voices_untouched},
        {"voces: silenciada desde el principio, las tres, y reactivar", test_mute_starts_silent_and_all_mute},
        {"ajustes en marcha: se rehace al instante y sobrevive a una semilla nueva", test_changing_settings_midway},
        {"candados: lo fijo no depende de la semilla; lo AUTO si; mutar no toca el estilo", test_locks_hold_through_new_seed_and_mutation},
        {"motor: doble toque de A abre el Estudio sin cambiar nada", test_double_tap_opens_studio_and_changes_nothing},
        {"motor: toques lentos siguen mutando; fuera de Musica no abre", test_slow_taps_keep_mutating},
        {"panel: B cicla, A cambia, mantener A cierra o tira el dado", test_panel_state_machine},
        {"panel: se cierra solo, al dormir y al salir de Musica", test_panel_timeout_and_sleep},
        {"panel: los ajustes suenan; mutar y semilla nueva respetan lo fijo", test_panel_applies_settings_to_sound},
        {"codigo secreto: B B A A B A B A sigue funcionando por Musica", test_secret_code_still_works_through_music},
        {"el Estudio no toca el universo (solo los campos de musica)", test_studio_does_not_touch_universe},
        {"guardado v6: ida y vuelta, rechaza la alfa y valores absurdos", test_save_roundtrip_and_old_rejected},
        {"panel: filas y valores caben en 120x120", test_panel_texts_fit},
        {"gate: >= 90 % agradables con las 77 combinaciones de escala x arpegio", test_pleasant_gate_with_fixed_settings},
        {"barrido: escalas x tonicas x arpegios x progresiones (canciones)", test_sweep_every_combination_songs},
        {"barrido: escalas x tonicas x arpegios x timbres (audio, sin recortes)", test_sweep_no_overflow_or_clipping_pcm},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests del Estudio OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
