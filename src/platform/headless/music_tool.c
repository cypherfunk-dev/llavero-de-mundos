/* llv_music: banco de pruebas de la música generativa (fase 4, spec §14: "suena agradable 9 de cada 10
 * semillas"). Como no se puede escuchar desde el código, renderiza N semillas a WAV (mono, 16 bits),
 * mide métricas objetivas sobre las notas y el PCM (src/util/musicstats.h), las juzga con umbrales
 * explícitos y escribe un informe. También dibuja el piano roll y el espectrograma de unas cuantas.
 * Ver --help. Esta herramienta sí usa float (FFT del espectrograma); el motor de música no. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/font.h"
#include "../../core/music.h"
#include "../../core/studio.h"
#include "../../util/imgout.h"
#include "../../util/musicstats.h"
#include "../../util/wav.h"

#define PI 3.14159265358979323846

static void usage(const char *p)
{
    printf("Uso: %s [opciones]\n"
           "  --seeds N     renderiza N semillas a WAV y las mide (por defecto 200)\n"
           "  --first S     primera semilla (por defecto 1); las siguientes son S+1, S+2...\n"
           "  --seconds T   duración de cada WAV (por defecto 30)\n"
           "  --rate R      tasa de muestreo (por defecto 22050)\n"
           "  --out DIR     carpeta de salida (WAV, informe y gráficos; por defecto .)\n"
           "  --report F    nombre del informe dentro de --out (por defecto informe_musica.txt)\n"
           "  --roll K      piano roll y espectrograma PNG de las primeras K semillas (por defecto 6)\n"
           "  --no-wav      no escribe los WAV (solo mide)\n"
           "  --sweep N     mide las notas de N semillas (0..N-1, sin renderizar) con todos los ánimos\n"
           "  --baseline    compara con una melodía ingenua (notas al azar de la escala)\n"
           "  --one S [M]   semilla S con ánimo M (0..255): volcado de notas por pantalla\n"
           "Ajustes del Estudio (v1.1), fijos para todas las semillas (0 = AUTO):\n"
           "  --scale N     1 pent. mayor, 2 pent. menor, 3 mayor, 4 menor, 5 dórica, 6 frigia, 7 lidia, 8 mixolidia,\n"
           "                9 blues, 10 menor armónica\n"
           "  --key N       tónica 1..12 (Do..Si)       --tempo N   1..11 (60..140 BPM, de 8 en 8)\n"
           "  --arp N       1 apagado, 2 sube, 3 baja, 4 sube y baja, 5 azar, 6 roto\n"
           "  --prog N      progresión 1..22 de la tabla  --duty N  1..3 (12,5 / 25 / 50 %%)   --rhythm N  1..3\n"
           "  --mute M      máscara de voces silenciadas (1 melodía, 2 bajo, 4 ruido)\n"
           "  --matrix N    informe del Estudio: las 11 escalas (AUTO + 10) x 7 arpegios (AUTO + 6) con N semillas cada una\n"
           "                (audio incluido); con --roll K también el piano roll de la primera semilla de cada combinación\n"
           "  --examples    WAV de 20 s y piano roll de cada escala con un arpegio distinto (estudio_*.wav / .png)\n"
           "El ánimo de cada semilla rota entre franjas del día, lluvia, cielo raro y mascota.\n",
           p);
}

/* Los ajustes fijos de esta ejecución (todo AUTO si no se pide nada). */
static MusicStudio g_studio;

static uint8_t mood_for(int i)
{
    return MOOD_MAKE(i % 4, (i / 4) % 2, (i / 8) % 3, (i / 24) % 5);
}

/* ---------- gráficos ---------- */

#define RW 800
#define RH 330
static uint32_t img[RW * 700];

static void fillr(uint32_t *px, int w, int h, int x, int y, int rw, int rh, uint32_t c)
{
    for (int j = y < 0 ? 0 : y; j < y + rh && j < h; j++)
        for (int i = x < 0 ? 0 : x; i < x + rw && i < w; i++)
            px[j * w + i] = c;
}

static void text(uint32_t *px, int w, int h, int x, int y, const char *s, uint32_t c, int sc)
{
    while (*s) {
        uint32_t cp = font_utf8_next(&s);
        const uint8_t *g = font_glyph(cp);
        for (int r = 0; r < 8; r++)
            for (int b = 0; b < 5; b++)
                if (g[r] & (0x10 >> b))
                    fillr(px, w, h, x + b * sc, y + r * sc, sc, sc, c);
        x += 6 * sc;
    }
}

static const char *NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* Piano roll: melodía (cian), bajo (magenta), batería (amarillo/naranja/blanco), acordes y frases. */
static int write_roll(const char *path, uint16_t seed, uint8_t mood, const MusicStyle *st, const MusicSong *sg)
{
    const int sx = 3, ry = 4, lo = 34, hi = 86, left = 30, top = 28;
    int w = RW, h = top + (hi - lo + 1) * ry + 64;
    for (int i = 0; i < w * h; i++)
        img[i] = 0x14102a;
    char t[128];
    snprintf(t, sizeof(t), "#%04X  %s  %s  %d BPM  %s  %s  mood %02X", seed, music_mode_name(st->mode), NN[st->key], st->bpm,
             music_prog_name(st->prog_a), st->arp ? music_arp_name(st->arp) : "", mood);
    text(img, w, h, 4, 4, t, 0xf8f8f0, 2);
    /* franjas de las frases y líneas de compás */
    static const char *const PH[4] = {"A", "A'", "B", "A''"};
    for (int b = 0; b < MUSIC_BARS; b++) {
        int x = left + b * 16 * sx;
        fillr(img, w, h, x, top, 1, (hi - lo + 1) * ry, b % 4 == 0 ? 0x6050a0 : 0x2a2450);
        if (b % 4 == 0)
            text(img, w, h, x + 3, 17, PH[b / 4], 0xf848a8, 1);
    }
    /* fila de las notas de la escala */
    const uint8_t *sc = music_scale(st->mode);
    int sl = music_scale_len(st->mode);
    for (int p = lo; p <= hi; p++) {
        int rel = (p + 120 - st->key) % 12;
        bool in = false;
        for (int d = 0; d < sl; d++)
            in |= sc[d] == rel;
        int y = top + (hi - p) * ry;
        if (in)
            fillr(img, w, h, left, y, MUSIC_STEPS * sx, ry, rel == 0 ? 0x2a2255 : 0x1c1838);
        if (p % 12 == st->key)
            text(img, w, h, 2, y - 2, NN[p % 12], 0x88d058, 1);
    }
    for (int s = 0; s < MUSIC_STEPS; s++) {
        const MusicNote *m = &sg->mel[s], *b = &sg->bass[s];
        if (m->len && m->pitch >= lo && m->pitch <= hi)
            fillr(img, w, h, left + s * sx, top + (hi - m->pitch) * ry, m->len * sx - 1, ry - 1, 0x48f8e8);
        if (b->len && b->pitch >= lo && b->pitch <= hi)
            fillr(img, w, h, left + s * sx, top + (hi - b->pitch) * ry, b->len * sx - 1, ry - 1, 0xf848a8);
    }
    int dy = top + (hi - lo + 1) * ry + 6;
    for (int s = 0; s < MUSIC_STEPS; s++) {
        uint8_t d = sg->drums[s];
        if (d & DR_KICK)
            fillr(img, w, h, left + s * sx, dy, sx - 1, 8, 0xf8e048);
        if (d & DR_SNARE)
            fillr(img, w, h, left + s * sx, dy + 10, sx - 1, 8, 0xf07818);
        if (d & (DR_HAT | DR_OPEN))
            fillr(img, w, h, left + s * sx, dy + 20, sx - 1, d & DR_OPEN ? 8 : 4, 0xf8f8f0);
    }
    for (int b = 0; b < MUSIC_BARS; b++) {
        char c[8];
        snprintf(c, sizeof(c), "%s%s", NN[sg->chord[b].root], sg->chord[b].minor ? "m" : "");
        text(img, w, h, left + b * 16 * sx + 3, dy + 34, c, 0xd8b878, 1);
    }
    return img_write_png(path, img, w, h);
}

/* FFT de Cooley-Tukey (solo en la herramienta). */
static void fft(double *re, double *im, int n)
{
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * PI / len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; k++) {
                double wr = cos(ang * k), wi = sin(ang * k);
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
                double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
            }
    }
}

/* Espectrograma (0..5,5 kHz) de hasta 30 s, con una paleta de oscuro a cian a blanco. */
static int write_spec(const char *path, const int16_t *pcm, size_t n, uint32_t rate)
{
    enum { N = 1024, ROWS = 256 };
    size_t hop = 1024;
    int cols = (int)((n > N ? n - N : 0) / hop);
    if (cols > 760)
        cols = 760;
    int w = cols + 40, h = ROWS + 20;
    if (w * h > (int)(sizeof(img) / sizeof(img[0])))
        return 1;
    for (int i = 0; i < w * h; i++)
        img[i] = 0x0c0a18;
    double re[N], im[N];
    for (int c = 0; c < cols; c++) {
        for (int i = 0; i < N; i++) {
            double win = 0.5 - 0.5 * cos(2 * PI * i / (N - 1));
            re[i] = pcm[c * hop + (size_t)i] * win / 32768.0;
            im[i] = 0;
        }
        fft(re, im, N);
        for (int r = 0; r < ROWS; r++) {
            double mag = sqrt(re[r] * re[r] + im[r] * im[r]) / (N / 4.0);
            double db = 20 * log10(mag + 1e-9);
            double v = (db + 90) / 70; /* -90 dB..-20 dB */
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            int rr = (int)(v * v * 255), gg = (int)(v * 230), bb = (int)(60 + v * 195);
            if (v > 0.85) {
                rr = 255; gg = 255;
            }
            img[(ROWS - 1 - r + 10) * w + 36 + c] = (uint32_t)(rr << 16 | gg << 8 | bb);
        }
    }
    char t[32];
    for (int k = 0; k <= 5; k++) {
        int y = ROWS - 1 - (int)(k * 1000.0 / ((double)rate / N)) + 10;
        snprintf(t, sizeof(t), "%dk", k);
        text(img, w, h, 2, y - 3, t, 0xb8b8d0, 1);
        fillr(img, w, h, 32, y, 4, 1, 0xb8b8d0);
    }
    return img_write_png(path, img, w, h);
}

/* ---------- análisis ---------- */

typedef struct {
    uint16_t seed;
    uint8_t mood;
    MusicMetrics mm;
    AudioStats au;
    unsigned fail;
    bool judged_audio;
} Row;

static void print_row(FILE *f, const Row *r, const MusicStyle *st)
{
    const MusicMetrics *m = &r->mm;
    fprintf(f, "semilla #%04X  ánimo %02X  %s  %s  %d BPM  prog %s / %s  %s\n", r->seed, r->mood,
            music_mode_name(st->mode), NN[st->key], st->bpm, music_prog_name(st->prog_a),
            music_prog_name(st->prog_b), r->fail ? "NO CUMPLE" : "agradable");
    fprintf(f, "  notas %d  en escala %d.%d%%  del acorde %d.%d%%  rango %d  salto máx %d  medio %d.%d  "
               "conjuntos %d.%d%%  saltos grandes %d.%d%%\n",
            m->notes, m->in_scale_pm / 10, m->in_scale_pm % 10, m->chord_tone_pm / 10, m->chord_tone_pm % 10,
            m->range, m->max_leap, m->mean_leap_x10 / 10, m->mean_leap_x10 % 10, m->step_pm / 10,
            m->step_pm % 10, m->big_leap_pm / 10, m->big_leap_pm % 10);
    fprintf(f, "  racha igual %d  racha monótona %d  clases de altura %d  motivo %d.%d%% (distancia %d compases, "
               "de frase %d.%d%%)  densidad %d.%d/compás  sin melodía %d.%d%%  resuelve %s\n",
            m->max_run, m->max_mono, m->distinct_pcs, m->motif_rep_pm / 10, m->motif_rep_pm % 10, m->motif_lag,
            m->phrase_rep_pm / 10, m->phrase_rep_pm % 10, m->density_x10 / 10, m->density_x10 % 10,
            m->rest_pm / 10, m->rest_pm % 10, m->resolves ? "sí" : "NO");
    fprintf(f, "  consonancia en tiempos fuertes %d.%d%% (de %d)  choques ásperos %d.%d%% (disonancias en total %d.%d%%)",
            m->cons_pm / 10, m->cons_pm % 10, m->cons_n, m->harsh_pm / 10, m->harsh_pm % 10,
            m->dissonant_pm / 10, m->dissonant_pm % 10);
    if (r->judged_audio)
        fprintf(f, "  audio: RMS %d.%d dBFS  pico %d  continua %d  recortes %ld  silencio %d.%d%%",
                r->au.dbfs_x10 / 10, abs(r->au.dbfs_x10) % 10, r->au.peak, r->au.dc, r->au.clipped,
                r->au.silence_pm / 10, r->au.silence_pm % 10);
    fprintf(f, "\n");
    if (r->fail) {
        fprintf(f, "  incumple:");
        for (int c = 0; c < MC_COUNT; c++)
            if (r->fail >> c & 1)
                fprintf(f, " %s", ms_criterion_name(c));
        fprintf(f, "\n");
    }
}

typedef struct {
    long n, ok, fails[MC_COUNT];
    long sum[9];
    int min[9], max[9];
} Summary;

static const char *const SUMN[9] = {"en escala ‰", "salto medio x10", "grados conjuntos ‰", "motivo ‰",
                                    "consonancia ‰", "del acorde ‰", "notas por compás x10", "rango",
                                    "choques ásperos ‰"};

static void sum_add(Summary *s, const Row *r)
{
    const MusicMetrics *m = &r->mm;
    int v[9] = {m->in_scale_pm, m->mean_leap_x10, m->step_pm, m->motif_rep_pm, m->cons_pm,
                m->chord_tone_pm, m->density_x10, m->range, m->harsh_pm};
    for (int i = 0; i < 9; i++) {
        s->sum[i] += v[i];
        if (s->n == 0 || v[i] < s->min[i])
            s->min[i] = v[i];
        if (s->n == 0 || v[i] > s->max[i])
            s->max[i] = v[i];
    }
    s->n++;
    if (!r->fail)
        s->ok++;
    for (int c = 0; c < MC_COUNT; c++)
        if (r->fail >> c & 1)
            s->fails[c]++;
}

static void sum_print(FILE *f, const char *title, const Summary *s)
{
    fprintf(f, "\n=== %s ===\n", title);
    fprintf(f, "semillas: %ld  agradables: %ld  (%.1f %%)\n", s->n, s->ok, s->n ? 100.0 * s->ok / s->n : 0.0);
    for (int c = 0; c < MC_COUNT; c++)
        if (s->fails[c])
            fprintf(f, "  incumplen \"%s\": %ld (%.1f %%)\n", ms_criterion_name(c), s->fails[c],
                    100.0 * s->fails[c] / s->n);
    for (int i = 0; i < 9; i++)
        fprintf(f, "  %-22s mín %5d  media %7.1f  máx %5d\n", SUMN[i], s->min[i],
                s->n ? (double)s->sum[i] / s->n : 0.0, s->max[i]);
}

static int one(uint16_t seed, uint8_t mood)
{
    MusicStyle st;
    static MusicSong sg;
    music_style_ex(&st, seed, mood, &g_studio);
    music_song(&sg, &st, seed, 0);
    printf("#%04X ánimo %02X: %s %s %d BPM, progresiones %s / %s\n", seed, mood, music_mode_name(st.mode),
           NN[st.key], st.bpm, music_prog_name(st.prog_a), music_prog_name(st.prog_b));
    for (int b = 0; b < MUSIC_BARS; b++) {
        printf("%2d %-3s%s | ", b, NN[sg.chord[b].root], sg.chord[b].minor ? "m" : " ");
        for (int k = 0; k < 16; k++) {
            const MusicNote *n = &sg.mel[b * 16 + k];
            if (n->len)
                printf("%s%d/%d ", NN[n->pitch % 12], n->pitch / 12 - 1, n->len);
        }
        printf("\n");
    }
    MusicMetrics mm;
    ms_analyze(&sg, &st, &mm);
    Row r = {seed, mood, mm, {0}, 0, false};
    r.fail = music_judge(&mm, NULL);
    print_row(stdout, &r, &st);
    return 0;
}


/* ---------- el Estudio: barrido de escalas x arpegios ---------- */

static const char *const SCALE_SLUG[STUDIO_SCALES + 1] = {"auto", "pent_mayor", "pent_menor", "mayor", "menor", "dorica",
                                                          "frigia", "lidia", "mixolidia", "blues", "menor_armonica"};
static const char *const ARP_SLUG[ARP_COUNT] = {"auto", "apagado", "sube", "baja", "sube_baja", "azar", "roto"};

static const char *scale_label(int scale) { return scale ? music_mode_name(music_studio_mode(scale)) : "AUTO"; }

/* Informe del Estudio: para cada combinación de escala fija (o AUTO) y arpegio fijo (o AUTO), 'nseeds' semillas
 * (con el audio de 12 s, que también se juzga) medidas con los mismos umbrales de "agradable" que la música de
 * siempre. Los umbrales NO se tocan: lo que se ajusta, si una combinación sale áspera, es el generador. */
static int studio_report(const char *out, const char *report, int nseeds, bool examples, int rolls, uint32_t first,
                         uint32_t rate)
{
    static MusicSong sg;
    static Music mus;
    char path[600];
    snprintf(path, sizeof(path), "%s/%s", out, report);
    FILE *rep = fopen(path, "w");
    if (!rep) {
        fprintf(stderr, "no se pudo escribir %s\n", path);
        return 1;
    }
    fprintf(rep, "Informe del Estudio de la música (v1.1)\n");
    if (nseeds > 0) {
        const double secs = 12;
        size_t n = (size_t)(secs * rate);
        int16_t *pcm = malloc(n * sizeof(int16_t));
        if (!pcm)
            return 1;
        fprintf(rep, "%d escalas (AUTO + 10) x %d arpegios (AUTO + 6) x %d semillas desde #%04X, ánimo rotando; notas y %.0f s de audio\n"
                     "juzgados con los umbrales de \"agradable\" de siempre (README). El resto de ajustes, los de la línea de órdenes.\n\n",
                STUDIO_SCALES + 1, ARP_COUNT, nseeds, (unsigned)first, secs);
        static Summary by_scale[STUDIO_SCALES + 1], by_arp[ARP_COUNT], all;
        memset(by_scale, 0, sizeof(by_scale));
        memset(by_arp, 0, sizeof(by_arp));
        memset(&all, 0, sizeof(all));
        long worst_fail[MC_COUNT] = {0};
        int combos_90 = 0, combos = 0;
        fprintf(rep, "%-15s %-10s %8s %7s  incumplen (semillas)\n", "escala", "arpegio", "cumplen", "%");
        for (int scale = 0; scale <= STUDIO_SCALES; scale++)
            for (int arp = 0; arp < ARP_COUNT; arp++) {
                MusicStudio ms = g_studio;
                ms.scale = (uint8_t)scale;
                ms.arp = (uint8_t)arp;
                Summary c = {0};
                for (int i = 0; i < nseeds; i++) {
                    uint16_t seed = (uint16_t)(first + (uint32_t)i * 7u + (uint32_t)(scale * 13 + arp));
                    uint8_t mood = mood_for(i);
                    MusicStyle st;
                    music_style_ex(&st, seed, mood, &ms);
                    music_song(&sg, &st, seed, 0);
                    music_start_studio(&mus, seed, 0, mood, rate, &ms);
                    if (music_render(&mus, pcm, n) != n)
                        return 1;
                    Row r = {seed, mood, {0}, {0}, 0, true};
                    ms_analyze(&sg, &st, &r.mm);
                    ms_audio(pcm, n, rate, &r.au);
                    r.fail = music_judge(&r.mm, &r.au);
                    sum_add(&c, &r);
                    sum_add(&by_scale[scale], &r);
                    sum_add(&by_arp[arp], &r);
                    sum_add(&all, &r);
                    if (i == 0 && rolls > 0) {
                        snprintf(path, sizeof(path), "%s/estudio_%s_%s.png", out, SCALE_SLUG[scale], ARP_SLUG[arp]);
                        if (write_roll(path, seed, mood, &st, &sg))
                            fprintf(stderr, "no se pudo escribir %s\n", path);
                    }
                }
                fprintf(rep, "%-15s %-10s %4ld/%-3ld %6.1f%%  ", scale_label(scale), arp ? music_arp_name(arp) : "AUTO", c.ok,
                        c.n, c.n ? 100.0 * c.ok / c.n : 0.0);
                for (int k = 0; k < MC_COUNT; k++)
                    if (c.fails[k]) {
                        fprintf(rep, " %s:%ld", ms_criterion_name(k), c.fails[k]);
                        worst_fail[k] += c.fails[k];
                    }
                fprintf(rep, "\n");
                combos++;
                combos_90 += c.n && 10 * c.ok >= 9 * c.n;
            }
        fprintf(rep, "\n=== POR ESCALA (todos los arpegios) ===\n");
        for (int scale = 0; scale <= STUDIO_SCALES; scale++)
            fprintf(rep, "%-15s %5ld/%-5ld %6.1f%%\n", scale_label(scale), by_scale[scale].ok, by_scale[scale].n,
                    by_scale[scale].n ? 100.0 * by_scale[scale].ok / by_scale[scale].n : 0.0);
        fprintf(rep, "\n=== POR ARPEGIO (todas las escalas) ===\n");
        for (int arp = 0; arp < ARP_COUNT; arp++)
            fprintf(rep, "%-15s %5ld/%-5ld %6.1f%%\n", arp ? music_arp_name(arp) : "AUTO", by_arp[arp].ok, by_arp[arp].n,
                    by_arp[arp].n ? 100.0 * by_arp[arp].ok / by_arp[arp].n : 0.0);
        fprintf(rep, "\n=== INCUMPLIMIENTOS POR CRITERIO (semillas, de %ld) ===\n", all.n);
        for (int k = 0; k < MC_COUNT; k++)
            if (worst_fail[k])
                fprintf(rep, "%-22s %5ld  (%.1f %%)\n", ms_criterion_name(k), worst_fail[k], 100.0 * worst_fail[k] / all.n);
        fprintf(rep, "\nTOTAL: %ld de %ld semillas cumplen (%.1f %%) en %d combinaciones; %d de ellas con >= 90 %%\n", all.ok,
                all.n, 100.0 * all.ok / all.n, combos, combos_90);
        printf("Estudio: %ld de %ld semillas cumplen (%.1f %%) en %d combinaciones (%d con >= 90 %%)\n", all.ok, all.n,
               100.0 * all.ok / all.n, combos, combos_90);
        free(pcm);
    }
    if (examples) {
        const double secs = 20;
        size_t n = (size_t)(secs * rate);
        int16_t *pcm = malloc(n * sizeof(int16_t));
        if (!pcm)
            return 1;
        fprintf(rep, "\n=== EJEMPLOS (WAV de %.0f s y piano roll) ===\n", secs);
        /* Una por escala, con un arpegio distinto cada vez, más algunas con otra tónica, tempo, timbre y ritmo. */
        for (int k = 0; k < STUDIO_SCALES + 4; k++) {
            MusicStudio ms = g_studio;
            ms.scale = (uint8_t)(k < STUDIO_SCALES ? k + 1 : 4 + (k - STUDIO_SCALES) * 2);
            ms.arp = (uint8_t)(1 + k % 6);
            if (k >= STUDIO_SCALES) {
                ms.key = (uint8_t)(3 + 3 * (k - STUDIO_SCALES));
                ms.tempo = (uint8_t)(2 + 3 * (k - STUDIO_SCALES));
                ms.duty = (uint8_t)(1 + (k - STUDIO_SCALES) % 3);
                ms.rhythm = (uint8_t)(1 + (k - STUDIO_SCALES) % 3);
            }
            uint16_t seed = (uint16_t)(first + (uint32_t)k * 11u);
            uint8_t mood = 0x02;
            MusicStyle st;
            music_style_ex(&st, seed, mood, &ms);
            music_song(&sg, &st, seed, 0);
            music_start_studio(&mus, seed, 0, mood, rate, &ms);
            if (music_render(&mus, pcm, n) != n)
                return 1;
            const char *base = SCALE_SLUG[ms.scale];
            snprintf(path, sizeof(path), "%s/estudio_%s_%s%s.wav", out, base, ARP_SLUG[ms.arp], k >= STUDIO_SCALES ? "_extra" : "");
            if (wav_write(path, pcm, n, rate))
                fprintf(stderr, "no se pudo escribir %s\n", path);
            snprintf(path, sizeof(path), "%s/estudio_%s_%s%s.png", out, base, ARP_SLUG[ms.arp], k >= STUDIO_SCALES ? "_extra" : "");
            if (write_roll(path, seed, mood, &st, &sg))
                fprintf(stderr, "no se pudo escribir %s\n", path);
            MusicMetrics mm;
            AudioStats au;
            ms_analyze(&sg, &st, &mm);
            ms_audio(pcm, n, rate, &au);
            unsigned fail = music_judge(&mm, &au);
            fprintf(rep, "estudio_%s_%s%s: semilla #%04X, %s, tónica %s, %d BPM, progresión %s, arpegio %s, timbre %d, ritmo %d  -> %s",
                    base, ARP_SLUG[ms.arp], k >= STUDIO_SCALES ? "_extra" : "", seed, music_mode_name(st.mode), NN[st.key],
                    st.bpm, music_prog_name(st.prog_a), music_arp_name(ms.arp), st.lead_duty, st.drums,
                    fail ? "NO CUMPLE:" : "agradable");
            for (int c = 0; c < MC_COUNT; c++)
                if (fail >> c & 1)
                    fprintf(rep, " %s", ms_criterion_name(c));
            fprintf(rep, "\n");
        }
        free(pcm);
    }
    fclose(rep);
    snprintf(path, sizeof(path), "%s/%s", out, report);
    printf("informe: %s\n", path);
    return 0;
}

int main(int argc, char **argv)
{
    int seeds = 200, roll = 6, sweep = 0;
    uint32_t first = 1, rate = LLV_AUDIO_RATE;
    double seconds = 30;
    const char *out = ".", *report = "informe_musica.txt";
    bool wav = true, baseline = false, do_one = false, examples = false;
    int matrix = 0;
    uint16_t one_seed = 0;
    uint8_t one_mood = 2;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "--seeds") && more) {
            seeds = atoi(argv[++i]);
        } else if (!strcmp(a, "--first") && more) {
            first = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(a, "--seconds") && more) {
            seconds = atof(argv[++i]);
        } else if (!strcmp(a, "--rate") && more) {
            rate = (uint32_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--out") && more) {
            out = argv[++i];
        } else if (!strcmp(a, "--report") && more) {
            report = argv[++i];
        } else if (!strcmp(a, "--roll") && more) {
            roll = atoi(argv[++i]);
        } else if (!strcmp(a, "--no-wav")) {
            wav = false;
        } else if (!strcmp(a, "--sweep") && more) {
            sweep = atoi(argv[++i]);
        } else if (!strcmp(a, "--baseline")) {
            baseline = true;
        } else if (!strcmp(a, "--one") && more) {
            one_seed = (uint16_t)strtoul(argv[++i], NULL, 0);
            if (i + 1 < argc && argv[i + 1][0] != '-')
                one_mood = (uint8_t)strtoul(argv[++i], NULL, 0);
            do_one = true;
        } else if (!strcmp(a, "--scale") && more) {
            g_studio.scale = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--key") && more) {
            g_studio.key = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--tempo") && more) {
            g_studio.tempo = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--arp") && more) {
            g_studio.arp = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--prog") && more) {
            g_studio.prog = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--duty") && more) {
            g_studio.duty = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--rhythm") && more) {
            g_studio.rhythm = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--mute") && more) {
            g_studio.mute = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(a, "--matrix") && more) {
            matrix = atoi(argv[++i]);
        } else if (!strcmp(a, "--examples")) {
            examples = true;
        } else {
            fprintf(stderr, "opción desconocida: %s\n", a);
            usage(argv[0]);
            return 2;
        }
    }
    if (rate < MUSIC_RATE_MIN || rate > MUSIC_RATE_MAX || seconds < 1 || seconds > 300 || seeds < 0)
        return 2;
    if (!music_studio_valid(&g_studio)) {
        fprintf(stderr, "ajustes del Estudio fuera de rango\n");
        return 2;
    }
    if (do_one)
        return one(one_seed, one_mood);
    if (matrix > 0 || examples)
        return studio_report(out, report, matrix, examples, roll, first, rate);

    static MusicSong sg, bsg;
    static Music mus;
    size_t n = (size_t)(seconds * rate);
    int16_t *pcm = malloc(n * sizeof(int16_t));
    if (!pcm)
        return 1;

    char path[600];
    snprintf(path, sizeof(path), "%s/%s", out, report);
    FILE *rep = fopen(path, "w");
    if (!rep) {
        fprintf(stderr, "no se pudo escribir %s\n", path);
        return 1;
    }
    fprintf(rep, "Informe de la música generativa (fase 4)\n"
                 "%d semillas desde #%04X, %.0f s a %u Hz. Umbrales de \"agradable\": ver README.md.\n"
                 "Mide si está bien formada, no el gusto: el gate real se cierra escuchando.\n\n",
            seeds, (unsigned)first, seconds, rate);

    Summary S = {0}, B = {0};
    int rolled = 0;
    for (int i = 0; i < seeds; i++) {
        uint16_t seed = (uint16_t)(first + (uint32_t)i);
        uint8_t mood = mood_for(i);
        MusicStyle st;
        music_style_ex(&st, seed, mood, &g_studio);
        music_song(&sg, &st, seed, 0);
        music_start_studio(&mus, seed, 0, mood, rate, &g_studio);
        size_t got = music_render(&mus, pcm, n);
        if (got != n) {
            fprintf(stderr, "render corto en la semilla %u\n", seed);
            return 1;
        }
        Row r = {seed, mood, {0}, {0}, 0, true};
        ms_analyze(&sg, &st, &r.mm);
        ms_audio(pcm, n, rate, &r.au);
        r.fail = music_judge(&r.mm, &r.au);
        print_row(rep, &r, &st);
        sum_add(&S, &r);
        if (wav) {
            snprintf(path, sizeof(path), "%s/musica_%04X.wav", out, seed);
            if (wav_write(path, pcm, n, rate)) {
                fprintf(stderr, "no se pudo escribir %s\n", path);
                return 1;
            }
        }
        if (rolled < roll) {
            snprintf(path, sizeof(path), "%s/roll_%04X.png", out, seed);
            if (write_roll(path, seed, mood, &st, &sg))
                fprintf(stderr, "no se pudo escribir %s\n", path);
            snprintf(path, sizeof(path), "%s/espectro_%04X.png", out, seed);
            if (write_spec(path, pcm, n, rate))
                fprintf(stderr, "no se pudo escribir %s\n", path);
            rolled++;
        }
        if (baseline) {
            ms_random_song(&bsg, &st, seed);
            Row b = {seed, mood, {0}, {0}, 0, false};
            ms_analyze(&bsg, &st, &b.mm);
            b.fail = music_judge(&b.mm, NULL);
            sum_add(&B, &b);
        }
    }
    sum_print(rep, "RESUMEN (audio renderizado + notas)", &S);
    if (baseline)
        sum_print(rep, "COMPARACIÓN: melodía ingenua (notas al azar de la escala), solo notas", &B);

    if (sweep > 0) {
        Summary W = {0};
        for (int i = 0; i < sweep; i++) {
            uint16_t seed = (uint16_t)i;
            uint8_t mood = mood_for(i);
            MusicStyle st;
            music_style_ex(&st, seed, mood, &g_studio);
            music_song(&sg, &st, seed, 0);
            Row r = {seed, mood, {0}, {0}, 0, false};
            ms_analyze(&sg, &st, &r.mm);
            r.fail = music_judge(&r.mm, NULL);
            sum_add(&W, &r);
        }
        sum_print(rep, "BARRIDO (solo notas, semillas 0..N-1, todos los ánimos)", &W);
        printf("barrido: %ld de %ld semillas cumplen (%.2f %%)\n", W.ok, W.n, W.n ? 100.0 * W.ok / W.n : 0.0);
    }
    fclose(rep);
    free(pcm);
    printf("semillas renderizadas: %ld, agradables: %ld (%.1f %%)\n", S.n, S.ok, S.n ? 100.0 * S.ok / S.n : 0.0);
    if (baseline)
        printf("melodía ingenua: %ld de %ld cumplen (%.1f %%)\n", B.ok, B.n, B.n ? 100.0 * B.ok / B.n : 0.0);
    snprintf(path, sizeof(path), "%s/%s", out, report);
    printf("informe: %s\n", path);
    return 0;
}
