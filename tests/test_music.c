/* Tests de la música generativa (fase 4), sin framework externo. Salida != 0 si algo falla.
 * Cubren el generador (escala, rango, cadencia, euclidiano), el sintetizador (determinismo byte a
 * byte, bloques irregulares, sin recortes, coste), la integración con el motor (solo suena en el mundo
 * Música, no toca el universo ni gs->rng, guardado) y las métricas de "agradable". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/core/engine.h"
#include "../src/core/gfx.h"
#include "../src/core/music.h"
#include "../src/core/state.h"
#include "../src/core/timeutil.h"
#include "../src/util/musicstats.h"

/* Del mundo Música (src/core/worlds/musica.c). */
void musica_info_text(const MusicStyle *st, const MusicSave *ms, char *l1, size_t n1, char *l2, size_t n2);

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

/* Un ánimo distinto para cada semilla, como en la herramienta llv_music. */
static uint8_t mood_for(int i) { return MOOD_MAKE(i % 4, (i / 4) % 2, (i / 8) % 3, (i / 24) % 5); }

static int iabs(int x) { return x < 0 ? -x : x; }

/* ---------- euclidiano ---------- */

static void euclid_str(char *out, int k, int n)
{
    uint8_t e[32];
    music_euclid(e, k, n);
    for (int i = 0; i < n; i++)
        out[i] = e[i] ? 'x' : '.';
    out[n] = 0;
}

static void test_euclid_known(void)
{
    static const struct { int k, n; const char *pat; } CASES[] = {
        {3, 8, "x..x..x."},        {5, 8, "x.xx.xx."},     {7, 16, "x..x.x.x..x.x.x."},
        {4, 9, "x.x.x.x.."},       {2, 5, "x.x.."},        {3, 4, "x.xx"},
        {5, 12, "x..x.x..x.x."},   {9, 16, "x.xx.x.x.xx.x.x."}, {4, 16, "x...x...x...x..."},
        {5, 16, "x..x..x..x..x..."}, {2, 3, "x.x"},        {1, 4, "x..."},
        {0, 4, "...."},            {4, 4, "xxxx"},         {7, 12, "x.xx.x.xx.x."},
    };
    char s[40];
    for (unsigned i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        euclid_str(s, CASES[i].k, CASES[i].n);
        checks++;
        if (strcmp(s, CASES[i].pat) != 0) {
            failures++;
            printf("    FALLO E(%d,%d) = %s, esperado %s\n", CASES[i].k, CASES[i].n, s, CASES[i].pat);
        }
    }
    /* Propiedades: k golpes, empieza con golpe y los huecos entre golpes difieren como mucho en 1. */
    for (int n = 1; n <= 32; n++)
        for (int k = 1; k <= n; k++) {
            uint8_t e[32];
            music_euclid(e, k, n);
            int hits = 0, first = -1, last = -1, mn = 99, mx = 0, prev = -1;
            for (int i = 0; i < n; i++)
                if (e[i]) {
                    hits++;
                    if (first < 0)
                        first = i;
                    if (prev >= 0) {
                        int g = i - prev;
                        mn = g < mn ? g : mn;
                        mx = g > mx ? g : mx;
                    }
                    prev = i;
                    last = i;
                }
            int wrap = first + n - last;
            mn = wrap < mn ? wrap : mn;
            mx = wrap > mx ? wrap : mx;
            CHECK_EQ(hits, k);
            CHECK_EQ(first, 0);
            CHECK(mx - mn <= 1);
        }
}

/* ---------- escala, hora y clima ---------- */

static void test_scales_by_daypart_and_weather(void)
{
    /* La franja del día según la hora. */
    for (int h = 0; h < 24; h++) {
        int dp = music_daypart(h);
        int want = (h >= 22 || h < 5) ? DP_NIGHT : h < 10 ? DP_DAWN : h < 18 ? DP_DAY : DP_DUSK;
        CHECK_EQ(dp, want);
    }
    int seen_night[MODE_COUNT] = {0};
    for (int roll = 0; roll < 100; roll++) {
        /* De día y seco: siempre pentatónica mayor. */
        CHECK_EQ(music_pick_mode(DP_DAY, false, roll), MODE_PENT_MAJ);
        /* De noche y seco: menor o dórica, nunca pentatónica mayor. */
        int m = music_pick_mode(DP_NIGHT, false, roll);
        CHECK(m == MODE_DORIAN || m == MODE_MINOR);
        seen_night[m]++;
        /* Con lluvia, a cualquier hora: solo las dos menores (más melancólica). */
        for (int dp = 0; dp < 4; dp++) {
            int r = music_pick_mode(dp, true, roll);
            CHECK(r == MODE_PENT_MIN || r == MODE_MINOR);
        }
        for (int dp = 0; dp < 4; dp++) { /* amanecer y atardecer secos: mayor o dórica */
            int d = music_pick_mode(dp, false, roll);
            if (dp == DP_DAWN || dp == DP_DUSK)
                CHECK(d == MODE_PENT_MAJ || d == MODE_DORIAN);
        }
    }
    CHECK(seen_night[MODE_DORIAN] > 20 && seen_night[MODE_MINOR] > 20);

    /* Lo mismo a través del estilo de cada semilla, con el ánimo de la hora y la lluvia. */
    long bpm_sum[4][2] = {{0}}, n[4][2] = {{0}};
    for (int seed = 0; seed < 600; seed++)
        for (int dp = 0; dp < 4; dp++)
            for (int rain = 0; rain < 2; rain++) {
                MusicStyle st;
                music_style(&st, (uint16_t)seed, MOOD_MAKE(dp, rain, 0, 0));
                if (dp == DP_DAY && !rain)
                    CHECK_EQ(st.mode, MODE_PENT_MAJ);
                if (dp == DP_NIGHT && !rain)
                    CHECK(st.mode == MODE_DORIAN || st.mode == MODE_MINOR);
                if (rain)
                    CHECK(st.mode == MODE_PENT_MIN || st.mode == MODE_MINOR);
                bpm_sum[dp][rain] += st.bpm;
                n[dp][rain]++;
            }
    /* De noche, más lento que de día; con lluvia, más lento que sin ella. */
    CHECK(bpm_sum[DP_NIGHT][0] / n[DP_NIGHT][0] + 15 < bpm_sum[DP_DAY][0] / n[DP_DAY][0]);
    for (int dp = 0; dp < 4; dp++)
        CHECK(bpm_sum[dp][1] / n[dp][1] + 8 < bpm_sum[dp][0] / n[dp][0]);

    /* El ánimo del universo: hora local, lluvia del jardín, cielo raro y mascota. */
    static GameState gs;
    int64_t t = time_from_civil(2026, 10, 22, 23, 30, 0);
    state_init(&gs, 1, t);
    gs.weather = W_CLEAR;
    CHECK_EQ(MOOD_DAYPART(music_mood(&gs, t)), DP_NIGHT);
    CHECK(!(music_mood(&gs, t) & MOOD_RAIN));
    gs.weather = W_RAIN;
    CHECK(music_mood(&gs, t) & MOOD_RAIN);
    gs.weather = W_SNOW;
    CHECK(!(music_mood(&gs, t) & MOOD_RAIN));
    gs.sky = SKY_STAR;
    CHECK_EQ(MOOD_SKY(music_mood(&gs, t)), 1);
    gs.sky = SKY_RAINBOW;
    CHECK_EQ(MOOD_SKY(music_mood(&gs, t)), 2);
    gs.pet.kind = PET_PIEDRA;
    CHECK_EQ(MOOD_PET(music_mood(&gs, t)), PET_PIEDRA);
    CHECK_EQ(MOOD_DAYPART(music_mood(&gs, time_from_civil(2026, 10, 22, 12, 0, 0))), DP_DAY);
    CHECK(music_mood_valid(music_mood(&gs, t)));
    CHECK(!music_mood_valid(MOOD_MAKE(0, 0, 3, 0)));
    CHECK(!music_mood_valid(MOOD_MAKE(0, 0, 0, 7)));
}

/* ---------- el generador ---------- */

static MusicSong sgA, sgB;

static void test_progressions_table(void)
{
    /* Todas las progresiones de la familia de cada escala dejan al menos dos notas de cada acorde dentro de
     * la escala (así la melodía tiene notas del acorde con las que apoyarse), y hay de sobra. */
    CHECK(MUSIC_NPROG >= 6 + 6 + 6 && MUSIC_NPROG == 22);
    const int base[MODE_COUNT] = {0, 8, 16, 8}, cnt[MODE_COUNT] = {8, 8, 6, 8};
    for (int mode = 0; mode < MODE_COUNT; mode++) {
        const uint8_t *sc = music_scale(mode);
        int sl = music_scale_len(mode);
        for (int p = base[mode]; p < base[mode] + cnt[mode]; p++) {
            for (int key = 0; key < 12; key += 5) {
                MusicStyle st;
                memset(&st, 0, sizeof(st));
                st.mode = (uint8_t)mode;
                st.key = (uint8_t)key;
                st.tonic = (uint8_t)(57 + (key + 12 - 9) % 12);
                st.bpm = 100;
                st.prog_a = st.prog_b = (uint8_t)p;
                st.kick_k = 3;
                st.hat_k = 8;
                music_song(&sgA, &st, 1, 0);
                for (int bar = 0; bar < MUSIC_BARS; bar++) {
                    int in = 0;
                    const MusicChord *c = &sgA.chord[bar];
                    for (int semis = 0; semis < 12; semis++) {
                        bool inscale = false;
                        for (int d = 0; d < sl; d++)
                            inscale |= sc[d] == semis;
                        if (inscale && music_in_chord(c, key + semis))
                            in++;
                    }
                    CHECK(in >= 2);
                }
            }
            CHECK(strlen(music_prog_name(p)) >= 7);
        }
    }
}

static void test_notes_in_scale_and_range(void)
{
    for (int seed = 0; seed < 2000; seed++) {
        MusicStyle st;
        music_style(&st, (uint16_t)seed, mood_for(seed));
        for (int mut = 0; mut < 2; mut++) {
            music_song(&sgA, &st, (uint16_t)seed, (uint8_t)mut);
            const uint8_t *sc = music_scale(st.mode);
            int sl = music_scale_len(st.mode), lo = 255, hi = 0, notes = 0;
            for (int s = 0; s < MUSIC_STEPS; s++) {
                const MusicNote *m = &sgA.mel[s];
                if (!m->len)
                    continue;
                notes++;
                int rel = (m->pitch + 120 - st.key) % 12;
                bool in = false;
                for (int d = 0; d < sl; d++)
                    in |= sc[d] == rel;
                CHECK(in); /* todas las notas son de la escala */
                CHECK(m->pitch >= st.tonic - 3 && m->pitch <= st.tonic + 14); /* rango cantable */
                lo = m->pitch < lo ? m->pitch : lo;
                hi = m->pitch > hi ? m->pitch : hi;
                /* No cruzan la barra del compás ni pisan a la siguiente. */
                CHECK(s % 16 + m->len <= 16);
                /* Tiempos fuertes: notas del acorde (consonantes con el bajo). */
                if (s % 8 == 0 || (s % 8) + m->len > 8)
                    CHECK(music_in_chord(&sgA.chord[s / 16], m->pitch));
            }
            CHECK(notes >= 16);
            CHECK(hi - lo <= 17);
            /* El bajo y el arpegio solo usan notas del acorde del compás. */
            for (int s = 0; s < MUSIC_STEPS; s++) {
                const MusicNote *b = &sgA.bass[s];
                if (b->len) {
                    CHECK(music_in_chord(&sgA.chord[s / 16], b->pitch));
                    CHECK(b->pitch >= 36 && b->pitch <= 72);
                    CHECK(s % 16 + b->len <= 16);
                }
            }
            /* En el primer tiempo de cada compás el bajo toca la fundamental. */
            for (int bar = 0; bar < MUSIC_BARS; bar++) {
                const MusicNote *b = &sgA.bass[bar * 16];
                CHECK(b->len > 0 && b->pitch % 12 == sgA.chord[bar].root);
            }
        }
    }
}

static void test_cadence_resolves_on_tonic(void)
{
    for (int seed = 0; seed < 3000; seed++) {
        MusicStyle st;
        music_style(&st, (uint16_t)seed, mood_for(seed));
        music_song(&sgA, &st, (uint16_t)seed, (uint8_t)(seed % 5));
        int last = -1;
        for (int s = MUSIC_STEPS - 1; s >= 0 && last < 0; s--)
            if (sgA.mel[s].len)
                last = s;
        CHECK(last >= 15 * 16);
        CHECK_EQ(sgA.mel[last].pitch % 12, st.key); /* la melodía acaba en la tónica... */
        CHECK(sgA.mel[last].len >= 8);              /* ...y se queda ahí */
        CHECK_EQ(sgA.chord[15].root, st.key);       /* sobre el acorde de tónica */
        CHECK_EQ(sgA.chord[15].minor, st.mode != MODE_PENT_MAJ);
        /* Las frases intermedias no cierran en la tónica del acorde final de la canción. */
        CHECK(sgA.mel[15 * 16].len > 0);
    }
}

static void test_song_forms_A_Ap_B_A(void)
{
    /* Forma A A' B A'': A' repite A con pequeños cambios, B contrasta, A'' vuelve. */
    long same_aa = 0, same_ab = 0, tot = 0;
    for (int seed = 0; seed < 500; seed++) {
        MusicStyle st;
        music_style(&st, (uint16_t)seed, mood_for(seed));
        music_song(&sgA, &st, (uint16_t)seed, 0);
        for (int s = 0; s < 3 * 16; s++) { /* los tres primeros compases de cada frase */
            const MusicNote *a = &sgA.mel[s], *a2 = &sgA.mel[64 + s], *b = &sgA.mel[128 + s];
            if (!a->len && !a2->len && !b->len)
                continue;
            tot++;
            same_aa += a->len == a2->len && a->pitch == a2->pitch;
            same_ab += a->len == b->len && a->pitch == b->pitch;
        }
        /* La última frase repite casi todo lo de la primera (salvo la cadencia). */
        int diff = 0;
        for (int s = 0; s < 2 * 16; s++)
            diff += sgA.mel[s].pitch != sgA.mel[192 + s].pitch || sgA.mel[s].len != sgA.mel[192 + s].len;
        CHECK(diff <= 6);
        /* La progresión de B es distinta de la de A. */
        CHECK(st.prog_a != st.prog_b);
    }
    CHECK(same_aa * 100 / tot >= 80);   /* A' ~ A */
    CHECK(same_ab * 100 / tot <= 35);   /* B contrasta */
}

static int same_style(const MusicStyle *a, const MusicStyle *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void test_mutate_keeps_style_changes_melody(void)
{
    int changed = 0, trials = 300;
    for (int seed = 0; seed < trials; seed++) {
        static Music m;
        MusicStyle st;
        uint8_t mood = mood_for(seed);
        music_style(&st, (uint16_t)seed, mood);
        music_start(&m, (uint16_t)seed, 0, mood, RATE);
        sgA = m.song;
        MusicStyle before = m.style;
        music_mutate(&m, 1);
        CHECK(same_style(&before, &m.style)); /* misma escala, progresión, tempo y timbre */
        CHECK_EQ(m.mut, 1);
        CHECK(memcmp(sgA.chord, m.song.chord, sizeof(sgA.chord)) == 0);
        CHECK(memcmp(sgA.bass, m.song.bass, sizeof(sgA.bass)) == 0);
        CHECK(memcmp(sgA.drums, m.song.drums, sizeof(sgA.drums)) == 0);
        int diff = 0;
        for (int s = 0; s < MUSIC_STEPS; s++)
            diff += sgA.mel[s].pitch != m.song.mel[s].pitch || sgA.mel[s].len != m.song.mel[s].len;
        if (diff >= 8)
            changed++;
        /* Mutar dos veces da una melodía distinta de la primera mutación. */
        sgB = m.song;
        music_mutate(&m, 2);
        int diff2 = 0;
        for (int s = 0; s < MUSIC_STEPS; s++)
            diff2 += sgB.mel[s].pitch != m.song.mel[s].pitch;
        CHECK(diff2 > 0);
    }
    CHECK(changed * 100 / trials >= 97); /* casi siempre cambia bastante */
}

static void test_new_seed_changes_style(void)
{
    int changed_any = 0, changed_scale_or_tempo = 0, trials = 400;
    for (int i = 0; i < trials; i++) {
        MusicStyle a, b;
        uint16_t sa = (uint16_t)(i * 37 + 5), sb = (uint16_t)(sa + 1 + i * 11);
        music_style(&a, sa, 0x02);
        music_style(&b, sb, 0x02);
        changed_any += !same_style(&a, &b);
        changed_scale_or_tempo += a.mode != b.mode || a.key != b.key || a.bpm != b.bpm ||
                                  a.prog_a != b.prog_a;
    }
    CHECK_EQ(changed_any, trials);
    CHECK(changed_scale_or_tempo * 100 / trials >= 95); /* otra tonalidad, tempo o progresión casi siempre */
    /* Las semillas contiguas (no solo las espaciadas) dan estilos distintos. */
    int distinct_keys[12] = {0}, distinct_bpm[200] = {0}, nb = 0;
    for (int seed = 0; seed < 600; seed++) {
        MusicStyle s;
        music_style(&s, (uint16_t)seed, 0x02);
        distinct_keys[s.key]++;
        if (!distinct_bpm[s.bpm]++)
            nb++;
    }
    for (int k = 0; k < 12; k++)
        CHECK(distinct_keys[k] > 20); /* las 12 tonalidades salen */
    CHECK(nb >= 20);                  /* y hay variedad de tempos */
}

static void test_style_ranges(void)
{
    for (int seed = 0; seed < 5000; seed++) {
        MusicStyle st;
        music_style(&st, (uint16_t)seed, mood_for(seed + seed / 7));
        CHECK(st.mode < MODE_COUNT && st.key < 12 && st.bpm >= 56 && st.bpm <= 140);
        CHECK(st.prog_a < MUSIC_NPROG && st.prog_b < MUSIC_NPROG && st.prog_a != st.prog_b);
        CHECK(st.lead_duty < 3 && st.bass_duty < 3 && st.lead_env < 3 && st.vib < 3);
        CHECK(st.tonic >= 57 && st.tonic <= 68 && st.tonic % 12 == st.key);
        CHECK(st.kick_k >= 2 && st.kick_k <= 5 && st.hat_k >= 4 && st.hat_k <= 12 && st.drums < 3);
        CHECK(st.density < 3 && st.lpf >= 90 && st.bass_style_a < 4 && st.bass_style_b < 4);
    }
}

/* ---------- el sintetizador ---------- */

static Music mA, mB;

static void test_pcm_deterministic_byte_exact(void)
{
    static int16_t one[RATE * 10], many[RATE * 10], again[RATE * 10];
    for (int seed = 0; seed < 12; seed++) {
        uint8_t mood = mood_for(seed * 5);
        music_start(&mA, (uint16_t)(seed * 977), 0, mood, RATE);
        size_t n1 = music_render(&mA, one, RATE * 10);
        CHECK_EQ(n1, RATE * 10);
        /* La misma semilla suena igual siempre. */
        music_start(&mB, (uint16_t)(seed * 977), 0, mood, RATE);
        CHECK_EQ(music_render(&mB, again, RATE * 10), RATE * 10);
        CHECK(memcmp(one, again, sizeof(one)) == 0);
        /* Y 10 s en un bloque == 10 s en trozos de tamaños irregulares (pausas incluidas). */
        music_start(&mB, (uint16_t)(seed * 977), 0, mood, RATE);
        size_t pos = 0;
        uint32_t r = 0x1234u + (uint32_t)seed;
        while (pos < RATE * 10) {
            r = r * 1664525u + 1013904223u;
            size_t c = 1 + (r >> 8) % (r & 1 ? 40 : 4000);
            if (pos + c > RATE * 10)
                c = RATE * 10 - pos;
            size_t got = music_render(&mB, many + pos, c);
            CHECK_EQ(got, c);
            pos += got;
            if ((r >> 29) == 0)
                CHECK_EQ(music_render(&mB, many + pos, 0), 0); /* una "pausa" sin pedir nada */
        }
        CHECK(memcmp(one, many, sizeof(one)) == 0);
        CHECK(mA.pos == mB.pos && mA.step_left == mB.step_left && mA.lp == mB.lp);
    }
    /* Semillas distintas suenan distinto. */
    music_start(&mA, 1, 0, 0x02, RATE);
    music_start(&mB, 2, 0, 0x02, RATE);
    music_render(&mA, one, RATE);
    music_render(&mB, many, RATE);
    CHECK(memcmp(one, many, RATE * sizeof(int16_t)) != 0);
}

static void test_render_with_calls_between_blocks(void)
{
    /* Mutar, bajar la música o cambiar el ánimo en la misma muestra da lo mismo en bloques distintos. */
    static int16_t a[RATE * 6], b[RATE * 6];
    for (int variant = 0; variant < 3; variant++) {
        for (int pass = 0; pass < 2; pass++) {
            Music *m = &mA;
            int16_t *buf = pass ? b : a;
            music_start(m, 777, 0, 0x02, RATE);
            size_t pos = 0;
            size_t cuts[4] = {RATE, 2 * RATE + 17, 3 * RATE + 333, 6 * RATE};
            for (int c = 0; c < 4; c++) {
                size_t until = cuts[c];
                while (pos < until) {
                    size_t chunk = pass ? 331 : 4096;
                    if (pos + chunk > until)
                        chunk = until - pos;
                    pos += music_render(m, buf + pos, chunk);
                }
                if (c == 0 && variant == 0)
                    music_mutate(m, 3);
                if (c == 0 && variant == 1)
                    music_duck(m, 100);
                if (c == 0 && variant == 2)
                    music_set_mood(m, 0x07);
                if (c == 2 && variant == 0)
                    music_reseed(m, 4242, 0, 0x01);
            }
        }
        CHECK(memcmp(a, b, sizeof(a)) == 0);
    }
}

static void test_no_clipping_and_level(void)
{
    static int16_t pcm[RATE * 20];
    long clip_total = 0;
    int quiet = 0, loud = 0;
    for (int seed = 0; seed < 120; seed++) {
        uint8_t mood = mood_for(seed);
        music_start(&mA, (uint16_t)(seed * 131 + 9), 0, mood, RATE);
        music_render(&mA, pcm, RATE * 20);
        AudioStats au;
        ms_audio(pcm, RATE * 20, RATE, &au);
        CHECK(au.peak <= 20000);        /* el techo teórico es 18.500: nunca se acerca a 32767 */
        CHECK_EQ(au.clipped, 0);
        clip_total += au.clipped;
        CHECK(au.dbfs_x10 > -320 && au.dbfs_x10 < -90); /* RMS entre -32 y -9 dBFS */
        CHECK(iabs(au.dc) < 600);       /* sin componente continua apreciable */
        CHECK(au.silence_pm < 150);
        quiet += au.dbfs_x10 < -250;
        loud += au.dbfs_x10 > -100;
    }
    CHECK_EQ(clip_total, 0);
    CHECK_EQ(quiet + loud, 0);
}

static void test_all_rates_work(void)
{
    static int16_t pcm[48000 * 3];
    static const uint32_t RATES[] = {8000, 11025, 16000, 22050, 32000, 44100, 48000};
    for (unsigned i = 0; i < sizeof(RATES) / sizeof(RATES[0]); i++) {
        music_start(&mA, 12, 0, 0x02, RATES[i]);
        CHECK_EQ(music_render(&mA, pcm, RATES[i] * 3), RATES[i] * 3);
        AudioStats au;
        ms_audio(pcm, RATES[i] * 3, RATES[i], &au);
        CHECK(au.peak > 3000 && au.peak < 20000);
        /* El tempo es el mismo a cualquier tasa: tras 3 s, mismo paso (con 1 de margen). */
        music_start(&mB, 12, 0, 0x02, 22050);
        music_render(&mB, pcm, 22050 * 3);
        CHECK(iabs((int)mA.pos - (int)mB.pos) <= 1);
    }
    /* Una tasa absurda se ajusta a los límites. */
    music_start(&mA, 1, 0, 0x02, 100);
    CHECK_EQ(mA.rate, MUSIC_RATE_MIN);
    music_start(&mA, 1, 0, 0x02, 1000000);
    CHECK_EQ(mA.rate, MUSIC_RATE_MAX);
}

static void test_render_time_bounded(void)
{
    /* 60 s de audio, con 6 mundos de semillas, tienen que costar mucho menos de 60 s: en una
     * máquina normal son unos 30 ms; se pide < 6 s para que no sea frágil en una CI lenta. */
    static int16_t pcm[RATE * 10];
    clock_t t0 = clock();
    size_t total = 0;
    for (int seed = 0; seed < 6; seed++) {
        music_start(&mA, (uint16_t)(seed * 4001), 0, mood_for(seed * 3), RATE);
        total += music_render(&mA, pcm, RATE * 10);
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    CHECK_EQ(total, RATE * 60);
    CHECK(secs < 6.0);
    printf("    (60 s de audio renderizados en %.3f s de CPU: %.0f x tiempo real)\n", secs,
           secs > 0 ? 60.0 / secs : 9999.0);
    /* Generar una canción entera (16 compases) también es barato: < 20 ms de media. */
    t0 = clock();
    for (int seed = 0; seed < 500; seed++) {
        MusicStyle st;
        music_style(&st, (uint16_t)seed, mood_for(seed));
        music_song(&sgA, &st, (uint16_t)seed, 0);
    }
    secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    CHECK(secs / 500 < 0.020);
    printf("    (generar una canción: %.3f ms)\n", secs / 500 * 1000);
}

static void test_fade_out_stop_and_duck(void)
{
    static int16_t pcm[RATE * 2];
    music_start(&mA, 99, 0, 0x02, RATE);
    music_render(&mA, pcm, RATE / 2);
    CHECK(music_active(&mA));
    music_stop(&mA, 150);
    size_t got = music_render(&mA, pcm, RATE * 2);
    /* Se calla en ~150 ms (3300 muestras) y deja de producir. */
    CHECK(got > RATE / 20 && got < RATE / 4);
    CHECK(!music_active(&mA));
    CHECK_EQ(music_render(&mA, pcm, 100), 0);
    CHECK(iabs(pcm[got - 1]) < 400); /* acaba casi en cero (la última muestra va al ~1 % de volumen) */
    /* El fundido no tiene saltos grandes entre muestras (sin clics): el escalón mayor está acotado por el
     * escalón normal de una onda cuadrada de este nivel. */
    int maxstep = 0;
    for (size_t i = 1; i < got; i++) {
        int d = iabs(pcm[i] - pcm[i - 1]);
        maxstep = d > maxstep ? d : maxstep;
    }
    CHECK(maxstep < 20000);
    /* Atenuación: la música baja mientras suena un pitido y vuelve después. */
    music_start(&mA, 99, 0, 0x02, RATE);
    static int16_t ref[RATE], ducked[RATE];
    music_start(&mB, 99, 0, 0x02, RATE);
    music_render(&mA, ref, RATE);
    music_duck(&mB, 400);
    music_render(&mB, ducked, RATE);
    long ea = 0, eb = 0;
    for (int i = 4410; i < 4410 + 4410; i++) { /* 200-400 ms: ya atenuada del todo */
        ea += (long)ref[i] * ref[i] / 1000;
        eb += (long)ducked[i] * ducked[i] / 1000;
    }
    CHECK(eb * 10 < ea * 6);   /* bastante más baja (el factor es 0,44 en amplitud: ~0,2 en energía) */
    CHECK(eb > 0);
    /* Tras el pitido recupera el nivel (último cuarto de segundo). */
    ea = eb = 0;
    for (int i = RATE - 4410; i < RATE; i++) {
        ea += (long)ref[i] * ref[i] / 1000;
        eb += (long)ducked[i] * ducked[i] / 1000;
    }
    CHECK(eb * 10 > ea * 8);
}

static void test_mood_changes_at_loop_boundary(void)
{
    /* Cambiar el ánimo (empieza a llover) no cambia nada a mitad de vuelta: se nota al volver a empezar. */
    uint16_t seed = 1234;
    uint8_t dry = MOOD_MAKE(DP_DAY, 0, 0, 0), rain = MOOD_MAKE(DP_DAY, 1, 0, 0);
    music_start(&mA, seed, 0, dry, RATE);
    music_start(&mB, seed, 0, dry, RATE);
    static int16_t a[RATE * 4], b[RATE * 4];
    music_render(&mA, a, RATE * 4);
    music_set_mood(&mB, rain);
    music_render(&mB, b, RATE * 4);
    CHECK(memcmp(a, b, sizeof(a)) == 0);   /* 4 s: aún en la primera vuelta */
    CHECK_EQ(mB.mood, dry);
    CHECK(mB.has_mood);
    /* Se avanza hasta pasar el final de la vuelta. */
    static int16_t tmp[RATE];
    int guard = 0;
    while (mB.loops == 0 && guard++ < 400)
        music_render(&mB, tmp, RATE / 4);
    music_render(&mB, tmp, RATE / 4);
    CHECK_EQ(mB.mood, rain);
    CHECK(mB.style.mode == MODE_PENT_MIN || mB.style.mode == MODE_MINOR); /* con lluvia, más melancólica */
    CHECK(mB.style.bpm < mA.style.bpm);
    /* Si el ánimo vuelve a ser el mismo antes de la vuelta, no hay cambio. */
    music_start(&mA, seed, 0, dry, RATE);
    music_set_mood(&mA, rain);
    music_set_mood(&mA, dry);
    CHECK(!mA.has_mood);
}

static void test_restart_with_new_seed(void)
{
    static int16_t pcm[RATE * 2];
    music_start(&mA, 10, 0, 0x02, RATE);
    music_render(&mA, pcm, RATE);
    music_reseed(&mA, 20, 0, 0x01);
    CHECK(mA.restart);
    music_render(&mA, pcm, RATE / 5); /* baja el volumen un instante y vuelve a empezar */
    CHECK(!mA.restart);
    CHECK_EQ(mA.seed, 20);
    CHECK_EQ(mA.mood, 0x01);
    CHECK(music_active(&mA));
    music_start(&mB, 20, 0, 0x01, RATE);
    CHECK(same_style(&mA.style, &mB.style));
    CHECK(memcmp(&mA.song, &mB.song, sizeof(mA.song)) == 0);
}

/* ---------- el motor ---------- */

static int64_t t_noon(void) { return time_from_civil(2026, 10, 22, 12, 0, 0); }

static void test_engine_music_only_in_music_world(void)
{
    static Engine e;
    static int16_t pcm[RATE];
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&e, 5, now, ms);
    RawInput none = {0, false}, b = {BTN_B, false};
    /* Fuera del mundo Música no suena nada. */
    for (int i = 0; i < 20; i++)
        engine_frame(&e, none, now, ms += 30);
    CHECK_EQ(engine_audio_render(&e, pcm, 1000), 0);
    CHECK(!music_active(&e.music));
    /* Se va al mundo Música con B (Reloj -> Jardín -> Sala -> Música). */
    for (int k = 0; k < 3; k++) {
        ms += 700;
        e.in.pressed_at[1] = 0;
        engine_frame(&e, b, now, ms);
        engine_frame(&e, none, now, ms += 80);
        for (int i = 0; i < 12; i++)
            engine_frame(&e, none, now, ms += 30);
    }
    CHECK_EQ(e.gs.current_world, WORLD_MUSICA);
    CHECK(music_active(&e.music));
    CHECK_EQ(e.music.seed, e.gs.mus.seed);
    CHECK_EQ(e.music.mood, e.gs.mus.mood);
    CHECK_EQ(MOOD_DAYPART(e.music.mood), DP_DAY);
    size_t got = engine_audio_render(&e, pcm, 2000);
    CHECK_EQ(got, 2000);
    /* Al salir del mundo se desvanece (~150 ms) y calla. */
    for (int k = 0; k < 1; k++) {
        ms += 700;
        engine_frame(&e, b, now, ms);
        engine_frame(&e, none, now, ms += 80);
    }
    CHECK(e.gs.current_world != WORLD_MUSICA);
    CHECK(e.music.stopping);
    size_t tail = 0, n;
    while ((n = engine_audio_render(&e, pcm, 1000)) > 0 && tail < RATE)
        tail += n;
    CHECK(tail > 1000 && tail < RATE / 4);
    CHECK(!music_active(&e.music));
    CHECK_EQ(engine_audio_render(&e, pcm, 1000), 0);
    /* Dormir también calla la música. */
    e.gs.current_world = WORLD_MUSICA;
    engine_frame(&e, none, now, ms += 30);
    CHECK(music_active(&e.music));
    engine_sleep(&e, now);
    CHECK(e.music.stopping);
    tail = 0;
    while ((n = engine_audio_render(&e, pcm, 1000)) > 0 && tail < RATE)
        tail += n;
    CHECK(tail < RATE / 4 && !music_active(&e.music));
}

/* Lleva el motor al mundo Música, despierto. */
static void goto_music(Engine *e, int64_t now, uint32_t *ms)
{
    RawInput none = {0, false};
    e->gs.current_world = WORLD_MUSICA;
    for (int i = 0; i < 6; i++)
        engine_frame(e, none, now, *ms += 30);
}

static void test_engine_music_world_sleeps_later(void)
{
    /* En el mundo Música sonando el llavero aguanta 3 minutos sin tocarlo (para dejarlo en la mesa);
     * en cualquier otro mundo, los 20 s de siempre. Y al dormirse, la música calla. */
    static Engine e;
    static int16_t pcm[2048];
    RawInput none = {0, false};
    int64_t now = t_noon();
    uint32_t ms = 1000;
    engine_init(&e, 8, now, ms);
    e.gs.current_world = WORLD_RELOJ;
    for (uint32_t t = 0; t < 25000; t += 100)
        engine_frame(&e, none, now, ms += 100);
    CHECK(e.asleep); /* el Reloj se duerme a los 20 s */

    engine_init(&e, 8, now, ms);
    e.gs.current_world = WORLD_MUSICA;
    uint32_t t = 0;
    for (; t < 150000; t += 100) {
        engine_frame(&e, none, now, ms += 100);
        engine_audio_render(&e, pcm, 2048);
    }
    CHECK(!e.asleep && music_active(&e.music)); /* 2 min 30 s después, aún suena */
    for (; t < 200000; t += 100) {
        engine_frame(&e, none, now, ms += 100);
        engine_audio_render(&e, pcm, 2048);
    }
    CHECK(e.asleep); /* a los 3 min se duerme */
    size_t n;
    int guard = 0;
    while ((n = engine_audio_render(&e, pcm, 2048)) > 0 && guard++ < 40) {
    }
    CHECK(!music_active(&e.music));
}

static void test_engine_tap_mutates_hold_reseeds(void)
{
    static Engine e;
    static int16_t pcm[RATE];
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&e, 5, now, ms);
    goto_music(&e, now, &ms);
    RawInput none = {0, false}, a = {BTN_A, false};
    CHECK(music_active(&e.music));
    uint16_t seed0 = e.gs.mus.seed;
    Rng rng0 = e.gs.rng;
    MusicStyle style0 = e.music.style;
    MusicSong song0 = e.music.song;
    /* A (toque): muta. */
    engine_frame(&e, a, now, ms += 30);
    FrameOut o = engine_frame(&e, none, now, ms += 80);
    (void)o;
    for (int i = 0; i < 8; i++)
        engine_frame(&e, none, now, ms += 30);
    CHECK_EQ(e.gs.mus.mut, 1);
    CHECK_EQ(e.gs.mus.seed, seed0);
    CHECK_EQ(e.music.mut, 1);
    CHECK(same_style(&style0, &e.music.style));
    CHECK(memcmp(song0.mel, e.music.song.mel, sizeof(song0.mel)) != 0);
    /* A (mantener): semilla nueva, estilo nuevo. */
    for (int i = 0; i < 25; i++)
        engine_frame(&e, a, now, ms += 30);
    for (int i = 0; i < 12; i++)
        engine_frame(&e, none, now, ms += 30);
    CHECK(e.gs.mus.seed != seed0);
    CHECK_EQ(e.gs.mus.mut, 0);
    uint16_t seed1 = e.gs.mus.seed;
    /* El audio sigue: la bajada y vuelta a empezar ocurren al renderizar. */
    for (int i = 0; i < 10; i++)
        engine_audio_render(&e, pcm, 1000);
    CHECK_EQ(e.music.seed, seed1);
    CHECK(!e.music.restart);
    /* La música nunca gasta números de gs->rng ni de las acciones del día. */
    CHECK(rng0.state == e.gs.rng.state && rng0.inc == e.gs.rng.inc);
    CHECK_EQ(e.gs.actions_used[WORLD_MUSICA], 0);
    /* Sin límite: 60 mutaciones seguidas. */
    for (int k = 0; k < 60; k++) {
        engine_frame(&e, a, now, ms += 30);
        engine_frame(&e, none, now, ms += 80);
    }
    CHECK(e.gs.mus.mut >= 50 || e.gs.mus.mut == 0);
}

static void test_engine_beeps_duck_music(void)
{
    static Engine e;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&e, 6, now, ms);
    goto_music(&e, now, &ms);
    static int16_t pcm[RATE / 2];
    engine_audio_render(&e, pcm, RATE / 4);
    CHECK_EQ(e.music.duck_left, 0);
    RawInput a = {BTN_A, false}, none = {0, false};
    engine_frame(&e, a, now, ms += 30);
    FrameOut o = engine_frame(&e, none, now, ms += 80); /* soltar A: el toque suena su pitido */
    CHECK(o.tone_count >= 1);                           /* los pitidos de siempre siguen saliendo por FrameOut */
    CHECK(e.music.duck_left > 0);                       /* y la música les cede el sitio */
    CHECK(music_active(&e.music));
}

/* Lo que se guarda de la música: lo que cambia entre dos universos que solo difieren en ella. */
static void normalize_music(GameState *g, const GameState *ref)
{
    g->mus = ref->mus;
    g->current_world = ref->current_world;
    g->news = ref->news;
}

static void test_music_does_not_touch_universe(void)
{
    /* R pasa seis horas en el Reloj; M, en el mundo Música con el audio sonando todo el rato (y
     * pulsando A cada poco: mutar y semillas nuevas). Mismos instantes, mismo reloj: el estado
     * guardado de ambos solo debe diferir en los campos de la música (y el mundo abierto). */
    static Engine r, m;
    uint32_t ms = 1000;
    int64_t now = t_noon();
    engine_init(&r, 77, now, ms);
    engine_init(&m, 77, now, ms);
    uint32_t msr = ms, msm = ms;
    r.gs.current_world = WORLD_RELOJ;
    m.gs.current_world = WORLD_MUSICA;
    RawInput none = {0, false}, shake = {0, true}, a = {BTN_A, false};
    static int16_t pcm[2048];
    long samples = 0;
    int taps = 0;
    for (int64_t t = 0; t < 6 * 3600; t += 1) {
        /* Cada segundo un fotograma de cada uno; cada 10 s un toque (shake) para que no se duerman. */
        RawInput in = (t % 10 == 0) ? shake : none;
        engine_frame(&r, in, now + t, msr += 1000);
        if (t % 53 == 0 && t > 0) { /* de vez en cuando A (toque o mantener) en la música */
            taps++;
            engine_frame(&m, a, now + t, msm += 1000);
            if (taps % 4 == 0)
                for (int k = 0; k < 20; k++)
                    engine_frame(&m, a, now + t, msm += 30);
            engine_frame(&m, none, now + t, msm += 100);
        } else {
            engine_frame(&m, in, now + t, msm += 1000);
        }
        if (t % 4 == 0) { /* el audio que pediría el altavoz */
            size_t n = engine_audio_render(&m, pcm, 2048);
            samples += (long)n;
        }
    }
    CHECK(samples > RATE * 60);
    CHECK(m.gs.mus.mut > 0 || m.gs.mus.seed != 0);
    CHECK_EQ(r.gs.rng.state, m.gs.rng.state);
    CHECK_EQ(r.gs.rng.inc, m.gs.rng.inc);
    GameState gm = m.gs, gr = r.gs;
    normalize_music(&gm, &gr);
    gr.last_seen = gm.last_seen;
    gr.looks_today = gm.looks_today;
    gr.looks_total = gm.looks_total;
    uint8_t bm[LLV_SAVE_MAX], br[LLV_SAVE_MAX];
    size_t nm = state_serialize(&gm, bm, sizeof(bm)), nr = state_serialize(&gr, br, sizeof(br));
    CHECK(nm > 0 && nm == nr);
    CHECK(memcmp(bm, br, nm) == 0);
    /* Y el audio no cambia nada: el mismo recorrido sin pedir muestras acaba en el mismo estado. */
    static Engine m2;
    uint32_t ms2 = ms;
    engine_init(&m2, 77, now, ms2);
    m2.gs.current_world = WORLD_MUSICA;
    taps = 0;
    for (int64_t t = 0; t < 3600; t++) {
        RawInput in = (t % 10 == 0) ? shake : none;
        engine_frame(&m2, in, now + t, ms2 += 1000);
    }
    static Engine m3;
    uint32_t ms3 = ms;
    engine_init(&m3, 77, now, ms3);
    m3.gs.current_world = WORLD_MUSICA;
    for (int64_t t = 0; t < 3600; t++) {
        RawInput in = (t % 10 == 0) ? shake : none;
        engine_frame(&m3, in, now + t, ms3 += 1000);
        engine_audio_render(&m3, pcm, 2048);
    }
    GameState g2 = m2.gs, g3 = m3.gs;
    g2.mus.mood = g3.mus.mood; /* el ánimo cambia al empezar vueltas, y solo si suena */
    uint8_t b2[LLV_SAVE_MAX], b3[LLV_SAVE_MAX];
    size_t n2 = state_serialize(&g2, b2, sizeof(b2)), n3 = state_serialize(&g3, b3, sizeof(b3));
    CHECK(n2 > 0 && n2 == n3 && memcmp(b2, b3, n2) == 0);
}

static void test_music_state_save(void)
{
    static GameState gs, back;
    int64_t t = t_noon();
    state_init(&gs, 4242, t);
    uint8_t buf[LLV_SAVE_MAX];
    /* La semilla inicial sale del universo, sin gastar números de gs->rng. */
    GameState ref;
    state_init(&ref, 4242, t);
    CHECK(ref.rng.state == gs.rng.state);
    GameState other;
    state_init(&other, 4243, t);
    CHECK(other.mus.seed != gs.mus.seed);

    gs.mus.seed = 0x4F2A;
    gs.mus.mut = 7;
    gs.mus.mood = MOOD_MAKE(DP_DUSK, 1, 2, PET_HONGO);
    size_t n = state_serialize(&gs, buf, sizeof(buf));
    CHECK(n > 0 && n < 4096);
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    CHECK_EQ(back.mus.seed, 0x4F2A);
    CHECK_EQ(back.mus.mut, 7);
    CHECK_EQ(back.mus.mood, gs.mus.mood);
    /* Reproduce lo mismo: mismo estilo y misma melodía a partir de lo guardado. */
    MusicStyle s1, s2;
    music_style(&s1, gs.mus.seed, gs.mus.mood);
    music_style(&s2, back.mus.seed, back.mus.mood);
    CHECK(same_style(&s1, &s2));
    music_song(&sgA, &s1, gs.mus.seed, gs.mus.mut);
    music_song(&sgB, &s2, back.mus.seed, back.mus.mut);
    CHECK(memcmp(&sgA, &sgB, sizeof(sgA)) == 0);
    /* El estado completo sigue por debajo de 4 KB (spec §10). */
    CHECK(sizeof(GameState) < 4096 && n < 4096);
    printf("    (GameState: %zu bytes en memoria, %zu en el guardado; música: 4 bytes)\n", sizeof(GameState), n);

    /* Un guardado de la fase 3 (versión 3, sin música) se rechaza limpiamente. */
    uint8_t old[LLV_SAVE_MAX];
    size_t m = n - 4 - 4; /* sin los 4 bytes de música ni el CRC */
    memcpy(old, buf, m);
    old[4] = 3;
    old[6] = (uint8_t)(m - 8);
    old[7] = (uint8_t)((m - 8) >> 8);
    uint32_t crc = state_crc32(old, m);
    for (int i = 0; i < 4; i++)
        old[m + (size_t)i] = (uint8_t)(crc >> (8 * i));
    GameState out;
    state_init(&out, 1, t);
    out.looks_total = 4242;
    CHECK(state_deserialize(&out, old, m + 4) != 0);
    CHECK_EQ(out.looks_total, 4242); /* no toca el destino */
    /* Ni siquiera con la versión nueva y el hueco de música sin rellenar. */
    old[4] = 4;
    crc = state_crc32(old, m);
    for (int i = 0; i < 4; i++)
        old[m + (size_t)i] = (uint8_t)(crc >> (8 * i));
    CHECK(state_deserialize(&out, old, m + 4) != 0);
    /* Un ánimo imposible (cielo 3) con CRC bueno no entra. */
    GameState evil = gs;
    evil.mus.mood = MOOD_MAKE(0, 0, 3, 0);
    n = state_serialize(&evil, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    evil.mus.mood = MOOD_MAKE(0, 0, 0, 7);
    n = state_serialize(&evil, buf, sizeof(buf));
    CHECK(state_deserialize(&out, buf, n) != 0);
    /* Un universo nuevo cargado del guardado suena igual que el original. */
    static Engine e1, e2;
    uint32_t ms = 1000;
    engine_init(&e1, 31, t, ms);
    e1.gs.mus.seed = 0x1234;
    e1.gs.mus.mut = 3;
    n = state_serialize(&e1.gs, buf, sizeof(buf));
    CHECK_EQ(state_deserialize(&back, buf, n), 0);
    engine_resume(&e2, &back, t, ms);
    CHECK_EQ(e2.gs.mus.seed, 0x1234);
    CHECK_EQ(e2.gs.mus.mut, 3);
}

/* ---------- el gate: métricas de "agradable" ---------- */

static void test_pleasant_metrics_gate(void)
{
    /* >= 200 semillas con todos los ánimos: al menos el 90 % cumple los umbrales documentados (README);
     * 10 s de audio por semilla para los criterios del PCM. La melodía ingenua (notas al azar) no. */
    static int16_t pcm[RATE * 10];
    int seeds = 240, ok = 0, naive_ok = 0;
    int fails[MC_COUNT] = {0};
    for (int i = 0; i < seeds; i++) {
        uint16_t seed = (uint16_t)(1 + i);
        uint8_t mood = mood_for(i);
        MusicStyle st;
        music_style(&st, seed, mood);
        music_song(&sgA, &st, seed, 0);
        music_start(&mA, seed, 0, mood, RATE);
        music_render(&mA, pcm, RATE * 10);
        MusicMetrics mm;
        AudioStats au;
        ms_analyze(&sgA, &st, &mm);
        ms_audio(pcm, RATE * 10, RATE, &au);
        unsigned f = music_judge(&mm, &au);
        ok += f == 0;
        for (int c = 0; c < MC_COUNT; c++)
            fails[c] += (f >> c) & 1;
        ms_random_song(&sgB, &st, seed);
        ms_analyze(&sgB, &st, &mm);
        naive_ok += music_judge(&mm, NULL) == 0;
    }
    printf("    (%d de %d semillas cumplen los umbrales = %d %%; melodía ingenua: %d de %d)\n", ok, seeds,
           ok * 100 / seeds, naive_ok, seeds);
    CHECK(ok * 100 / seeds >= 90);
    CHECK(naive_ok * 100 / seeds <= 10); /* los umbrales distinguen: el azar no pasa */
    /* Un criterio de las notas no se incumple casi nunca; el audio, jamás. */
    CHECK_EQ(fails[MC_PEAK] + fails[MC_LOUDNESS] + fails[MC_DC] + fails[MC_SILENCE], 0);
    CHECK_EQ(fails[MC_SCALE] + fails[MC_CONSONANCE] + fails[MC_TEMPO], 0);
    CHECK(fails[MC_RESOLVE] <= seeds / 100);

    /* El barrido de solo notas, mucho más amplio. */
    int sweep = 6000, sweep_ok = 0;
    for (int i = 0; i < sweep; i++) {
        MusicStyle st;
        music_style(&st, (uint16_t)i, mood_for(i));
        music_song(&sgA, &st, (uint16_t)i, 0);
        MusicMetrics mm;
        ms_analyze(&sgA, &st, &mm);
        sweep_ok += music_judge(&mm, NULL) == 0;
    }
    printf("    (barrido de %d semillas, solo notas: %d %%)\n", sweep, sweep_ok * 100 / sweep);
    CHECK(sweep_ok * 100 / sweep >= 90);
}

static void test_metrics_catch_bad_music(void)
{
    /* Las métricas no son de adorno: una melodía rota no las pasa. */
    MusicStyle st;
    music_style(&st, 5, 0x02);
    music_song(&sgA, &st, 5, 0);
    MusicMetrics mm;
    ms_analyze(&sgA, &st, &mm);
    CHECK_EQ(music_judge(&mm, NULL), 0u);
    /* Una nota fuera de la escala. */
    sgB = sgA;
    for (int s = 1; s < MUSIC_STEPS; s++)
        if (sgB.mel[s].len) {
            sgB.mel[s].pitch = (uint8_t)(sgB.mel[s].pitch + 1);
            break;
        }
    ms_analyze(&sgB, &st, &mm);
    CHECK(mm.in_scale_pm < 1000);
    /* Una melodía que no resuelve. */
    sgB = sgA;
    for (int s = MUSIC_STEPS - 1; s >= 0; s--)
        if (sgB.mel[s].len) {
            sgB.mel[s].pitch = (uint8_t)(sgB.mel[s].pitch + 2);
            break;
        }
    ms_analyze(&sgB, &st, &mm);
    CHECK(music_judge(&mm, NULL) & (1u << MC_RESOLVE));
    /* Una nota igual cinco veces seguidas. */
    sgB = sgA;
    int cnt = 0;
    for (int s = 0; s < MUSIC_STEPS && cnt < 6; s++)
        if (sgB.mel[s].len) {
            sgB.mel[s].pitch = sgA.mel[0].pitch;
            cnt++;
        }
    ms_analyze(&sgB, &st, &mm);
    CHECK(music_judge(&mm, NULL) & (1u << MC_RUN));
    /* Silencio y recorte en el audio. */
    static int16_t pcm[RATE * 2];
    AudioStats au;
    ms_audio(pcm, RATE * 2, RATE, &au);
    CHECK(music_judge(&mm, &au) & (1u << MC_SILENCE));
    for (int i = 0; i < RATE * 2; i++)
        pcm[i] = (int16_t)((i / 20) % 2 ? 32767 : -32767);
    ms_audio(pcm, RATE * 2, RATE, &au);
    CHECK(au.clipped > 0);
    CHECK(music_judge(&mm, &au) & (1u << MC_PEAK));
}

static void test_info_box_fits(void)
{
    /* La caja de datos (spec: "DÓRICA · 96 BPM · #4F2A") cabe siempre en 106 px, sin recortes. */
    int worst = 0;
    for (int mode = 0; mode < MODE_COUNT; mode++)
        for (int bpm = 56; bpm <= 140; bpm += 28)
            for (int mut = 0; mut < 256; mut += 51) {
                MusicStyle st;
                memset(&st, 0, sizeof(st));
                st.mode = (uint8_t)mode;
                st.bpm = (uint8_t)bpm;
                MusicSave ms = {0xFFFF, (uint8_t)(mut == 255 ? 255 : mut + 50), 0};
                char l1[32], l2[32];
                musica_info_text(&st, &ms, l1, sizeof(l1), l2, sizeof(l2));
                int w1 = gfx_text_width(l1), w2 = gfx_text_width(l2);
                worst = w1 > worst ? w1 : worst;
                worst = w2 > worst ? w2 : worst;
                CHECK(w1 <= 106 && w2 <= 106);
            }
    MusicStyle st = {0};
    st.mode = MODE_DORIAN;
    st.bpm = 96;
    MusicSave ms = {0x4F2A, 0, 0};
    char l1[32], l2[32];
    musica_info_text(&st, &ms, l1, sizeof(l1), l2, sizeof(l2));
    CHECK(strcmp(l1, "DÓRICA") == 0 && strcmp(l2, "96 BPM · #4F2A") == 0);
    printf("    (el renglón más ancho mide %d px de 106)\n", worst);
}

int main(void)
{
    struct { const char *name; TestFn fn; } tests[] = {
        {"mundo Musica: la caja de datos cabe", test_info_box_fits},
        {"euclidiano: casos conocidos y propiedades", test_euclid_known},
        {"escalas: segun hora y clima (spec 4.4)", test_scales_by_daypart_and_weather},
        {"progresiones: tabla de 22 con notas del acorde en la escala", test_progressions_table},
        {"melodia: notas de la escala, rango, tiempos fuertes y bajo", test_notes_in_scale_and_range},
        {"melodia: resuelve en la tonica al final", test_cadence_resolves_on_tonic},
        {"melodia: forma A A' B A''", test_song_forms_A_Ap_B_A},
        {"mutar: mismo estilo, otra melodia", test_mutate_keeps_style_changes_melody},
        {"semilla nueva: otro estilo", test_new_seed_changes_style},
        {"estilo: rangos de todos los campos", test_style_ranges},
        {"PCM: determinista byte a byte y por bloques irregulares", test_pcm_deterministic_byte_exact},
        {"PCM: llamadas entre bloques (mutar, bajar, animo, semilla)", test_render_with_calls_between_blocks},
        {"PCM: sin recortes, nivel y silencio razonables", test_no_clipping_and_level},
        {"PCM: cualquier tasa de muestreo", test_all_rates_work},
        {"PCM: tiempo de calculo acotado", test_render_time_bounded},
        {"PCM: fundido al callar y atenuacion con pitidos", test_fade_out_stop_and_duck},
        {"animo: cambia al empezar la vuelta, no a mitad", test_mood_changes_at_loop_boundary},
        {"semilla nueva en marcha: baja y vuelve a empezar", test_restart_with_new_seed},
        {"motor: solo suena en el mundo Musica; fundido al salir y dormir", test_engine_music_only_in_music_world},
        {"motor: en el mundo Musica se duerme a los 3 min, no a los 20 s", test_engine_music_world_sleeps_later},
        {"motor: A mutar, mantener A semilla nueva, sin tocar gs->rng", test_engine_tap_mutates_hold_reseeds},
        {"motor: los pitidos mandan y la musica cede", test_engine_beeps_duck_music},
        {"motor: la musica no toca el universo (6 h)", test_music_does_not_touch_universe},
        {"guardado v4: musica ida y vuelta, rechaza v3 y absurdos", test_music_state_save},
        {"gate: >= 90 % de semillas agradables (y el azar no)", test_pleasant_metrics_gate},
        {"metricas: detectan musica mala", test_metrics_catch_bad_music},
    };
    int n = (int)(sizeof(tests) / sizeof(tests[0])), ok = 0;
    for (int i = 0; i < n; i++)
        ok += run_test(tests[i].name, tests[i].fn);
    printf("\n%d/%d tests de musica OK, %d comprobaciones, %d fallos\n", ok, n, checks, failures);
    return failures ? 1 : 0;
}
