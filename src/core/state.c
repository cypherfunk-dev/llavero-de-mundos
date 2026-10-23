#include "state.h"

#include <string.h>

#include <stdio.h>

#include "garden.h"
#include "habits.h"
#include "phrases.h"
#include "timeutil.h"

#define SAVE_MAGIC 0x31564C4Cu /* "LLV1" */
#define SAVE_VERSION 3         /* v1 (fase 0) y v2 (fase 2) no tenían la Sala ni el Director: se rechazan */

/* Peso de cada objeto al recargar la bolsa (spec §3.1). */
static const uint8_t ITEM_WEIGHT[ITEM_COUNT] = {
    [ITEM_NUBE] = 25, [ITEM_VIENTO] = 25, [ITEM_SEMILLA] = 25,
    [ITEM_ALPISTE] = 10, [ITEM_LOMBRIZ] = 12, [ITEM_DESEO] = 3,
};

static const char *const ITEM_NAME[ITEM_COUNT] = {
    [ITEM_NONE] = "", [ITEM_NUBE] = "Nube", [ITEM_VIENTO] = "Viento",
    [ITEM_SEMILLA] = "Semilla", [ITEM_ALPISTE] = "Alpiste",
    [ITEM_LOMBRIZ] = "Lombriz", [ITEM_DESEO] = "Deseo",
};

const char *state_item_name(ItemKind k)
{
    return (k > ITEM_NONE && k < ITEM_COUNT) ? ITEM_NAME[k] : "";
}

void state_init(GameState *gs, uint64_t seed, int64_t now)
{
    memset(gs, 0, sizeof(*gs));
    gs->seed = seed;
    rng_seed(&gs->rng, seed, 0x4C4C56u);
    gs->tick = time_floor_div(now, LLV_TICK_SECONDS);
    gs->day_key = time_day_key(gs->tick * LLV_TICK_SECONDS);
    gs->last_seen = now;
    gs->refill_left = LLV_BAG_REFILL_TICKS;
    /* Se empieza con algo en la bolsa para no recibir al usuario con las manos vacías. */
    gs->bag[0] = ITEM_NUBE;
    gs->bag[1] = ITEM_SEMILLA;
    gs->bag_count = 2;
    garden_init(gs);
    /* La Sala: todos empiezan con afinidad neutra, los hábitos activados y nada preguntado. */
    for (int i = 0; i < SPK_COUNT; i++)
        gs->sala.affinity[i] = LLV_AFF_START;
    gs->hab.enabled = (uint8_t)((1u << HAB_COUNT) - 1u);
    gs->dir.last_surprise_day = (uint16_t)gs->day_key;
    gs->last_look_day = (uint16_t)gs->day_key;
    gs->hab.steps_last = 0xFFFFFFFFu;     /* aún no hay lectura del podómetro */
}

/* Cada suceso del diario avisa al mundo al que pertenece (el ❗ del HUD) y queda "fresco"
 * hasta que un personaje lo comenta. */
int state_event_world(EventKind kind)
{
    switch (kind) {
    case EV_APPLE_FALL: case EV_APPLE_ROT: case EV_SPROUT: case EV_TREE_ADULT: case EV_MUSHROOM:
    case EV_BLOOM: case EV_BIRD_SEED: case EV_FROG: case EV_RAINBOW: case EV_STAR: case EV_COMPOST:
        return WORLD_JARDIN;
    case EV_PET_STAGE: case EV_PET_GEN: case EV_PET_WORD: case EV_PET_SPORES: case EV_PET_COMPOST:
        return WORLD_COBERTIZO;
    case EV_VISITOR:
        return WORLD_SALA;
    default:
        return -1;
    }
}

/* Lo "raro" que enciende el LED si pasa mientras duermes (spec §3.2). */
int state_event_is_rare(EventKind kind)
{
    return kind == EV_RAINBOW || kind == EV_STAR || kind == EV_FROG || kind == EV_BLOOM ||
           kind == EV_PET_GEN || kind == EV_PATIENCE;
}

uint64_t state_fresh(const GameState *gs)
{
    return gs->sala.fresh_a | gs->sala.fresh_b;
}

void state_fresh_clear(GameState *gs, EventKind kind)
{
    gs->sala.fresh_a &= ~(1ull << kind);
    gs->sala.fresh_b &= ~(1ull << kind);
}

void state_log(GameState *gs, EventKind kind, uint8_t a)
{
    DiaryEntry *e = &gs->diary[gs->diary_head];
    e->tick = gs->tick;
    e->kind = (uint8_t)kind;
    e->a = a;
    gs->diary_head = (uint8_t)((gs->diary_head + 1) % LLV_DIARY_SIZE);
    if (gs->diary_count < LLV_DIARY_SIZE)
        gs->diary_count++;
    gs->diary_total++;

    if (kind > EV_NONE && kind < EV_KIND_COUNT) {
        gs->sala.fresh_a |= 1ull << kind;
        int w = state_event_world(kind);
        if (w >= 0)
            gs->news |= (uint8_t)(1u << w);
        if ((gs->flags & SF_ASLEEP) && state_event_is_rare(kind) && gs->rare_unseen < 255)
            gs->rare_unseen++;
    }
}

int state_bag_add(GameState *gs, ItemKind item)
{
    if (item <= ITEM_NONE || item >= ITEM_COUNT || gs->bag_count >= LLV_BAG_SIZE)
        return 0;
    gs->bag[gs->bag_count++] = (uint8_t)item;
    return 1;
}

static const char *const COLL_NAME[COLL_COUNT] = {
    [COLL_MANZANA] = "Manzana", [COLL_HONGO] = "Hongo", [COLL_FLOR] = "Flor",
    [COLL_FLOR_RARA] = "Flor rara", [COLL_ABEJA] = "Abeja", [COLL_PAJARO] = "Pájaro",
    [COLL_LOMBRIZ] = "Lombriz", [COLL_RANA] = "Rana", [COLL_ESTRELLA] = "Estrella fugaz",
    [COLL_ARCOIRIS] = "Arcoíris",
    [COLL_PEZ0] = "Pez", [COLL_PEZ1] = "Pez", [COLL_PEZ2] = "Pez", [COLL_PEZ3] = "Pez",
    [COLL_PEZ4] = "Pez", [COLL_PEZ5] = "Pez",
};

const char *state_coll_name(CollSlot slot)
{
    return slot < COLL_COUNT ? COLL_NAME[slot] : "";
}

int state_collect(GameState *gs, CollSlot slot)
{
    if (slot >= COLL_COUNT || (gs->collection & (1u << slot)))
        return 0;
    gs->collection = (uint16_t)(gs->collection | (1u << slot));
    state_log(gs, EV_DISCOVER, (uint8_t)slot);
    return 1;
}

static const char *const PET_SHORT[PET_COUNT] = {"", "Hongo", "Semilla", "Piedra", "Compostera"};

/* Textos del diario: caben en ~14 letras para dejar sitio a "hace 3 h". */
int state_event_text(char *buf, size_t n, const DiaryEntry *d)
{
    const char *txt = NULL;
    switch ((EventKind)d->kind) {
    case EV_BAG_REFILL: snprintf(buf, n, "+1 %s", state_item_name(d->a)); return 1;
    case EV_BAG_FULL_LOST: snprintf(buf, n, "%s: no cupo", state_item_name(d->a)); return 1;
    case EV_NEW_DAY: txt = "Nuevo día"; break;
    case EV_WAKE:
        if (d->a == 0)
            return 0; /* siestas de minutos: ruido */
        snprintf(buf, n, "Dormí %uh", (unsigned)d->a);
        return 1;
    case EV_ITEM_USED: snprintf(buf, n, "Usaste %s", state_item_name(d->a)); return 1;
    case EV_APPLE_FALL: txt = "Cae manzana"; break;
    case EV_APPLE_ROT: txt = "Fruta podrida"; break;
    case EV_SPROUT: txt = "Nace un brote"; break;
    case EV_TREE_ADULT: txt = "Crece un árbol"; break;
    case EV_MUSHROOM: txt = "Sale un hongo"; break;
    case EV_BLOOM: txt = "Flor rara"; break;
    case EV_BIRD: txt = "Llega pájaro"; break;
    case EV_BIRD_SEED: txt = "Deja semilla"; break;
    case EV_BEE: txt = "Pasa una abeja"; break;
    case EV_WORM: txt = "Sale lombriz"; break;
    case EV_FROG: txt = "Llega una rana"; break;
    case EV_RAIN: txt = d->a ? "Nube: llueve" : "Llueve"; break;
    case EV_WIND: txt = "Viento"; break;
    case EV_SNOW: txt = "Nieva"; break;
    case EV_RAINBOW: txt = "Arcoíris"; break;
    case EV_STAR: txt = "Estrella fugaz"; break;
    case EV_COMPOST: txt = "Abono"; break;
    case EV_DISCOVER: snprintf(buf, n, "Nuevo: %s", state_coll_name((CollSlot)d->a)); return 1;
    case EV_PET_CHOSEN:
        snprintf(buf, n, "Llega %s", d->a < PET_COUNT ? PET_SHORT[d->a] : "");
        return 1;
    case EV_PET_STAGE: txt = "Mascota crece"; break;
    case EV_PET_GEN: snprintf(buf, n, "Nace Gen %u", (unsigned)d->a); return 1;
    case EV_PET_CARE: txt = "Cuidas mascota"; break;
    case EV_PET_WORD: txt = "Dice algo…"; break;
    case EV_PET_SPORES: txt = "Esporas"; break;
    case EV_PET_COMPOST: txt = "Come manzana"; break;
    case EV_VISITOR: snprintf(buf, n, "Visita: %s", speaker_short(d->a)); return 1;
    case EV_HABIT: snprintf(buf, n, "Cuidado: %s", habit_label((int)d->a)); return 1;
    case EV_WALK: txt = "Paseo largo"; break;
    case EV_PATIENCE: txt = "Algo raro…"; break;
    case EV_NONE:
    case EV_KIND_COUNT:
    default: return 0;
    }
    snprintf(buf, n, "%s", txt);
    return 1;
}

const DiaryEntry *state_diary_get(const GameState *gs, int i)
{
    if (i < 0 || i >= gs->diary_count)
        return NULL;
    int idx = (gs->diary_head - 1 - i + LLV_DIARY_SIZE * 2) % LLV_DIARY_SIZE;
    return &gs->diary[idx];
}

ItemKind state_random_item(Rng *r)
{
    uint32_t total = 0;
    for (int k = 0; k < ITEM_COUNT; k++)
        total += ITEM_WEIGHT[k];
    uint32_t roll = rng_below(r, total);
    for (int k = 0; k < ITEM_COUNT; k++) {
        if (roll < ITEM_WEIGHT[k])
            return (ItemKind)k;
        roll -= ITEM_WEIGHT[k];
    }
    return ITEM_NUBE;
}

void state_tick(GameState *gs)
{
    gs->tick++;

    int64_t day = time_day_key(gs->tick * LLV_TICK_SECONDS);
    if (day != gs->day_key) {
        gs->day_key = day;
        memset(gs->actions_used, 0, sizeof(gs->actions_used));
        /* Un día sin mirarlo enfría un poco el cariño (solo el tono, nunca un castigo). */
        if (gs->looks_today == 0)
            for (int i = 0; i < SPK_COUNT; i++)
                gs->sala.affinity[i] = (uint8_t)(gs->sala.affinity[i] > LLV_AFF_DECAY ?
                                                 gs->sala.affinity[i] - LLV_AFF_DECAY : 0);
        gs->looks_today = 0;
        gs->sala.fresh_b = gs->sala.fresh_a; /* los sucesos viven hoy y mañana; luego se olvidan */
        gs->sala.fresh_a = 0;
        habits_new_day(gs);
        state_log(gs, EV_NEW_DAY, 0);
    }

    if (--gs->refill_left == 0) {
        gs->refill_left = LLV_BAG_REFILL_TICKS;
        ItemKind item = state_random_item(&gs->rng);
        if (gs->bag_count < LLV_BAG_SIZE) {
            gs->bag[gs->bag_count++] = (uint8_t)item;
            state_log(gs, EV_BAG_REFILL, (uint8_t)item);
        } else {
            state_log(gs, EV_BAG_FULL_LOST, (uint8_t)item);
        }
    }
}

ItemKind state_bag_take(GameState *gs)
{
    if (gs->bag_count == 0)
        return ITEM_NONE;
    int pick = -1;
    if (gs->prepared != ITEM_NONE) {
        for (int i = 0; i < gs->bag_count; i++) {
            if (gs->bag[i] == gs->prepared) {
                pick = i;
                break;
            }
        }
        gs->prepared = ITEM_NONE;
    }
    if (pick < 0)
        pick = (int)rng_below(&gs->rng, gs->bag_count);
    ItemKind item = (ItemKind)gs->bag[pick];
    for (int i = pick; i + 1 < gs->bag_count; i++)
        gs->bag[i] = gs->bag[i + 1];
    gs->bag[--gs->bag_count] = ITEM_NONE;
    return item;
}

/* ---------- guardado ---------- */

uint32_t state_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return ~crc;
}

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t len;
    int err;
} Writer;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    int err;
} Reader;

static void w_bytes(Writer *w, uint64_t v, int n)
{
    if (w->len + (size_t)n > w->cap) {
        w->err = 1;
        return;
    }
    for (int i = 0; i < n; i++)
        w->buf[w->len++] = (uint8_t)(v >> (8 * i));
}

static uint64_t r_bytes(Reader *r, int n)
{
    if (r->pos + (size_t)n > r->len) {
        r->err = 1;
        return 0;
    }
    uint64_t v = 0;
    for (int i = 0; i < n; i++)
        v |= (uint64_t)r->buf[r->pos++] << (8 * i);
    return v;
}

/* Un único listado de campos para escribir y leer: imposible que se desincronicen.
 * X(campo, bytes) para escalares; EACH recorre un array de estructuras con su propio listado. */
#define EACH(arr, count, FIELDS, X)                        \
    for (int i_ = 0; i_ < (count); i_++) {                 \
        FIELDS(X, &(arr)[i_])                              \
    }
#define BYTES(arr, count, X) EACH(arr, count, BYTE_FIELD, X)
#define BYTE_FIELD(X, p) X(*(p), 1)
#define ENT_FIELDS(X, e) X((e)->sp, 1) X((e)->st, 1) X((e)->cell, 1) X((e)->left, 2)
#define DIARY_FIELDS(X, e) X((e)->tick, 8) X((e)->kind, 1) X((e)->a, 1)
#define U16_FIELD(X, p) X(*(p), 2)
#define U32_FIELD(X, p) X(*(p), 4)
#define SALA_FIELDS(X, s)                                                              \
    X((s)->visitor, 1) X((s)->visitor_left, 1) X((s)->stage, 1) X((s)->who, 1)         \
    X((s)->phrase, 2) EACH((s)->fill, 3, U16_FIELD, X)                                 \
    X((s)->scene, 1) X((s)->scene_line, 1) X((s)->q, 1)                                \
    X((s)->fresh_a, 8) X((s)->fresh_b, 8)                                              \
    EACH((s)->recent, LLV_RECENT, U32_FIELD, X) X((s)->recent_head, 1)                 \
    BYTES((s)->affinity, SPK_COUNT, X) BYTES((s)->memo, LLV_Q_MAX, X)                  \
    EACH((s)->memo_day, LLV_Q_MAX, U16_FIELD, X)
#define DIR_FIELDS(X, d)                                                               \
    X((d)->last, 1) X((d)->cur, 1) X((d)->arg, 1) X((d)->armed, 1)                     \
    X((d)->last_surprise_day, 2) X((d)->rolls, 2) X((d)->surprises, 2)
#define HAB_FIELDS(X, h)                                                               \
    X((h)->enabled, 1) X((h)->done_today, 1) X((h)->asked_today, 1) X((h)->asked_count, 1) \
    X((h)->night_look, 1) X((h)->night_look_prev, 1) X((h)->sleep_streak, 1)           \
    X((h)->walk_rewarded, 1) X((h)->steps_last, 4) X((h)->steps_today, 2)
#define PET_FIELDS(X, p)                                   \
    X((p)->kind, 1) X((p)->gen, 1) X((p)->age, 2) X((p)->happy, 1) X((p)->thought, 1) \
    X((p)->word_left, 2) X((p)->task_left, 2) X((p)->stage_seen, 1)

#define SAVE_FIELDS(X, s)                                  \
    X((s)->seed, 8)                                        \
    X((s)->rng.state, 8)                                   \
    X((s)->rng.inc, 8)                                     \
    X((s)->tick, 8)                                        \
    X((s)->day_key, 8)                                     \
    X((s)->last_seen, 8)                                   \
    X((s)->bag_count, 1)                                   \
    BYTES((s)->bag, LLV_BAG_SIZE, X)                       \
    X((s)->prepared, 1)                                    \
    X((s)->refill_left, 2)                                 \
    BYTES((s)->actions_used, LLV_WORLD_COUNT, X)           \
    X((s)->current_world, 1)                               \
    X((s)->reloj_style, 1)                                 \
    X((s)->looks_today, 4)                                 \
    X((s)->looks_total, 4)                                 \
    EACH((s)->ents, LLV_GARDEN_MAX, ENT_FIELDS, X)         \
    X((s)->weather, 1)                                     \
    X((s)->sky, 1)                                         \
    X((s)->weather_left, 2)                                \
    X((s)->sky_left, 1)                                    \
    X((s)->collection, 2)                                  \
    PET_FIELDS(X, &(s)->pet)                               \
    EACH((s)->diary, LLV_DIARY_SIZE, DIARY_FIELDS, X)      \
    X((s)->diary_head, 1)                                  \
    X((s)->diary_count, 1)                                 \
    X((s)->diary_total, 4)                                 \
    X((s)->flags, 1)                                       \
    X((s)->news, 1)                                        \
    X((s)->rare_unseen, 1)                                 \
    X((s)->unseen_ticks, 1)                                \
    X((s)->gap_min, 2)                                     \
    X((s)->last_look_day, 2)                               \
    X((s)->streak, 1)                                      \
    SALA_FIELDS(X, &(s)->sala)                             \
    DIR_FIELDS(X, &(s)->dir)                               \
    HAB_FIELDS(X, &(s)->hab)

size_t state_serialize(const GameState *gs, uint8_t *buf, size_t cap)
{
    Writer w = {buf, cap, 0, 0};
    w_bytes(&w, SAVE_MAGIC, 4);
    w_bytes(&w, SAVE_VERSION, 2);
    w_bytes(&w, 0, 2); /* longitud del payload, se rellena al final */
#define WRITE_FIELD(f, n) w_bytes(&w, (uint64_t)(f), n);
    SAVE_FIELDS(WRITE_FIELD, gs)
#undef WRITE_FIELD
    if (w.err)
        return 0;
    size_t payload = w.len - 8;
    buf[6] = (uint8_t)payload;
    buf[7] = (uint8_t)(payload >> 8);
    w_bytes(&w, state_crc32(buf, w.len), 4);
    return w.err ? 0 : w.len;
}

/* Comprueba las invariantes: un guardado con CRC bueno pero valores absurdos no entra. */
static int state_valid(const GameState *t)
{
    if (t->bag_count > LLV_BAG_SIZE || t->diary_count > LLV_DIARY_SIZE ||
        t->diary_head >= LLV_DIARY_SIZE || t->current_world >= LLV_WORLD_COUNT ||
        t->refill_left == 0 || t->refill_left > LLV_BAG_REFILL_TICKS ||
        t->prepared >= ITEM_COUNT || t->weather > W_SNOW || t->sky > SKY_RAINBOW ||
        t->pet.kind >= PET_COUNT || t->pet.age >= LLV_PET_CYCLE_TICKS ||
        (t->pet.kind != PET_NONE && t->pet.gen == 0))
        return 0;
    const Sala *sa = &t->sala;
    if (sa->visitor >= SPK_COUNT || sa->who >= SPK_COUNT || sa->stage > DLG_ASK ||
        sa->q > Q_COUNT || sa->recent_head >= LLV_RECENT || sa->scene_line > 6 ||
        (sa->stage != DLG_IDLE && sa->phrase > phrase_count()) || t->dir.last >= DK_COUNT ||
        t->dir.cur >= DS_COUNT || t->dir.arg >= 255 || t->hab.enabled >= (1u << HAB_COUNT) ||
        t->hab.done_today >= (1u << HAB_COUNT) || t->hab.asked_today >= (1u << HAB_COUNT) ||
        t->hab.asked_count > LLV_HABIT_MAX_PER_DAY || t->news >= (1u << LLV_WORLD_COUNT) ||
        (t->flags & ~(SF_ASLEEP | SF_GRANDMA)) != 0)
        return 0;
    for (int i = 0; i < LLV_Q_MAX; i++)
        if ((sa->memo[i] & 3) == 3 || sa->memo[i] > 7 || (i >= Q_COUNT && (sa->memo[i] || sa->memo_day[i])))
            return 0;
    for (int i = 0; i < LLV_BAG_SIZE; i++)
        if (t->bag[i] >= ITEM_COUNT || (i < t->bag_count) != (t->bag[i] != ITEM_NONE))
            return 0;
    uint64_t used = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++) {
        const Entity *e = &t->ents[i];
        if (e->sp == SP_NONE)
            continue;
        if (e->sp >= SP_COUNT || e->st >= GARDEN_STAGES || e->cell >= LLV_GARDEN_CELLS ||
            (used >> e->cell & 1))
            return 0;
        used |= 1ull << e->cell;
    }
    return 1;
}

int state_deserialize(GameState *gs, const uint8_t *buf, size_t len)
{
    if (len < 12)
        return 1;
    uint32_t stored = (uint32_t)buf[len - 4] | (uint32_t)buf[len - 3] << 8 |
                      (uint32_t)buf[len - 2] << 16 | (uint32_t)buf[len - 1] << 24;
    if (state_crc32(buf, len - 4) != stored)
        return 2;

    Reader r = {buf, len - 4, 0, 0};
    if (r_bytes(&r, 4) != SAVE_MAGIC || r_bytes(&r, 2) != SAVE_VERSION)
        return 3;
    if (r_bytes(&r, 2) != len - 12)
        return 4;

    GameState tmp;
    memset(&tmp, 0, sizeof(tmp));
#define READ_FIELD(f, n) f = r_bytes(&r, n); /* conversión implícita: sin extensiones GNU */
    SAVE_FIELDS(READ_FIELD, &tmp)
#undef READ_FIELD
    if (r.err || r.pos != r.len)
        return 5;
    if (!state_valid(&tmp))
        return 6;
    *gs = tmp;
    return 0;
}
