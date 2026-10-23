/* Métricas objetivas de "suena agradable" (fase 4, spec §14): no se puede escuchar desde el código,
 * así que se miden propiedades que la teoría musical sencilla asocia a una melodía cantable y a un
 * acompañamiento coherente, sobre las notas que genera music.c y sobre el PCM que sale.
 *
 * Ojo con lo que SÍ y lo que NO dicen: miden si la música está bien formada (en la escala, sin saltos
 * absurdos, con motivos que vuelven, resolviendo en la tónica, consonante en los tiempos fuertes, sin
 * recortes ni silencios largos). No miden gusto, timbre real ni si cansa. El gate de oído se cierra
 * escuchando (README). Los umbrales están razonados en README.md y aquí como constantes. */
#ifndef LLV_MUSICSTATS_H
#define LLV_MUSICSTATS_H

#include <stddef.h>
#include <stdint.h>

#include "../core/music.h"

typedef struct {
    int notes;             /* notas de la melodía */
    int in_scale_pm;       /* ‰ de notas dentro de la escala */
    int chord_tone_pm;     /* ‰ de la duración sobre notas del acorde que suena */
    int range;             /* semitonos entre la más grave y la más aguda */
    int max_leap;          /* mayor salto entre notas seguidas (semitonos) */
    int leaps_over_oct;    /* saltos de más de una octava */
    int mean_leap_x10;     /* salto medio (semitonos x 10) */
    int step_pm;           /* ‰ de intervalos de 3 semitonos o menos (grados conjuntos y repeticiones) */
    int big_leap_pm;       /* ‰ de intervalos de más de 7 semitonos */
    int max_run;           /* mayor racha de notas iguales seguidas */
    int max_mono;          /* mayor racha de notas seguidas subiendo (o bajando) */
    int distinct_pcs;      /* clases de altura distintas */
    int motif_rep_pm;      /* ‰ de semejanza entre compases a la mejor distancia (autocorrelación) */
    int motif_lag;         /* esa distancia en compases */
    int phrase_rep_pm;     /* ‰ de semejanza entre compases a distancia de frase (4) */
    int density_x10;       /* inicios de nota por compás (x 10) */
    int rest_pm;           /* ‰ de pasos sin melodía */
    int resolves;          /* 1 si la última nota es la tónica y dura al menos una negra */
    int cons_pm;           /* ‰ de consonancia melodía/bajo en tiempos fuertes (pasos 0 y 8) */
    int cons_n;            /* ocasiones medidas */
    int harsh_pm;          /* ‰ del tiempo con melodía y bajo a segunda menor, séptima mayor o tritono */
    int dissonant_pm;      /* ‰ con cualquier disonancia (también segunda mayor y séptima menor): informativo */
    int bpm;
} MusicMetrics;

typedef struct {
    int rms;               /* valor eficaz (0..32767) */
    int dbfs_x10;          /* RMS en dBFS x 10 */
    int peak;
    int dc;                /* media */
    long clipped;          /* muestras a menos de 0,2 % del tope */
    int silence_pm;        /* ‰ de ventanas de 20 ms prácticamente mudas */
} AudioStats;

/* Criterios de "agradable": cada uno es un bit de la máscara que devuelve music_judge. */
enum {
    MC_SCALE, MC_RANGE, MC_LEAP, MC_MEAN_LEAP, MC_STEPS, MC_BIG_LEAPS, MC_RUN, MC_MONO, MC_VARIETY,
    MC_MOTIF, MC_DENSITY, MC_RESTS, MC_RESOLVE, MC_CONSONANCE, MC_CHORD_TONES, MC_HARSH, MC_TEMPO,
    MC_PEAK, MC_LOUDNESS, MC_DC, MC_SILENCE,
    MC_COUNT
};

/* Umbrales (razonados en el README: aquí solo los números). */
#define MC_IN_SCALE_MIN_PM 950        /* notas en la escala */
#define MC_RANGE_MIN 7                /* al menos una quinta... */
#define MC_RANGE_MAX 17               /* ...y como mucho octava y cuarta (rango cantable) */
#define MC_LEAP_MAX 12                /* ningún salto de más de una octava */
#define MC_MEAN_LEAP_MAX_X10 40       /* salto medio como mucho de 4 semitonos */
#define MC_STEP_MIN_PM 550            /* al menos el 55 % de los intervalos, por grados conjuntos */
#define MC_BIG_LEAP_MAX_PM 100        /* como mucho el 10 % de saltos mayores que una quinta */
#define MC_RUN_MAX 3                  /* a la cuarta nota igual seguida suena a tartamudeo */
#define MC_MONO_MAX 7                 /* escalas de más de 7 notas seguidas no son melodía */
#define MC_VARIETY_MIN 4              /* al menos 4 clases de altura */
#define MC_MOTIF_MIN_PM 300           /* un motivo que vuelve: >= 30 % de semejanza (el azar da ~10-20 %) */
#define MC_DENSITY_MIN_X10 20         /* entre 2 y 9 notas por compás */
#define MC_DENSITY_MAX_X10 90
#define MC_REST_MAX_PM 300            /* como mucho el 30 % de los pasos sin melodía */
#define MC_CONS_MIN_PM 850            /* tiempos fuertes: >= 85 % de consonancias */
#define MC_CHORD_MIN_PM 550           /* >= 55 % de la melodía sobre notas del acorde */
#define MC_HARSH_MAX_PM 80            /* los choques más ásperos (2.ª menor, 7.ª mayor, tritono): < 8 % del tiempo */
#define MC_BPM_MIN 56
#define MC_BPM_MAX 140
#define MC_PEAK_MAX 32000             /* sin recortes */
#define MC_DBFS_MIN_X10 (-320)        /* entre -32 y -9 dBFS: ni inaudible ni a tope */
#define MC_DBFS_MAX_X10 (-90)
#define MC_DC_MAX 1000                /* componente continua < 3 % del fondo de escala */
#define MC_SILENCE_MAX_PM 150         /* como mucho el 15 % de ventanas mudas */

void ms_analyze(const MusicSong *sg, const MusicStyle *st, MusicMetrics *out);
void ms_audio(const int16_t *pcm, size_t n, uint32_t rate, AudioStats *out);
/* Máscara de criterios incumplidos (bit MC_*). 'au' puede ser NULL (solo se juzgan las notas). */
unsigned music_judge(const MusicMetrics *mm, const AudioStats *au);
const char *ms_criterion_name(int c);

/* Una melodía "ingenua" de comparación: notas al azar de la escala con ritmo al azar, sin cadenas de
 * Markov, sin motivos ni cadencias. Sirve para comprobar que los umbrales distinguen. */
void ms_random_song(MusicSong *sg, const MusicStyle *st, uint16_t seed);

#endif
