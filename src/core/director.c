#include "director.h"

#include <string.h>

#include "garden.h"
#include "talk.h"
#include "timeutil.h"

static const char *const NAME[DS_COUNT] = {
    [DS_NONE] = "ninguna", [DS_GLITCH] = "cartucho sucio", [DS_ARRANQUE] = "arranque falso",
    [DS_AUSENCIA] = "mundo ausente", [DS_INVASION] = "invasión", [DS_CUARTA_PARED] = "cuarta pared",
    [DS_CALENDARIO] = "calendario", [DS_REBELDIA] = "rebeldía", [DS_COLAPSO] = "colapso cuántico",
    [DS_GATO] = "gato de Schrödinger", [DS_MONEDA] = "insert coin",
};

const char *surprise_name(int s)
{
    return s >= 0 && s < DS_COUNT ? NAME[s] : "";
}

int director_phrase(const GameState *gs, uint32_t kinds, int speaker)
{
    return phrase_nth(kinds, speaker, (gs->flags & SF_GRANDMA) != 0, gs->dir.rolls);
}

int director_pick_world(const GameState *gs)
{
    uint8_t n = gs->news;
    if ((n & (1u << WORLD_JARDIN)) && gs->rare_unseen)
        return WORLD_JARDIN;
    if (n & (1u << WORLD_SALA))
        return WORLD_SALA;
    if (n & (1u << WORLD_COBERTIZO))
        return WORLD_COBERTIZO;
    if (n & (1u << WORLD_JARDIN))
        return WORLD_JARDIN;
    return -1;
}

/* Visitantes que pueden aparecer fuera de la Sala. */
static const uint8_t GUESTS[3] = {SPK_GOTICA, SPK_GAMER, SPK_BOOMER};

/* Pone en marcha una sorpresa: deja el estado listo y devuelve el mundo que conviene abrir
 * (-1: el que estaba). Si no se puede (p. ej. no hay frase), devuelve -2. */
static int start(GameState *gs, const PhraseEnv *env, int s, int arg)
{
    Director *d = &gs->dir;
    int open = -1;
    d->cur = (uint8_t)s;
    d->arg = (uint8_t)arg;
    switch (s) {
    case DS_AUSENCIA:
        open = arg;
        break;
    case DS_INVASION:
        open = arg >> 4;
        break;
    case DS_CUARTA_PARED: {
        if (!talk_say(gs, env, PK_BIT(PK_CUARTA_PARED), SPK_BIT(SPK_ABUELA) | SPK_BIT(SPK_ROBOT), -1)) {
            d->cur = DS_NONE;
            return -2;
        }
        open = WORLD_SALA;
        break;
    }
    case DS_CALENDARIO: {
        static const uint8_t THEMED[6] = {0, SPK_GOTICA, SPK_GATO, SPK_BOOMER, SPK_GAMER, 0};
        int date = phrase_special_date(env->now);
        if (!THEMED[date]) {
            d->cur = DS_NONE;
            return -2;
        }
        talk_visitor(gs, THEMED[date], 7 * LLV_TICKS_PER_HOUR, true);
        talk_say(gs, env, PK_BIT(PK_CHARLA), SPK_BIT(THEMED[date]), -1);
        open = WORLD_SALA;
        break;
    }
    case DS_REBELDIA:
        d->armed = 1;
        open = WORLD_SALA;
        break;
    case DS_GATO:
        talk_gato(gs, env);
        open = WORLD_SALA;
        break;
    default:
        break;
    }
    return open;
}

static int weight_of(int s, int date)
{
    switch (s) {
    case DS_GLITCH: return 3;
    case DS_ARRANQUE: return 2;
    case DS_AUSENCIA: return 3;
    case DS_INVASION: return 3;
    case DS_CUARTA_PARED: return 3;
    case DS_CALENDARIO: return date != DATE_NONE ? 8 : 0;
    case DS_REBELDIA: return 2;
    case DS_COLAPSO: return 2;
    default: return 0;
    }
}

/* Argumento al azar de cada sorpresa que lo lleva (siempre 2 tiradas fijas por sorpresa). */
static int roll_arg(GameState *gs, int s)
{
    switch (s) {
    case DS_AUSENCIA: {
        static const uint8_t W[4] = {WORLD_SALA, WORLD_SALA, WORLD_JARDIN, WORLD_COBERTIZO};
        return W[rng_below(&gs->rng, 4)];
    }
    case DS_INVASION: {
        int guest = GUESTS[rng_below(&gs->rng, 3)];
        int world = rng_below(&gs->rng, 2) ? WORLD_JARDIN : WORLD_COBERTIZO;
        return world << 4 | guest;
    }
    case DS_REBELDIA:
        return rng_below(&gs->rng, 2) ? SPK_ABUELA : SPK_ROBOT;
    default:
        return 0;
    }
}

void director_force(GameState *gs, const PhraseEnv *env, int surprise, int arg)
{
    gs->dir.cur = DS_NONE;
    gs->dir.armed = 0;
    int open = start(gs, env, surprise, arg);
    if (open >= 0)
        gs->current_world = (uint8_t)open;
}

void director_wake(GameState *gs, const PhraseEnv *env, int64_t slept, bool first)
{
    Director *d = &gs->dir;
    d->cur = DS_NONE;
    d->arg = 0;
    d->armed = 0;
    if (first || slept < LLV_DIR_COOLDOWN_S)
        return; /* mirar muchas veces seguidas no da nada nuevo (spec §3.2) */

    uint16_t day = (uint16_t)gs->day_key;
    uint32_t r = rng_below(&gs->rng, 1000);
    int kind = r < LLV_DIR_NORMAL_PM ? DK_NORMAL
             : r < LLV_DIR_NORMAL_PM + LLV_DIR_COMMENT_PM ? DK_COMMENT
             : r < LLV_DIR_NORMAL_PM + LLV_DIR_COMMENT_PM + LLV_DIR_SURPRISE_PM ? DK_SURPRISE : DK_RARE;
    bool prev_surprise = d->last >= DK_SURPRISE;
    if (prev_surprise) {
        if (kind >= DK_SURPRISE) /* nunca dos sorpresas seguidas */
            kind = (r & 1) ? DK_COMMENT : DK_NORMAL;
    } else if ((uint16_t)(day - d->last_surprise_day) >= LLV_DIR_PENA_DAYS && kind < DK_SURPRISE) {
        kind = DK_SURPRISE; /* seguro de pena */
    }
    d->rolls++;

    int open = -1;
    if (kind == DK_COMMENT) {
        if (talk_comment(gs, env))
            open = WORLD_SALA;
        else
            kind = DK_NORMAL;
    } else if (kind == DK_SURPRISE) {
        int date = phrase_special_date(env->now), total = 0;
        for (int s = DS_GLITCH; s <= DS_COLAPSO; s++)
            total += weight_of(s, date);
        int roll = (int)rng_below(&gs->rng, (uint32_t)total), s = DS_GLITCH;
        for (; s <= DS_COLAPSO; s++) {
            int w = weight_of(s, date);
            if (roll < w)
                break;
            roll -= w;
        }
        open = start(gs, env, s, roll_arg(gs, s));
        if (open == -2) { /* no había frase que encajara: queda en una sorpresa visual */
            open = start(gs, env, DS_COLAPSO, 0);
        }
    } else if (kind == DK_RARE) {
        open = start(gs, env, rng_below(&gs->rng, 3) == 0 ? DS_GATO : DS_MONEDA, 0);
    }
    d->last = (uint8_t)kind;
    if (kind >= DK_SURPRISE) {
        d->last_surprise_day = day;
        d->surprises++;
    }
    if (open < 0)
        open = director_pick_world(gs);
    if (open >= 0)
        gs->current_world = (uint8_t)open;
}

/* ---------- bono de paciencia ---------- */

/* Un suceso raro que llega a escondidas: lo que más espera premia (spec §3.2). */
static void rare_gift(GameState *gs)
{
    int start = (int)rng_below(&gs->rng, 5);
    bool day = garden_is_day(gs);
    for (int k = 0; k < 5; k++) {
        switch ((start + k) % 5) {
        case 0:
            if (!state_bag_add(gs, ITEM_DESEO))
                continue;
            state_log(gs, EV_BAG_REFILL, ITEM_DESEO);
            break;
        case 1:
            if (gs->sky != SKY_NONE)
                continue;
            gs->sky = day ? SKY_RAINBOW : SKY_STAR;
            gs->sky_left = day ? LLV_RAINBOW_TICKS : 6;
            state_log(gs, day ? EV_RAINBOW : EV_STAR, 0);
            state_collect(gs, day ? COLL_ARCOIRIS : COLL_ESTRELLA);
            break;
        case 2:
            if (!garden_place(gs, SP_FLOR, 3, -1, false))
                continue;
            state_log(gs, EV_BLOOM, 0);
            break;
        case 3:
            if (!garden_place(gs, SP_RANA, 0, -1, false))
                continue;
            state_log(gs, EV_FROG, 0);
            break;
        default:
            if (!garden_place(gs, SP_HONGO, 0, -1, false))
                continue;
            state_log(gs, EV_MUSHROOM, 0);
            break;
        }
        state_log(gs, EV_PATIENCE, 0);
        return;
    }
}

void director_patience(GameState *gs)
{
    if (!(gs->flags & SF_ASLEEP) || gs->unseen_ticks >= LLV_PATIENCE_CAP_TICKS)
        return;
    gs->unseen_ticks++;
    /* El riesgo por tick crece con la espera (de 0 al tope de 12 h): mirar cada poco no da nada,
     * esperar un buen rato sí; pasado el tope, esperar más no suma. */
    uint32_t ppm = (uint32_t)LLV_PATIENCE_MAX_PPM * gs->unseen_ticks / LLV_PATIENCE_CAP_TICKS;
    if (rng_below(&gs->rng, 1000000) < ppm)
        rare_gift(gs);
}
