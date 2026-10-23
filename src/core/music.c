/* Música generativa (spec §4.4). Ver music.h para el diseño. Solo enteros. */
#include "music.h"

#include <string.h>

#include "state.h"
#include "timeutil.h"

static uint64_t mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

/* ======================================================================================= */
/* Tablas                                                                                  */
/* ======================================================================================= */

static const uint8_t SCALE_LEN[MODE_COUNT] = {5, 5, 7, 7};
static const uint8_t SCALE[MODE_COUNT][7] = {
    {0, 2, 4, 7, 9, 0, 0},      /* pentatónica mayor: Do Re Mi Sol La */
    {0, 3, 5, 7, 10, 0, 0},     /* pentatónica menor */
    {0, 2, 3, 5, 7, 9, 10},     /* dórica: menor con la sexta mayor */
    {0, 2, 3, 5, 7, 8, 10},     /* menor natural (eólica) */
};
static const char *const MODE_NAME[MODE_COUNT] = {"PENTATÓNICA", "PENT. MENOR", "DÓRICA", "MENOR"};

int music_scale_len(int mode) { return SCALE_LEN[mode % MODE_COUNT]; }
const uint8_t *music_scale(int mode) { return SCALE[mode % MODE_COUNT]; }
const char *music_mode_name(int mode) { return MODE_NAME[mode % MODE_COUNT]; }

/* Escala según la franja del día y la lluvia, en % (spec §4.4: pentatónica de día, menor o dórica de
 * noche, más melancólica con lluvia: con lluvia solo salen las dos menores). */
static const uint8_t MODE_W[4][2][MODE_COUNT] = {
    /* noche      */ {{0, 0, 55, 45}, {0, 20, 0, 80}},
    /* amanecer   */ {{80, 0, 20, 0}, {0, 50, 0, 50}},
    /* día        */ {{100, 0, 0, 0}, {0, 55, 0, 45}},
    /* atardecer  */ {{60, 0, 40, 0}, {0, 40, 0, 60}},
};
/* Tempo (negras por minuto) por franja: de noche más lento; con lluvia, -15 %. */
static const uint8_t BPM_RANGE[4][2] = {{66, 88}, {92, 112}, {104, 132}, {84, 104}};

int music_daypart(int hour)
{
    if (hour >= 22 || hour < 5)
        return DP_NIGHT;
    if (hour < 10)
        return DP_DAWN;
    if (hour < 18)
        return DP_DAY;
    return DP_DUSK;
}

int music_pick_mode(int daypart, bool rain, int roll)
{
    const uint8_t *w = MODE_W[daypart & 3][rain ? 1 : 0];
    int acc = 0;
    for (int i = 0; i < MODE_COUNT; i++) {
        acc += w[i];
        if (roll < acc)
            return i;
    }
    return MODE_COUNT - 1;
}

/* Progresiones: fundamental de cada acorde (semitonos sobre la tónica) y qué acordes son menores. */
typedef struct {
    uint8_t root[4];
    uint8_t minor;       /* bit i: el acorde i es menor */
    const char *name;
} Prog;

#define PROG_MAJ 0       /* 0..7: pentatónica mayor */
#define PROG_MIN 8       /* 8..15: menor y pentatónica menor */
#define PROG_DOR 16      /* 16..21: dórica */
static const Prog PROGS[MUSIC_NPROG] = {
    {{0, 7, 9, 5}, 0x4, "I-V-vi-IV"},
    {{0, 9, 5, 7}, 0x2, "I-vi-IV-V"},
    {{9, 5, 0, 7}, 0x1, "vi-IV-I-V"},
    {{0, 5, 7, 5}, 0x0, "I-IV-V-IV"},
    {{0, 4, 5, 7}, 0x2, "I-iii-IV-V"},
    {{0, 5, 9, 7}, 0x4, "I-IV-vi-V"},
    {{5, 0, 7, 9}, 0x8, "IV-I-V-vi"},
    {{0, 7, 5, 0}, 0x0, "I-V-IV-I"},
    {{0, 10, 8, 10}, 0x1, "i-VII-VI-VII"},
    {{0, 8, 3, 10}, 0x1, "i-VI-III-VII"},
    {{0, 5, 10, 0}, 0xB, "i-iv-VII-i"},
    {{0, 8, 10, 0}, 0x9, "i-VI-VII-i"},
    {{0, 5, 7, 0}, 0xF, "i-iv-v-i"},
    {{0, 3, 10, 8}, 0x1, "i-III-VII-VI"},
    {{8, 10, 0, 0}, 0xC, "VI-VII-i-i"},
    {{0, 10, 3, 10}, 0x1, "i-VII-III-VII"},
    {{0, 5, 0, 10}, 0x5, "i-IV-i-VII"},
    {{0, 10, 5, 0}, 0x9, "i-VII-IV-i"},
    {{0, 5, 10, 5}, 0x1, "i-IV-VII-IV"},
    {{0, 3, 10, 5}, 0x1, "i-III-VII-IV"},
    {{0, 7, 5, 0}, 0xB, "i-v-IV-i"},
    {{0, 2, 5, 0}, 0xB, "i-ii-IV-i"},
};

const char *music_prog_name(int prog) { return PROGS[prog % MUSIC_NPROG].name; }

/* Ritmos de la melodía por compás, en semicorcheas (suman 16); negativo = silencio. */
#define NCELL 18
#define CELL_MAX 10
static const int8_t CELLS[NCELL][CELL_MAX] = {
    {4, 4, 4, 4},          {8, 4, 4},               {4, 4, 8},            {6, 2, 4, 4},
    {4, 2, 2, 4, 4},       {2, 2, 4, 2, 2, 4},      {4, 6, 2, 4},         {3, 3, 2, 4, 4},
    {2, 2, 2, 2, 4, 4},    {8, 8},                  {12, 4},              {4, 4, 2, 2, 4},
    {6, 2, 6, 2},          {4, 2, 2, 2, 2, 4},      {2, 2, 2, 2, 2, 2, 2, 2}, {3, 3, 3, 3, 4},
    {4, 4, -4, 4},         {6, 2, -4, 4},
};
/* Peso de cada ritmo según la densidad (escasa, media, densa). Incluso la escasa prefiere ritmos con
 * notas de paso libres (negras en el 2.º y el 4.º tiempo): una melodía solo de notas del acorde da
 * saltos de tercera y suena a arpegio, no a canción. */
static const uint8_t CELL_W[NCELL][3] = {
    {10, 8, 4}, {5, 6, 2}, {9, 5, 2}, {6, 8, 6}, {5, 9, 8}, {2, 6, 9}, {5, 6, 5}, {3, 7, 8}, {1, 4, 8},
    {3, 2, 0},  {2, 1, 0}, {5, 8, 7}, {4, 7, 6}, {2, 5, 8}, {0, 1, 4}, {2, 5, 7}, {4, 3, 1}, {3, 3, 1},
};
/* Compases finales de frase: acaban en nota larga. El último (16) solo cierra la canción. */
#define NCAD 7
static const int8_t CADS[NCAD][CELL_MAX] = {
    {4, 4, 8}, {8, 8}, {4, 2, 2, 8}, {6, 2, 8}, {2, 2, 4, 8}, {4, 4, 6, 2}, {16},
};

/* Tendencias melódicas (peso 0..10) entre los grados de cada escala: fila = grado actual,
 * columna = grado siguiente. Es una cadena de Markov de orden 1 sobre grados; el orden 2 se
 * aproxima con las reglas de salto y de repetición (pick_next). Los grados que "piden" resolver
 * (el 4.º baja al 3.º, el 7.º sube a la tónica, el 6.º baja al 5.º) llevan los pesos altos. */
typedef uint8_t Trans[7][7];
static const Trans TR_PENT_MAJ = {   /* 1 2 3 5 6 */
    {2, 8, 8, 6, 3}, {9, 2, 8, 4, 1}, {7, 8, 2, 8, 3}, {5, 3, 8, 2, 8}, {6, 2, 4, 9, 2}};
static const Trans TR_PENT_MIN = {   /* 1 b3 4 5 b7 */
    {2, 9, 5, 6, 3}, {8, 2, 8, 7, 2}, {3, 10, 2, 7, 3}, {6, 5, 7, 2, 8}, {10, 2, 3, 8, 2}};
static const Trans TR_DORIAN = {     /* 1 2 b3 4 5 6 b7 */
    {2, 8, 9, 3, 6, 2, 3}, {9, 2, 8, 5, 3, 1, 2}, {6, 9, 2, 7, 8, 2, 3}, {3, 3, 10, 2, 7, 3, 2},
    {7, 3, 7, 7, 2, 6, 6}, {3, 2, 3, 2, 9, 2, 8}, {10, 3, 3, 2, 7, 6, 2}};
static const Trans TR_MINOR = {      /* 1 2 b3 4 5 b6 b7 */
    {2, 8, 9, 3, 6, 1, 3}, {9, 2, 8, 5, 3, 1, 1}, {6, 9, 2, 7, 8, 2, 3}, {3, 3, 10, 2, 7, 3, 1},
    {7, 3, 7, 7, 2, 7, 5}, {3, 1, 3, 3, 10, 2, 7}, {10, 2, 3, 2, 7, 6, 2}};
static const Trans *const TRANS[MODE_COUNT] = {&TR_PENT_MAJ, &TR_PENT_MIN, &TR_DORIAN, &TR_MINOR};

/* Cuánto cuesta cada tamaño de intervalo (en semitonos): sobre todo grados conjuntos y terceras; la
 * quinta es rara y por encima de ella (sextas, séptimas, octava) no se salta. */
static const uint8_t W_SEMI[13] = {2, 10, 10, 9, 4, 2, 1, 1, 0, 0, 0, 0, 0};

/* ======================================================================================= */
/* Euclidiano (Björklund) y utilidades                                                     */
/* ======================================================================================= */

void music_euclid(uint8_t *out, int k, int n)
{
    if (n <= 0)
        return;
    if (n > 32)
        n = 32;
    if (k <= 0 || k >= n) {
        for (int i = 0; i < n; i++)
            out[i] = k >= n ? 1 : 0;
        return;
    }
    /* Grupos de bits (el primero del grupo es el bit más alto): k de "1" y n-k de "0"; se van
     * pegando los "0" restantes a los grupos hasta que sobra uno como mucho. */
    uint32_t abits[32], bbits[32];
    uint8_t alen[32], blen[32];
    int na = k, nb = n - k;
    for (int i = 0; i < na; i++) {
        abits[i] = 1;
        alen[i] = 1;
    }
    for (int i = 0; i < nb; i++) {
        bbits[i] = 0;
        blen[i] = 1;
    }
    for (;;) { /* se empareja primero y se mira después si queda más de un grupo suelto */
        int m = na < nb ? na : nb;
        uint32_t nbits[32], rbits[32];
        uint8_t nlen[32], rlen[32];
        for (int i = 0; i < m; i++) {
            nbits[i] = (abits[i] << blen[i]) | bbits[i];
            nlen[i] = (uint8_t)(alen[i] + blen[i]);
        }
        int nr = 0;
        if (na > m) {
            for (int i = m; i < na; i++, nr++) {
                rbits[nr] = abits[i];
                rlen[nr] = alen[i];
            }
        } else {
            for (int i = m; i < nb; i++, nr++) {
                rbits[nr] = bbits[i];
                rlen[nr] = blen[i];
            }
        }
        for (int i = 0; i < m; i++) {
            abits[i] = nbits[i];
            alen[i] = nlen[i];
        }
        for (int i = 0; i < nr; i++) {
            bbits[i] = rbits[i];
            blen[i] = rlen[i];
        }
        na = m;
        nb = nr;
        if (nb <= 1)
            break;
    }
    int pos = 0;
    for (int i = 0; i < na; i++)
        for (int b = alen[i] - 1; b >= 0; b--)
            out[pos++] = (uint8_t)((abits[i] >> b) & 1);
    for (int i = 0; i < nb; i++)
        for (int b = blen[i] - 1; b >= 0; b--)
            out[pos++] = (uint8_t)((bbits[i] >> b) & 1);
}

/* Patrón euclidiano rotado a la derecha 'rot' pasos, en 16 casillas. */
static void euclid16(uint8_t *out, int k, int rot)
{
    uint8_t t[16];
    music_euclid(t, k, 16);
    for (int i = 0; i < 16; i++)
        out[(i + rot) & 15] = t[i];
}

/* Igual, pero rotado para que el compás empiece en el golpe número 'which' (siempre hay golpe en 0). */
static void euclid16_from_hit(uint8_t *out, int k, int which)
{
    uint8_t t[16];
    int on[16], n = 0;
    music_euclid(t, k, 16);
    for (int i = 0; i < 16; i++)
        if (t[i])
            on[n++] = i;
    int start = n ? on[which % n] : 0;
    for (int i = 0; i < 16; i++)
        out[i] = t[(start + i) & 15];
}

bool music_in_chord(const MusicChord *c, int pitch)
{
    int rel = (pitch + 120 - c->root) % 12;
    return rel == 0 || rel == (c->minor ? 3 : 4) || rel == 7;
}

uint8_t music_sounding(const MusicNote *track, int step)
{
    for (int k = 0; k < 32 && step - k >= 0; k++) {
        const MusicNote *n = &track[step - k];
        if (n->len > k)
            return n->pitch;
        if (n->len > 0)
            return 0;
    }
    return 0;
}

static int pick_w(Rng *r, const uint8_t *w, int n)
{
    uint32_t tot = 0;
    for (int i = 0; i < n; i++)
        tot += w[i];
    if (!tot)
        return 0;
    uint32_t x = rng_below(r, tot);
    for (int i = 0; i < n; i++) {
        if (x < w[i])
            return i;
        x -= w[i];
    }
    return n - 1;
}

/* ======================================================================================= */
/* Ánimo y estilo                                                                          */
/* ======================================================================================= */

uint8_t music_mood(const GameState *gs, int64_t now)
{
    CivilTime c = time_to_civil(now);
    int sky = gs->sky == SKY_STAR ? 1 : gs->sky == SKY_RAINBOW ? 2 : 0;
    int pet = gs->pet.kind < PET_COUNT ? gs->pet.kind : 0;
    return MOOD_MAKE(music_daypart(c.hour), gs->weather == W_RAIN, sky, pet);
}

bool music_mood_valid(uint8_t mood)
{
    return MOOD_SKY(mood) <= 2 && MOOD_PET(mood) < PET_COUNT;
}

void music_style(MusicStyle *st, uint16_t seed, uint8_t mood)
{
    Rng r;
    rng_seed(&r, mix64(0x57A1E000ull ^ seed), 0x5E1);
    memset(st, 0, sizeof(*st));
    int dp = MOOD_DAYPART(mood), rain = (mood & MOOD_RAIN) != 0;
    int sky = MOOD_SKY(mood), pet = MOOD_PET(mood);
    bool calm = dp == DP_NIGHT || rain;

    st->mode = (uint8_t)music_pick_mode(dp, rain, (int)rng_below(&r, 100));
    st->key = (uint8_t)rng_below(&r, 12);
    st->tonic = (uint8_t)(57 + (st->key + 12 - 57 % 12) % 12); /* La3..Sol#4: melodía cantable */

    int lo = BPM_RANGE[dp][0], hi = BPM_RANGE[dp][1];
    int bpm = lo + (int)rng_below(&r, (uint32_t)(hi - lo + 1));
    if (rain)
        bpm = bpm * 85 / 100;
    if (pet == PET_PIEDRA)
        bpm = bpm * 94 / 100;
    else if (pet == PET_SEMILLA)
        bpm = bpm * 103 / 100;
    if (sky == 2)
        bpm = bpm * 104 / 100; /* un arcoíris la anima un poco */
    st->bpm = (uint8_t)(bpm < 56 ? 56 : bpm > 140 ? 140 : bpm);

    /* Progresiones: A y B de la misma familia, distintas entre sí. */
    int base = st->mode == MODE_PENT_MAJ ? PROG_MAJ : st->mode == MODE_DORIAN ? PROG_DOR : PROG_MIN;
    int cnt = st->mode == MODE_PENT_MAJ ? 8 : st->mode == MODE_DORIAN ? 6 : 8;
    int a = (int)rng_below(&r, (uint32_t)cnt);
    int b = (a + 1 + (int)rng_below(&r, (uint32_t)(cnt - 1))) % cnt;
    st->prog_a = (uint8_t)(base + a);
    st->prog_b = (uint8_t)(base + b);

    /* Timbre: de noche y con lluvia, más suave y redondo; de día, más brillante. */
    static const uint8_t DUTY_W[2][3] = {{12, 45, 43}, {4, 31, 65}};
    st->lead_duty = (uint8_t)pick_w(&r, DUTY_W[calm], 3);
    if (sky == 2)
        st->lead_duty = 1;
    else if (pet == PET_HONGO && !calm && st->lead_duty == 2)
        st->lead_duty = 0; /* el hongo, algo nasal */
    st->bass_duty = (uint8_t)(rng_below(&r, 4) == 0 ? 1 : 2);
    static const uint8_t ENV_W[2][3] = {{40, 45, 15}, {30, 20, 50}};
    st->lead_env = (uint8_t)pick_w(&r, ENV_W[calm], 3);
    static const uint8_t VIB_W[2][3] = {{45, 45, 10}, {15, 45, 40}};
    st->vib = (uint8_t)pick_w(&r, VIB_W[calm], 3);
    if (sky == 1 && st->vib == 0)
        st->vib = 1; /* la estrella fugaz le da un temblor suave */
    st->lpf = (uint8_t)(rain ? 100 : dp == DP_NIGHT ? 115 : dp == DP_DAY ? 175 : 150);

    static const uint8_t BASS_W[2][4] = {{25, 30, 25, 20}, {40, 10, 30, 20}};
    st->bass_style_a = (uint8_t)pick_w(&r, BASS_W[calm], 4);
    st->bass_style_b = (uint8_t)((st->bass_style_a + 1 + rng_below(&r, 3)) % 4);

    static const uint8_t DRUM_W[2][3] = {{5, 50, 45}, {45, 50, 5}};
    st->drums = (uint8_t)pick_w(&r, DRUM_W[calm], 3);
    if (pet == PET_PIEDRA)
        st->drums = 0;
    static const uint8_t KICK_K[3][2] = {{2, 2}, {3, 4}, {4, 5}};
    static const uint8_t HAT_K[3][2] = {{4, 4}, {6, 8}, {10, 12}};
    st->kick_k = KICK_K[st->drums][rng_below(&r, 2)];
    st->hat_k = HAT_K[st->drums][rng_below(&r, 2)];
    st->snare_pat = (uint8_t)(st->drums == 0 ? 3 : rng_below(&r, 3));
    static const uint8_t DENS_W[2][3] = {{10, 50, 40}, {55, 40, 5}};
    st->density = (uint8_t)pick_w(&r, DENS_W[calm], 3);
}

/* ======================================================================================= */
/* La canción                                                                              */
/* ======================================================================================= */

typedef struct {
    uint8_t n;
    uint8_t pitch[16];
    uint8_t deg[16];
} Window;

typedef struct {
    const MusicStyle *st;
    MusicSong *sg;
    Rng r;
    Window w;
    const Trans *tr;
    int cur, prev;   /* posiciones en la ventana de las dos últimas notas (-1: ninguna) */
    int center;      /* altura hacia la que tira suavemente la melodía */
    int approach;    /* si es >= 0, la última nota del compás acaba a una quinta como mucho de esta altura */
} Gen;

enum { END_NONE, END_OPEN, END_TONIC };

static int iabs(int x) { return x < 0 ? -x : x; }

/* Las notas permitidas: la escala dentro de [tónica-3, tónica+14] (17 semitonos, un registro cantable). */
static void build_window(Gen *g)
{
    const MusicStyle *st = g->st;
    int len = SCALE_LEN[st->mode];
    int lo = st->tonic - 3, hi = st->tonic + 14;
    g->w.n = 0;
    for (int o = -1; o <= 2; o++)
        for (int d = 0; d < len; d++) {
            int p = st->tonic + 12 * o + SCALE[st->mode][d];
            if (p >= lo && p <= hi) {
                g->w.pitch[g->w.n] = (uint8_t)p;
                g->w.deg[g->w.n] = (uint8_t)d;
                g->w.n++;
            }
        }
}

static int win_index(const Gen *g, int pitch)
{
    int best = 0, bd = 1000;
    for (int j = 0; j < g->w.n; j++) {
        int d = iabs(g->w.pitch[j] - pitch);
        if (d < bd) {
            bd = d;
            best = j;
        }
    }
    return best;
}

/* Una nota en el primer o el tercer tiempo, o que dura por encima de uno de ellos, cae en una
 * nota del acorde (así los tiempos fuertes son consonantes con el bajo); las demás pasan libres. */
static bool needs_chord(int step, int len)
{
    return step % 8 == 0 || (step % 8) + len > 8;
}

static uint8_t accent(int step) { return (uint8_t)(step % 8 == 0 ? 255 : step % 4 == 0 ? 225 : 200); }

static int nearest_chord_tone(const Gen *g, int bar, int pitch)
{
    const MusicChord *ch = &g->sg->chord[bar];
    int best = -1, bd = 1000;
    for (int j = 0; j < g->w.n; j++)
        if (music_in_chord(ch, g->w.pitch[j])) {
            int d = iabs(g->w.pitch[j] - pitch);
            if (d < bd) {
                bd = d;
                best = j;
            }
        }
    return best >= 0 ? best : win_index(g, pitch);
}

static int roulette(Rng *r, const uint32_t *wt, int n, uint32_t tot)
{
    uint32_t x = rng_below(r, tot);
    for (int j = 0; j < n; j++) {
        if (x < wt[j])
            return j;
        x -= wt[j];
    }
    return n - 1;
}

/* ¿Choca esta nota (durante toda su duración) con lo que toca el bajo? Segunda menor, séptima mayor o
 * tritono: los intervalos más ásperos; en una nota de paso se toleran, pero se evitan. */
static bool clashes_with_bass(const Gen *g, int bar, int step, int len, int pitch)
{
    for (int k = step; k < step + len && k < MUSIC_BAR_STEPS; k++) {
        int b = music_sounding(g->sg->bass, bar * MUSIC_BAR_STEPS + k);
        if (b) {
            int ic = iabs(pitch - b) % 12;
            if (ic == 1 || ic == 6 || ic == 11)
                return true;
        }
    }
    return false;
}

/* Pesos de cada nota de la ventana para la siguiente: tabla de grados x tamaño del salto x reglas
 * (tras un salto grande se vuelve en sentido contrario por grados conjuntos; nunca más de dos notas
 * iguales seguidas; en tiempo fuerte solo notas del acorde; la altura tira suavemente hacia el centro
 * de la frase). Con 'appr' >= 0 (última nota de un compás) se enlaza con el compás siguiente. */
static uint32_t next_weights(const Gen *g, int bar, int step, int len, int appr, uint32_t *wt)
{
    const Window *w = &g->w;
    const MusicChord *ch = &g->sg->chord[bar];
    bool strong = needs_chord(step, len);
    int cur = g->cur, prev = g->prev;
    uint32_t tot = 0;
    for (int j = 0; j < w->n; j++) {
        uint32_t x = 0;
        int d = w->pitch[j] - w->pitch[cur], ad = iabs(d);
        if (ad <= 12) {
            x = (uint32_t)(*g->tr)[w->deg[cur]][w->deg[j]] * W_SEMI[ad];
            if (d == 0)
                x /= 3;   /* repetir la misma nota es la excepción */
            if (prev >= 0 && x) {
                int pd = w->pitch[cur] - w->pitch[prev];
                if (iabs(pd) >= 5) {
                    if ((d > 0 && pd > 0) || (d < 0 && pd < 0))
                        x /= 4;
                    else if (ad <= 4 && d != 0)
                        x *= 2;
                    if (ad >= 5)
                        x /= 2;
                }
                if (d == 0 && pd == 0)
                    x = 0;
            }
            bool ct = music_in_chord(ch, w->pitch[j]);
            if (strong)
                x = ct ? x * 6 : 0;
            else if (ct)
                x *= 2;
            else if (x && clashes_with_bass(g, bar, step, len, w->pitch[j]))
                x /= 6;
            int dist = iabs(w->pitch[j] - g->center);
            x *= (uint32_t)(8 + (dist < 8 ? 8 - dist : 0));
            if (appr >= 0) {
                int da = iabs(w->pitch[j] - appr);
                x = da > 7 ? 0 : x * (uint32_t)(10 - da);
            }
        }
        wt[j] = x;
        tot += x;
    }
    return tot;
}

static int pick_next(Gen *g, int bar, int step, int len)
{
    uint32_t wt[16];
    uint32_t tot = 0;
    if (g->approach >= 0 && step + len >= MUSIC_BAR_STEPS)
        tot = next_weights(g, bar, step, len, g->approach, wt);
    if (tot == 0) /* sin enlace posible (o no hace falta): las reglas normales */
        tot = next_weights(g, bar, step, len, -1, wt);
    if (tot == 0)
        return nearest_chord_tone(g, bar, g->w.pitch[g->cur]);
    return roulette(&g->r, wt, g->w.n, tot);
}

/* Primera nota (de la canción o de un compás sin pasado): una nota del acorde cerca del centro. */
static int pick_start(Gen *g, int bar)
{
    const Window *w = &g->w;
    const MusicChord *ch = &g->sg->chord[bar];
    uint32_t wt[16], tot = 0;
    for (int j = 0; j < w->n; j++) {
        uint32_t x = 0;
        if (music_in_chord(ch, w->pitch[j])) {
            int dist = iabs(w->pitch[j] - g->center);
            x = (uint32_t)(1 + (dist < 8 ? 8 - dist : 0));
            if (g->cur >= 0) {
                int dd = iabs(w->pitch[j] - w->pitch[g->cur]);
                x *= (uint32_t)(dd <= 12 ? W_SEMI[dd] : 0) + 1u;
            }
        }
        wt[j] = x;
        tot += x;
    }
    if (!tot)
        return nearest_chord_tone(g, bar, g->center);
    return roulette(&g->r, wt, w->n, tot);
}

/* Última nota de una frase: abierta (otra nota del acorde, no la tónica) o la tónica. */
static int pick_end(Gen *g, int bar, int mode)
{
    const Window *w = &g->w;
    const MusicChord *ch = &g->sg->chord[bar];
    int from = g->cur >= 0 ? w->pitch[g->cur] : g->center;
    if (mode == END_TONIC) {
        int best = -1, bd = 1000;
        for (int j = 0; j < w->n; j++) {
            int d = iabs(w->pitch[j] - from);
            if (w->pitch[j] % 12 == g->st->key && d < bd) {
                bd = d;
                best = j;
            }
        }
        if (best >= 0)
            return best;
    }
    for (int pass = 0; pass < 2; pass++) { /* 1.ª: enlazando con el compás siguiente; 2.ª: sin enlace */
        uint32_t wt[16], tot = 0;
        for (int j = 0; j < w->n; j++) {
            uint32_t x = 0;
            int p = w->pitch[j], d = iabs(p - from);
            if (music_in_chord(ch, p) && p % 12 != g->st->key && d <= 9) {
                x = d == 0 ? 1u : (uint32_t)W_SEMI[d] + 1u;
                if (pass == 0 && g->approach >= 0) {
                    int da = iabs(p - g->approach);
                    x = da > 7 ? 0 : x * (uint32_t)(10 - da);
                }
            }
            wt[j] = x;
            tot += x;
        }
        if (tot)
            return roulette(&g->r, wt, w->n, tot);
        if (g->approach < 0)
            break;
    }
    return nearest_chord_tone(g, bar, from);
}

static void put_note(Gen *g, int bar, int step, int idx, int len)
{
    MusicNote *n = &g->sg->mel[bar * MUSIC_BAR_STEPS + step];
    n->pitch = g->w.pitch[idx];
    n->len = (uint8_t)len;
    n->vol = accent(step);
    g->prev = g->cur;
    g->cur = idx;
}

/* Genera un compás con el ritmo 'cell'. end != END_NONE fuerza la última nota. */
static void gen_bar(Gen *g, int bar, const int8_t *cell, int end)
{
    memset(&g->sg->mel[bar * MUSIC_BAR_STEPS], 0, MUSIC_BAR_STEPS * sizeof(MusicNote));
    int last = -1, n = 0;
    while (n < CELL_MAX && cell[n] != 0) {
        if (cell[n] > 0)
            last = n;
        n++;
    }
    int step = 0;
    for (int i = 0; i < n; i++) {
        int len = iabs(cell[i]);
        if (cell[i] > 0) {
            int idx;
            if (g->cur < 0)
                idx = pick_start(g, bar);
            else if (i == last && end != END_NONE)
                idx = pick_end(g, bar, end);
            else
                idx = pick_next(g, bar, step, len);
            put_note(g, bar, step, idx, len);
        }
        step += len;
    }
}

static int bar_onsets(const Gen *g, int bar, int *steps)
{
    int n = 0;
    for (int k = 0; k < MUSIC_BAR_STEPS; k++)
        if (g->sg->mel[bar * MUSIC_BAR_STEPS + k].len)
            steps[n++] = k;
    return n;
}

static void set_cur_from_bar(Gen *g, int bar)
{
    int st[16];
    int n = bar_onsets(g, bar, st);
    g->cur = g->prev = -1;
    if (n >= 2)
        g->prev = win_index(g, g->sg->mel[bar * MUSIC_BAR_STEPS + st[n - 2]].pitch);
    if (n >= 1)
        g->cur = win_index(g, g->sg->mel[bar * MUSIC_BAR_STEPS + st[n - 1]].pitch);
}

/* Las notas fuertes se ajustan al acorde del compás (al copiar un compás con otro acorde). */
static void fix_strong(Gen *g, int bar)
{
    for (int k = 0; k < MUSIC_BAR_STEPS; k++) {
        MusicNote *n = &g->sg->mel[bar * MUSIC_BAR_STEPS + k];
        if (n->len && needs_chord(k, n->len) && !music_in_chord(&g->sg->chord[bar], n->pitch))
            n->pitch = g->w.pitch[nearest_chord_tone(g, bar, n->pitch)];
    }
}

/* Mueve una nota 'shift' pasos: por notas del acorde si es fuerte, por notas de la escala si no. */
static int shift_idx(const Gen *g, int bar, int idx, int shift, bool strong)
{
    int n = g->w.n, j = idx;
    if (strong) {
        const MusicChord *ch = &g->sg->chord[bar];
        int step = shift > 0 ? 1 : -1;
        for (int moved = 0; moved < (shift > 0 ? shift : -shift); moved++) {
            int k = j + step;
            while (k >= 0 && k < n && !music_in_chord(ch, g->w.pitch[k]))
                k += step;
            if (k < 0 || k >= n)
                break;
            j = k;
        }
        return j;
    }
    j += shift;
    return j < 0 ? 0 : j >= n ? n - 1 : j;
}

/* Copia un compás (el motivo que se repite), desplazado 'shift' notas (una secuencia). */
static void copy_bar(Gen *g, int src, int dst, int shift)
{
    for (int k = 0; k < MUSIC_BAR_STEPS; k++) {
        MusicNote n = g->sg->mel[src * MUSIC_BAR_STEPS + k];
        if (n.len && shift) {
            int j = shift_idx(g, dst, win_index(g, n.pitch), shift, needs_chord(k, n.len));
            n.pitch = g->w.pitch[j];
        }
        g->sg->mel[dst * MUSIC_BAR_STEPS + k] = n;
    }
    fix_strong(g, dst);
}

/* Muta poco a poco: vuelve a tirar las últimas 'count' notas de un compás (distintas de las de antes). */
static void reroll_tail(Gen *g, int bar, int count, int approach)
{
    g->approach = approach;
    int st[16];
    int n = bar_onsets(g, bar, st);
    if (n == 0) {
        g->approach = -1;
        return;
    }
    int from = n - (count < n ? count : n);
    if (from > 0) {
        g->cur = win_index(g, g->sg->mel[bar * MUSIC_BAR_STEPS + st[from - 1]].pitch);
        g->prev = from > 1 ? win_index(g, g->sg->mel[bar * MUSIC_BAR_STEPS + st[from - 2]].pitch) : -1;
    } else if (bar > 0) {
        set_cur_from_bar(g, bar - 1);
    } else {
        g->cur = g->prev = -1;
    }
    for (int i = from; i < n; i++) {
        MusicNote *nt = &g->sg->mel[bar * MUSIC_BAR_STEPS + st[i]];
        int old = nt->pitch, idx = 0;
        for (int tries = 0; tries < 6; tries++) {
            idx = g->cur < 0 ? pick_start(g, bar) : pick_next(g, bar, st[i], nt->len);
            if (g->w.pitch[idx] != old)
                break;
        }
        nt->pitch = g->w.pitch[idx];
        g->prev = g->cur;
        g->cur = idx;
    }
    g->approach = -1;
}

static int pick_cell(Gen *g, int not_idx)
{
    uint8_t w[NCELL];
    for (int i = 0; i < NCELL; i++)
        w[i] = i == not_idx ? 0 : CELL_W[i][g->st->density];
    return pick_w(&g->r, w, NCELL);
}

static const int8_t *pick_cadence(Gen *g, bool final)
{
    static const uint8_t FINAL[4] = {0, 1, 3, 6};
    int i = final ? FINAL[rng_below(&g->r, 4)] : (int)rng_below(&g->r, 6);
    return CADS[i];
}

static void gen_chords(Gen *g)
{
    const MusicStyle *st = g->st;
    for (int ph = 0; ph < 4; ph++) {
        const Prog *p = &PROGS[ph == 2 ? st->prog_b : st->prog_a];
        for (int b = 0; b < 4; b++) {
            MusicChord *c = &g->sg->chord[ph * 4 + b];
            c->root = (uint8_t)((st->key + p->root[b]) % 12);
            c->minor = (uint8_t)((p->minor >> b) & 1);
        }
    }
    /* Cadencia final: dominante (V, o la subtónica en los modos menores) y tónica. */
    bool maj = st->mode == MODE_PENT_MAJ;
    g->sg->chord[14].root = (uint8_t)((st->key + (maj ? 7 : 10)) % 12);
    g->sg->chord[14].minor = 0;
    g->sg->chord[15].root = st->key;
    g->sg->chord[15].minor = (uint8_t)!maj;
}

/* Altura de la primera nota de un compás. */
static int first_pitch(const Gen *g, int bar)
{
    for (int k = 0; k < MUSIC_BAR_STEPS; k++)
        if (g->sg->mel[bar * MUSIC_BAR_STEPS + k].len)
            return g->sg->mel[bar * MUSIC_BAR_STEPS + k].pitch;
    return -1;
}

/* Tres notas iguales seguidas suenan a tartamudeo: la tercera pasa a la nota vecina de la escala. */
static void fix_runs(Gen *g)
{
    int last = -1;
    for (int s = MUSIC_STEPS - 1; s >= 0 && last < 0; s--)
        if (g->sg->mel[s].len)
            last = s;
    int pp = -1, p = -1, ps = -1;
    for (int s = 0; s < MUSIC_STEPS; s++) {
        MusicNote *n = &g->sg->mel[s];
        if (!n->len)
            continue;
        if (n->pitch == p && p == pp) {
            /* La nota final (la tónica que cierra la canción) no se toca: se cambia la anterior. */
            int at = s == last ? ps : s;
            MusicNote *t = &g->sg->mel[at];
            int bar = at / MUSIC_BAR_STEPS, best = -1, bd = 1000;
            for (int j = 0; j < g->w.n; j++) {
                int d = iabs(g->w.pitch[j] - t->pitch);
                if (g->w.pitch[j] == p || d > 5)
                    continue;
                if (needs_chord(at % MUSIC_BAR_STEPS, t->len) &&
                    !music_in_chord(&g->sg->chord[bar], g->w.pitch[j]))
                    continue;
                if (d < bd) {
                    bd = d;
                    best = j;
                }
            }
            if (best >= 0)
                t->pitch = g->w.pitch[best];
        }
        pp = p;
        p = n->pitch;
        ps = s;
    }
}

static void gen_melody(Gen *g)
{
    build_window(g);
    const MusicStyle *st = g->st;
    g->cur = g->prev = -1;
    g->approach = -1;
    g->center = st->tonic + 5;

    /* Frase A (compases 0-3): motivo de un compás, respuesta, el motivo otra vez (secuencia) y cadencia.
     * Cada compás acaba cerca de donde empieza el siguiente (la melodía no da saltos al enlazar). */
    int ca1 = pick_cell(g, -1), ca2 = pick_cell(g, ca1);
    gen_bar(g, 0, CELLS[ca1], END_NONE);
    static const int8_t SHIFT[4] = {0, 0, 1, -1};
    copy_bar(g, 0, 2, SHIFT[rng_below(&g->r, 4)]);
    g->approach = first_pitch(g, 2);
    gen_bar(g, 1, CELLS[ca2], END_NONE);
    set_cur_from_bar(g, 2);
    g->approach = first_pitch(g, 0);
    gen_bar(g, 3, pick_cadence(g, false), END_OPEN);
    g->approach = -1;

    /* Frase A' (4-7): la misma con pequeños cambios y otra cadencia. */
    for (int b = 0; b < 3; b++)
        copy_bar(g, b, 4 + b, 0);
    reroll_tail(g, 5, 2, first_pitch(g, 6));
    reroll_tail(g, 6, 1, -1);
    set_cur_from_bar(g, 6);
    gen_bar(g, 7, pick_cadence(g, false), END_OPEN);

    /* Frase B (8-11): contraste, más aguda, con su propio ritmo; el compás 10 es el clímax. */
    g->center = st->tonic + 9;
    int cb1 = pick_cell(g, ca1), cb2 = pick_cell(g, ca2);
    gen_bar(g, 8, CELLS[cb1], END_NONE);
    copy_bar(g, 8, 10, 1 + (int)rng_below(&g->r, 2));
    g->approach = first_pitch(g, 10);
    gen_bar(g, 9, CELLS[cb2], END_NONE);
    set_cur_from_bar(g, 10);
    g->approach = first_pitch(g, 0);
    gen_bar(g, 11, pick_cadence(g, false), END_OPEN);
    g->approach = -1;

    /* Frase A'' (12-15): vuelve el motivo y resuelve en la tónica. */
    g->center = st->tonic + 5;
    for (int b = 0; b < 3; b++)
        copy_bar(g, b, 12 + b, 0);
    reroll_tail(g, 13, 1, first_pitch(g, 14));
    reroll_tail(g, 14, 1, -1);
    set_cur_from_bar(g, 14);
    gen_bar(g, 15, pick_cadence(g, true), END_TONIC);
    fix_runs(g);
}

/* El bajo (voz cuadrada 2): siempre notas del acorde, con la fundamental en el primer tiempo. */
static void bass_note(MusicSong *sg, int bar, int step, int pitch, int len, uint8_t vol)
{
    MusicNote *n = &sg->bass[bar * MUSIC_BAR_STEPS + step];
    n->pitch = (uint8_t)pitch;
    n->len = (uint8_t)len;
    n->vol = vol;
}

static void gen_bass(Gen *g, Rng *rb)
{
    MusicSong *sg = g->sg;
    int kb = 3 + (int)rng_below(rb, 3), rot = (int)rng_below(rb, 4);
    int pat = (int)rng_below(rb, 2);
    for (int bar = 0; bar < MUSIC_BARS; bar++) {
        const MusicChord *c = &sg->chord[bar];
        int style = bar / 4 == 2 ? g->st->bass_style_b : g->st->bass_style_a;
        int r = 36 + c->root;                          /* 36..47: Do2..Si2 */
        int third = r + (c->minor ? 3 : 4), fifth = r + 7;
        switch (style) {
        case 0: { /* euclidiano: fundamental, quinta y octava en los golpes de E(k,16) */
            uint8_t hit[16];
            euclid16_from_hit(hit, kb, rot);
            int pos[16], n = 0;
            for (int s = 0; s < 16; s++)
                if (hit[s])
                    pos[n++] = s;
            for (int i = 0; i < n; i++) {
                int len = (i + 1 < n ? pos[i + 1] : 16) - pos[i];
                int roll = (int)rng_below(rb, 100);
                int p = pos[i] == 0 ? r : roll < 55 ? r : roll < 85 ? fifth : r + 12;
                bass_note(sg, bar, pos[i], p, len, pos[i] % 8 == 0 ? 255 : 215);
            }
            break;
        }
        case 1: /* octavas en corcheas */
            for (int s = 0; s < 16; s += 2)
                bass_note(sg, bar, s, (s / 2) % 2 ? r + 12 : r, 2, s % 8 == 0 ? 255 : 215);
            break;
        case 2: { /* arpegio en corcheas sobre la fundamental grave */
            int t = c->minor ? 3 : 4;
            static const int8_t UP[2][8] = {{0, 4, 7, 12, 7, 4, 0, 4}, {0, 7, 4, 7, 12, 7, 4, 7}};
            for (int i = 0; i < 8; i++) {
                int off = UP[pat][i];
                int p = i == 0 ? r : r + 12 + (off == 4 ? t : off);
                bass_note(sg, bar, i * 2, p, 2, i % 4 == 0 ? 255 : 205);
            }
            break;
        }
        default: /* caminante: fundamental, quinta, tercera, quinta */
            bass_note(sg, bar, 0, r, 4, 255);
            bass_note(sg, bar, 4, fifth, 4, 215);
            bass_note(sg, bar, 8, third, 4, 235);
            bass_note(sg, bar, 12, fifth, 4, 215);
            break;
        }
    }
}

/* La batería (canal de ruido) con patrones euclidianos. */
static void gen_drums(Gen *g)
{
    const MusicStyle *st = g->st;
    uint8_t kick[16], hat[16], fill[8];
    euclid16(kick, st->kick_k, 0);
    kick[0] = 1;
    euclid16(hat, st->hat_k, st->hat_k <= 4 ? 2 : st->hat_k <= 6 ? 1 : 0);
    music_euclid(fill, 5, 8);
    for (int bar = 0; bar < MUSIC_BARS; bar++) {
        uint8_t *d = &g->sg->drums[bar * MUSIC_BAR_STEPS];
        uint8_t k2[16];
        memcpy(k2, kick, sizeof(k2));
        if (bar / 4 == 2 && st->drums >= 1) /* en B, un golpe más de bombo */
            k2[10] = 1;
        for (int s = 0; s < 16; s++) {
            uint8_t v = 0;
            if (k2[s])
                v |= DR_KICK;
            if (hat[s] && !(st->drums == 2 && bar / 4 == 2 && s % 2)) /* en B la plena afloja el hi-hat */
                v |= DR_HAT;
            d[s] = v;
        }
        switch (st->snare_pat) {
        case 0: d[4] |= DR_SNARE; d[12] |= DR_SNARE; break;
        case 1: d[8] |= DR_SNARE; break;
        case 2: d[4] |= DR_SNARE; d[12] |= DR_SNARE; d[15] |= DR_SNARE; break;
        default:
            if (bar % 2)
                d[12] |= DR_SNARE;
            break;
        }
        if (st->drums >= 1 && bar % 2 == 1 && !(d[14] & DR_KICK))
            d[14] |= DR_OPEN;
        if (st->drums >= 1 && (bar == 7 || bar == 11)) /* redoble de final de frase: E(5,8) */
            for (int i = 0; i < 8; i++)
                if (fill[i])
                    d[8 + i] = DR_SNARE;
        if (bar == 15) /* el final respira: solo el bombo del primer tiempo */
            for (int s = 8; s < 16; s++)
                d[s] = 0;
    }
}

void music_song(MusicSong *sg, const MusicStyle *st, uint16_t seed, uint8_t mut)
{
    memset(sg, 0, sizeof(*sg));
    Gen g;
    memset(&g, 0, sizeof(g));
    g.st = st;
    g.sg = sg;
    g.tr = TRANS[st->mode];
    gen_chords(&g);
    Rng rb;
    rng_seed(&rb, mix64(0xBA550000ull ^ seed), 0xB5);
    gen_bass(&g, &rb);
    gen_drums(&g);
    rng_seed(&g.r, mix64(0x3E10D1E00000ull ^ ((uint64_t)seed << 8) ^ mut), 0x3E1);
    gen_melody(&g);
}

/* ======================================================================================= */
/* Sintetizador                                                                            */
/* ======================================================================================= */

enum { ST_IDLE, ST_ATT, ST_DEC, ST_SUS, ST_REL };

typedef struct {
    uint8_t att, dec, sus, rel;   /* ataque y decaimiento en ticks de control (2 ms), nivel y relajación */
} EnvDef;
static const EnvDef LEAD_ENV[3] = {{2, 20, 215, 6}, {1, 45, 95, 8}, {6, 30, 190, 14}};
static const EnvDef BASS_ENV = {1, 60, 190, 6};
static const int16_t VMAX[MV_COUNT] = {7000, 7000, 4500};   /* suma máxima 18.500: sin recortes */

/* Frecuencias de la octava 0 (Do0 = 16,35 Hz) en 1/64 Hz; cada octava se multiplica por 2. */
static const uint16_t BASE64[12] = {1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976};

static void init_tables(Music *m)
{
    for (int n = 0; n < 128; n++) {
        int oct = n / 12 - 1;
        uint32_t f = oct >= 0 ? (uint32_t)BASE64[n % 12] << oct : (uint32_t)BASE64[n % 12] >> 1;
        m->inc_tab[n] = (uint32_t)(((uint64_t)f << 26) / m->rate);   /* f/64 Hz * 2^32 / rate */
    }
    m->ctrl_n = m->rate / 500;   /* un tick de control cada ~2 ms */
    if (m->ctrl_n < 1)
        m->ctrl_n = 1;
}

static void set_duty(MusicVoice *v, int duty)
{
    static const uint8_t THR[3] = {32, 64, 128};
    static const uint8_t LO[3] = {37, 85, 255};   /* d/(1-d) * 256: sin componente continua */
    v->thr = THR[duty % 3];
    v->lomul = LO[duty % 3];
}

static void apply_style(Music *m)
{
    set_duty(&m->v[MV_LEAD], m->style.lead_duty);
    set_duty(&m->v[MV_BASS], m->style.bass_duty);
    uint32_t num = m->rate * 15u;
    m->step_base = num / m->style.bpm;
    m->step_rem = num % m->style.bpm;
}

void music_start(Music *m, uint16_t seed, uint8_t mut, uint8_t mood, uint32_t rate)
{
    if (rate < MUSIC_RATE_MIN)
        rate = MUSIC_RATE_MIN;
    if (rate > MUSIC_RATE_MAX)
        rate = MUSIC_RATE_MAX;
    memset(m, 0, sizeof(*m));
    m->rate = rate;
    m->seed = seed;
    m->mut = mut;
    m->mood = mood;
    music_style(&m->style, seed, mood);
    music_song(&m->song, &m->style, seed, mut);
    init_tables(m);
    apply_style(m);
    m->lfsr = 1;
    m->active = 1;
    m->duck = 0x10000;
    m->gain = 0;
    m->gain_target = 0x10000;
    m->gain_step = 0x10000 / 10;   /* entrada suave de ~20 ms */
}

void music_mutate(Music *m, uint8_t mut)
{
    m->mut = mut;
    music_song(&m->song, &m->style, m->seed, mut);
}

static void set_fade(Music *m, uint32_t target, uint32_t ms)
{
    uint32_t ticks = ms / 2;
    if (ticks < 1)
        ticks = 1;
    m->gain_target = target;
    m->gain_step = 0x10000 / ticks + 1;
}

void music_reseed(Music *m, uint16_t seed, uint8_t mut, uint8_t mood)
{
    if (!m->active) {
        music_start(m, seed, mut, mood, m->rate ? m->rate : 22050);
        return;
    }
    m->restart = 1;
    m->stopping = 0;
    m->rs_seed = seed;
    m->rs_mut = mut;
    m->rs_mood = mood;
    set_fade(m, 0, 40);
}

void music_set_mood(Music *m, uint8_t mood)
{
    m->has_mood = mood != m->mood;
    m->pend_mood = mood;
}

void music_stop(Music *m, uint16_t fade_ms)
{
    if (!m->active)
        return;
    m->stopping = 1;
    m->restart = 0;
    set_fade(m, 0, fade_ms);
}

void music_duck(Music *m, uint16_t ms)
{
    uint32_t t = ms / 2 + 1;
    if (t > m->duck_left)
        m->duck_left = t;
}

bool music_active(const Music *m) { return m->active != 0; }

/* Una nota nueva en una voz cuadrada. */
static void pulse_on(Music *m, MusicVoice *v, const MusicNote *n, const EnvDef *e, int gate_pct, bool vib)
{
    uint8_t p = n->pitch > 127 ? 127 : n->pitch;
    v->inc_base = v->inc = m->inc_tab[p];
    v->vol = n->vol;
    v->att_step = (uint8_t)(255 / e->att);
    v->dec_step = (uint8_t)(((255 - e->sus) / e->dec) + 1);
    v->sus = e->sus;
    v->rel_ticks = e->rel;
    v->stage = ST_ATT;
    v->age = 0;
    v->vibe = vib;
    m->flash[v == &m->v[MV_LEAD] ? MV_LEAD : MV_BASS] = n->vol;
    uint32_t ticks = (uint32_t)n->len * m->step_base * (uint32_t)gate_pct / 100u / m->ctrl_n;
    v->gate = (uint16_t)(ticks < 2 ? 2 : ticks > 65535 ? 65535 : ticks);
}

static uint32_t clock_inc(const Music *m, uint32_t hz)
{
    uint64_t inc = ((uint64_t)hz << 32) / m->rate;
    return inc > 0xC0000000u ? 0xC0000000u : (uint32_t)inc;
}

static void noise_hit(Music *m, int kind)
{
    MusicVoice *v = &m->v[MV_NOISE];
    v->stage = ST_SUS;
    v->env = 255;
    switch (kind) {
    case DR_KICK:   /* bombo: ruido grave que baja rápido de frecuencia */
        v->vol = 255; v->decay_shift = 4; v->sweep_shift = 3;
        v->inc = clock_inc(m, 3200); v->inc_min = clock_inc(m, 260);
        break;
    case DR_SNARE:  /* caja: ruido medio con cola */
        v->vol = 200; v->decay_shift = 5; v->sweep_shift = 5;
        v->inc = clock_inc(m, 9000); v->inc_min = clock_inc(m, 5000);
        break;
    case DR_OPEN:   /* hi-hat abierto */
        v->vol = 90; v->decay_shift = 5; v->sweep_shift = 7;
        v->inc = v->inc_min = clock_inc(m, 16000);
        break;
    default:        /* hi-hat cerrado: ruido brillante y seco */
        v->vol = 100; v->decay_shift = 3; v->sweep_shift = 7;
        v->inc = v->inc_min = clock_inc(m, 16000);
        break;
    }
}

static void next_step(Music *m)
{
    uint16_t s = m->step;
    if (s == 0 && m->steps_played > 0) {
        m->loops++;
        /* Una vuelta nueva: el ánimo del universo (lluvia, hora...) puede cambiar el estilo. */
        if (m->has_mood) {
            m->has_mood = 0;
            if (m->pend_mood != m->mood) {
                m->mood = m->pend_mood;
                music_style(&m->style, m->seed, m->mood);
                music_song(&m->song, &m->style, m->seed, m->mut);
                apply_style(m);
            }
        }
    }
    const MusicNote *n = &m->song.mel[s];
    if (n->len)
        pulse_on(m, &m->v[MV_LEAD], n, &LEAD_ENV[m->style.lead_env % 3],
                 n->len <= 2 ? 80 : n->len <= 4 ? 88 : 94, m->style.vib != 0 && n->len >= 4);
    n = &m->song.bass[s];
    if (n->len)
        pulse_on(m, &m->v[MV_BASS], n, &BASS_ENV, n->len <= 2 ? 85 : 94, false);
    uint8_t d = m->song.drums[s];
    if (d & DR_KICK)
        noise_hit(m, DR_KICK);
    else if (d & DR_SNARE)
        noise_hit(m, DR_SNARE);
    else if (d & DR_OPEN)
        noise_hit(m, DR_OPEN);
    else if (d & DR_HAT)
        noise_hit(m, DR_HAT);
    m->pos = s;
    m->step = (uint16_t)((s + 1) % MUSIC_STEPS);
    m->steps_played++;
    uint32_t len = m->step_base;
    m->step_frac += m->step_rem;
    if (m->step_frac >= m->style.bpm) {
        m->step_frac -= m->style.bpm;
        len++;
    }
    m->step_left = len;
}

static void pulse_ctrl(Music *m, MusicVoice *v, int vmax)
{
    if (v->age < 65535)
        v->age++;
    switch (v->stage) {
    case ST_ATT:
        if (v->env + v->att_step >= 255) {
            v->env = 255;
            v->stage = v->sus < 255 ? ST_DEC : ST_SUS;
        } else {
            v->env = (uint8_t)(v->env + v->att_step);
        }
        break;
    case ST_DEC:
        if (v->env <= v->sus + v->dec_step) {
            v->env = v->sus;
            v->stage = ST_SUS;
        } else {
            v->env = (uint8_t)(v->env - v->dec_step);
        }
        break;
    case ST_REL:
        if (v->env <= v->rel_step) {
            v->env = 0;
            v->stage = ST_IDLE;
        } else {
            v->env = (uint8_t)(v->env - v->rel_step);
        }
        break;
    default:
        break;
    }
    if (v->stage == ST_ATT || v->stage == ST_DEC || v->stage == ST_SUS) {
        if (v->gate)
            v->gate--;
        else {
            v->stage = ST_REL;
            v->rel_step = (uint8_t)(v->env / (v->rel_ticks ? v->rel_ticks : 1) + 1);
        }
    }
    /* Vibrato ligero (triángulo de ~5,5 Hz), que entra tarde en las notas largas. */
    v->inc = v->inc_base;
    if (v->vibe && v->age > 55 && v->stage != ST_IDLE) {
        int t = (m->lfo >> 8) & 255;
        int tri = t < 128 ? t - 64 : 191 - t;                     /* -64..63 */
        v->inc = (uint32_t)((int32_t)v->inc_base + (int32_t)(v->inc_base >> 13) * tri * m->style.vib);
    }
    v->amp = (int16_t)((((int)v->env * v->vol) >> 8) * vmax >> 8);
}

static void do_restart(Music *m)
{
    uint32_t rate = m->rate;
    music_start(m, m->rs_seed, m->rs_mut, m->rs_mood, rate);
}

/* Un tick de control (~2 ms): volumen, atenuación, envolventes y vibrato. */
static void ctrl_tick(Music *m)
{
    if (m->gain < m->gain_target)
        m->gain = m->gain + m->gain_step > m->gain_target ? m->gain_target : m->gain + m->gain_step;
    else if (m->gain > m->gain_target)
        m->gain = m->gain < m->gain_target + m->gain_step ? m->gain_target : m->gain - m->gain_step;
    if (m->gain == 0 && m->gain_target == 0) {
        if (m->restart) {
            do_restart(m);
            return;
        }
        if (m->stopping) {
            m->active = 0;
            return;
        }
    }
    uint32_t dt = m->duck_left ? 0x7000u : 0x10000u;
    if (m->duck_left)
        m->duck_left--;
    if (m->duck < dt)
        m->duck = m->duck + 0x1800 > dt ? dt : m->duck + 0x1800;
    else if (m->duck > dt)
        m->duck = m->duck < dt + 0x1800 ? dt : m->duck - 0x1800;
    m->lfo = (uint16_t)(m->lfo + 720);   /* ~5,5 Hz con ticks de 2 ms */
    pulse_ctrl(m, &m->v[MV_LEAD], VMAX[MV_LEAD]);
    pulse_ctrl(m, &m->v[MV_BASS], VMAX[MV_BASS]);
    MusicVoice *nz = &m->v[MV_NOISE];
    if (nz->env) {
        uint8_t dec = (uint8_t)(nz->env >> nz->decay_shift);
        nz->env = (uint8_t)(nz->env > dec + 1 ? nz->env - (dec ? dec : 1) : 0);
    }
    if (nz->inc > nz->inc_min) {
        uint32_t gap = nz->inc - nz->inc_min, step = (gap >> nz->sweep_shift) + 1;
        nz->inc -= step > gap ? gap : step;
    }
    nz->amp = (int16_t)((((int)nz->env * nz->vol) >> 8) * VMAX[MV_NOISE] >> 8);
    /* Para la pantalla: las voces con tono dan un destello al empezar cada nota que cae enseguida;
     * el ruido sigue su propia envolvente. */
    for (int i = 0; i < MV_NOISE; i++) {
        m->flash[i] = (uint8_t)(m->flash[i] - (m->flash[i] >> 5) - (m->flash[i] ? 1 : 0));
        int hold = ((int)m->v[i].env * m->v[i].vol >> 8) * 2 / 5;
        m->level[i] = (uint8_t)(m->flash[i] > hold ? m->flash[i] : hold);
    }
    m->level[MV_NOISE] = (uint8_t)(((int)nz->env * nz->vol) >> 8);
}

size_t music_render(Music *m, int16_t *buf, size_t n)
{
    size_t i = 0;
    for (; i < n; i++) {
        if (!m->active)
            break;
        if (m->ctrl_left == 0) {
            ctrl_tick(m);
            if (!m->active)
                break;
            m->ctrl_left = m->ctrl_n;
        }
        m->ctrl_left--;
        if (m->step_left == 0)
            next_step(m);
        m->step_left--;

        MusicVoice *a = &m->v[MV_LEAD], *b = &m->v[MV_BASS], *nz = &m->v[MV_NOISE];
        a->ph += a->inc;
        b->ph += b->inc;
        int mix = ((a->ph >> 24) < a->thr) ? a->amp : -((a->amp * a->lomul) >> 8);
        mix += ((b->ph >> 24) < b->thr) ? b->amp : -((b->amp * b->lomul) >> 8);
        uint32_t np = m->noise_ph + nz->inc;
        if (np < nz->inc) {   /* el reloj del ruido dio la vuelta: un paso del LFSR de 15 bits */
            uint16_t fb = (uint16_t)((m->lfsr ^ (m->lfsr >> 1)) & 1u);
            m->lfsr = (uint16_t)((m->lfsr >> 1) | (fb << 14));
        }
        m->noise_ph = np;
        mix += (m->lfsr & 1u) ? nz->amp : -nz->amp;

        /* Filtro de un polo (suaviza el timbre y los alias) y volumen. */
        m->lp += ((mix - m->lp) * (int)m->style.lpf) >> 8;
        int32_t g8 = (int32_t)(((m->gain >> 8) * (m->duck >> 8)) >> 8);
        int32_t out = (m->lp * g8) >> 8;
        if (out > 32767)
            out = 32767;
        else if (out < -32767)
            out = -32767;
        buf[i] = (int16_t)out;

        if ((m->scope_div++ & 1) == 0) {
            m->scope[m->scope_pos] = (int16_t)out;
            m->scope_pos = (uint8_t)((m->scope_pos + 1) % MUSIC_SCOPE);
        }
    }
    return i;
}
