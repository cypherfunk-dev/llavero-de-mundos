#include "fish.h"

#include <string.h>

#include "garden.h"
#include "pet.h"
#include "timeutil.h"

const FishInfo *fish_info(int idx)
{
    return idx >= 0 && idx < FISH_TOTAL ? &FISH_TABLE[idx] : NULL;
}

const char *fish_name(int idx)
{
    return idx >= 0 && idx < FISH_TOTAL ? FISH_TABLE[idx].name : "";
}

const char *fish_temper_name(int temper)
{
    static const char *const N[FT_COUNT] = {"tranquilo", "vago", "saltarín", "caprichoso", "fugaz"};
    return temper >= 0 && temper < FT_COUNT ? N[temper] : "";
}

int fish_ctx(const GameState *gs)
{
    int64_t t = gs->tick * LLV_TICK_SECONDS;
    CivilTime c = time_to_civil(t);
    int m = c.hour * 60 + c.minute, ctx = 0;
    ctx |= (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) ? FC_DAY : FC_NIGHT;
    if (gs->weather == W_RAIN)
        ctx |= FC_RAIN;
    if (gs->weather == W_SNOW)
        ctx |= FC_SNOW;
    if (gs->weather == W_WIND)
        ctx |= FC_WIND;
    if (time_moon_phase(t) == 4)
        ctx |= FC_FULLMOON;
    if (time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE) == SEASON_WINTER)
        ctx |= FC_WINTER;
    return ctx;
}

int fish_season(const GameState *gs)
{
    CivilTime c = time_to_civil(gs->tick * LLV_TICK_SECONDS);
    return time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE);
}

int fish_weight(const FishInfo *f, int ctx, int season)
{
    if ((f->need & ctx) != f->need || (f->forbid & ctx) || !(f->seasons & (1u << season)))
        return 0;
    int w = f->weight;
    if (f->boost_if & ctx)
        w = w * f->boost_pct / 100;
    /* "Llueve, mejores peces": la lluvia aleja las basuras y atrae a los raros. */
    if (ctx & FC_RAIN) {
        if (f->junk)
            w = w / 2;
        else if (f->weight <= 60)
            w = w * 3 / 2;
    }
    return w < 1 ? 1 : w;
}

int fish_pick(int ctx, int season, Rng *r)
{
    uint32_t total = 0;
    for (int i = 0; i < FISH_TOTAL; i++)
        total += (uint32_t)fish_weight(&FISH_TABLE[i], ctx, season);
    if (total == 0)
        return 0;
    uint32_t roll = rng_below(r, total);
    for (int i = 0; i < FISH_TOTAL; i++) {
        uint32_t w = (uint32_t)fish_weight(&FISH_TABLE[i], ctx, season);
        if (roll < w)
            return i;
        roll -= w;
    }
    return 0;
}

int fish_size(int idx, Rng *r)
{
    const FishInfo *f = fish_info(idx);
    if (!f || f->junk || f->smax <= f->smin)
        return f ? f->smin : 0;
    return f->smin + (int)rng_below(r, (uint32_t)(f->smax - f->smin + 1));
}

/* ---------- lanzar ---------- */

void fish_plan(FishPlan *p, uint32_t seed, int ctx, int season)
{
    Rng r;
    rng_seed(&r, seed, 0xF15);
    p->idx = (uint8_t)fish_pick(ctx, season, &r);
    p->size = (uint8_t)fish_size(p->idx, &r);
    p->wait_ms = (uint16_t)(1200 + rng_below(&r, 2200));
    p->game_seed = rng_next(&r);
}

CastResult fish_cast(GameState *gs, int widx, FishPlan *plan, bool *used_prepared)
{
    if (gs->actions_used[widx] >= LLV_PESCA_ACTIONS)
        return CAST_NO_ACTIONS;
    int pick = -1;
    bool prep = false;
    if (gs->prepared == ITEM_LOMBRIZ) /* la preparada cuenta si es lombriz */
        for (int i = 0; i < gs->bag_count && pick < 0; i++)
            if (gs->bag[i] == ITEM_LOMBRIZ) {
                pick = i;
                prep = true;
            }
    for (int i = 0; i < gs->bag_count && pick < 0; i++)
        if (gs->bag[i] == ITEM_LOMBRIZ)
            pick = i;
    if (pick < 0)
        return CAST_NO_WORM;
    for (int i = pick; i + 1 < gs->bag_count; i++)
        gs->bag[i] = gs->bag[i + 1];
    gs->bag[--gs->bag_count] = ITEM_NONE;
    if (gs->prepared == ITEM_LOMBRIZ) /* si estaba preparada, ya se usó; otra cosa preparada se queda */
        gs->prepared = ITEM_NONE;
    gs->actions_used[widx]++;
    if (gs->fish.casts < 65535)
        gs->fish.casts++;
    state_log(gs, EV_CAST, 0);
    /* El único número que el minijuego saca del universo: todo lo demás sale de esta semilla. */
    uint32_t seed = rng_next(&gs->rng);
    fish_plan(plan, seed, fish_ctx(gs), fish_season(gs));
    if (used_prepared)
        *used_prepared = prep;
    return CAST_OK;
}

/* ---------- resolver ---------- */

int fish_collected(const GameState *gs)
{
    int n = 0;
    for (int i = 0; i < FISH_SPECIES; i++)
        n += (gs->collection >> (COLL_PEZ0 + i)) & 1;
    return n;
}

int fish_total_caught(const GameState *gs)
{
    int n = 0;
    for (int i = 0; i < FISH_SPECIES; i++)
        n += gs->fish.caught[i];
    return n;
}

static void sat_inc(uint8_t *v)
{
    if (*v < 255)
        ++*v;
}

void fish_resolve(GameState *gs, const FishPlan *plan, bool won, FishResult *out)
{
    FishResult res;
    memset(&res, 0, sizeof(res));
    const FishInfo *f = fish_info(plan->idx);
    res.won = won;
    if (!f) {
        if (out)
            *out = res;
        return;
    }
    Fishing *fs = &gs->fish;
    if (!won) {
        sat_inc(&fs->fled);
        state_log(gs, EV_FISH_LOST, plan->idx);
        if (out)
            *out = res;
        return;
    }
    if (f->junk) {
        sat_inc(&fs->junk);
        state_log(gs, EV_FISH_JUNK, (uint8_t)(plan->idx - FISH_SPECIES));
    } else {
        int i = plan->idx;
        uint8_t old = fs->best[i];
        sat_inc(&fs->caught[i]);
        if (plan->size > old) {
            fs->best[i] = plan->size;
            res.record = old != 0;
        }
        res.legend = i == FISH_LEGEND;
        state_log(gs, res.legend ? EV_FISH_LEGEND : EV_FISH, (uint8_t)i);
        res.new_species = state_collect(gs, (CollSlot)(COLL_PEZ0 + i)) != 0;
        res.complete = res.new_species && fish_collected(gs) == FISH_SPECIES;
    }
    /* Lo barato que deja pescar (universo compartido): un objeto, una alegría o restos para la compostera. */
    if (f->gift_item != ITEM_NONE && state_bag_add(gs, (ItemKind)f->gift_item)) {
        state_log(gs, EV_BAG_REFILL, f->gift_item);
        res.gift_item = f->gift_item;
    }
    if ((f->gift & FG_JOY) && gs->pet.kind != PET_NONE) {
        pet_cheer(gs, 60);
        res.joy = true;
    }
    if ((f->gift & FG_COMPOST) && gs->pet.kind == PET_COMPOSTERA) {
        pet_cheer(gs, 40);
        if (state_bag_add(gs, ITEM_LOMBRIZ))
            state_log(gs, EV_BAG_REFILL, ITEM_LOMBRIZ);
        res.compost = true;
    }
    if (out)
        *out = res;
}

/* ---------- el minijuego ---------- */

#define FG_METER_START 300

typedef struct {
    int8_t vmax, acc;
    uint8_t hold_lo, hold_hi;
    int16_t jump;       /* máximo salto al elegir objetivo (0 = a cualquier sitio) */
    int8_t jitter;      /* impulsos al azar por paso */
    int16_t far_min;    /* el objetivo nuevo está al menos a esta distancia */
} Temper;

static const Temper TEMPER[FT_COUNT] = {
    [FT_CALM] = {3, 1, 50, 110, 0, 0, 0},
    [FT_LAZY] = {2, 1, 90, 200, 150, 0, 0},
    [FT_JUMPY] = {6, 3, 14, 36, 0, 0, 0},
    [FT_ERRATIC] = {5, 2, 18, 60, 0, 2, 0},
    [FT_DARTER] = {9, 4, 40, 90, 0, 0, 140},
};

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void fishgame_init(FishGame *g, uint32_t seed, int temper, int diff)
{
    memset(g, 0, sizeof(*g));
    rng_seed(&g->rng, seed, 0xF1);
    g->temper = (uint8_t)clampi(temper, 0, FT_COUNT - 1);
    g->diff = (uint8_t)clampi(diff, 1, 10);
    g->zone = (int16_t)(FISHGAME_ZONE0 - g->diff * 8);
    g->bar = (int16_t)(g->zone / 2);
    g->fpos = (int16_t)((FISHGAME_MIN + FISHGAME_MAX) / 2);
    g->target = g->fpos;
    g->meter = FG_METER_START;
    g->state = FGS_PLAYING;
}

bool fishgame_in_zone(const FishGame *g)
{
    int d = g->fpos - g->bar;
    return d >= -g->zone / 2 && d <= g->zone / 2;
}

static void fish_move(FishGame *g)
{
    const Temper *t = &TEMPER[g->temper];
    int vmax = t->vmax + g->diff / 3;
    if (g->target_left == 0) {
        int tg;
        for (int tries = 0;; tries++) {
            if (t->jump)
                tg = g->fpos + (int)rng_below(&g->rng, (uint32_t)(2 * t->jump + 1)) - t->jump;
            else
                tg = FISHGAME_MIN + (int)rng_below(&g->rng, FISHGAME_MAX - FISHGAME_MIN + 1);
            tg = clampi(tg, FISHGAME_MIN, FISHGAME_MAX);
            int dist = tg > g->fpos ? tg - g->fpos : g->fpos - tg;
            if (dist >= t->far_min || tries >= 6)
                break;
        }
        g->target = (int16_t)tg;
        int hi = t->hold_hi - g->diff * 2;
        if (hi < t->hold_lo)
            hi = t->hold_lo;
        g->target_left = (uint16_t)(t->hold_lo + (int)rng_below(&g->rng, (uint32_t)(hi - t->hold_lo + 1)));
    }
    g->target_left--;
    int want = clampi((g->target - g->fpos) / 6, -vmax, vmax);
    g->fvel = (int16_t)(g->fvel + clampi(want - g->fvel, -t->acc, t->acc));
    if (t->jitter && rng_below(&g->rng, 4) == 0)
        g->fvel = (int16_t)(g->fvel + (int)rng_below(&g->rng, (uint32_t)(2 * t->jitter + 1)) - t->jitter);
    g->fvel = (int16_t)clampi(g->fvel, -vmax, vmax);
    int p = g->fpos + g->fvel;
    if (p < FISHGAME_MIN || p > FISHGAME_MAX) {
        p = clampi(p, FISHGAME_MIN, FISHGAME_MAX);
        g->fvel = 0;
    }
    g->fpos = (int16_t)p;
}

int fishgame_step(FishGame *g, bool hold)
{
    if (g->state != FGS_PLAYING)
        return g->state;
    /* la barra: sube al mantener A, cae al soltar */
    g->bvel = (int16_t)clampi(g->bvel + (hold ? 5 : -3), -24, 18);
    int lo = g->zone / 2, hi = 1000 - g->zone / 2, b = g->bar + g->bvel;
    if (b < lo) {
        b = lo;
        g->bvel = g->bvel < -6 ? (int16_t)(-g->bvel / 3) : 0; /* un rebote pequeño en el fondo */
    } else if (b > hi) {
        b = hi;
        g->bvel = 0;
    }
    g->bar = (int16_t)b;
    fish_move(g);
    /* el medidor: sube dentro de la zona, baja fuera */
    g->meter = (int16_t)(g->meter + (fishgame_in_zone(g) ? 8 : -(3 + g->diff / 2)));
    if (g->steps < 65535)
        g->steps++;
    if (g->meter >= 1000) {
        g->meter = 1000;
        g->state = FGS_WON;
    } else if (g->meter <= 0) {
        g->meter = 0;
        g->state = FGS_LOST;
    } else if (g->steps >= LLV_FISH_TIMEOUT_STEPS) {
        g->state = FGS_LOST; /* se cansó de esperar */
    }
    return g->state;
}

bool fishgame_ai(const FishGame *g)
{
    /* Velocidad que querría la barra: acercarse al pez y acompañarlo; se acelera si queda por debajo. */
    int want = clampi((g->fpos - g->bar) / 4 + g->fvel, -24, 18);
    return g->bvel < want;
}
