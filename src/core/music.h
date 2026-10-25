/* Música generativa (spec §4.4): chiptune de tres voces al estilo NES (dos ondas cuadradas con
 * ciclo de trabajo y un canal de ruido) generado a partir de una semilla de 16 bits.
 *
 * Portable y sin float ni malloc: el generador de notas y el sintetizador usan solo enteros (fase
 * acumulada de 32 bits, tablas de 16 bits), así que corre en tiempo real en un Cortex-M33 sin FPU.
 * La salida es PCM mono de 16 bits: music_render() es determinista, dependiente solo del estado,
 * y da exactamente los mismos bytes si se pide en un bloque o en muchos de tamaños irregulares.
 *
 * Aleatoriedad: PCG32 propio sembrado con la semilla de la melodía. La música NO usa gs->rng ni
 * avanza con el tiempo del universo (ni en World.tick ni en el catch-up): solo suena, por trozos de
 * muestras, mientras estás en el mundo Música.
 *
 * Qué sale de qué:
 *   (seed, mood)  -> MusicStyle: escala, tonalidad, tempo, progresiones, timbre, ritmos (el "estilo")
 *   (style, seed, mut) -> MusicSong: la canción de 16 compases (A A' B A'') con melodía, bajo, batería
 *   mut cambia solo la melodía (motivo y variaciones): es lo que hace "mutar" (A, toque)
 * Una semilla nueva (A, mantener) cambia todo el estilo.
 */
#ifndef LLV_MUSIC_H
#define LLV_MUSIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rng.h"
#include "state.h"

#define MUSIC_BARS 16                       /* compases de la canción: 4 frases de 4 */
#define MUSIC_BAR_STEPS 16                  /* semicorcheas por compás */
#define MUSIC_STEPS (MUSIC_BARS * MUSIC_BAR_STEPS)
#define MUSIC_SCOPE 128                     /* últimas muestras guardadas para el osciloscopio */
#define MUSIC_NPROG 22                      /* progresiones de acordes de la tabla */

/* Las escalas. Las cuatro primeras son las que elige solas la hora y el clima (MODE_COUNT = cuántas);
 * las otras seis solo salen si el usuario las fija en el Estudio (MODE_ALL = todas). */
typedef enum {
    MODE_PENT_MAJ, MODE_PENT_MIN, MODE_DORIAN, MODE_MINOR, MODE_COUNT,
    MODE_MAJOR = MODE_COUNT, MODE_PHRYGIAN, MODE_LYDIAN, MODE_MIXOLYDIAN, MODE_BLUES, MODE_HARMONIC,
    MODE_ALL
} MusicMode;

/* Franja del día para la música: manda en la escala, el tempo y el timbre. */
typedef enum { DP_NIGHT, DP_DAWN, DP_DAY, DP_DUSK } MusicDaypart;

/* Ánimo del universo (1 byte, se guarda): franja (bits 0-1), lluvia (bit 2), cielo raro (bits 3-4:
 * 0 nada, 1 estrella fugaz, 2 arcoíris) y mascota (bits 5-7: PetKind). */
#define MOOD_DAYPART(m) ((m) & 3)
#define MOOD_RAIN 4
#define MOOD_SKY(m) (((m) >> 3) & 3)
#define MOOD_PET(m) (((m) >> 5) & 7)
#define MOOD_MAKE(dp, rain, sky, pet) ((uint8_t)((dp) | ((rain) ? MOOD_RAIN : 0) | ((sky) << 3) | ((pet) << 5)))

/* Arpegios de la voz de acompañamiento (Estudio). ARP_AUTO = el estilo que sale de la semilla. */
enum { ARP_AUTO, ARP_OFF, ARP_UP, ARP_DOWN, ARP_UPDOWN, ARP_RANDOM, ARP_BROKEN, ARP_COUNT };

/* Los ajustes del Estudio (v1.1). Cada parámetro de generación es AUTO (0: lo decide la semilla, la hora
 * y el clima) o FIJO (1..N: lo elegido por el usuario; ni "mutar" ni "semilla nueva" lo tocan). Las voces
 * silenciadas no son candados: solo apagan la salida de una voz, sin cambiar lo que se genera. */
#define STUDIO_SCALES 10        /* escalas fijables, en el orden de la lista (music_studio_mode) */
#define STUDIO_TEMPOS 11        /* 60, 68, ... 140 BPM */
#define STUDIO_DUTIES 3         /* 12,5 / 25 / 50 % */
#define STUDIO_RHYTHMS 3        /* densidad del ruido: baja, media, alta */
typedef struct {
    uint8_t scale;     /* 0 AUTO; 1..10 */
    uint8_t key;       /* 0 AUTO; 1..12: la tónica es la clase de altura key-1 (0 = Do) */
    uint8_t tempo;     /* 0 AUTO; 1..11: 52 + 8 * tempo BPM */
    uint8_t arp;       /* 0 AUTO; ARP_OFF..ARP_BROKEN */
    uint8_t prog;      /* 0 AUTO; 1..MUSIC_NPROG: progresión de la tabla (prog-1) en las cuatro frases */
    uint8_t duty;      /* 0 AUTO; 1..3: ciclo de trabajo de las dos cuadradas */
    uint8_t rhythm;    /* 0 AUTO; 1..3: densidad del canal de ruido */
    uint8_t mute;      /* bit v = la voz v (MV_LEAD, MV_BASS, MV_NOISE) está silenciada */
} MusicStudio;

/* Empaquetado en 27 bits (GameState.mus.studio): escala 4, tónica 4, tempo 4, arpegio 3, progresión 5,
 * timbre 2, ritmo 2 y voces 3. Todo AUTO y nada silenciado = 0. */
uint32_t music_studio_pack(const MusicStudio *s);
MusicStudio music_studio_unpack(uint32_t packed);
bool music_studio_valid(const MusicStudio *s);
bool music_studio_packed_valid(uint32_t packed);   /* sin bits sueltos y todos los campos en rango */
int music_studio_bpm(int tempo);                   /* 1..11 -> 60..140 */
int music_studio_mode(int scale);                  /* 1..10 -> MusicMode (orden de la lista) */
const char *music_arp_name(int arp);               /* "SUBE" */
/* ¿Hay algún parámetro de generación FIJO? (las voces silenciadas no cuentan) */
bool music_studio_any_locked(const MusicStudio *s);

/* El estilo: todo lo que decide cómo suena, aparte de las notas. */
typedef struct {
    uint8_t mode;          /* MusicMode */
    uint8_t key;           /* clase de altura de la tónica, 0..11 (0 = Do) */
    uint8_t tonic;         /* nota MIDI de la tónica en el registro de la melodía */
    uint8_t bpm;
    uint8_t prog_a, prog_b;/* progresiones (índice en la tabla) de las frases A y B */
    uint8_t lead_duty;     /* 0 = 12,5 %, 1 = 25 %, 2 = 50 % */
    uint8_t bass_duty;
    uint8_t lead_env;      /* 0 órgano, 1 pluck, 2 suave */
    uint8_t vib;           /* vibrato de la melodía: 0 ninguno, 1 ligero, 2 algo más */
    uint8_t lpf;           /* filtro de salida (0..255): más alto = más brillante */
    uint8_t bass_style_a, bass_style_b;
    uint8_t kick_k, hat_k; /* golpes euclidianos por compás (de 16 pasos) */
    uint8_t snare_pat;
    uint8_t drums;         /* intensidad de la batería: 0 mínima, 1 normal, 2 plena */
    uint8_t density;       /* de la melodía: 0 escasa, 1 media, 2 densa */
    /* Estudio (0 con todo en AUTO: el generador es el de la alfa) */
    uint8_t arp;           /* ARP_AUTO: bajo_style_a/b; si no, ese patrón en todos los compases */
    uint8_t adapt;         /* 1: los acordes se ajustan a la escala (escala o progresión fijas) */
} MusicStyle;

typedef struct {
    uint8_t pitch;         /* nota MIDI; 0 = sin nota */
    uint8_t len;           /* duración en pasos; 0 = aquí no empieza ninguna nota */
    uint8_t vol;           /* acento, 0..255 */
} MusicNote;

typedef struct {
    uint8_t root;          /* clase de altura de la fundamental, 0..11 */
    uint8_t minor;
} MusicChord;

/* Batería: bits de MusicSong.drums. */
enum { DR_KICK = 1, DR_SNARE = 2, DR_HAT = 4, DR_OPEN = 8 };

typedef struct {
    MusicNote mel[MUSIC_STEPS];   /* la melodía (voz cuadrada 1) */
    MusicNote bass[MUSIC_STEPS];  /* bajo o arpegio (voz cuadrada 2) */
    uint8_t drums[MUSIC_STEPS];   /* canal de ruido */
    MusicChord chord[MUSIC_BARS];
} MusicSong;

/* ---------- generador (puro: solo depende de los argumentos) ---------- */

/* Franja del día según la hora (0..23). */
int music_daypart(int hour);
/* La escala elegida para una franja y un clima con el número aleatorio 'roll' (0..99). */
int music_pick_mode(int daypart, bool rain, int roll);
const char *music_mode_name(int mode);           /* "DÓRICA" */
const char *music_prog_name(int prog);           /* "i-IV-i-VII" */
int music_scale_len(int mode);
const uint8_t *music_scale(int mode);            /* semitonos desde la tónica */

void music_style(MusicStyle *st, uint16_t seed, uint8_t mood);
/* Igual con los ajustes del Estudio (NULL = todo AUTO). Lo FIJO se aplica por encima de lo que sale de la
 * semilla: las demás decisiones de la semilla no se mueven, y con todo AUTO es idéntico a music_style. */
void music_style_ex(MusicStyle *st, uint16_t seed, uint8_t mood, const MusicStudio *studio);
void music_song(MusicSong *sg, const MusicStyle *st, uint16_t seed, uint8_t mut);
/* Björklund con enteros: k golpes repartidos en n pasos (n <= 32). out[i] = 0/1. */
void music_euclid(uint8_t *out, int k, int n);
/* Altura que suena en un paso de una pista (0 = silencio), sin importar dónde empezó la nota. */
uint8_t music_sounding(const MusicNote *track, int step);
/* Acorde que suena en un compás y sus tres notas (clases de altura). */
bool music_in_chord(const MusicChord *c, int pitch);

/* Ánimo del universo (franja de la hora, lluvia del jardín, cielo raro, mascota). */
uint8_t music_mood(const GameState *gs, int64_t now);
bool music_mood_valid(uint8_t mood);

/* ---------- sintetizador ---------- */

enum { MV_LEAD, MV_BASS, MV_NOISE, MV_COUNT };

typedef struct {
    uint32_t ph, inc, inc_base;
    uint8_t thr;            /* umbral de la onda cuadrada (32 / 64 / 128 de 256) */
    uint8_t lomul;          /* nivel bajo, relativo al alto, para que no haya componente continua */
    uint8_t stage;
    uint8_t env;            /* 0..255 */
    uint8_t sus;
    uint8_t vol;            /* acento de la nota */
    uint8_t att_step, dec_step, rel_ticks;
    uint8_t rel_step;
    uint16_t gate;          /* ticks que quedan de la nota */
    uint16_t age;           /* ticks desde que empezó la nota (vibrato tardío) */
    uint8_t vibe;           /* la nota admite vibrato */
    uint8_t decay_shift;    /* ruido: decaimiento exponencial */
    uint32_t inc_min;       /* ruido: la frecuencia del reloj baja hasta aquí */
    uint8_t sweep_shift;
    int16_t amp;
} MusicVoice;

typedef struct {
    /* qué suena */
    uint16_t seed;
    uint8_t mut, mood;
    uint32_t rate;
    MusicStyle style;
    MusicSong song;
    /* reproducción */
    MusicStudio studio;     /* ajustes del Estudio en vigor */
    uint32_t vgain[MV_COUNT];   /* Q16: ganancia de cada voz (el silencio es un fundido de ~16 ms) */
    uint8_t active;         /* 1 = produce muestras (también mientras se desvanece) */
    uint8_t stopping;       /* se está apagando */
    uint8_t restart;        /* hay una semilla nueva esperando a que baje el volumen */
    uint16_t rs_seed;
    uint8_t rs_mut, rs_mood;
    uint8_t has_mood;       /* ánimo pendiente: se aplica al empezar la vuelta siguiente */
    uint8_t pend_mood;
    uint16_t step;          /* siguiente paso a tocar */
    uint16_t pos;           /* paso que suena ahora (para la pantalla) */
    uint32_t step_left, step_frac, step_base, step_rem;
    uint32_t ctrl_n, ctrl_left;
    MusicVoice v[MV_COUNT];
    uint16_t lfsr;
    uint32_t noise_ph;
    int32_t lp;
    uint32_t gain, gain_target, gain_step;   /* Q16 */
    uint32_t duck, duck_left;                /* Q16; ticks que faltan de atenuación */
    uint16_t lfo;
    uint32_t inc_tab[128];  /* incremento de fase por nota MIDI */
    /* para la pantalla (se calcula del mismo generador, no del dispositivo) */
    int16_t scope[MUSIC_SCOPE];
    uint8_t scope_pos, scope_div;
    uint8_t level[MV_COUNT];  /* 0..255 por voz: destello al empezar cada nota + el nivel sostenido */
    uint8_t flash[2];
    uint32_t steps_played;
    uint8_t loops;
} Music;

#define MUSIC_RATE_MIN 8000
#define MUSIC_RATE_MAX 48000

/* Empieza a sonar desde el principio (entrada suave). rate en Hz (16000..22050 recomendado). */
void music_start(Music *m, uint16_t seed, uint8_t mut, uint8_t mood, uint32_t rate);
/* Igual con los ajustes del Estudio (NULL = todo AUTO). Las voces silenciadas empiezan en silencio. */
void music_start_studio(Music *m, uint16_t seed, uint8_t mut, uint8_t mood, uint32_t rate, const MusicStudio *studio);
/* Cambia los ajustes en marcha. Si cambia algo de lo que se genera, la canción se rehace al instante (como
 * al mutar: sin cortar la nota que suena y sin perder el compás); silenciar o reactivar una voz es solo un
 * fundido de su salida: no toca lo que se genera, y al volver suena sincronizada. */
void music_set_studio(Music *m, const MusicStudio *studio);
/* Cambia solo la melodía (mismo estilo), al instante y sin cortar la nota que suena. */
void music_mutate(Music *m, uint8_t mut);
/* Semilla nueva: baja el volumen un instante y vuelve a empezar con otro estilo. */
void music_reseed(Music *m, uint16_t seed, uint8_t mut, uint8_t mood);
/* El ánimo del universo ha cambiado: se aplica al empezar la vuelta siguiente, no a mitad. */
void music_set_mood(Music *m, uint8_t mood);
/* Desvanece y calla en 'fade_ms'. Tras él, music_active() es falso y music_render() da 0 muestras. */
void music_stop(Music *m, uint16_t fade_ms);
/* Baja la música un instante (un pitido de la interfaz suena por encima). */
void music_duck(Music *m, uint16_t ms);
bool music_active(const Music *m);
/* Escribe hasta n muestras. Devuelve cuántas (menos que n solo si la música acaba de callar). */
size_t music_render(Music *m, int16_t *buf, size_t n);
/* Igual, y además deja en voice[v][i] lo que aporta cada voz a la mezcla (antes del filtro y del volumen),
 * con el fundido de silenciar ya aplicado: sirve para comprobar que silenciar una voz no cambia las demás.
 * voice[v] puede ser NULL. Mismo audio que music_render. */
size_t music_render_voices(Music *m, int16_t *buf, size_t n, int16_t *const voice[MV_COUNT]);

#endif
