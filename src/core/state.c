#include "state.h"

#include <string.h>

#include "timeutil.h"

#define SAVE_MAGIC 0x31564C4Cu /* "LLV1" */
#define SAVE_VERSION 1

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
}

const DiaryEntry *state_diary_get(const GameState *gs, int i)
{
    if (i < 0 || i >= gs->diary_count)
        return NULL;
    int idx = (gs->diary_head - 1 - i + LLV_DIARY_SIZE * 2) % LLV_DIARY_SIZE;
    return &gs->diary[idx];
}

static ItemKind random_item(Rng *r)
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
        gs->looks_today = 0;
        state_log(gs, EV_NEW_DAY, 0);
    }

    if (--gs->refill_left == 0) {
        gs->refill_left = LLV_BAG_REFILL_TICKS;
        ItemKind item = random_item(&gs->rng);
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

/* Un único listado de campos para escribir y leer: imposible que se desincronicen. */
#define SAVE_FIELDS(X, s)                                  \
    X((s)->seed, 8)                                        \
    X((s)->rng.state, 8)                                   \
    X((s)->rng.inc, 8)                                     \
    X((s)->tick, 8)                                        \
    X((s)->day_key, 8)                                     \
    X((s)->last_seen, 8)                                   \
    X((s)->bag_count, 1)                                   \
    X((s)->prepared, 1)                                    \
    X((s)->refill_left, 2)                                 \
    X((s)->current_world, 1)                               \
    X((s)->reloj_style, 1)                                 \
    X((s)->looks_today, 4)                                 \
    X((s)->looks_total, 4)                                 \
    X((s)->diary_head, 1)                                  \
    X((s)->diary_count, 1)

size_t state_serialize(const GameState *gs, uint8_t *buf, size_t cap)
{
    Writer w = {buf, cap, 0, 0};
    w_bytes(&w, SAVE_MAGIC, 4);
    w_bytes(&w, SAVE_VERSION, 2);
    w_bytes(&w, 0, 2); /* longitud del payload, se rellena al final */
#define WRITE_FIELD(f, n) w_bytes(&w, (uint64_t)(f), n);
    SAVE_FIELDS(WRITE_FIELD, gs)
#undef WRITE_FIELD
    for (int i = 0; i < LLV_BAG_SIZE; i++)
        w_bytes(&w, gs->bag[i], 1);
    for (int i = 0; i < LLV_WORLD_COUNT; i++)
        w_bytes(&w, gs->actions_used[i], 1);
    for (int i = 0; i < LLV_DIARY_SIZE; i++) {
        w_bytes(&w, (uint64_t)gs->diary[i].tick, 8);
        w_bytes(&w, gs->diary[i].kind, 1);
        w_bytes(&w, gs->diary[i].a, 1);
    }
    if (w.err)
        return 0;
    size_t payload = w.len - 8;
    buf[6] = (uint8_t)payload;
    buf[7] = (uint8_t)(payload >> 8);
    w_bytes(&w, state_crc32(buf, w.len), 4);
    return w.err ? 0 : w.len;
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
    for (int i = 0; i < LLV_BAG_SIZE; i++)
        tmp.bag[i] = (uint8_t)r_bytes(&r, 1);
    for (int i = 0; i < LLV_WORLD_COUNT; i++)
        tmp.actions_used[i] = (uint8_t)r_bytes(&r, 1);
    for (int i = 0; i < LLV_DIARY_SIZE; i++) {
        tmp.diary[i].tick = (int64_t)r_bytes(&r, 8);
        tmp.diary[i].kind = (uint8_t)r_bytes(&r, 1);
        tmp.diary[i].a = (uint8_t)r_bytes(&r, 1);
    }
    if (r.err || r.pos != r.len)
        return 5;
    if (tmp.bag_count > LLV_BAG_SIZE || tmp.diary_count > LLV_DIARY_SIZE ||
        tmp.diary_head >= LLV_DIARY_SIZE || tmp.current_world >= LLV_WORLD_COUNT ||
        tmp.refill_left == 0 || tmp.refill_left > LLV_BAG_REFILL_TICKS)
        return 6;
    *gs = tmp;
    return 0;
}
