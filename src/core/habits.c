#include "habits.h"

#include "garden.h"
#include "timeutil.h"

static const char *const TITLE[HAB_COUNT] = {
    [HAB_AGUA] = "Beber agua", [HAB_DORMIR] = "Dormir bien", [HAB_PAUSA] = "Respirar",
    [HAB_PANTALLA] = "Soltar el móvil", [HAB_SOL] = "Salir al sol", [HAB_LLAMAR] = "Llamar a alguien",
    [HAB_MOVERSE] = "Moverse",
};
static const char *const LABEL[HAB_COUNT] = {
    [HAB_AGUA] = "agua", [HAB_DORMIR] = "sueño", [HAB_PAUSA] = "respirar",
    [HAB_PANTALLA] = "sin móvil", [HAB_SOL] = "sol", [HAB_LLAMAR] = "llamada", [HAB_MOVERSE] = "paseo",
};

/* Franja del día en la que se pregunta (hora de inicio y de fin, fin exclusivo) y días entre preguntas. */
static const struct { uint8_t from, to, every; } WINDOW[HAB_WITH_QUESTION] = {
    [HAB_AGUA] = {8, 22, 1}, [HAB_DORMIR] = {5, 12, 1}, [HAB_PAUSA] = {9, 21, 1},
    [HAB_PANTALLA] = {18, 24, 1}, [HAB_SOL] = {10, 17, 1}, [HAB_LLAMAR] = {10, 21, 7},
};

const char *habit_title(int h) { return h >= 0 && h < HAB_COUNT ? TITLE[h] : ""; }
const char *habit_label(int h) { return h >= 0 && h < HAB_COUNT ? LABEL[h] : ""; }

bool habit_enabled(const GameState *gs, int h)
{
    return h >= 0 && h < HAB_COUNT && (gs->hab.enabled >> h & 1);
}

void habit_set_enabled(GameState *gs, int h, bool on)
{
    if (h < 0 || h >= HAB_COUNT)
        return;
    if (on)
        gs->hab.enabled = (uint8_t)(gs->hab.enabled | (1u << h));
    else
        gs->hab.enabled = (uint8_t)(gs->hab.enabled & ~(1u << h));
}

void habits_new_day(GameState *gs)
{
    Habits *h = &gs->hab;
    h->done_today = 0;
    h->asked_today = 0;
    h->asked_count = 0;
    h->walk_rewarded = 0;
    h->steps_today = 0;
    h->night_look_prev = h->night_look; /* lo que pasó esta noche se pregunta por la mañana */
    h->night_look = 0;
}

void habits_on_look(GameState *gs, int64_t now)
{
    if (time_to_civil(now).hour < 5)
        gs->hab.night_look = 1;
}

bool habit_question_ok(const GameState *gs, int q, int64_t now)
{
    if (q < 0 || q >= HAB_WITH_QUESTION || !habit_enabled(gs, q))
        return false;
    const Habits *h = &gs->hab;
    if ((h->done_today | h->asked_today) >> q & 1)
        return false;
    if (h->asked_count >= LLV_HABIT_MAX_PER_DAY)
        return false;
    int hour = time_to_civil(now).hour;
    if (hour < WINDOW[q].from || hour >= WINDOW[q].to)
        return false;
    uint16_t last = gs->sala.memo_day[q];
    uint16_t today = (uint16_t)gs->day_key;
    return last == 0 || (uint16_t)(today - last) >= WINDOW[q].every;
}

void habit_note_asked(GameState *gs, int q, int64_t now)
{
    (void)now;
    if (q < 0 || q >= HAB_WITH_QUESTION)
        return;
    gs->hab.asked_today = (uint8_t)(gs->hab.asked_today | (1u << q));
    if (gs->hab.asked_count < 255)
        gs->hab.asked_count++;
    gs->sala.memo_day[q] = (uint16_t)gs->day_key;
}

static void pet_cheer(GameState *gs, int amount)
{
    if (gs->pet.kind == PET_NONE)
        return;
    int v = gs->pet.happy + amount;
    gs->pet.happy = (uint8_t)(v > 255 ? 255 : v);
}

/* El efecto de cumplir cada hábito: ayuda al universo sin gastar objetos ni dar nada que se
 * pueda "farmear" (una vez al día como mucho). */
static void apply_effect(GameState *gs, int h)
{
    switch (h) {
    case HAB_AGUA: { /* llueve un poco en el jardín (nieva en invierno) */
        uint8_t w = garden_season(gs) == SEASON_WINTER ? W_SNOW : W_RAIN;
        if (gs->weather != w)
            state_log(gs, w == W_RAIN ? EV_RAIN : EV_SNOW, 2);
        gs->weather = w;
        if (gs->weather_left < LLV_HABIT_RAIN_TICKS)
            gs->weather_left = LLV_HABIT_RAIN_TICKS;
        break;
    }
    case HAB_PAUSA:
        pet_cheer(gs, 35);
        break;
    case HAB_PANTALLA: /* sin el móvil se oye el jardín: baja un pájaro */
        garden_place(gs, SP_PAJARO, 0, -1, false);
        break;
    case HAB_SOL: /* al sol florecen cosas */
        for (int i = 0; i < 2; i++)
            garden_place(gs, SP_FLOR, 0, -1, false);
        break;
    case HAB_LLAMAR: /* la Abuela te manda una semilla de su huerto */
        if (state_bag_add(gs, ITEM_SEMILLA))
            state_log(gs, EV_BAG_REFILL, ITEM_SEMILLA);
        pet_cheer(gs, 20);
        break;
    default:
        break;
    }
}

bool habit_do(GameState *gs, int h)
{
    if (h < 0 || h >= HAB_COUNT || (gs->hab.done_today >> h & 1))
        return false;
    gs->hab.done_today = (uint8_t)(gs->hab.done_today | (1u << h));
    apply_effect(gs, h);
    state_log(gs, EV_HABIT, (uint8_t)h);
    return true;
}

int habit_answer(GameState *gs, int h, bool yes)
{
    int r = 0;
    if (h < 0 || h >= HAB_COUNT)
        return 0;
    if (h == HAB_DORMIR) {
        if (!yes || gs->hab.night_look_prev) {
            /* Un no, o un sí de alguien al que se vio despierto de madrugada: se corta la racha
             * (solo es un contador: sin castigo) y, en el segundo caso, "detecto una mentira". */
            gs->hab.sleep_streak = 0;
            if (yes)
                r |= HA_LIE;
        } else if (gs->hab.sleep_streak < 255) {
            gs->hab.sleep_streak++;
        }
    }
    if (yes && !(r & HA_LIE) && habit_do(gs, h))
        r |= HA_EFFECT;
    return r;
}

bool habit_breath(GameState *gs, int cycles)
{
    return cycles >= 3 && habit_enabled(gs, HAB_PAUSA) && habit_do(gs, HAB_PAUSA);
}

void habit_steps(GameState *gs, uint32_t total)
{
    Habits *h = &gs->hab;
    if (h->steps_last == 0xFFFFFFFFu || total < h->steps_last) {
        h->steps_last = total; /* primera lectura o contador reiniciado: sin salto */
        return;
    }
    uint32_t delta = total - h->steps_last;
    h->steps_last = total;
    uint32_t sum = (uint32_t)h->steps_today + delta;
    h->steps_today = (uint16_t)(sum > 65535u ? 65535u : sum);
    if (!habit_enabled(gs, HAB_MOVERSE) || h->walk_rewarded || h->steps_today < LLV_WALK_STEPS)
        return;
    /* Un paseo largo suma un objeto extra (máx. 1 al día). Si la bolsa está llena se espera, y se
     * mira antes de tirar el dado: esto corre en cada fotograma y el azar no puede depender de ellos. */
    if (gs->bag_count >= LLV_BAG_SIZE)
        return;
    ItemKind it = state_random_item(&gs->rng);
    if (!state_bag_add(gs, it))
        return;
    h->walk_rewarded = 1;
    h->done_today = (uint8_t)(h->done_today | (1u << HAB_MOVERSE));
    state_log(gs, EV_BAG_REFILL, (uint8_t)it);
    state_log(gs, EV_WALK, 0);
    state_log(gs, EV_HABIT, HAB_MOVERSE);
    if (gs->pet.kind == PET_COMPOSTERA) /* alimenta la compostera */
        pet_cheer(gs, 60);
}
