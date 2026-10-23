/* Sistema de frases (spec §5): banco binario incrustado, contexto, selección con
 * antirrepetición y gramática de huecos. El banco sale de los .txt de data/frases con
 * tools/phrases2bin.py y vive en la flash (array const): aquí solo se lee. */
#ifndef LLV_PHRASES_H
#define LLV_PHRASES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phrases_vocab.h"
#include "state.h"

/* ---------- el banco ---------- */

/* Banco incrustado (phrases_data.c, generado). Formato (todo little-endian):
 *   "LLVP", u16 versión, u16 n, n x u32 desplazamiento de cada frase,
 *   y por frase: u8 personaje, u8 propósito, u8 karg, u8 kn, u8 banderas (1 = picaro),
 *   u8 nº de etiquetas, u8 longitud del texto, nº x (u8 tipo, u16 argumento), texto UTF-8. */
extern const uint8_t PHRASE_BANK[];
extern const unsigned PHRASE_BANK_LEN;

typedef struct {
    uint8_t speaker;     /* SPK_* */
    uint8_t kind;        /* PK_* */
    uint8_t karg;        /* pregunta (PREGUNTA, SI, NO, MENTIRA, RETOMA) o escena (ESCENA) */
    uint8_t kn;          /* línea de la escena, desde 1 */
    bool picaro;
    uint8_t ntags;
    uint8_t len;         /* bytes del texto */
    const uint8_t *tags; /* ntags x 3 bytes */
    const uint8_t *text;
} PhraseRec;

/* Cambia el banco (solo para tests). NULL restaura el incrustado. */
void phrase_bank_set(const uint8_t *blob, unsigned len);
int phrase_count(void);
/* Devuelve false si idx está fuera de rango o el registro está dañado. */
bool phrase_get(int idx, PhraseRec *out);
uint8_t phrase_tag_kind(const PhraseRec *r, int i);
uint16_t phrase_tag_arg(const PhraseRec *r, int i);
/* Texto crudo (con huecos) como cadena terminada en NUL. */
void phrase_text(const PhraseRec *r, char *buf, size_t n);
/* Hash estable de personaje + texto (nunca 0): es lo que va al buffer antirrepetición. */
uint32_t phrase_hash(const PhraseRec *r);
/* Comprueba la cabecera y que todos los registros caben. 0 = banco sano. */
int phrase_bank_check(void);

/* Escenas: línea 'line' (desde 1) de la escena, y quién habla en ella (máscara de SPK_*). */
int phrase_scene_find(int scene, int line);
uint32_t phrase_scene_speakers(int scene);

/* ---------- contexto (spec §5.1) ---------- */

/* Lo que el motor sabe del cuerpo del llavero en este momento (lo pone la HAL). */
typedef struct {
    int64_t now;         /* segundos locales */
    bool shaken;         /* lo han agitado hace un momento */
    uint8_t battery;     /* 1..100; 0 = no se sabe */
    bool charging;
} PhraseEnv;

typedef struct {
    int64_t now;
    int hour, minute, wday, season, moon;
    uint8_t date;        /* DATE_* */
    int gap_min;         /* minutos desde la mirada anterior */
    int looks, streak;
    uint8_t aff[SPK_COUNT];
    bool shaken, batt_low, charging;
    uint8_t weather, pet;
    int garden_n, coll_n, bag_n;
    uint8_t habits;
    uint8_t resp[LLV_Q_MAX];   /* 0 nunca, 1 sí, 2 no */
    uint64_t fresh;            /* sucesos nuevos sin comentar (bit = EventKind) */
    uint32_t present;          /* máscara de SPK_* en la sala */
    bool grandma;
} PhraseCtx;

void phrase_ctx_make(PhraseCtx *c, const GameState *gs, const PhraseEnv *env);
/* Fecha especial de un día (DATE_*), para decorar la Sala y llamar a los visitantes. */
int phrase_special_date(int64_t now);

/* ---------- selección ---------- */

typedef struct {
    uint32_t kinds;      /* máscara de PK_*; se admite cualquiera de ellos */
    uint32_t speakers;   /* máscara de SPK_* */
    int karg;            /* -1: cualquiera; si no, la pregunta o escena */
    int scene_line;      /* 0: cualquiera; si no, solo esa línea de la escena */
    bool scene_start;    /* escenas: exige que hablen solo personajes presentes */
    int need_tag;        /* 0: nada; si no, solo frases que lleven una etiqueta de este PT_* */
    bool remember;       /* apunta el hash en el buffer antirrepetición */
} PhraseQuery;

#define PK_BIT(k) (1u << (k))
#define SPK_BIT(s) (1u << (s))

/* Nº de frases elegibles (sin tener en cuenta la antirrepetición). */
int phrase_eligible(const PhraseCtx *c, const PhraseQuery *q);
/* Elige una con gs->rng: pondera por lo específica que es la etiqueta, evita las ~30 últimas
 * y, si el banco no da para tanto, la que lleva más tiempo sin salir. -1 si no hay ninguna. */
int phrase_pick(const PhraseCtx *c, GameState *gs, const PhraseQuery *q);
/* Apunta la frase en el buffer circular. */
void phrase_remember(GameState *gs, int idx);

/* La frase número 'nth' (módulo el total) de ciertos propósitos y personaje (0 = cualquiera),
 * sin contexto ni azar: para textos fijos de una mirada (cartel, glitch...). -1 si no hay. */
int phrase_nth(uint32_t kinds, int speaker, bool grandma, unsigned nth);

/* ---------- huecos ---------- */

typedef struct { uint16_t v[3]; } PhraseFill;
/* Decide las partes aleatorias de los huecos (variante del suceso, hora...) con gs->rng. */
void phrase_fill_prepare(const GameState *gs, const PhraseCtx *c, int idx, PhraseFill *f, Rng *rng);
/* Texto final, sin huecos. n >= 192 recomendable. */
void phrase_render(const GameState *gs, int idx, const PhraseFill *f, char *buf, size_t n);
/* Máximo de caracteres por frase y límites de la caja de texto de la Sala. */
#define PHRASE_BOX_W 108
#define PHRASE_BOX_LINES 6   /* líneas de la caja de la Sala (18 letras por línea) */
#define PHRASE_ASK_LINES 5   /* una pregunta deja la última línea para "A: sí · Mantén: no" */

/* ---------- observador (volcado de frases en las simulaciones) ---------- */

typedef void (*PhraseSeenFn)(void *user, int speaker, int kind, bool picaro, const char *text);
void phrase_set_observer(PhraseSeenFn fn, void *user);
void phrase_notify(int speaker, int kind, bool picaro, const char *text);

/* ---------- nombres ---------- */

const char *speaker_name(int spk);   /* para la etiqueta de la caja: "ABUELA SOL" */
const char *speaker_short(int spk);  /* "Gótica" */
const char *speaker_vocab(int spk);  /* nombre en los .txt: "gotica" */
const char *phrase_event_vocab(int pe);    /* nombre del evento en los .txt */
int phrase_event_kind(int pe);             /* EventKind del evento de vocabulario */
const char *phrase_question_vocab(int q);

#endif
