/* Estado global del universo (spec §9.1, §10). Todo de tamaño fijo, sin malloc.
 * Solo cambia en macro ticks o por acciones del usuario: así lo simulado en vivo
 * y lo recalculado al despertar coinciden por construcción. */
#ifndef LLV_STATE_H
#define LLV_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "phrases_vocab.h"
#include "rng.h"

typedef enum {
    ITEM_NONE,
    ITEM_NUBE,
    ITEM_VIENTO,
    ITEM_SEMILLA,
    ITEM_ALPISTE,
    ITEM_LOMBRIZ,
    ITEM_DESEO,
    ITEM_COUNT
} ItemKind;

typedef enum {
    EV_NONE,
    EV_BAG_REFILL,     /* a = objeto */
    EV_BAG_FULL_LOST,  /* a = objeto que no cupo */
    EV_NEW_DAY,
    EV_WAKE,           /* a = horas dormido (tope 255) */
    /* Fase 2: jardín */
    EV_ITEM_USED,      /* a = objeto que gastaste */
    EV_APPLE_FALL,
    EV_APPLE_ROT,
    EV_SPROUT,         /* nace un brote */
    EV_TREE_ADULT,     /* un árbol joven se hace árbol */
    EV_MUSHROOM,
    EV_BLOOM,          /* florece una flor rara */
    EV_BIRD,
    EV_BIRD_SEED,      /* el pájaro deja una semilla */
    EV_BEE,
    EV_WORM,
    EV_FROG,
    EV_RAIN,           /* a = 1 si la trajo una Nube */
    EV_WIND,
    EV_SNOW,
    EV_RAINBOW,
    EV_STAR,
    EV_COMPOST,        /* una lombriz abona */
    EV_DISCOVER,       /* a = hueco de la colección */
    /* Fase 2: mascota */
    EV_PET_CHOSEN,     /* a = PetKind */
    EV_PET_STAGE,      /* a = etapa nueva */
    EV_PET_GEN,        /* a = generación nueva */
    EV_PET_CARE,
    EV_PET_WORD,
    EV_PET_SPORES,
    EV_PET_COMPOST,
    /* Fase 3: la Sala, los hábitos y la paciencia */
    EV_VISITOR,        /* a = SPK_* del visitante que llega */
    EV_HABIT,          /* a = HAB_* cumplido */
    EV_WALK,           /* un paseo largo (podómetro) */
    EV_PATIENCE,       /* el bono de paciencia trajo algo raro */
    EV_KIND_COUNT
} EventKind;

/* Posición de cada mundo en el ciclo de B (spec §2). */
enum { WORLD_RELOJ, WORLD_JARDIN, WORLD_SALA, WORLD_MUSICA, WORLD_PESCA, WORLD_COBERTIZO };

/* Banderas de GameState.flags. */
enum {
    SF_ASLEEP = 1,     /* el llavero duerme: el bono de paciencia solo corre así */
    SF_GRANDMA = 2,    /* modo apto para la abuela: solo humor blanco (spec §5.3) */
};

/* Huecos de la colección del Cobertizo (bitset de 16). Los peces llegan en la fase 5. */
typedef enum {
    COLL_MANZANA, COLL_HONGO, COLL_FLOR, COLL_FLOR_RARA, COLL_ABEJA, COLL_PAJARO,
    COLL_LOMBRIZ, COLL_RANA, COLL_ESTRELLA, COLL_ARCOIRIS,
    COLL_PEZ0, COLL_PEZ1, COLL_PEZ2, COLL_PEZ3, COLL_PEZ4, COLL_PEZ5,
    COLL_COUNT
} CollSlot;

typedef enum { W_CLEAR, W_RAIN, W_WIND, W_SNOW } Weather;
typedef enum { SKY_NONE, SKY_STAR, SKY_RAINBOW } SkyEvent;

typedef enum {
    PET_NONE,          /* aún sin elegir */
    PET_HONGO,
    PET_SEMILLA,
    PET_PIEDRA,
    PET_COMPOSTERA,
    PET_COUNT
} PetKind;

/* Una cosa viva del jardín (spec §4.2): especie + etapa + cuenta atrás. */
typedef struct {
    uint8_t sp;        /* Species (garden.h); 0 = hueco libre */
    uint8_t st;        /* etapa dentro de la especie */
    uint8_t cell;      /* casilla 0..LLV_GARDEN_CELLS-1 */
    uint16_t left;     /* ticks hasta la siguiente etapa (0 = estable) */
} Entity;

/* La mascota (spec §4.7). Nunca muere: solo cambia de humor. */
typedef struct {
    uint8_t kind;      /* PetKind */
    uint8_t gen;       /* generación, empieza en 1 */
    uint16_t age;      /* ticks dentro del ciclo actual */
    uint8_t happy;     /* 0..255; solo decide el humor */
    uint8_t thought;   /* índice del último pensamiento (por mascota) */
    uint16_t word_left;  /* hongo: ticks hasta su próxima palabra */
    uint16_t task_left;  /* ticks hasta su próxima gracia (esporas, abono) */
    uint8_t stage_seen;  /* para anotar los cambios de etapa */
} Pet;

typedef struct {
    int64_t tick;
    uint8_t kind;
    uint8_t a;
} DiaryEntry;

/* La Sala (spec §4.3, §5). Lo que se dice y lo que se recuerda vive aquí, no en variables
 * estáticas: así el guardado y el determinismo lo cubren todo. */
typedef enum { DLG_IDLE, DLG_SAY, DLG_ASK } DlgStage;

typedef struct {
    uint8_t visitor;           /* SPK_* presente (0 = nadie) */
    uint8_t visitor_left;      /* ticks hasta que se vaya */
    uint8_t stage;             /* DlgStage */
    uint8_t who;               /* SPK_* de quien habla */
    uint16_t phrase;           /* índice de la frase en el banco + 1 (0 = ninguna) */
    uint16_t fill[3];          /* parámetros de los huecos ({evento}, {hora}...) */
    uint8_t scene;             /* escena en curso (0 = ninguna) */
    uint8_t scene_line;        /* línea de la escena que se muestra */
    uint8_t q;                 /* pregunta pendiente + 1 (0 = ninguna) */
    uint64_t fresh_a, fresh_b; /* sucesos del diario sin comentar: de hoy y de ayer (bit = EventKind) */
    uint32_t recent[LLV_RECENT]; /* hashes de las últimas frases (antirrepetición) */
    uint8_t recent_head;
    uint8_t affinity[SPK_COUNT]; /* 0..255; solo cambia el tono, nunca castiga */
    uint8_t memo[LLV_Q_MAX];   /* respuestas: bits 0-1 (0 nunca, 1 sí, 2 no), bit 2 retomada */
    uint16_t memo_day[LLV_Q_MAX]; /* día en que se preguntó por última vez */
} Sala;

/* El Director de Sorpresas (spec §7). */
typedef enum { DK_NORMAL, DK_COMMENT, DK_SURPRISE, DK_RARE, DK_COUNT } DirRoll;
typedef enum {
    DS_NONE, DS_GLITCH, DS_ARRANQUE, DS_AUSENCIA, DS_INVASION, DS_CUARTA_PARED, DS_CALENDARIO,
    DS_REBELDIA, DS_COLAPSO, DS_GATO, DS_MONEDA, DS_COUNT
} SurpriseKind;

typedef struct {
    uint8_t last;              /* DirRoll de la última tirada (las miradas seguidas no tiran) */
    uint8_t cur;               /* SurpriseKind activa en esta mirada */
    uint8_t arg;               /* mundo, visitante o personaje de la sorpresa */
    uint8_t armed;             /* rebeldía armada: el próximo B no cambia de mundo */
    uint16_t last_surprise_day;/* día de la última sorpresa (seguro de pena) */
    uint16_t rolls;            /* tiradas hechas (para estadísticas) */
    uint16_t surprises;        /* sorpresas y rarezas salidas */
} Director;

/* Hábitos (spec §3.3). */
typedef struct {
    uint8_t enabled;           /* máscara de HAB_* activos */
    uint8_t done_today;        /* máscara de hábitos cumplidos hoy */
    uint8_t asked_today;       /* máscara de hábitos preguntados hoy */
    uint8_t asked_count;       /* preguntas de hábitos hoy (tope LLV_HABIT_MAX_PER_DAY) */
    uint8_t night_look;        /* lo miraste de madrugada (esta noche) */
    uint8_t night_look_prev;   /* ...y la noche anterior, para la pregunta de la mañana */
    uint8_t sleep_streak;      /* noches buenas seguidas */
    uint8_t walk_rewarded;     /* ya hubo objeto extra por caminar hoy */
    uint32_t steps_last;       /* última lectura del podómetro */
    uint16_t steps_today;
} Habits;

/* La música (fase 4, spec §4.4): lo mínimo para volver a oír lo mismo. La semilla de la melodía
 * (#4F2A), cuántas veces se ha mutado y el ánimo del universo con el que sonó (ver music.h). La
 * música no usa gs->rng: tiene su propio PCG32 sembrado con esta semilla. */
typedef struct {
    uint16_t seed;
    uint8_t mut;
    uint8_t mood;
} MusicSave;

typedef struct {
    uint64_t seed;
    Rng rng;
    int64_t tick;            /* último macro tick procesado (segundos locales / LLV_TICK_SECONDS) */
    int64_t day_key;         /* día del llavero en curso (empieza a las 05:00) */
    int64_t last_seen;       /* segundos locales de la última vez que se miró */

    uint8_t bag[LLV_BAG_SIZE];
    uint8_t bag_count;
    uint8_t prepared;        /* objeto preparado desde el Cobertizo (fase 2) */
    uint16_t refill_left;    /* ticks hasta el próximo objeto */

    uint8_t actions_used[LLV_WORLD_COUNT];
    uint8_t current_world;
    uint8_t reloj_style;

    uint32_t looks_today;
    uint32_t looks_total;

    /* Jardín (fase 2) */
    Entity ents[LLV_GARDEN_MAX];
    uint8_t weather;         /* Weather */
    uint8_t sky;             /* SkyEvent */
    uint16_t weather_left;   /* ticks que dura el clima actual */
    uint8_t sky_left;
    uint16_t collection;     /* bitset de CollSlot */

    Pet pet;

    DiaryEntry diary[LLV_DIARY_SIZE];
    uint8_t diary_head;      /* próxima posición a escribir */
    uint8_t diary_count;
    uint32_t diary_total;    /* eventos anotados en toda la vida (no se pisa) */

    /* Fase 3 */
    uint8_t flags;           /* SF_* */
    uint8_t news;            /* bit por mundo con novedades (el ❗ del HUD) */
    uint8_t rare_unseen;     /* sucesos raros mientras no mirabas: enciende el LED */
    uint8_t unseen_ticks;    /* ticks dormido, hasta el tope del bono de paciencia */
    uint16_t gap_min;        /* minutos desde la mirada anterior, al despertar (saturado) */
    uint16_t last_look_day;  /* día de la última mirada (para la racha) */
    uint8_t streak;          /* días seguidos mirando */
    Sala sala;
    Director dir;
    Habits hab;

    /* Fase 4 */
    MusicSave mus;
} GameState;

void state_init(GameState *gs, uint64_t seed, int64_t now);

/* Un macro tick del universo (recarga de bolsa, cambio de día). */
void state_tick(GameState *gs);

void state_log(GameState *gs, EventKind kind, uint8_t a);
/* Texto corto (cabe en ~14 letras) para el diario. false si ese evento no se muestra. */
int state_event_text(char *buf, size_t n, const DiaryEntry *d);
/* Añade un objeto a la bolsa si cabe. true si entró. */
int state_bag_add(GameState *gs, ItemKind item);
/* Apunta un descubrimiento en la colección (y lo anota). true si era nuevo. */
int state_collect(GameState *gs, CollSlot slot);
const char *state_coll_name(CollSlot slot);
/* i = 0 es el evento más reciente. NULL si no existe. */
const DiaryEntry *state_diary_get(const GameState *gs, int i);

/* Quita de los sucesos "frescos" el bit de este tipo (ya se comentó). */
void state_fresh_clear(GameState *gs, EventKind kind);
/* Sucesos con novedades sin comentar (hoy o ayer). */
uint64_t state_fresh(const GameState *gs);
/* Mundo al que pertenece un suceso del diario (-1: a ninguno) y si cuenta como "raro". */
int state_event_world(EventKind kind);
int state_event_is_rare(EventKind kind);

/* Saca un objeto de la bolsa (el preparado si lo hay; si no, uno al azar). */
ItemKind state_bag_take(GameState *gs);

const char *state_item_name(ItemKind k);
/* Un objeto al azar con los pesos de la recarga de la bolsa (spec §3.1). */
ItemKind state_random_item(Rng *r);

/* Guardado portable: little-endian, con cabecera y CRC32. El estado completo debe
 * quedarse por debajo de 4 KB (spec §10); este tope se puede ajustar al compilar. */
#ifndef LLV_SAVE_MAX
#define LLV_SAVE_MAX 2048
#endif
size_t state_serialize(const GameState *gs, uint8_t *buf, size_t cap);
/* Devuelve 0 si el guardado es válido y lo carga en gs; distinto de 0 si no. */
int state_deserialize(GameState *gs, const uint8_t *buf, size_t len);

uint32_t state_crc32(const uint8_t *data, size_t len);

#endif
