#include "pet.h"

#include <stdio.h>
#include <string.h>

#include "garden.h"

static const char *const NAME[PET_COUNT] = {"", "HONGO", "SEMILLA", "PIEDRA", "COMPOSTERA"};
static const char *const BLURB[PET_COUNT] = {
    "", "Habla poco.", "Mira al sol.", "Nunca dice nada.", "Muchos gusanos.",
};

static const uint8_t STAGES[PET_COUNT] = {0, 6, 5, 4, 5};
static const char *const STAGE_HONGO[] = {"espora", "punto", "botón", "pequeño", "con sombrero",
                                          "anciano con barba"};
static const char *const STAGE_SEMILLA[] = {"semilla", "brote", "plantita", "bonsái", "bonsái anciano"};
static const char *const STAGE_PIEDRA[] = {"desnuda", "con musgo", "con liquen", "con mariquita"};
static const char *const STAGE_COMPOST[] = {"recién puesta", "tibia", "activa", "rebosante",
                                            "legendaria"};

static const char *const MOOD_HONGO[4] = {"pensativo", "contemplativo", "satisfecho", "esporulando"};
static const char *const MOOD_SEMILLA[4] = {"dormilona", "expectante", "soleada", "radiante"};
static const char *const MOOD_PIEDRA[4] = {"sólido", "estable", "inamovible", "pétreo"};
static const char *const MOOD_COMPOST[4] = {"fermentando", "activa", "burbujeante", "rebosante"};

static const char *const THOUGHT_HONGO[8] = {"hm.", "oh.", "sí.", "luz.", "pues.", "lluvia.", "mm.", "tal vez."};
static const char *const THOUGHT_SEMILLA[6] = {"…", "sol", "¿agua?", "más sol", "crecer", "¿ya?"};
static const char *const THOUGHT_COMPOST[5] = {"glub.", "ñam.", "mmm.", "digiriendo", "todo es compost"};

/* Gustos [mascota][objeto]: nube viento semilla alpiste lombriz deseo. */
static const int8_t LIKE[PET_COUNT][ITEM_COUNT] = {
    [PET_HONGO]      = {0, 2, 0, 0, 0, 0, 1},      /* ama la humedad; la lombriz le da igual */
    [PET_SEMILLA]    = {0, 2, 1, -1, -1, 1, 2},    /* el alpiste le da miedo */
    [PET_PIEDRA]     = {0, 1, 1, 0, 0, 0, 0},      /* el viento la erosiona con cariño */
    [PET_COMPOSTERA] = {0, 1, 0, 1, 0, 2, 1},
};

const char *pet_name(PetKind kind)
{
    return kind < PET_COUNT ? NAME[kind] : "";
}

const char *pet_blurb(PetKind kind)
{
    return kind < PET_COUNT ? BLURB[kind] : "";
}

int pet_stage_count(PetKind kind)
{
    return kind < PET_COUNT ? STAGES[kind] : 0;
}

int pet_stage(const Pet *p)
{
    int n = pet_stage_count((PetKind)p->kind);
    if (n == 0)
        return 0;
    int s = (int)((uint32_t)p->age * (uint32_t)n / LLV_PET_CYCLE_TICKS);
    return s >= n ? n - 1 : s;
}

const char *pet_stage_name(const Pet *p)
{
    int s = pet_stage(p);
    switch (p->kind) {
    case PET_HONGO: return STAGE_HONGO[s];
    case PET_SEMILLA: return STAGE_SEMILLA[s];
    case PET_PIEDRA: return STAGE_PIEDRA[s];
    case PET_COMPOSTERA: return STAGE_COMPOST[s];
    default: return "";
    }
}

int pet_day(const Pet *p)
{
    return p->age / LLV_TICKS_PER_DAY + 1;
}

/* Crece 0,1 px al día: el hongo del ejemplo del spec mide 7 px el día 43. */
int pet_height_tenths(const Pet *p)
{
    int day = p->age / LLV_TICKS_PER_DAY;
    return p->kind == PET_SEMILLA ? 20 + day : 28 + day;
}

int pet_worms(const Pet *p)
{
    return 3 + (int)((uint32_t)p->age * 9u / LLV_PET_CYCLE_TICKS);
}

const char *pet_mood(const Pet *p)
{
    int q = p->happy < 60 ? 0 : p->happy < 120 ? 1 : p->happy < 190 ? 2 : 3;
    switch (p->kind) {
    case PET_HONGO: return MOOD_HONGO[q];
    case PET_SEMILLA: return MOOD_SEMILLA[q];
    case PET_PIEDRA: return MOOD_PIEDRA[q];
    case PET_COMPOSTERA: return MOOD_COMPOST[q];
    default: return "";
    }
}

const char *pet_thought(const Pet *p)
{
    switch (p->kind) {
    case PET_HONGO: return THOUGHT_HONGO[p->thought % 8];
    case PET_SEMILLA: return THOUGHT_SEMILLA[p->thought % 6];
    case PET_COMPOSTERA: return THOUGHT_COMPOST[p->thought % 5];
    default: return ""; /* la piedra nunca dice nada */
    }
}

static void tenths(char *buf, size_t n, int t)
{
    if (t % 10 == 0)
        snprintf(buf, n, "%d px", t / 10);
    else
        snprintf(buf, n, "%d,%d px", t / 10, t % 10);
}

int pet_ficha(const Pet *p, char lines[][24], int max)
{
    int n = 0;
    char h[32];
#define LINE(...) do { if (n < max) snprintf(lines[n++], 24, __VA_ARGS__); } while (0)
    LINE("%s", pet_name((PetKind)p->kind));
    LINE("Gen %u · Día %d", (unsigned)p->gen, pet_day(p));
    switch (p->kind) {
    case PET_HONGO:
    case PET_SEMILLA:
        tenths(h, sizeof(h), pet_height_tenths(p));
        LINE("Altura: %.10s", h);
        LINE("(+0,1 hoy)");
        break;
    case PET_PIEDRA:
        LINE("Peso: 120 g");
        LINE("(estable)");
        break;
    case PET_COMPOSTERA:
        LINE("Gusanos: %d", pet_worms(p));
        LINE("Productividad:");
        LINE("+0,2 %%");
        break;
    default:
        break;
    }
    LINE("Humor:");
    LINE("%s", pet_mood(p));
    LINE("Último");
    LINE("pensamiento:");
    LINE("\"%s\"", pet_thought(p));
#undef LINE
    return n;
}

int pet_like(PetKind kind, ItemKind item)
{
    return kind < PET_COUNT && item < ITEM_COUNT ? LIKE[kind][item] : 0;
}

void pet_cheer(GameState *gs, int amount)
{
    if (gs->pet.kind == PET_NONE)
        return;
    int h = gs->pet.happy + amount;
    gs->pet.happy = (uint8_t)(h > 255 ? 255 : h);
}

bool pet_choose(GameState *gs, PetKind kind)
{
    if (gs->pet.kind != PET_NONE || kind <= PET_NONE || kind >= PET_COUNT)
        return false;
    Pet *p = &gs->pet;
    memset(p, 0, sizeof(*p));
    p->kind = (uint8_t)kind;
    p->gen = 1;
    p->happy = 80;
    p->word_left = (uint16_t)(7 * LLV_TICKS_PER_DAY); /* una palabra a la semana */
    p->task_left = (uint16_t)(2 * LLV_TICKS_PER_DAY);
    state_log(gs, EV_PET_CHOSEN, (uint8_t)kind);
    return true;
}

/* Descendencia al completar el ciclo (spec §4.7: "deja un hijo"). */
static void new_generation(GameState *gs)
{
    Pet *p = &gs->pet;
    if (p->gen < 255)
        p->gen++;
    p->age = 0;
    p->stage_seen = 0;
    state_log(gs, EV_PET_GEN, p->gen);
    switch (p->kind) {
    case PET_HONGO:
        for (int i = 0; i < 2; i++)
            garden_place(gs, SP_HONGO, 0, -1, true);
        break;
    case PET_SEMILLA:
        garden_place(gs, SP_SEMILLA, 0, -1, true);
        if (state_bag_add(gs, ITEM_SEMILLA))
            state_log(gs, EV_BAG_REFILL, ITEM_SEMILLA);
        break;
    case PET_COMPOSTERA:
        if (state_bag_add(gs, ITEM_LOMBRIZ))
            state_log(gs, EV_BAG_REFILL, ITEM_LOMBRIZ);
        break;
    default:
        break;
    }
}

void pet_tick(GameState *gs)
{
    Pet *p = &gs->pet;
    if (p->kind == PET_NONE)
        return;
    p->age++;
    if (gs->tick % 12 == 0 && p->happy > 30)
        p->happy--; /* el humor se enfría despacio, sin pasar de "pensativo" */

    int st = pet_stage(p);
    if (st != p->stage_seen) {
        p->stage_seen = (uint8_t)st;
        state_log(gs, EV_PET_STAGE, (uint8_t)st);
    }

    switch (p->kind) {
    case PET_HONGO:
        if (p->word_left > 0 && --p->word_left == 0) {
            uint8_t t = (uint8_t)rng_below(&gs->rng, 8);
            p->thought = t == p->thought ? (uint8_t)((t + 1) % 8) : t;
            p->word_left = (uint16_t)(7 * LLV_TICKS_PER_DAY);
            state_log(gs, EV_PET_WORD, p->thought);
        }
        /* De adulto suelta esporas: nacen hongos en el Jardín (universo compartido). */
        if (p->task_left > 0 && --p->task_left == 0) {
            if (st >= 4 && garden_place(gs, SP_HONGO, 0, -1, false))
                state_log(gs, EV_PET_SPORES, 0);
            p->task_left = (uint16_t)(3 * LLV_TICKS_PER_DAY + rng_below(&gs->rng, LLV_TICKS_PER_DAY));
        }
        break;
    case PET_COMPOSTERA:
        /* Se come las manzanas podridas y devuelve abono (una lombriz para la bolsa). */
        if (p->task_left > 0 && --p->task_left == 0) {
            Entity *rot = NULL;
            for (int i = 0; i < LLV_GARDEN_MAX && !rot; i++)
                if (gs->ents[i].sp == SP_MANZANA && gs->ents[i].st == 1)
                    rot = &gs->ents[i];
            if (rot) {
                garden_remove(gs, rot);
                state_log(gs, EV_PET_COMPOST, 0);
                if (state_bag_add(gs, ITEM_LOMBRIZ))
                    state_log(gs, EV_BAG_REFILL, ITEM_LOMBRIZ);
                p->task_left = (uint16_t)LLV_TICKS_PER_DAY;
            } else {
                p->task_left = (uint16_t)(6 * LLV_TICKS_PER_HOUR);
            }
        }
        break;
    default:
        break;
    }

    if (p->age >= LLV_PET_CYCLE_TICKS)
        new_generation(gs);
}

static const char *const GREET[PET_COUNT] = {
    "", "El hongo te mira. Es suficiente.", "La semilla se inclina hacia ti.",
    "Silencio. Respeto mutuo.", "Los gusanos te saludan.",
};
static const char *const REACT[4] = {"lo mira raro.", "le da igual.", "le gusta.", "¡le encanta!"};

CareResult pet_care(GameState *gs, int widx, char *msg, size_t n)
{
    Pet *p = &gs->pet;
    if (p->kind == PET_NONE)
        return CARE_NO_PET;
    if (gs->actions_used[widx] >= LLV_PET_ACTIONS) {
        snprintf(msg, n, "Hoy ya lo cuidaste. Mañana, más.");
        return CARE_NONE_LEFT;
    }
    gs->actions_used[widx]++;
    state_log(gs, EV_PET_CARE, 0);

    int gain = 25;
    int offered = 0;
    if (gs->prepared != ITEM_NONE) {
        int in_bag = 0;
        for (int i = 0; i < gs->bag_count; i++)
            in_bag |= gs->bag[i] == gs->prepared;
        if (in_bag)
            offered = 1;
    }
    if (offered) {
        ItemKind it = state_bag_take(gs);
        int like = pet_like((PetKind)p->kind, it);
        gain = like == 2 ? 70 : like == 1 ? 45 : like == 0 ? 20 : 8;
        snprintf(msg, n, "%s: %s %s", state_item_name(it), pet_name((PetKind)p->kind), REACT[like + 1]);
    } else {
        gs->prepared = ITEM_NONE;
        snprintf(msg, n, "%s", GREET[p->kind]);
    }
    p->happy = (uint8_t)(p->happy + gain > 255 ? 255 : p->happy + gain);

    /* Gracias de cada mascota. El hongo no habla fuera de su palabra semanal. */
    if (p->kind == PET_SEMILLA) {
        p->thought = (uint8_t)rng_below(&gs->rng, 6);
        if (rng_below(&gs->rng, 100) < 35 && state_bag_add(gs, ITEM_SEMILLA)) {
            state_log(gs, EV_BAG_REFILL, ITEM_SEMILLA);
            size_t l = strlen(msg);
            if (l + 20 < n)
                snprintf(msg + l, n - l, " Te da una Semilla.");
        }
    } else if (p->kind == PET_COMPOSTERA) {
        p->thought = (uint8_t)rng_below(&gs->rng, 5);
    }
    return CARE_OK;
}
