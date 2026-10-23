#include "phrases.h"

#include <stdio.h>
#include <string.h>

#include "fish.h"
#include "garden.h"
#include "timeutil.h"

/* El vocabulario del .txt y los enums del motor tienen que coincidir. */
_Static_assert((int)PETV_HONGO == (int)PET_HONGO && (int)PETV_SEMILLA == (int)PET_SEMILLA &&
               (int)PETV_PIEDRA == (int)PET_PIEDRA && (int)PETV_COMPOSTERA == (int)PET_COMPOSTERA,
               "phrases_vocab.h: PV_PETS no coincide con PetKind");
_Static_assert(Q_COUNT <= LLV_Q_MAX, "demasiadas preguntas");
_Static_assert((int)HAB_PAUSA == (int)Q_PAUSA && (int)HAB_LLAMAR == (int)Q_LLAMAR,
               "los hábitos y sus preguntas comparten número");
_Static_assert(PK_COUNT <= 32 && SPK_COUNT <= 32, "las máscaras de propósito y personaje caben en 32 bits");

/* ---------- banco ---------- */

static const uint8_t *bank = PHRASE_BANK;
static unsigned bank_len;
static bool bank_custom;

void phrase_bank_set(const uint8_t *blob, unsigned len)
{
    bank = blob ? blob : PHRASE_BANK;
    bank_len = blob ? len : 0;
    bank_custom = blob != NULL;
}

static unsigned blen(void)
{
    return bank_custom ? bank_len : PHRASE_BANK_LEN;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

#define HEAD 8
#define REC_HEAD 7

int phrase_count(void)
{
    if (blen() < HEAD || memcmp(bank, "LLVP", 4) != 0 || rd16(bank + 4) != 1)
        return 0;
    int n = rd16(bank + 6);
    return (unsigned)(HEAD + 4 * n) <= blen() ? n : 0;
}

bool phrase_get(int idx, PhraseRec *out)
{
    if (idx < 0 || idx >= phrase_count())
        return false;
    uint32_t off = rd32(bank + HEAD + 4 * idx);
    if (off + REC_HEAD > blen())
        return false;
    const uint8_t *p = bank + off;
    out->speaker = p[0];
    out->kind = p[1];
    out->karg = p[2];
    out->kn = p[3];
    out->picaro = (p[4] & 1) != 0;
    out->ntags = p[5];
    out->len = p[6];
    if (off + REC_HEAD + 3u * out->ntags + out->len > blen())
        return false;
    out->tags = p + REC_HEAD;
    out->text = p + REC_HEAD + 3 * out->ntags;
    return true;
}

uint8_t phrase_tag_kind(const PhraseRec *r, int i)
{
    return r->tags[3 * i];
}

uint16_t phrase_tag_arg(const PhraseRec *r, int i)
{
    return rd16(r->tags + 3 * i + 1);
}

void phrase_text(const PhraseRec *r, char *buf, size_t n)
{
    size_t l = r->len < n - 1 ? r->len : n - 1;
    memcpy(buf, r->text, l);
    buf[l] = '\0';
}

uint32_t phrase_hash(const PhraseRec *r)
{
    uint32_t h = 2166136261u ^ r->speaker;
    for (int i = 0; i < r->len; i++)
        h = (h ^ r->text[i]) * 16777619u;
    return h ? h : 1;
}

int phrase_bank_check(void)
{
    if (blen() < HEAD || memcmp(bank, "LLVP", 4) != 0)
        return 1;
    if (rd16(bank + 4) != 1)
        return 2;
    int n = rd16(bank + 6);
    if (n == 0 || (unsigned)(HEAD + 4 * n) > blen())
        return 3;
    for (int i = 0; i < n; i++) {
        PhraseRec r;
        if (!phrase_get(i, &r))
            return 4;
        if (r.speaker == SPK_NONE || r.speaker >= SPK_COUNT || r.kind >= PK_COUNT || r.len == 0)
            return 5;
        for (int t = 0; t < r.ntags; t++)
            if (phrase_tag_kind(&r, t) == 0 || phrase_tag_kind(&r, t) >= PT_COUNT)
                return 6;
    }
    return 0;
}

int phrase_scene_find(int scene, int line)
{
    int n = phrase_count();
    PhraseRec r;
    for (int i = 0; i < n; i++)
        if (phrase_get(i, &r) && r.kind == PK_ESCENA && r.karg == scene && r.kn == line)
            return i;
    return -1;
}

uint32_t phrase_scene_speakers(int scene)
{
    int n = phrase_count();
    uint32_t mask = 0;
    PhraseRec r;
    for (int i = 0; i < n; i++)
        if (phrase_get(i, &r) && r.kind == PK_ESCENA && r.karg == scene)
            mask |= SPK_BIT(r.speaker);
    return mask;
}

/* ---------- nombres ---------- */

static const char *const SPK_VOCAB[SPK_COUNT] = {PV_SPEAKERS(PV_NAME)};
static const char *const EV_VOCAB[PE_COUNT] = {PV_EVENTS(PV_NAME)};
static const char *const Q_VOCAB[Q_COUNT] = {PV_QUESTIONS(PV_NAME)};

static const char *const SPK_NAME[SPK_COUNT] = {
    [SPK_NONE] = "", [SPK_ABUELA] = "ABUELA SOL", [SPK_ROBOT] = "UNIDAD-7", [SPK_GOTICA] = "GÓTICA",
    [SPK_GAMER] = "GAMER", [SPK_BOOMER] = "BOOMER", [SPK_GATO] = "GATO", [SPK_SISTEMA] = "",
};
static const char *const SPK_SHORT[SPK_COUNT] = {
    [SPK_NONE] = "", [SPK_ABUELA] = "Abuela", [SPK_ROBOT] = "Unidad-7", [SPK_GOTICA] = "Gótica",
    [SPK_GAMER] = "Gamer", [SPK_BOOMER] = "Boomer", [SPK_GATO] = "Gato", [SPK_SISTEMA] = "",
};

const char *speaker_name(int spk) { return spk > 0 && spk < SPK_COUNT ? SPK_NAME[spk] : ""; }
const char *speaker_short(int spk) { return spk > 0 && spk < SPK_COUNT ? SPK_SHORT[spk] : ""; }
const char *speaker_vocab(int spk) { return spk > 0 && spk < SPK_COUNT ? SPK_VOCAB[spk] : ""; }
const char *phrase_event_vocab(int pe) { return pe >= 0 && pe < PE_COUNT ? EV_VOCAB[pe] : ""; }
const char *phrase_question_vocab(int q) { return q >= 0 && q < Q_COUNT ? Q_VOCAB[q] : ""; }

/* Suceso de vocabulario -> suceso del diario. */
static const uint8_t PE_TO_EV[PE_COUNT] = {
    [PE_MANZANA_CAE] = EV_APPLE_FALL, [PE_MANZANA_PODRIDA] = EV_APPLE_ROT, [PE_BROTE] = EV_SPROUT,
    [PE_ARBOL] = EV_TREE_ADULT, [PE_HONGO] = EV_MUSHROOM, [PE_FLOR_RARA] = EV_BLOOM,
    [PE_PAJARO] = EV_BIRD, [PE_SEMILLA_PAJARO] = EV_BIRD_SEED, [PE_ABEJA] = EV_BEE,
    [PE_LOMBRIZ] = EV_WORM, [PE_RANA] = EV_FROG, [PE_LLUVIA] = EV_RAIN, [PE_VIENTO] = EV_WIND,
    [PE_NIEVE] = EV_SNOW, [PE_ARCOIRIS] = EV_RAINBOW, [PE_ESTRELLA] = EV_STAR, [PE_ABONO] = EV_COMPOST,
    [PE_DESCUBRIMIENTO] = EV_DISCOVER, [PE_MASCOTA_CRECE] = EV_PET_STAGE, [PE_MASCOTA_NACE] = EV_PET_GEN,
    [PE_MASCOTA_CUIDADA] = EV_PET_CARE, [PE_MASCOTA_PALABRA] = EV_PET_WORD, [PE_ESPORAS] = EV_PET_SPORES,
    [PE_COMPOSTERA_COME] = EV_PET_COMPOST, [PE_BOLSA_LLENA] = EV_BAG_FULL_LOST, [PE_OBJETO] = EV_ITEM_USED,
    [PE_VISITA] = EV_VISITOR, [PE_HABITO] = EV_HABIT, [PE_PASEO] = EV_WALK, [PE_PACIENCIA] = EV_PATIENCE,
    [PE_CEBO] = EV_CAST, [PE_PEZ] = EV_FISH, [PE_PEZ_ESCAPA] = EV_FISH_LOST, [PE_LEGENDARIO] = EV_FISH_LEGEND,
    [PE_BASURA] = EV_FISH_JUNK,
};

int phrase_event_kind(int pe)
{
    return pe >= 0 && pe < PE_COUNT ? PE_TO_EV[pe] : EV_NONE;
}

/* ---------- observador ---------- */

static PhraseSeenFn observer;
static void *observer_user;

void phrase_set_observer(PhraseSeenFn fn, void *user)
{
    observer = fn;
    observer_user = user;
}

void phrase_notify(int speaker, int kind, bool picaro, const char *text)
{
    if (observer)
        observer(observer_user, speaker, kind, picaro, text);
}

/* ---------- contexto ---------- */

int phrase_special_date(int64_t now)
{
    CivilTime c = time_to_civil(now);
    if (c.month == 10 && c.day == 31)
        return DATE_HALLOWEEN;
    if (c.wday == 5 && c.day == 13)
        return DATE_VIERNES13;
    if ((c.month == 1 && c.day == 1) || (c.month == 12 && c.day == 31 && c.hour >= 18))
        return DATE_ANIO_NUEVO;
    if (c.month == 12 && (c.day == 24 || c.day == 25))
        return DATE_NAVIDAD;
    /* DATE_CUMPLE: gancho para la fase 7 (el cumpleaños de quien lo lleva llegará con los regalos). */
    return DATE_NONE;
}

void phrase_ctx_make(PhraseCtx *c, const GameState *gs, const PhraseEnv *env)
{
    memset(c, 0, sizeof(*c));
    CivilTime t = time_to_civil(env->now);
    c->now = env->now;
    c->hour = t.hour;
    c->minute = t.minute;
    c->wday = t.wday;
    c->season = time_season(t.month, t.day, LLV_SOUTH_HEMISPHERE);
    c->moon = time_moon_phase(env->now);
    c->date = (uint8_t)phrase_special_date(env->now);
    /* La primera mirada no tiene "anterior": se queda en un punto neutro (ni prisa ni ausencia). */
    c->gap_min = gs->looks_total <= 1 ? 30 : gs->gap_min;
    c->looks = (int)gs->looks_today;
    c->streak = gs->streak;
    memcpy(c->aff, gs->sala.affinity, sizeof(c->aff));
    c->shaken = env->shaken;
    c->batt_low = env->battery != 0 && env->battery < 20;
    c->charging = env->charging;
    c->weather = gs->weather;
    c->pet = gs->pet.kind;
    c->garden_n = garden_count(gs);
    for (int i = 0; i < COLL_COUNT; i++)
        c->coll_n += (gs->collection >> i) & 1;
    c->bag_n = gs->bag_count;
    c->habits = gs->hab.enabled;
    for (int i = 0; i < LLV_Q_MAX; i++)
        c->resp[i] = gs->sala.memo[i] & 3;
    c->fresh = state_fresh(gs);
    c->present = SPK_BIT(SPK_ABUELA) | SPK_BIT(SPK_ROBOT);
    if (gs->sala.visitor)
        c->present |= SPK_BIT(gs->sala.visitor);
    c->grandma = (gs->flags & SF_GRANDMA) != 0;
}

static bool tod_ok(int hour, int tod)
{
    switch (tod) {
    case TOD_MADRUGADA: return hour < 5;
    case TOD_MANANA: return hour >= 5 && hour < 12;
    case TOD_MEDIODIA: return hour >= 12 && hour < 15;
    case TOD_TARDE: return hour >= 15 && hour < 21;
    case TOD_NOCHE: return hour >= 21 || hour < 5;
    default: return false;
    }
}

static bool tag_ok(const PhraseCtx *c, int speaker, int kind, int arg)
{
    switch (kind) {
    case PT_TOD: return tod_ok(c->hour, arg);
    case PT_WDAY: return c->wday == arg;
    case PT_WEEKEND: return c->wday == 0 || c->wday == 6;
    case PT_SEASON: return c->season == arg;
    case PT_MOON: return c->moon == arg;
    case PT_DATE: return c->date == arg;
    case PT_ABS_GT: return c->gap_min > arg;
    case PT_ABS_LT: return c->gap_min < arg;
    case PT_LOOKS_GT: return c->looks > arg;
    case PT_LOOKS_LT: return c->looks < arg;
    case PT_STREAK_GT: return c->streak > arg;
    case PT_AFF: return arg ? c->aff[speaker] >= LLV_AFF_HIGH : c->aff[speaker] <= LLV_AFF_LOW;
    case PT_RESP: return (arg & 0xFF) < LLV_Q_MAX && c->resp[arg & 0xFF] == (arg >> 8);
    case PT_SHAKEN: return c->shaken;
    case PT_BATT: return c->batt_low;
    case PT_CHARGING: return c->charging;
    case PT_EVENT: return arg < PE_COUNT && (c->fresh >> PE_TO_EV[arg] & 1);
    case PT_WEATHER: return c->weather == arg;
    case PT_GARDEN: return arg ? c->garden_n >= 20 : c->garden_n <= 4;
    case PT_PET: return c->pet == arg;
    case PT_COLL_GT: return c->coll_n > arg;
    case PT_BAG: return arg ? c->bag_n >= LLV_BAG_SIZE : c->bag_n == 0;
    case PT_HABIT: return arg < HAB_COUNT && (c->habits >> arg & 1);
    default: return false;
    }
}

/* ---------- selección ---------- */

static bool eligible(const PhraseCtx *c, const PhraseQuery *q, const PhraseRec *r)
{
    if (!(q->kinds & PK_BIT(r->kind)) || !(q->speakers & SPK_BIT(r->speaker)))
        return false;
    if (q->karg >= 0 && r->karg != q->karg)
        return false;
    if (r->kind == PK_ESCENA) {
        if (q->scene_line && r->kn != q->scene_line)
            return false;
        if (q->scene_start && (phrase_scene_speakers(r->karg) & ~c->present))
            return false;
    }
    if (c->grandma && r->picaro)
        return false;
    if (q->need_tag) {
        bool has = false;
        for (int i = 0; i < r->ntags; i++)
            has |= phrase_tag_kind(r, i) == q->need_tag;
        if (!has)
            return false;
    }
    for (int i = 0; i < r->ntags; i++)
        if (!tag_ok(c, r->speaker, phrase_tag_kind(r, i), phrase_tag_arg(r, i)))
            return false;
    return true;
}

static uint32_t weight(const PhraseCtx *c, const PhraseRec *r)
{
    uint32_t w = 2 + 3u * r->ntags;
    for (int i = 0; i < r->ntags; i++)
        if (phrase_tag_kind(r, i) == PT_DATE)
            w += 40; /* el día de Halloween se nota */
    /* El visitante que está en la sala habla más que lo que le tocaría por número de frases. */
    if (r->speaker != SPK_ABUELA && r->speaker != SPK_ROBOT && r->speaker != SPK_SISTEMA &&
        (c->present & SPK_BIT(r->speaker)))
        w *= 6;
    return w;
}

/* 0 = no está entre las recientes; k = salió hace k frases (LLV_RECENT = la más antigua). */
static int recent_age(const GameState *gs, uint32_t h)
{
    for (int k = 0; k < LLV_RECENT; k++) {
        int p = (gs->sala.recent_head - 1 - k + 2 * LLV_RECENT) % LLV_RECENT;
        if (gs->sala.recent[p] == h)
            return k + 1;
    }
    return 0;
}

void phrase_remember(GameState *gs, int idx)
{
    PhraseRec r;
    if (!phrase_get(idx, &r))
        return;
    gs->sala.recent[gs->sala.recent_head] = phrase_hash(&r);
    gs->sala.recent_head = (uint8_t)((gs->sala.recent_head + 1) % LLV_RECENT);
}

int phrase_eligible(const PhraseCtx *c, const PhraseQuery *q)
{
    int n = phrase_count(), k = 0;
    PhraseRec r;
    for (int i = 0; i < n; i++)
        if (phrase_get(i, &r) && eligible(c, q, &r))
            k++;
    return k;
}

int phrase_pick(const PhraseCtx *c, GameState *gs, const PhraseQuery *q)
{
    int n = phrase_count();
    PhraseRec r;
    uint64_t total = 0;
    int oldest = -1, oldest_age = 0;
    for (int i = 0; i < n; i++) {
        if (!phrase_get(i, &r) || !eligible(c, q, &r))
            continue;
        int age = recent_age(gs, phrase_hash(&r));
        if (age == 0)
            total += weight(c, &r);
        else if (age > oldest_age) {
            oldest_age = age;
            oldest = i;
        }
    }
    int pick = -1;
    if (total > 0) {
        uint64_t roll = rng_below(&gs->rng, (uint32_t)total);
        for (int i = 0; i < n && pick < 0; i++) {
            if (!phrase_get(i, &r) || !eligible(c, q, &r) || recent_age(gs, phrase_hash(&r)))
                continue;
            uint32_t w = weight(c, &r);
            if (roll < w)
                pick = i;
            else
                roll -= w;
        }
    } else {
        pick = oldest; /* el banco no da para tanto: la que más tiempo lleva sin salir */
    }
    if (pick >= 0 && q->remember)
        phrase_remember(gs, pick);
    return pick;
}

int phrase_nth(uint32_t kinds, int speaker, bool grandma, unsigned nth)
{
    int n = phrase_count(), total = 0;
    PhraseRec r;
    for (int pass = 0; pass < 2; pass++) {
        int seen = 0;
        for (int i = 0; i < n; i++) {
            if (!phrase_get(i, &r) || !(kinds & PK_BIT(r.kind)) || (speaker && r.speaker != speaker) ||
                (grandma && r.picaro))
                continue;
            if (pass == 1 && seen == (int)(nth % (unsigned)total))
                return i;
            seen++;
        }
        total = seen;
        if (total == 0)
            return -1;
    }
    return -1;
}

/* ---------- huecos ---------- */

/* Variantes de cada suceso para {evento}: sin punto final, empiezan con mayúscula. */
#define EVN 3
static const char *const EVENT_TEXT[PE_COUNT][EVN] = {
    [PE_MANZANA_CAE] = {"Cayó una manzana", "Una manzana al suelo", "Se cayó una manzana"},
    [PE_MANZANA_PODRIDA] = {"Se pudrió una manzana", "Otra manzana podrida", "Vi que se pudrió otra manzana"},
    [PE_BROTE] = {"Ha nacido un brote", "Asomó un brote nuevo", "Hay un brote nuevo"},
    [PE_ARBOL] = {"Un árbol joven se hizo mayor", "Ya hay un árbol más grande", "Un árbol ha crecido"},
    [PE_HONGO] = {"Salió un hongo", "Un hongo nuevo en el jardín", "Ha brotado un hongo"},
    [PE_FLOR_RARA] = {"Floreció una flor rara", "Hay una flor rarísima", "Se abrió una flor rara"},
    [PE_PAJARO] = {"Pasó un pájaro", "Vino un pájaro", "Un pájaro visitó el jardín"},
    [PE_SEMILLA_PAJARO] = {"Un pájaro dejó una semilla", "Un pájaro sembró algo", "Hay una semilla de pájaro"},
    [PE_ABEJA] = {"Pasó una abeja", "Una abeja visitó las flores", "Vino una abeja"},
    [PE_LOMBRIZ] = {"Salió una lombriz", "Apareció una lombriz", "Hay una lombriz nueva"},
    [PE_RANA] = {"Llegó una rana", "Hay una rana en el charco", "Apareció una rana"},
    [PE_LLUVIA] = {"Llovió", "Ha llovido", "Cayó un buen chaparrón"},
    [PE_VIENTO] = {"Sopló el viento", "Hubo ventolera", "Ha soplado fuerte"},
    [PE_NIEVE] = {"Nevó", "Ha nevado", "Cayeron copos"},
    [PE_ARCOIRIS] = {"Salió un arcoíris", "Hubo arcoíris", "Se vio un arcoíris"},
    [PE_ESTRELLA] = {"Cayó una estrella fugaz", "Pasó una estrella fugaz", "Hubo estrella fugaz"},
    [PE_ABONO] = {"Hubo abono", "El jardín tiene abono nuevo", "Han abonado la tierra"},
    [PE_DESCUBRIMIENTO] = {"Encontraste algo nuevo", "Descubriste algo", "Hay algo nuevo en la colección"},
    [PE_MASCOTA_CRECE] = {"Tu mascota creció", "La mascota dio un estirón", "Tu mascota ha cambiado"},
    [PE_MASCOTA_NACE] = {"Ha nacido una nueva generación", "Hay una mascota nueva", "Tu mascota tiene un hijo"},
    [PE_MASCOTA_CUIDADA] = {"Cuidaste a tu mascota", "Le hiciste un cariño a tu mascota", "Tu mascota recibió mimos"},
    [PE_MASCOTA_PALABRA] = {"La mascota dijo algo", "Tu mascota habló", "Se oyó una palabra"},
    [PE_ESPORAS] = {"El hongo soltó esporas", "Tu hongo esporuló", "Hay esporas en el aire"},
    [PE_COMPOSTERA_COME] = {"La compostera se comió una manzana", "La compostera ha comido", "Hubo festín en la compostera"},
    [PE_BOLSA_LLENA] = {"Se perdió un objeto", "La bolsa estaba llena", "Sobró un objeto"},
    [PE_OBJETO] = {"Usaste un objeto", "Gastaste algo de la bolsa", "Sacaste algo de la bolsa"},
    [PE_VISITA] = {"Ha llegado visita", "Vino alguien", "Hay visita"},
    [PE_HABITO] = {"Te cuidaste", "Cumpliste un hábito", "Hiciste algo bueno por ti"},
    [PE_PASEO] = {"Caminaste un montón", "Diste un buen paseo", "Anduviste mucho"},
    [PE_PACIENCIA] = {"Pasaron cosas raras mientras no mirabas", "Mientras esperabas pasó algo raro",
                      "La espera trajo algo raro"},
    [PE_CEBO] = {"Lanzaste la caña", "Una lombriz se fue a pescar", "Usaste una lombriz de cebo"},
    [PE_PEZ] = {"Pescaste un pez", "Un pez picó el anzuelo", "Hay pescado nuevo"},
    [PE_PEZ_ESCAPA] = {"Se te escapó un pez", "El pez se soltó", "Un pez se libró del anzuelo"},
    [PE_LEGENDARIO] = {"Pescaste el pez legendario", "Picó el pez de la luna llena", "¡Pez legendario!"},
    [PE_BASURA] = {"Pescaste una basura simpática", "Picó algo que no era un pez", "Sacaste del agua un trasto"},
};

static const char *const SEASON_LOW[4] = {"primavera", "verano", "otoño", "invierno"};
static const char *const WDAY_LOW[7] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
static const char *const PET_LOW[PET_COUNT] = {"", "hongo", "semilla", "piedra", "compostera"};

static void duration_text(char *buf, size_t n, int minutes)
{
    if (minutes < 60) {
        int m = minutes < 1 ? 1 : minutes;
        snprintf(buf, n, "%d minuto%s", m, m == 1 ? "" : "s");
    } else if (minutes < 48 * 60) {
        int h = (minutes + 30) / 60;
        snprintf(buf, n, "%d hora%s", h, h == 1 ? "" : "s");
    } else {
        int d = minutes / 1440;
        snprintf(buf, n, "%d días", d);
    }
}

static void event_text(char *buf, size_t n, int pe, int variant, int a)
{
    if (pe == PE_VISITA && a > 0 && a < SPK_COUNT && variant != 1) {
        snprintf(buf, n, "Vino %s", speaker_short(a));
    } else if (pe == PE_DESCUBRIMIENTO && a < COLL_COUNT && variant == 0) {
        snprintf(buf, n, "Descubriste: %s", state_coll_name((CollSlot)a));
    } else if (pe == PE_OBJETO && a > ITEM_NONE && a < ITEM_COUNT && variant == 0) {
        snprintf(buf, n, "Usaste %s", state_item_name((ItemKind)a));
    } else if ((pe == PE_PEZ || pe == PE_LEGENDARIO) && a < FISH_SPECIES && variant == 0) {
        snprintf(buf, n, "Pescaste: %s", fish_name(a));
    } else if (pe == PE_PEZ_ESCAPA && a < FISH_TOTAL && variant == 0) {
        snprintf(buf, n, "Se escapó: %s", fish_name(a));
    } else if (pe == PE_BASURA && a < FISH_JUNK && variant == 0) {
        snprintf(buf, n, "Pescaste: %s", fish_name(FISH_SPECIES + a));
    } else {
        snprintf(buf, n, "%s", EVENT_TEXT[pe][variant % EVN]);
    }
}

static int diary_arg(const GameState *gs, int ek)
{
    for (int i = 0; i < gs->diary_count; i++) {
        const DiaryEntry *d = state_diary_get(gs, i);
        if (d->kind == ek)
            return d->a;
    }
    return 0;
}

/* El suceso (PE_*) que comenta una frase, o -1. */
static int phrase_event_of(const PhraseRec *r)
{
    for (int i = 0; i < r->ntags; i++)
        if (phrase_tag_kind(r, i) == PT_EVENT)
            return phrase_tag_arg(r, i);
    return -1;
}

void phrase_fill_prepare(const GameState *gs, const PhraseCtx *c, int idx, PhraseFill *f, Rng *rng)
{
    memset(f, 0, sizeof(*f));
    PhraseRec r;
    if (!phrase_get(idx, &r))
        return;
    f->v[2] = (uint16_t)(c->hour * 60 + c->minute);
    int pe = phrase_event_of(&r);
    if (pe >= 0) {
        f->v[0] = (uint16_t)rng_below(rng, EVN);
        f->v[1] = (uint16_t)diary_arg(gs, PE_TO_EV[pe]);
    }
}

static void put_steps(char *buf, size_t n, int steps)
{
    if (steps >= 1000)
        snprintf(buf, n, "%d.%03d", steps / 1000, steps % 1000);
    else
        snprintf(buf, n, "%d", steps);
}

static void fill_hole(const GameState *gs, const PhraseRec *r, const PhraseFill *f, const char *name,
                      char *out, size_t n)
{
    CivilTime t = time_to_civil(gs->tick * LLV_TICK_SECONDS);
    out[0] = '\0';
    if (!strcmp(name, "ausencia"))
        duration_text(out, n, gs->gap_min);
    else if (!strcmp(name, "evento")) {
        int pe = phrase_event_of(r);
        if (pe >= 0)
            event_text(out, n, pe, f->v[0], f->v[1]);
    } else if (!strcmp(name, "hora"))
        snprintf(out, n, "%d:%02d", f->v[2] / 60, f->v[2] % 60);
    else if (!strcmp(name, "miradas"))
        snprintf(out, n, "%u", (unsigned)gs->looks_today);
    else if (!strcmp(name, "racha"))
        snprintf(out, n, "%u", (unsigned)gs->streak);
    else if (!strcmp(name, "estacion"))
        snprintf(out, n, "%s", SEASON_LOW[time_season(t.month, t.day, LLV_SOUTH_HEMISPHERE)]);
    else if (!strcmp(name, "dia"))
        snprintf(out, n, "%s", WDAY_LOW[t.wday]);
    else if (!strcmp(name, "mascota"))
        snprintf(out, n, "%s", PET_LOW[gs->pet.kind < PET_COUNT ? gs->pet.kind : 0]);
    else if (!strcmp(name, "pasos"))
        put_steps(out, n, gs->hab.steps_today);
}

void phrase_render(const GameState *gs, int idx, const PhraseFill *f, char *buf, size_t n)
{
    PhraseRec r;
    buf[0] = '\0';
    if (n < 2 || !phrase_get(idx, &r))
        return;
    char raw[256];
    phrase_text(&r, raw, sizeof(raw));
    size_t o = 0;
    for (const char *p = raw; *p && o + 1 < n;) {
        if (*p != '{') {
            buf[o++] = *p++;
            continue;
        }
        const char *end = strchr(p, '}');
        if (!end) {
            buf[o++] = *p++;
            continue;
        }
        char name[16], val[96];
        size_t l = (size_t)(end - p - 1);
        if (l >= sizeof(name))
            l = sizeof(name) - 1;
        memcpy(name, p + 1, l);
        name[l] = '\0';
        fill_hole(gs, &r, f, name, val, sizeof(val));
        /* En medio de una frase, el hueco empieza en minúscula ("Ay, se pudrió una manzana"). */
        bool mid = o > 0 && buf[o - 1] == ' ' && o > 1 && buf[o - 2] != '.' && buf[o - 2] != '!' &&
                   buf[o - 2] != '?' && buf[o - 2] != ':';
        if (mid && val[0] >= 'A' && val[0] <= 'Z' && val[1] >= 'a' && val[1] <= 'z')
            val[0] = (char)(val[0] + 32);
        for (size_t k = 0; val[k] && o + 1 < n; k++)
            buf[o++] = val[k];
        p = end + 1;
    }
    buf[o] = '\0';
}
