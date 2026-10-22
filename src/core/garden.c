/* Motor genérico del jardín (spec §4.2, §10). Todo ocurre en macro ticks y usa solo
 * gs->rng y gs->tick: lo simulado en vivo y lo recalculado al despertar coinciden.
 * Las reglas están en garden_data.c; aquí no hay ninguna cadena escrita a mano. */
#include "garden.h"

#include <string.h>

#include "timeutil.h"

/* Entorno del tick actual. */
typedef struct {
    Season season;
    bool day;
} Env;

static Env make_env(const GameState *gs)
{
    CivilTime c = time_to_civil(gs->tick * LLV_TICK_SECONDS);
    int m = c.hour * 60 + c.minute;
    return (Env){time_season(c.month, c.day, LLV_SOUTH_HEMISPHERE),
                 m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN};
}

Season garden_season(const GameState *gs)
{
    return make_env(gs).season;
}

bool garden_is_day(const GameState *gs)
{
    return make_env(gs).day;
}

const GardenRule *garden_rule(uint8_t sp, uint8_t st)
{
    if (sp == SP_NONE || sp >= SP_COUNT || st >= GARDEN_STAGES)
        return NULL;
    const GardenRule *r = &GARDEN_RULES[sp][st];
    return r->name ? r : NULL;
}

/* ---------- consultas ---------- */

int garden_count(const GameState *gs)
{
    int n = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        n += gs->ents[i].sp != SP_NONE;
    return n;
}

int garden_count_sp(const GameState *gs, uint8_t sp, uint8_t st)
{
    int n = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        n += gs->ents[i].sp == sp && (st == ST_ANY || gs->ents[i].st == st);
    return n;
}

const Entity *garden_at(const GameState *gs, int cell)
{
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs->ents[i].sp != SP_NONE && gs->ents[i].cell == cell)
            return &gs->ents[i];
    return NULL;
}

static Entity *find_sp(GameState *gs, uint8_t sp, uint8_t st)
{
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs->ents[i].sp == sp && (st == ST_ANY || gs->ents[i].st == st))
            return &gs->ents[i];
    return NULL;
}

static uint64_t occupancy(const GameState *gs)
{
    uint64_t occ = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs->ents[i].sp != SP_NONE)
            occ |= 1ull << gs->ents[i].cell;
    return occ;
}

/* ---------- altas y bajas ---------- */

/* Casilla libre al azar; 'near' >= 0 prefiere las vecinas. -1 si no queda ninguna. */
static int pick_cell(GameState *gs, int near)
{
    uint64_t occ = occupancy(gs);
    int cand[LLV_GARDEN_CELLS], n = 0;
    if (near >= 0) {
        int nx = near % LLV_GARDEN_COLS, ny = near / LLV_GARDEN_COLS;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = nx + dx, y = ny + dy;
                if ((dx || dy) && x >= 0 && x < LLV_GARDEN_COLS && y >= 0 && y < LLV_GARDEN_ROWS) {
                    int c = y * LLV_GARDEN_COLS + x;
                    if (!(occ >> c & 1))
                        cand[n++] = c;
                }
            }
    }
    if (n == 0)
        for (int c = 0; c < LLV_GARDEN_CELLS; c++)
            if (!(occ >> c & 1))
                cand[n++] = c;
    return n ? cand[rng_below(&gs->rng, (uint32_t)n)] : -1;
}

/* ¿Cabe otra de esta especie? Los árboles (brote, joven, adulto) comparten tope. */
static bool species_ok(const GameState *gs, uint8_t newsp, uint8_t fromsp)
{
    if (newsp == fromsp)
        return true;
    const SpeciesInfo *in = &GARDEN_SPECIES[newsp];
    if (in->group && fromsp && GARDEN_SPECIES[fromsp].group == in->group)
        return true; /* crecer dentro de la misma línea no suma */
    int n = 0;
    for (int i = 0; i < LLV_GARDEN_MAX; i++) {
        uint8_t sp = gs->ents[i].sp;
        if (sp == SP_NONE)
            continue;
        n += in->group ? GARDEN_SPECIES[sp].group == in->group : sp == newsp;
    }
    return n < in->cap;
}

/* Cuenta atrás de una etapa nueva, con ±25 % para que no todo cambie a la vez. */
static uint16_t fresh_left(GameState *gs, uint8_t sp, uint8_t st)
{
    const GardenRule *r = garden_rule(sp, st);
    if (!r || r->dur == 0)
        return 0;
    uint32_t d = r->dur, var = d / 4;
    return (uint16_t)(d - var + rng_below(&gs->rng, 2 * var + 1));
}

static void discover_for(GameState *gs, uint8_t sp, uint8_t st)
{
    for (const GardenDiscover *d = GARDEN_DISCOVER; d->sp; d++)
        if (d->sp == sp && (d->st == ST_ANY || d->st == st))
            state_collect(gs, (CollSlot)d->slot);
}

static void set_entity(GameState *gs, Entity *e, uint8_t sp, uint8_t st, bool announce)
{
    e->sp = sp;
    e->st = st;
    e->left = fresh_left(gs, sp, st);
    if (announce)
        discover_for(gs, sp, st);
}

static Entity *place_at(GameState *gs, uint8_t sp, uint8_t st, int near, bool force, bool announce)
{
    if (sp == SP_NONE || sp >= SP_COUNT || !garden_rule(sp, st))
        return NULL;
    /* force ignora los topes de lo que se acaba solo, pero nunca el de los árboles: lo que
     * no se va por su cuenta tiene que respetar el límite o el jardín acabaría lleno. */
    if ((!force || GARDEN_SPECIES[sp].group) && !species_ok(gs, sp, SP_NONE))
        return NULL;
    Entity *slot = NULL;
    for (int i = 0; i < LLV_GARDEN_MAX && !slot; i++)
        if (gs->ents[i].sp == SP_NONE)
            slot = &gs->ents[i];
    if (!slot)
        return NULL; /* lleno: no nace nada (spec §10) */
    int cell = pick_cell(gs, near);
    if (cell < 0)
        return NULL;
    slot->cell = (uint8_t)cell;
    set_entity(gs, slot, sp, st, announce);
    return slot;
}

Entity *garden_place(GameState *gs, uint8_t sp, uint8_t st, int near, bool force)
{
    return place_at(gs, sp, st, near, force, true);
}

void garden_remove(GameState *gs, Entity *e)
{
    (void)gs;
    memset(e, 0, sizeof(*e));
}

void garden_init(GameState *gs)
{
    /* Un jardín que ya tiene vida el primer día. No cuenta como descubierto: eso se gana
     * mirando con la lupa o viéndolo nacer. */
    static const uint8_t START[][2] = {
        {SP_ARBOL, 1}, {SP_ARBOL_JOVEN, 0}, {SP_BROTE, 0}, {SP_FLOR, 1}, {SP_FLOR, 1},
        {SP_PIEDRA, 0}, {SP_PIEDRA, 0},
    };
    for (unsigned i = 0; i < sizeof(START) / sizeof(START[0]); i++) {
        Entity *e = place_at(gs, START[i][0], START[i][1], -1, true, false);
        if (e && e->left)
            e->left = (uint16_t)(e->left / 2 + rng_below(&gs->rng, (uint32_t)e->left / 2 + 1));
    }
}

/* ---------- resultados ---------- */

/* Abono: a todo lo que crece le queda la mitad de espera. */
static void apply_abono(GameState *gs)
{
    for (int i = 0; i < LLV_GARDEN_MAX; i++) {
        Entity *e = &gs->ents[i];
        const GardenRule *r = garden_rule(e->sp, e->st);
        if (r && (r->flags & RF_WET) && e->left > 1)
            e->left = (uint16_t)(e->left / 2);
    }
}

static void apply_outcome(GameState *gs, Entity *e, const Outcome *o)
{
    int cell = e ? e->cell : -1;
    if (o->eff == EFF_ABONO)
        apply_abono(gs);
    if (e) {
        if (o->sp == SP_NONE)
            garden_remove(gs, e);
        else if (o->sp == e->sp && o->st == e->st)
            ; /* se queda como está */
        else if (species_ok(gs, o->sp, e->sp))
            set_entity(gs, e, o->sp, o->st, true);
        else
            garden_remove(gs, e); /* ya hay demasiadas: se esfuma */
    }
    if (o->sp2 != SP_NONE)
        place_at(gs, o->sp2, o->st2, o->near ? cell : -1, false, true);
    if (o->ev)
        state_log(gs, (EventKind)o->ev, 0);
    if (o->item && state_bag_add(gs, (ItemKind)o->item))
        state_log(gs, EV_BAG_REFILL, o->item);
}

/* ---------- el tick ---------- */

static void step_entity(GameState *gs, Entity *e, const Env *env)
{
    const GardenRule *r = garden_rule(e->sp, e->st);
    if (!r || r->dur == 0)
        return;
    uint32_t delta = 1;
    if ((r->flags & RF_WET) && gs->weather == W_RAIN)
        delta = LLV_WET_MULT;
    if ((r->flags & RF_SLOW_WINTER) && env->season == SEASON_WINTER && (gs->tick & 3))
        delta = 0;
    if ((r->flags & RF_ONLY_WINTER) && env->season != SEASON_WINTER)
        delta = e->left; /* fuera de su estación se acaba ya */
    if (e->left > delta) {
        e->left = (uint16_t)(e->left - delta);
        return;
    }
    uint32_t roll = rng_below(&gs->rng, 100), acc = 0;
    const Outcome *pick = &r->next[0];
    for (int k = 0; k < GARDEN_OUTCOMES && r->next[k].pct; k++) {
        acc += r->next[k].pct;
        pick = &r->next[k];
        if (roll < acc)
            break;
    }
    Outcome o = *pick; /* copia: apply_outcome puede reordenar el jardín */
    apply_outcome(gs, e, &o);
}

static void trigger(GameState *gs, Trigger t, unsigned strength)
{
    for (const GardenReact *r = GARDEN_REACT; r->sp; r++) {
        if (r->trig != t)
            continue;
        for (int i = 0; i < LLV_GARDEN_MAX; i++) {
            Entity *e = &gs->ents[i];
            if (e->sp != r->sp || e->st != r->st)
                continue;
            unsigned p = r->pct * strength / 100;
            if (p >= 100 || rng_below(&gs->rng, 100) < p)
                apply_outcome(gs, e, &r->out);
        }
    }
}

static void interactions(GameState *gs)
{
    for (const GardenInteract *it = GARDEN_INTERACT; it->asp; it++) {
        for (int i = 0; i < LLV_GARDEN_MAX; i++) {
            Entity *a = &gs->ents[i];
            if (a->sp != it->asp || a->st != it->ast)
                continue;
            int n = garden_count_sp(gs, it->tsp, it->tst);
            if (n == 0 || rng_below(&gs->rng, 100) >= it->pct)
                continue;
            int k = (int)rng_below(&gs->rng, (uint32_t)n);
            Entity *t = NULL;
            for (int j = 0; j < LLV_GARDEN_MAX; j++) {
                const Entity *c = &gs->ents[j];
                if (c->sp == it->tsp && (it->tst == ST_ANY || c->st == it->tst) && k-- == 0) {
                    t = &gs->ents[j];
                    break;
                }
            }
            if (!t)
                continue;
            if (it->rsp == SP_NONE)
                garden_remove(gs, t);
            else if (t->sp != it->rsp || t->st != it->rst)
                set_entity(gs, t, it->rsp, it->rst, true);
            Outcome o = it->actor;
            apply_outcome(gs, a, &o);
        }
    }
}

static bool spawn_allowed(const GameState *gs, const GardenSpawn *s, const Env *env)
{
    if (!(s->seasons & SEAS(env->season)))
        return false;
    if ((s->when & SW_DAY) && !env->day)
        return false;
    if ((s->when & SW_NIGHT) && env->day)
        return false;
    if ((s->when & SW_RAIN) && gs->weather != W_RAIN)
        return false;
    if ((s->when & SW_DRY) && (gs->weather == W_RAIN || gs->weather == W_SNOW))
        return false;
    if ((s->when & SW_WIND) && gs->weather != W_WIND)
        return false;
    if ((s->when & SW_SNOW) && gs->weather != W_SNOW)
        return false;
    return s->need_sp == SP_NONE || garden_count_sp(gs, s->need_sp, s->need_st) > 0;
}

static void spawns(GameState *gs, const Env *env)
{
    for (const GardenSpawn *s = GARDEN_SPAWN; s->sp; s++) {
        if (!spawn_allowed(gs, s, env) || rng_below(&gs->rng, 1000) >= s->permille)
            continue;
        if (!place_at(gs, s->sp, s->st, -1, false, true))
            continue;
        if (s->ev)
            state_log(gs, (EventKind)s->ev, 0);
        if (s->item && state_bag_add(gs, (ItemKind)s->item))
            state_log(gs, EV_BAG_REFILL, s->item);
    }
}

static void update_weather(GameState *gs, const Env *env)
{
    if (gs->sky_left && --gs->sky_left == 0)
        gs->sky = SKY_NONE;
    if (gs->weather != W_CLEAR) {
        if (gs->weather_left > 0)
            gs->weather_left--;
        if (gs->weather_left == 0) {
            if (gs->weather == W_RAIN && env->day && rng_below(&gs->rng, 100) < LLV_RAINBOW_PCT) {
                gs->sky = SKY_RAINBOW;
                gs->sky_left = LLV_RAINBOW_TICKS;
                state_log(gs, EV_RAINBOW, 0);
                state_collect(gs, COLL_ARCOIRIS);
            }
            gs->weather = W_CLEAR;
        }
    } else {
        uint32_t roll = rng_below(&gs->rng, 1000);
        const uint8_t *wp = GARDEN_WEATHER[env->season];
        if (roll < wp[0]) {
            bool snow = env->season == SEASON_WINTER;
            gs->weather = snow ? W_SNOW : W_RAIN;
            gs->weather_left = (uint16_t)(LLV_TICKS_PER_HOUR + rng_below(&gs->rng, 2 * LLV_TICKS_PER_HOUR + 1));
            state_log(gs, snow ? EV_SNOW : EV_RAIN, 0);
        } else if (roll < (uint32_t)wp[0] + wp[1]) {
            gs->weather = W_WIND;
            gs->weather_left = (uint16_t)(3 + rng_below(&gs->rng, 6));
            if (garden_count_sp(gs, SP_ARBOL, 1))
                state_log(gs, EV_WIND, 0); /* solo importa si hay fruta que tirar */
        }
    }
    if (!env->day && gs->sky == SKY_NONE && gs->weather != W_RAIN &&
        rng_below(&gs->rng, 1000) < LLV_STAR_PERMILLE) {
        gs->sky = SKY_STAR;
        gs->sky_left = 6;
        state_log(gs, EV_STAR, 0);
        state_collect(gs, COLL_ESTRELLA);
    }
}

void garden_tick(GameState *gs)
{
    Env env = make_env(gs);
    update_weather(gs, &env);
    if (gs->weather == W_WIND)
        trigger(gs, TRG_WIND, LLV_WIND_NATURAL_PCT);
    for (int i = 0; i < LLV_GARDEN_MAX; i++)
        if (gs->ents[i].sp != SP_NONE)
            step_entity(gs, &gs->ents[i], &env);
    interactions(gs);
    spawns(gs, &env);
}

int garden_ticks_left(const GameState *gs, const Entity *e)
{
    const GardenRule *r = garden_rule(e->sp, e->st);
    if (!r || r->dur == 0)
        return -1;
    int left = e->left;
    if ((r->flags & RF_WET) && gs->weather == W_RAIN)
        left = (left + LLV_WET_MULT - 1) / LLV_WET_MULT;
    return left;
}

/* ---------- los objetos de la bolsa ---------- */

static const char *use_item(GameState *gs, ItemKind item)
{
    Season season = garden_season(gs);
    switch (item) {
    case ITEM_NUBE: {
        uint8_t w = season == SEASON_WINTER ? W_SNOW : W_RAIN;
        if (gs->weather != w)
            state_log(gs, w == W_RAIN ? EV_RAIN : EV_SNOW, 1);
        gs->weather = w;
        if (gs->weather_left < LLV_RAIN_ITEM_TICKS)
            gs->weather_left = LLV_RAIN_ITEM_TICKS;
        return w == W_RAIN ? "Llueve: todo crece más rápido." : "Nieva. El jardín se aquieta.";
    }
    case ITEM_VIENTO: {
        bool fruit = garden_count_sp(gs, SP_ARBOL, 1) > 0;
        gs->weather = W_WIND;
        if (gs->weather_left < 6)
            gs->weather_left = 6;
        trigger(gs, TRG_WIND, 100);
        int seeds = 0;
        for (int k = 1 + (int)rng_below(&gs->rng, 2); k > 0; k--)
            seeds += garden_place(gs, SP_SEMILLA, 0, -1, false) != NULL;
        if (fruit)
            return seeds ? "Cae la fruta y vuelan semillas." : "Cae la fruta.";
        return seeds ? "Vuelan semillas." : "Se mecen las hojas.";
    }
    case ITEM_SEMILLA: {
        uint32_t r = rng_below(&gs->rng, 100);
        uint8_t sp = r < 50 ? SP_SEMILLA : r < 80 ? SP_FLOR : SP_BROTE;
        Entity *e = garden_place(gs, sp, 0, -1, true);
        if (!e && sp == SP_BROTE) { /* ya hay tantos árboles como caben: queda en semilla */
            sp = SP_SEMILLA;
            e = garden_place(gs, sp, 0, -1, true);
        }
        if (!e)
            return "No cabe nada más.";
        return sp == SP_SEMILLA ? "Plantaste una semilla."
             : sp == SP_FLOR    ? "Plantaste una flor."
                                : "Plantaste un brote.";
    }
    case ITEM_ALPISTE: {
        Entity *b = find_sp(gs, SP_PAJARO, ST_ANY);
        if (b) {
            set_entity(gs, b, SP_PAJARO, 1, true);
            return "El pájaro se queda a comer.";
        }
        if (!garden_place(gs, SP_PAJARO, 1, -1, true))
            return "No cabe nada más.";
        state_log(gs, EV_BIRD, 0);
        return "Un pájaro baja a picotear.";
    }
    case ITEM_LOMBRIZ:
        apply_abono(gs);
        state_log(gs, EV_COMPOST, 0);
        garden_place(gs, SP_LOMBRIZ, 0, -1, true);
        return "Abono para las plantas.";
    case ITEM_DESEO: {
        bool day = garden_is_day(gs);
        if (day) {
            gs->sky = SKY_RAINBOW;
            gs->sky_left = LLV_RAINBOW_TICKS;
            state_log(gs, EV_RAINBOW, 0);
            state_collect(gs, COLL_ARCOIRIS);
        } else {
            gs->sky = SKY_STAR;
            gs->sky_left = 6;
            state_log(gs, EV_STAR, 0);
            state_collect(gs, COLL_ESTRELLA);
        }
        for (int i = 0; i < LLV_GARDEN_MAX; i++)
            if (gs->ents[i].sp == SP_FLOR && gs->ents[i].st == 0)
                set_entity(gs, &gs->ents[i], SP_FLOR, 1, true); /* el deseo hace florecer */
        garden_place(gs, SP_FLOR, 3, -1, true);
        return day ? "¡Un arcoíris!" : "¡Una estrella fugaz!";
    }
    default:
        return "";
    }
}

ActResult garden_act(GameState *gs, int widx, ItemKind *used, int *was_prepared, const char **effect)
{
    if (gs->actions_used[widx] >= LLV_GARDEN_ACTIONS)
        return ACT_NO_ACTIONS;
    if (gs->bag_count == 0)
        return ACT_NO_ITEMS;
    int prepared = 0;
    for (int i = 0; i < gs->bag_count && gs->prepared != ITEM_NONE; i++)
        prepared |= gs->bag[i] == gs->prepared;
    ItemKind item = state_bag_take(gs);
    gs->actions_used[widx]++;
    state_log(gs, EV_ITEM_USED, (uint8_t)item);
    const char *msg = use_item(gs, item);
    if (used)
        *used = item;
    if (was_prepared)
        *was_prepared = prepared;
    if (effect)
        *effect = msg;
    return ACT_OK;
}
