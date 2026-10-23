#include "musicstats.h"

#include <math.h>
#include <string.h>

static int iabs(int x) { return x < 0 ? -x : x; }

typedef struct {
    int step, pitch, len;
} Onset;

static int gather(const MusicSong *sg, Onset *o)
{
    int n = 0;
    for (int s = 0; s < MUSIC_STEPS; s++)
        if (sg->mel[s].len && sg->mel[s].pitch) {
            o[n].step = s;
            o[n].pitch = sg->mel[s].pitch;
            o[n].len = sg->mel[s].len;
            n++;
        }
    return n;
}

/* Semejanza entre dos compases: pasos con la misma altura / pasos en que suena algo en alguno. */
static int bar_sim_pm(const uint8_t *sp, int a, int b)
{
    int same = 0, any = 0;
    for (int k = 0; k < MUSIC_BAR_STEPS; k++) {
        int x = sp[a * MUSIC_BAR_STEPS + k], y = sp[b * MUSIC_BAR_STEPS + k];
        if (x || y)
            any++;
        if (x && x == y)
            same++;
    }
    return any ? same * 1000 / any : 0;
}

static int sim_at_lag(const uint8_t *sp, int lag)
{
    int sum = 0, cnt = 0;
    for (int b = 0; b + lag < MUSIC_BARS; b++, cnt++)
        sum += bar_sim_pm(sp, b, b + lag);
    return cnt ? sum / cnt : 0;
}

void ms_analyze(const MusicSong *sg, const MusicStyle *st, MusicMetrics *r)
{
    memset(r, 0, sizeof(*r));
    Onset o[MUSIC_STEPS];
    int n = gather(sg, o);
    r->notes = n;
    r->bpm = st->bpm;
    if (n == 0)
        return;

    const uint8_t *sc = music_scale(st->mode);
    int sl = music_scale_len(st->mode);
    int in_scale = 0, lo = 255, hi = 0;
    uint32_t pcs = 0;
    long chord_len = 0, tot_len = 0;
    for (int i = 0; i < n; i++) {
        int rel = (o[i].pitch + 120 - st->key) % 12;
        for (int d = 0; d < sl; d++)
            if (sc[d] == rel)
                in_scale++;
        if (o[i].pitch < lo)
            lo = o[i].pitch;
        if (o[i].pitch > hi)
            hi = o[i].pitch;
        pcs |= 1u << (o[i].pitch % 12);
        int bar = o[i].step / MUSIC_BAR_STEPS;
        if (music_in_chord(&sg->chord[bar], o[i].pitch))
            chord_len += o[i].len;
        tot_len += o[i].len;
    }
    r->in_scale_pm = in_scale * 1000 / n;
    r->chord_tone_pm = (int)(chord_len * 1000 / tot_len);
    r->range = hi - lo;
    for (int b = pcs; b; b &= b - 1)
        r->distinct_pcs++;

    int intervals = 0, sum = 0, steps = 0, big = 0, run = 1, mono = 0, last_sign = 0;
    r->max_run = 1;
    r->max_mono = 1;
    for (int i = 0; i + 1 < n; i++) {
        int d = o[i + 1].pitch - o[i].pitch, ad = iabs(d);
        intervals++;
        sum += ad;
        if (ad <= 3)
            steps++;
        if (ad > 7)
            big++;
        if (ad > r->max_leap)
            r->max_leap = ad;
        if (ad > 12)
            r->leaps_over_oct++;
        run = d == 0 ? run + 1 : 1;
        if (run > r->max_run)
            r->max_run = run;
        int sign = d > 0 ? 1 : d < 0 ? -1 : 0;
        if (sign == 0)
            mono = 0;
        else if (sign == last_sign)
            mono++;
        else
            mono = 1;
        last_sign = sign;
        if (mono + 1 > r->max_mono)
            r->max_mono = mono + 1;
    }
    if (intervals) {
        r->mean_leap_x10 = sum * 10 / intervals;
        r->step_pm = steps * 1000 / intervals;
        r->big_leap_pm = big * 1000 / intervals;
    }
    r->density_x10 = n * 10 / MUSIC_BARS;

    uint8_t sp[MUSIC_STEPS];
    int rests = 0;
    for (int s = 0; s < MUSIC_STEPS; s++) {
        sp[s] = music_sounding(sg->mel, s);
        if (!sp[s])
            rests++;
    }
    r->rest_pm = rests * 1000 / MUSIC_STEPS;
    for (int lag = 1; lag <= 12; lag++) { /* hasta A vs A'' (12 compases): al menos 4 pares */
        int s = sim_at_lag(sp, lag);
        if (s > r->motif_rep_pm) {
            r->motif_rep_pm = s;
            r->motif_lag = lag;
        }
    }
    r->phrase_rep_pm = sim_at_lag(sp, 4);

    r->resolves = o[n - 1].pitch % 12 == st->key && o[n - 1].len >= 4;

    int cons = 0, tot = 0;
    for (int bar = 0; bar < MUSIC_BARS; bar++)
        for (int k = 0; k < 16; k += 8) {
            int s = bar * MUSIC_BAR_STEPS + k;
            int m = music_sounding(sg->mel, s), b = music_sounding(sg->bass, s);
            if (!m || !b)
                continue;
            int ic = iabs(m - b) % 12; /* consonantes: unísono/octava, terceras, cuarta, quinta, sextas */
            tot++;
            if (ic == 0 || ic == 3 || ic == 4 || ic == 5 || ic == 7 || ic == 8 || ic == 9)
                cons++;
        }
    r->cons_n = tot;
    r->cons_pm = tot ? cons * 1000 / tot : 0;

    /* Todo el tiempo en que suenan melodía y bajo a la vez: los choques ásperos (clases 1, 6 y 11). */
    int both = 0, harsh = 0, dis = 0;
    for (int s = 0; s < MUSIC_STEPS; s++) {
        int m = music_sounding(sg->mel, s), b = music_sounding(sg->bass, s);
        if (!m || !b)
            continue;
        int ic = iabs(m - b) % 12;
        both++;
        if (ic == 1 || ic == 6 || ic == 11)
            harsh++;
        if (ic == 1 || ic == 2 || ic == 6 || ic == 10 || ic == 11)
            dis++;
    }
    r->harsh_pm = both ? harsh * 1000 / both : 0;
    r->dissonant_pm = both ? dis * 1000 / both : 0;
}

void ms_audio(const int16_t *pcm, size_t n, uint32_t rate, AudioStats *r)
{
    memset(r, 0, sizeof(*r));
    if (n == 0)
        return;
    double s2 = 0, mean = 0;
    int peak = 0;
    for (size_t i = 0; i < n; i++) {
        int v = pcm[i];
        s2 += (double)v * v;
        mean += v;
        if (iabs(v) > peak)
            peak = iabs(v);
        if (iabs(v) >= 32700)
            r->clipped++;
    }
    double rms = sqrt(s2 / (double)n);
    r->rms = (int)rms;
    r->dbfs_x10 = rms > 0.5 ? (int)lround(200.0 * log10(rms / 32768.0)) : -999;
    r->peak = peak;
    r->dc = (int)(mean / (double)n);
    size_t win = rate / 50, windows = 0, quiet = 0;
    if (win == 0)
        win = 1;
    for (size_t i = 0; i + win <= n; i += win) {
        int pk = 0;
        for (size_t k = 0; k < win; k++)
            if (iabs(pcm[i + k]) > pk)
                pk = iabs(pcm[i + k]);
        windows++;
        if (pk < 100)
            quiet++;
    }
    r->silence_pm = windows ? (int)(quiet * 1000 / windows) : 0;
}

unsigned music_judge(const MusicMetrics *m, const AudioStats *a)
{
    unsigned f = 0;
#define FAIL(c, cond) do { if (cond) f |= 1u << (c); } while (0)
    FAIL(MC_SCALE, m->in_scale_pm < MC_IN_SCALE_MIN_PM);
    FAIL(MC_RANGE, m->range < MC_RANGE_MIN || m->range > MC_RANGE_MAX);
    FAIL(MC_LEAP, m->max_leap > MC_LEAP_MAX);
    FAIL(MC_MEAN_LEAP, m->mean_leap_x10 > MC_MEAN_LEAP_MAX_X10);
    FAIL(MC_STEPS, m->step_pm < MC_STEP_MIN_PM);
    FAIL(MC_BIG_LEAPS, m->big_leap_pm > MC_BIG_LEAP_MAX_PM);
    FAIL(MC_RUN, m->max_run > MC_RUN_MAX);
    FAIL(MC_MONO, m->max_mono > MC_MONO_MAX);
    FAIL(MC_VARIETY, m->distinct_pcs < MC_VARIETY_MIN);
    FAIL(MC_MOTIF, m->motif_rep_pm < MC_MOTIF_MIN_PM);
    FAIL(MC_DENSITY, m->density_x10 < MC_DENSITY_MIN_X10 || m->density_x10 > MC_DENSITY_MAX_X10);
    FAIL(MC_RESTS, m->rest_pm > MC_REST_MAX_PM);
    FAIL(MC_RESOLVE, !m->resolves);
    FAIL(MC_CONSONANCE, m->cons_pm < MC_CONS_MIN_PM);
    FAIL(MC_CHORD_TONES, m->chord_tone_pm < MC_CHORD_MIN_PM);
    FAIL(MC_HARSH, m->harsh_pm > MC_HARSH_MAX_PM);
    FAIL(MC_TEMPO, m->bpm < MC_BPM_MIN || m->bpm > MC_BPM_MAX);
    if (a) {
        FAIL(MC_PEAK, a->peak > MC_PEAK_MAX || a->clipped > 0);
        FAIL(MC_LOUDNESS, a->dbfs_x10 < MC_DBFS_MIN_X10 || a->dbfs_x10 > MC_DBFS_MAX_X10);
        FAIL(MC_DC, iabs(a->dc) > MC_DC_MAX);
        FAIL(MC_SILENCE, a->silence_pm > MC_SILENCE_MAX_PM);
    }
#undef FAIL
    return f;
}

const char *ms_criterion_name(int c)
{
    static const char *const N[MC_COUNT] = {
        "escala", "rango", "salto-max", "salto-medio", "grados-conjuntos", "saltos-grandes",
        "notas-repetidas", "escalas-largas", "variedad", "motivo", "densidad", "silencios-melodia",
        "resuelve-en-tonica", "consonancia-fuertes", "notas-del-acorde", "choques-asperos", "tempo",
        "pico", "volumen", "continua", "silencio-audio",
    };
    return c >= 0 && c < MC_COUNT ? N[c] : "?";
}

/* ---------- comparación ingenua ---------- */

void ms_random_song(MusicSong *sg, const MusicStyle *st, uint16_t seed)
{
    music_song(sg, st, seed, 0); /* acordes, bajo y batería iguales: solo cambia la melodía */
    memset(sg->mel, 0, sizeof(sg->mel));
    Rng r;
    rng_seed(&r, 0xBADC0DEull * (seed + 1u), 99);
    const uint8_t *sc = music_scale(st->mode);
    int sl = music_scale_len(st->mode);
    int lo = st->tonic - 3, hi = st->tonic + 14;
    uint8_t pool[16];
    int np = 0;
    for (int o = -1; o <= 2; o++)
        for (int d = 0; d < sl; d++) {
            int p = st->tonic + 12 * o + sc[d];
            if (p >= lo && p <= hi)
                pool[np++] = (uint8_t)p;
        }
    for (int s = 0; s < MUSIC_STEPS;) {
        int len = 1 + (int)rng_below(&r, 4); /* 1..4 pasos */
        if ((s % 16) + len > 16)
            len = 16 - (s % 16);
        if (rng_below(&r, 5) != 0) {
            sg->mel[s].pitch = pool[rng_below(&r, (uint32_t)np)];
            sg->mel[s].len = (uint8_t)len;
            sg->mel[s].vol = 220;
        }
        s += len;
    }
}
