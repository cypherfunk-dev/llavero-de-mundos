/* Herramienta headless: corre el motor sin ventana con reloj virtual, simula gestos y
 * tiempo, y escribe capturas PNG/PPM. Pensada para revisar el Reloj a distintas horas
 * y para scripts de humo. Ver --help. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/director.h"
#include "../../core/engine.h"
#include "../../core/garden.h"
#include "../../core/habits.h"
#include "../../core/pet.h"
#include "../../core/phrases.h"
#include "../../core/talk.h"
#include "../../core/timeutil.h"
#include "../../util/imgout.h"
#include "../../util/wav.h"
#include "hal_audio_stub.h"

#define MAX_GRAB 48
#define MAX_LOG 6000

/* Una frase que apareció en pantalla (volcado de la semana). */
typedef struct {
    int64_t now;
    uint8_t speaker, kind;
    bool picaro;
    uint16_t hash;
    char text[160];
} Said;

typedef struct {
    Engine e;
    int64_t now;       /* segundos locales virtuales */
    uint32_t ms;       /* ms monótonos virtuales */
    uint32_t sub_ms;   /* resto de ms que aún no suma un segundo */
    FrameOut out;
    int scale;
    bool lcd, ppm;
    const char *dir;
    Framebuffer grabbed[MAX_GRAB];
    int grab_count;
    /* Estadísticas de la partida (solo observan el diario: no tocan el estado). */
    uint32_t ev_seen, ev_lost;
    uint32_t ev_hist[EV_KIND_COUNT];
    uint32_t items_used[ITEM_COUNT];
    uint32_t samples, ent_sum, ent_max;
    uint32_t sp_sum[SP_COUNT];
    uint32_t sp_max[SP_COUNT];
    /* Cuerpo simulado (fase 3). */
    uint8_t battery;
    bool charging;
    uint32_t steps;
    /* Frases vistas y estadísticas de la Sala. */
    Said said[MAX_LOG];
    int said_n;
    Rng rng;                      /* la "persona" que usa el llavero: aleatoria pero siempre igual */
    uint32_t surprise_hist[DS_COUNT];
    uint32_t world_open[LLV_WORLD_COUNT];
    uint32_t looks_sim;
    /* Música (fase 4): el audio pasa por el mismo camino que en el simulador; aquí no suena, pero se
     * puede grabar a WAV con rec / recstop. */
    uint32_t audio_carry;     /* resto de muestras por ms */
    int16_t *rec;             /* NULL = sin grabar */
    size_t rec_n, rec_cap;
    char rec_path[512];
} Sim;

static Sim *g_sim; /* para el observador de frases */

static void on_said(void *user, int speaker, int kind, bool picaro, const char *text)
{
    Sim *s = user;
    if (s->said_n >= MAX_LOG)
        return;
    Said *d = &s->said[s->said_n++];
    d->now = s->now;
    d->speaker = (uint8_t)speaker;
    d->kind = (uint8_t)kind;
    d->picaro = picaro;
    uint32_t h = 2166136261u;
    for (const char *p = text; *p; p++)
        h = (h ^ (uint8_t)*p) * 16777619u;
    d->hash = (uint16_t)(h ^ h >> 16);
    snprintf(d->text, sizeof(d->text), "%s", text);
}

/* Cuenta lo nuevo del diario. Si se acumulan más de 64 eventos entre dos miradas, los
 * que se pisaron se cuentan aparte (ev_lost). */
static void track(Sim *s)
{
    const GameState *g = &s->e.gs;
    uint32_t fresh = g->diary_total - s->ev_seen;
    if (fresh == 0)
        return;
    uint32_t n = fresh > g->diary_count ? g->diary_count : fresh;
    s->ev_lost += fresh - n;
    for (uint32_t i = n; i-- > 0;) {
        const DiaryEntry *d = state_diary_get(g, (int)i);
        s->ev_hist[d->kind]++;
        if (d->kind == EV_ITEM_USED && d->a < ITEM_COUNT)
            s->items_used[d->a]++;
    }
    s->ev_seen = g->diary_total;
}

/* Foto del jardín en este momento, para las medias de la semana. */
static void sample(Sim *s)
{
    const GameState *g = &s->e.gs;
    int n = garden_count(g);
    s->samples++;
    s->ent_sum += (uint32_t)n;
    if ((uint32_t)n > s->ent_max)
        s->ent_max = (uint32_t)n;
    for (int sp = 1; sp < SP_COUNT; sp++) {
        uint32_t c = (uint32_t)garden_count_sp(g, (uint8_t)sp, ST_ANY);
        s->sp_sum[sp] += c;
        if (c > s->sp_max[sp])
            s->sp_max[sp] = c;
    }
}

#define REC_MAX_SECONDS 120

static void on_audio(void *user, const int16_t *pcm, size_t n)
{
    Sim *s = user;
    if (!s->rec)
        return;
    for (size_t i = 0; i < n && s->rec_n < s->rec_cap; i++)
        s->rec[s->rec_n++] = pcm[i];
}

/* Lo que haría el bucle del simulador: el reloj pide muestras y el motor las produce. */
static void audio_step(Sim *s, uint32_t dt)
{
    uint32_t total = s->audio_carry + dt * LLV_AUDIO_RATE;
    hal_audio_stub_advance(total / 1000u);
    s->audio_carry = total % 1000u;
    hal_audio_pump(&s->e);
}

static void frame(Sim *s, uint32_t dt, uint8_t buttons, bool shake)
{
    s->ms += dt;
    s->sub_ms += dt;
    s->now += s->sub_ms / 1000;
    s->sub_ms %= 1000;
    RawInput in = {buttons, shake};
    engine_set_body(&s->e, s->battery, s->charging, s->steps);
    s->out = engine_frame(&s->e, in, s->now, s->ms);
    audio_step(s, dt);
    track(s);
}

static void idle(Sim *s, uint32_t total_ms)
{
    for (uint32_t t = 0; t < total_ms; t += 50)
        frame(s, 50, 0, false);
}

/* Salta 'secs' de tiempo. Si estaba despierto, deja que el llavero se duerma solo antes. */
static void advance(Sim *s, int64_t secs)
{
    int64_t guard = LLV_SLEEP_AFTER_MS / 1000 + 2;
    if (!s->e.asleep && secs > guard) {
        idle(s, (uint32_t)guard * 1000);
        secs -= guard;
    }
    if (s->e.asleep) {
        s->now += secs;
        s->ms += (uint32_t)(secs * 1000);
    } else {
        idle(s, (uint32_t)(secs * 1000));
    }
}

static void press(Sim *s, uint8_t buttons, uint32_t ms_down)
{
    for (uint32_t t = 0; t < ms_down; t += 20)
        frame(s, 20, buttons, false);
    frame(s, 20, 0, false);
    idle(s, 100);
}

static void tap(Sim *s, uint8_t b) { press(s, b, 80); }
static void hold(Sim *s, uint8_t b) { press(s, b, LLV_HOLD_MS + 150); }

static void ab(Sim *s)
{
    frame(s, 20, BTN_A, false);
    for (int i = 0; i < 4; i++)
        frame(s, 20, BTN_A | BTN_B, false);
    press(s, 0, 20);
}

static void wake_if_needed(Sim *s)
{
    if (s->e.asleep) {
        frame(s, 20, 0, true);
        idle(s, LLV_COLLAPSE_MS + 150);
        /* Las sorpresas de arranque (consola falsa, colapso largo) se llevan los gestos un rato. */
        for (int guard = 0; guard < 200 && (ms_pending(s->ms, s->e.collapse_until_ms) ||
                                            ms_pending(s->ms, s->e.boot_until_ms)); guard++)
            frame(s, 20, 0, false);
    }
}

static void goto_world(Sim *s, int w)
{
    wake_if_needed(s);
    for (int guard = 0; s->e.gs.current_world != w && guard < LLV_WORLD_COUNT; guard++)
        tap(s, BTN_B);
}

static void set_style(Sim *s, int style)
{
    goto_world(s, 0);
    for (int guard = 0; s->e.gs.reloj_style != style && guard < 8; guard++)
        tap(s, BTN_A);
    idle(s, 1500); /* que se apague la etiqueta del estilo */
}

static int bits(unsigned v)
{
    int n = 0;
    for (; v; v >>= 1)
        n += (int)(v & 1u);
    return n;
}

/* Elige la mascota (1 hongo, 2 semilla, 3 piedra, 4 compostera) como lo haría una persona. */
static void choose_pet(Sim *s, int kind)
{
    goto_world(s, 5);
    if (s->e.gs.pet.kind != PET_NONE)
        return;
    cobertizo_ui_reset();
    for (int k = PET_HONGO; k < kind; k++)
        tap(s, BTN_A);
    hold(s, BTN_A);
    idle(s, 600);
}

static void goto_pet(Sim *s)
{
    goto_world(s, 5);
    if (s->e.gs.pet.kind == PET_NONE)
        choose_pet(s, PET_HONGO);
    cobertizo_ui_reset();
    for (int guard = 0; guard < 8 && !cobertizo_on_pet(&s->e.gs); guard++)
        tap(s, BTN_A);
}

/* Una mirada del día, como las haría alguien normal. */
static void look(Sim *s, int which)
{
    wake_if_needed(s);
    switch (which) {
    case 0: /* mañana: jardín, una acción y un vistazo con la lupa */
        goto_world(s, 1);
        idle(s, 2500);
        tap(s, BTN_A);
        idle(s, 3500);
        hold(s, BTN_A);
        idle(s, 1500);
        break;
    case 1: /* mediodía: ve cómo va, saluda a la mascota y respira con ella */
        goto_world(s, 1);
        idle(s, 3000);
        goto_pet(s);
        hold(s, BTN_A); /* cuidado del día */
        idle(s, 1500);
        tap(s, BTN_A);  /* empieza a respirar */
        press(s, BTN_A, 3600);
        idle(s, 3600);
        tap(s, BTN_A);  /* sale */
        break;
    case 2: /* tarde: prepara un objeto y lo usa en el jardín */
        goto_world(s, 5);
        cobertizo_ui_reset();
        hold(s, BTN_A);
        idle(s, 1500);
        goto_world(s, 1);
        idle(s, 1500);
        tap(s, BTN_A);
        idle(s, 3500);
        break;
    default: /* noche: la hora y un último vistazo */
        goto_world(s, 0);
        idle(s, 2000);
        goto_world(s, 1);
        idle(s, 3000);
        break;
    }
    sample(s);
}

static const char *const EV_LABEL[EV_KIND_COUNT];

/* Resumen de lo que pasó en el día (sin contar lo rutinario: despertares, bolsa, tus acciones). */
static void day_summary(Sim *s, uint32_t *prev, char *out, size_t n)
{
    static const uint8_t MUNDANE[] = {EV_NONE, EV_BAG_REFILL, EV_BAG_FULL_LOST, EV_NEW_DAY, EV_WAKE,
                                      EV_ITEM_USED, EV_PET_CARE, EV_DISCOVER};
    uint32_t total = 0, delta[EV_KIND_COUNT];
    for (int k = 0; k < EV_KIND_COUNT; k++) {
        delta[k] = s->ev_hist[k] - prev[k];
        prev[k] = s->ev_hist[k];
        bool skip = false;
        for (unsigned m = 0; m < sizeof(MUNDANE); m++)
            skip |= MUNDANE[m] == k;
        if (skip)
            delta[k] = 0;
        total += delta[k];
    }
    size_t len = (size_t)snprintf(out, n, "%u sucesos:", total);
    for (int shown = 0; shown < 4; shown++) { /* los 4 más repetidos */
        int best = -1;
        for (int k = 1; k < EV_KIND_COUNT; k++)
            if (delta[k] && (best < 0 || delta[k] > delta[best]))
                best = k;
        if (best < 0)
            break;
        if (len < n)
            len += (size_t)snprintf(out + len, n - len, "%s%s %u", shown ? ", " : " ", EV_LABEL[best], delta[best]);
        delta[best] = 0;
    }
}

/* N días naturales empezando por hoy (las miradas que ya pasaron hoy se saltan). */
static void routine(Sim *s, int days)
{
    static const int LOOK_AT[4][2] = {{7, 30}, {12, 30}, {18, 30}, {22, 30}};
    int64_t midnight = time_floor_div(s->now, LLV_SECONDS_PER_DAY) * LLV_SECONDS_PER_DAY;
    uint32_t prev[EV_KIND_COUNT];
    memcpy(prev, s->ev_hist, sizeof(prev));
    for (int d = 0; d < days; d++) {
        for (int l = 0; l < 4; l++) {
            int64_t at = midnight + (int64_t)d * LLV_SECONDS_PER_DAY + LOOK_AT[l][0] * 3600 + LOOK_AT[l][1] * 60;
            if (at < s->now)
                continue;
            advance(s, at - s->now);
            look(s, l);
        }
        const GameState *g = &s->e.gs;
        CivilTime c = time_to_civil(s->now);
        char what[160];
        day_summary(s, prev, what, sizeof(what));
        printf("# %04d-%02d-%02d: %2d cosas, bolsa %d, colección %2d/16 | %s\n", c.year, c.month, c.day,
               garden_count(g), g->bag_count, bits(g->collection), what);
    }
}

static const char *const SP_LABEL[SP_COUNT] = {
    "", "semilla", "brote", "árbol joven", "árbol", "manzana", "hongo", "flor", "abeja",
    "pájaro", "lombriz", "charco", "rana", "piedra", "hojas", "nieve",
};

static const char *const EV_LABEL[EV_KIND_COUNT] = {
    [EV_BAG_REFILL] = "objeto a la bolsa", [EV_BAG_FULL_LOST] = "bolsa llena (perdido)",
    [EV_NEW_DAY] = "días nuevos", [EV_WAKE] = "despertares", [EV_ITEM_USED] = "objetos usados",
    [EV_APPLE_FALL] = "cae manzana", [EV_APPLE_ROT] = "se pudre manzana", [EV_SPROUT] = "nace brote",
    [EV_TREE_ADULT] = "árbol joven -> árbol", [EV_MUSHROOM] = "sale hongo", [EV_BLOOM] = "flor rara",
    [EV_BIRD] = "llega pájaro", [EV_BIRD_SEED] = "pájaro deja semilla", [EV_BEE] = "abeja",
    [EV_WORM] = "sale lombriz", [EV_FROG] = "rana", [EV_RAIN] = "lluvia", [EV_WIND] = "viento (con fruta)",
    [EV_SNOW] = "nieve", [EV_RAINBOW] = "arcoíris", [EV_STAR] = "estrella fugaz", [EV_COMPOST] = "abono",
    [EV_DISCOVER] = "descubrimientos", [EV_PET_CHOSEN] = "mascota elegida", [EV_PET_STAGE] = "mascota crece",
    [EV_PET_GEN] = "mascota: generación", [EV_PET_CARE] = "cuidados", [EV_PET_WORD] = "palabra del hongo",
    [EV_PET_SPORES] = "esporas", [EV_PET_COMPOST] = "compostera come",
    [EV_VISITOR] = "visitas a la Sala", [EV_HABIT] = "hábitos cumplidos", [EV_WALK] = "paseos largos",
    [EV_PATIENCE] = "bono de paciencia",
};

static void stats(const Sim *s)
{
    const GameState *g = &s->e.gs;
    printf("== estado del jardín ==\n");
    printf("cosas ahora: %d/%d |", garden_count(g), LLV_GARDEN_MAX);
    for (int sp = 1; sp < SP_COUNT; sp++) {
        int c = garden_count_sp(g, (uint8_t)sp, ST_ANY);
        if (c)
            printf(" %s %d", SP_LABEL[sp], c);
    }
    printf("\nclima %d, cielo %d, colección %d/16 (0x%04x)\n", g->weather, g->sky,
           bits(g->collection), g->collection);
    if (g->pet.kind) {
        printf("mascota: %s gen %u día %d etapa '%s' humor %s\n", pet_name((PetKind)g->pet.kind), g->pet.gen,
               pet_day(&g->pet), pet_stage_name(&g->pet), pet_mood(&g->pet));
    }
    if (s->samples) {
        printf("== medias en %u miradas: %.1f cosas (máx %u) ==\n", s->samples,
               (double)s->ent_sum / s->samples, s->ent_max);
        for (int sp = 1; sp < SP_COUNT; sp++)
            printf("  %-12s media %.2f  máx %u\n", SP_LABEL[sp], (double)s->sp_sum[sp] / s->samples,
                   s->sp_max[sp]);
    }
    printf("== eventos del diario ==\n");
    for (int k = 1; k < EV_KIND_COUNT; k++)
        if (s->ev_hist[k])
            printf("  %-24s %u\n", EV_LABEL[k], s->ev_hist[k]);
    if (s->ev_lost)
        printf("  (eventos pisados por el diario circular: %u)\n", s->ev_lost);
    printf("== objetos usados ==\n");
    for (int k = 1; k < ITEM_COUNT; k++)
        printf("  %-8s %u\n", state_item_name((ItemKind)k), s->items_used[k]);
}


/* ---------- la Sala (fase 3) ---------- */

#define TYPE_WAIT_MS 3900   /* lo que tarda en escribirse la frase más larga */

static void talk_a(Sim *s)
{
    wake_if_needed(s);
    tap(s, BTN_A);
    idle(s, TYPE_WAIT_MS);
}

static void answer(Sim *s, bool yes)
{
    wake_if_needed(s);
    if (yes)
        tap(s, BTN_A);
    else
        hold(s, BTN_A);
    idle(s, TYPE_WAIT_MS);
}

static void bell(Sim *s)
{
    wake_if_needed(s);
    hold(s, BTN_A);
    idle(s, TYPE_WAIT_MS);
}

/* El código secreto B B A A B A B A, con toques rápidos. */
static void secret_code(Sim *s)
{
    wake_if_needed(s);
    static const char CODE[] = "BBAABABA";
    for (int i = 0; CODE[i]; i++) {
        press(s, CODE[i] == 'A' ? BTN_A : BTN_B, 60);
        idle(s, 120);
    }
    idle(s, TYPE_WAIT_MS);
}

static PhraseEnv sim_env(const Sim *s)
{
    PhraseEnv e = {s->now, false, s->battery, s->charging};
    return e;
}

static int speaker_by_name(const char *n)
{
    for (int i = 1; i < SPK_COUNT; i++)
        if (!strcmp(n, speaker_vocab(i)))
            return i;
    return 0;
}

static int surprise_by_name(const char *n)
{
    static const char *const N[DS_COUNT] = {"", "glitch", "arranque", "ausencia", "invasion", "cuarta",
                                           "calendario", "rebeldia", "colapso", "gato", "moneda"};
    for (int i = 1; i < DS_COUNT; i++)
        if (!strcmp(n, N[i]))
            return i;
    return 0;
}

/* Una mirada de la semana: abre lo que decida el Director, atiende sus sorpresas y habla con la gente. */
static void week_look(Sim *s)
{
    wake_if_needed(s);
    GameState *g = &s->e.gs;
    s->looks_sim++;
    s->world_open[g->current_world]++;
    s->surprise_hist[g->dir.cur]++;
    s->steps += rng_below(&s->rng, 2200);
    s->battery = (uint8_t)(10 + rng_below(&s->rng, 80));
    idle(s, 600);
    /* Atiende la sorpresa como lo haría una persona. */
    switch (g->dir.cur) {
    case DS_GLITCH:
        idle(s, 1500);
        frame(s, 20, 0, true); /* agita */
        idle(s, 600);
        break;
    case DS_MONEDA:
        idle(s, 1200);
        tap(s, BTN_A);
        break;
    case DS_GATO:
    case DS_CUARTA_PARED:
    case DS_CALENDARIO:
        idle(s, TYPE_WAIT_MS);
        break;
    case DS_REBELDIA:
        if (rng_below(&s->rng, 100) < 70) {
            tap(s, BTN_B);
            idle(s, TYPE_WAIT_MS);
        }
        break;
    default:
        break;
    }
    uint32_t r = rng_below(&s->rng, 100);
    if (r < 70) { /* a charlar */
        goto_world(s, WORLD_SALA);
        idle(s, 800);
        int convs = 1 + (rng_below(&s->rng, 100) < 45 ? 1 : 0) + (rng_below(&s->rng, 100) < 10 ? 1 : 0);
        for (int c = 0; c < convs; c++) {
            if (rng_below(&s->rng, 100) < 12)
                bell(s);
            else
                talk_a(s);
            for (int guard = 0; guard < 8; guard++) {
                const Sala *sa = &s->e.gs.sala;
                if (sa->stage == DLG_ASK) {
                    answer(s, rng_below(&s->rng, 100) < 70);
                } else if (sa->stage == DLG_SAY && sa->scene > 0) {
                    talk_a(s);
                } else {
                    break;
                }
            }
        }
        if (rng_below(&s->rng, 100) < 15) { /* agita el llavero */
            frame(s, 20, 0, true);
            idle(s, TYPE_WAIT_MS);
        }
    } else if (r < 85) { /* un vistazo al jardín y un objeto */
        goto_world(s, WORLD_JARDIN);
        idle(s, 2000);
        tap(s, BTN_A);
        idle(s, 2500);
    } else { /* mascota y respirar */
        look(s, 1);
    }
    idle(s, 1500);
}

static void week(Sim *s, int days)
{
    int64_t midnight = time_floor_div(s->now, LLV_SECONDS_PER_DAY) * LLV_SECONDS_PER_DAY;
    static const int SLOT[6] = {450, 570, 750, 930, 1110, 1290}; /* minutos del día: 7:30 ... 21:30 */
    for (int d = 0; d < days; d++) {
        int n = 4 + (int)rng_below(&s->rng, 3);
        int times[8], k = 0;
        if (rng_below(&s->rng, 100) < 30) /* mirada de madrugada */
            times[k++] = 60 + (int)rng_below(&s->rng, 200);
        for (int i = 0; i < n && k < 8; i++)
            times[k++] = SLOT[i] + (int)rng_below(&s->rng, 70) - 35;
        if (d == 4) { /* un día de "ausencia": solo se asoma por la noche */
            times[0] = 1290;
            k = 1;
        }
        for (int i = 0; i < k; i++) {
            int64_t at = midnight + (int64_t)d * LLV_SECONDS_PER_DAY + times[i] * 60;
            if (at < s->now)
                continue;
            advance(s, at - s->now);
            week_look(s);
        }
    }
}

static const char *const KIND_LABEL[PK_COUNT] = {
    [PK_CHARLA] = "charla", [PK_RELLENO] = "relleno", [PK_SINACCIONES] = "sin acciones",
    [PK_CUARTA_PARED] = "cuarta pared", [PK_REBELDIA] = "rebeldía", [PK_TIMBRE] = "timbre",
    [PK_INVASION] = "invasión", [PK_PREGUNTA] = "pregunta", [PK_SI] = "reacción sí",
    [PK_NO] = "reacción no", [PK_MENTIRA] = "mentira", [PK_RETOMA] = "retoma", [PK_ESCENA] = "escena",
    [PK_COMENTARIO] = "comentario", [PK_GLITCH] = "glitch", [PK_GLITCH_OK] = "glitch ok",
    [PK_CARTEL] = "cartel", [PK_MONEDA] = "moneda",
};

static void dump_phrases(const Sim *s, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "no se pudo escribir %s\n", path);
        return;
    }
    fprintf(f, "# Frases que aparecieron en pantalla (%d), en orden. (p) = tono pícaro.\n", s->said_n);
    int last_day = -1;
    for (int i = 0; i < s->said_n; i++) {
        const Said *d = &s->said[i];
        CivilTime c = time_to_civil(d->now);
        int day = (int)time_floor_div(d->now, LLV_SECONDS_PER_DAY);
        if (day != last_day) {
            fprintf(f, "\n## %04d-%02d-%02d\n", c.year, c.month, c.day);
            last_day = day;
        }
        fprintf(f, "%02d:%02d  %-8s %-12s %s%s\n", c.hour, c.minute,
                d->speaker == SPK_SISTEMA ? "Sistema" : speaker_short(d->speaker),
                KIND_LABEL[d->kind], d->picaro ? "(p) " : "", d->text);
    }
    fclose(f);
    printf("%s\n", path);
}

static void sala_stats(const Sim *s, int64_t start)
{
    int total = s->said_n, distinct = 0, repeats30 = 0, picaro = 0;
    int per_spk[SPK_COUNT] = {0}, per_kind[PK_COUNT] = {0};
    int days = 1 + (int)(time_floor_div(s->now, LLV_SECONDS_PER_DAY) - time_floor_div(start, LLV_SECONDS_PER_DAY));
    if (days > 16)
        days = 16;
    int q_day[16] = {0}, said_day[16] = {0}, hab_q_day[16] = {0};
    int64_t day0 = time_floor_div(start, LLV_SECONDS_PER_DAY);
    for (int i = 0; i < total; i++) {
        const Said *d = &s->said[i];
        bool seen = false;
        for (int j = 0; j < i && !seen; j++)
            seen = s->said[j].hash == d->hash && s->said[j].speaker == d->speaker;
        distinct += !seen;
        for (int j = i > 30 ? i - 30 : 0; j < i; j++)
            if (s->said[j].hash == d->hash && s->said[j].speaker == d->speaker &&
                d->kind != PK_ESCENA && d->kind != PK_SI && d->kind != PK_NO) {
                repeats30++;
                break;
            }
        picaro += d->picaro;
        per_spk[d->speaker]++;
        per_kind[d->kind]++;
        int day = (int)(time_floor_div(d->now, LLV_SECONDS_PER_DAY) - day0);
        if (day >= 0 && day < 16) {
            said_day[day]++;
            if (d->kind == PK_PREGUNTA)
                q_day[day]++;
        }
    }
    (void)hab_q_day;
    printf("== La Sala: %d miradas simuladas, %d frases mostradas ==\n", (int)s->looks_sim, total);
    printf("frases distintas vistas: %d  |  repetidas: %d (%d %%)  |  repetidas dentro de las últimas 30: %d\n",
           distinct, total - distinct, total ? 100 * (total - distinct) / total : 0, repeats30);
    printf("pícaras: %d (%d %%)\n", picaro, total ? 100 * picaro / total : 0);
    printf("por personaje:");
    for (int i = 1; i < SPK_COUNT; i++)
        if (per_spk[i])
            printf(" %s %d", i == SPK_SISTEMA ? "Sistema" : speaker_short(i), per_spk[i]);
    printf("\npor propósito:");
    for (int i = 0; i < PK_COUNT; i++)
        if (per_kind[i])
            printf(" %s %d", KIND_LABEL[i], per_kind[i]);
    printf("\nfrases por día:");
    for (int d = 0; d < days; d++)
        printf(" %d", said_day[d]);
    printf("\npreguntas por día:");
    for (int d = 0; d < days; d++)
        printf(" %d", q_day[d]);
    uint32_t surprises = 0;
    printf("\nsorpresas (por tipo):");
    for (int i = 1; i < DS_COUNT; i++) {
        surprises += s->surprise_hist[i];
        if (s->surprise_hist[i])
            printf(" %s %u", surprise_name(i), s->surprise_hist[i]);
    }
    printf("\nsorpresas: %u en %d días (%.1f al día), tiradas del Director: %u\n", surprises, days,
           (double)surprises / (double)days, (unsigned)s->e.gs.dir.rolls);
    printf("mundo abierto al despertar:");
    static const char *const WN[LLV_WORLD_COUNT] = {"Reloj", "Jardín", "Sala", "Música", "Pesca", "Cobertizo"};
    for (int i = 0; i < LLV_WORLD_COUNT; i++)
        printf(" %s %u", WN[i], s->world_open[i]);
    printf("\n");
    const GameState *g = &s->e.gs;
    printf("memoria de respuestas:");
    for (int q = 0; q < Q_COUNT; q++)
        if (g->sala.memo[q] & 3)
            printf(" %s=%s", phrase_question_vocab(q), (g->sala.memo[q] & 3) == 1 ? "sí" : "no");
    printf("\nhábitos cumplidos hoy 0x%02x, racha de sueño %u, racha de días %u, afinidad: abuela %u, robot %u\n",
           g->hab.done_today, g->hab.sleep_streak, g->streak, g->sala.affinity[SPK_ABUELA],
           g->sala.affinity[SPK_ROBOT]);
    uint32_t habits = s->ev_hist[EV_HABIT];
    printf("hábitos cumplidos en total: %u, paseos largos: %u\n", habits, s->ev_hist[EV_WALK]);
}

static void sala_info(const Sim *s)
{
    const Sala *sa = &s->e.gs.sala;
    char buf[256];
    talk_text(&s->e.gs, buf, sizeof(buf));
    printf("sala: visitante %s | etapa %d | habla %s | quedan %d conversaciones | dir.cur %s | LED %d | ❗ 0x%02x | \"%s\"\n",
           sa->visitor ? speaker_short(sa->visitor) : "-", sa->stage,
           sa->who ? speaker_short(sa->who) : "-", LLV_SALA_ACTIONS - (int)s->e.gs.actions_used[WORLD_SALA],
           surprise_name(s->e.gs.dir.cur), s->out.led, s->e.gs.news, buf);
}

/* ---------- parseo ---------- */

static bool parse_date(const char *str, int64_t *out)
{
    int y, mo, d, h = 0, mi = 0, sec = 0;
    int n = sscanf(str, "%d-%d-%d%*[T ]%d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
    if (n < 3 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 59)
        return false;
    *out = time_from_civil(y, mo, d, h, mi, sec);
    return true;
}

static bool parse_duration(const char *str, int64_t *out)
{
    char *end;
    long long v = strtoll(str, &end, 10);
    if (end == str)
        return false;
    switch (*end) {
    case 's': case '\0': *out = v; break;
    case 'm': *out = v * 60; break;
    case 'h': *out = v * 3600; break;
    case 'd': *out = v * 86400; break;
    case 'w': *out = v * 7 * 86400; break;
    default: return false;
    }
    return true;
}

/* ---------- salida ---------- */

static uint32_t pixbuf[LLV_W * IMG_MAX_SCALE * LLV_H * IMG_MAX_SCALE];
static uint32_t sheetbuf[4096 * 4096 / 2];

static int write_img(const Sim *s, const char *name, const uint32_t *px, int w, int h)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.%s", s->dir, name, s->ppm ? "ppm" : "png");
    int rc = s->ppm ? img_write_ppm(path, px, w, h) : img_write_png(path, px, w, h);
    if (rc)
        fprintf(stderr, "no se pudo escribir %s\n", path);
    else
        printf("%s\n", path);
    return rc;
}

static int do_grab(Sim *s, const char *name);

static int do_shot(Sim *s, const char *name)
{
    wake_if_needed(s);
    idle(s, 100);
    return do_grab(s, name);
}

/* Captura sin esperar nada más (para ver el estado con un botón pulsado, un mensaje, etc.). */
static int do_grab(Sim *s, const char *name)
{
    if (s->grab_count < MAX_GRAB)
        s->grabbed[s->grab_count++] = s->e.fb;
    img_render(&s->e.fb, s->scale, s->lcd, pixbuf);
    return write_img(s, name, pixbuf, LLV_W * s->scale, LLV_H * s->scale);
}

/* Mantiene A pulsado 'ms' milisegundos, captura con el botón aún pulsado y lo suelta. */
static int do_shot_held(Sim *s, const char *name, uint32_t ms)
{
    wake_if_needed(s);
    for (uint32_t t = 0; t < ms; t += 20)
        frame(s, 20, BTN_A, false);
    int rc = do_grab(s, name);
    frame(s, 20, 0, false);
    idle(s, 100);
    return rc;
}

/* Contactos: junta las capturas desde la última hoja en una cuadrícula. */
static int do_sheet(Sim *s, const char *name, int cols)
{
    if (s->grab_count == 0 || cols < 1)
        return 1;
    int sc = s->scale;
    int cw = LLV_W * sc, ch = LLV_H * sc, gap = 6;
    int rows = (s->grab_count + cols - 1) / cols;
    int w = cols * cw + (cols + 1) * gap, h = rows * ch + (rows + 1) * gap;
    if ((size_t)w * (size_t)h > sizeof(sheetbuf) / sizeof(sheetbuf[0])) {
        fprintf(stderr, "hoja demasiado grande\n");
        return 1;
    }
    for (int i = 0; i < w * h; i++)
        sheetbuf[i] = 0x101018;
    for (int k = 0; k < s->grab_count; k++) {
        img_render(&s->grabbed[k], sc, s->lcd, pixbuf);
        int ox = gap + (k % cols) * (cw + gap), oy = gap + (k / cols) * (ch + gap);
        for (int y = 0; y < ch; y++)
            memcpy(&sheetbuf[(oy + y) * w + ox], &pixbuf[y * cw], (size_t)cw * sizeof(uint32_t));
    }
    s->grab_count = 0;
    return write_img(s, name, sheetbuf, w, h);
}

static void info(const Sim *s)
{
    const GameState *g = &s->e.gs;
    CivilTime c = time_to_civil(s->now);
    printf("%04d-%02d-%02d %02d:%02d:%02d | tick %lld | mundo %d | bolsa %d | diario %d | "
           "miradas %u | %s\n",
           c.year, c.month, c.day, c.hour, c.minute, c.second, (long long)g->tick,
           g->current_world, g->bag_count, g->diary_count, (unsigned)g->looks_total,
           s->e.asleep ? "dormido" : "despierto");
}

static void usage(const char *p)
{
    printf("Uso: %s [opciones] comando [comando...]\n"
           "Opciones:\n"
           "  --seed N       semilla (por defecto 1)\n"
           "  --date FECHA   inicio, 'AAAA-MM-DD[ HH:MM[:SS]]' (por defecto 2024-06-15 12:00)\n"
           "  --out DIR      carpeta de salida (por defecto .)\n"
           "  --scale N      ampliación de las capturas, 1-8 (por defecto 3)\n"
           "  --ppm          escribe PPM en vez de PNG\n"
           "  --lcd          rejilla LCD en las capturas\n"
           "Comandos (en orden):\n"
           "  shot NOMBRE    despierta si hace falta y captura NOMBRE.png\n"
           "  shotheld NOMBRE MS  mantiene A pulsado MS ms y captura con el botón pulsado\n"
           "  sheet NOMBRE C hoja de contacto con las capturas previas en C columnas\n"
           "  adv DUR        pasa el tiempo (90s 30m 6h 2d 1w); se duerme solo\n"
           "  at FECHA       salta a esa fecha (hacia delante)\n"
           "  style N        Reloj en el estilo 0-4 (0 celeste 1 palabras 2 binario 3 cuántico 4 cuenta)\n"
           "  world N        va al mundo 0-5 con B\n"
           "  tap a|b  hold a|b  ab  shake  sleep  wake  wait MS\n"
           "  pet N          elige mascota (1 hongo, 2 semilla, 3 piedra, 4 compostera)\n"
           "  give N         (pruebas) mete el objeto N (1 nube 2 viento 3 semilla 4 alpiste 5 lombriz 6 deseo)\n"
           "                 en la bolsa y lo deja preparado para la próxima acción del Jardín\n"
           "  routine DIAS   uso normal: 4 miradas al día (jardín, mascota, objetos) durante DIAS días\n"
           "  stats          estado del jardín, medias, eventos y objetos usados\n"
           "La Sala (fase 3):\n"
           "  talk           toca A en la Sala (da pie) y espera a que se escriba la frase\n"
           "  yes | no       contesta la pregunta pendiente (toque / mantener A)\n"
           "  bell           mantiene A: toca el timbre\n"
           "  drain          gasta las conversaciones del día y pide otra (sale el relleno);  pool  frases posibles ahora\n"
           "  code           teclea el código secreto B B A A B A B A\n"
           "  visitor NOMBRE gotica|gamer|boomer|gato|none: pone un visitante en la Sala\n"
           "  surprise TIPO  glitch|arranque|ausencia [mundo]|invasion|cuarta|calendario|rebeldia|colapso|gato|moneda\n"
           "  ask PREGUNTA   fuerza esa pregunta (agua, dormir, plantas...)\n"
           "  grandma on|off modo apto para la abuela;  habit NOMBRE on|off  activa o desactiva un hábito\n"
           "  steps N | battery N | streak N | news MASCARA   cuerpo simulado y estados de prueba\n"
           "  week DIAS      una semana de uso: varias miradas al día, charlas, preguntas y sorpresas\n"
           "  salastats      estadísticas de la Sala (tras week);  dumpfrases ARCHIVO  volcado de todas las frases\n"
           "  sala           resumen del estado de la Sala;  led  mira el LED de aviso con el llavero dormido\n"
           "Música (fase 4):\n"
           "  weather N | sky N   clima (0 despejado 1 lluvia 2 viento 3 nieve) y cielo (0 nada 1 estrella 2 arcoíris)\n"
           "  musicseed S    semilla de la melodía (0..65535), sin mutaciones;  music  describe lo que suena\n"
           "  rec ARCHIVO    empieza a grabar el audio (lo que sonaría) a un WAV;  recstop  lo cierra y lo escribe\n"
           "  info           imprime un resumen del estado\n",
           p);
}

int main(int argc, char **argv)
{
    static Sim s;
    uint64_t seed = 1;
    int64_t start, week_start = 0;
    parse_date("2024-06-15 12:00", &start);
    s.scale = 3;
    s.dir = ".";

    int i = 1;
    for (; i < argc && argv[i][0] == '-'; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "--seed") && more) {
            seed = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(a, "--date") && more) {
            if (!parse_date(argv[++i], &start)) {
                fprintf(stderr, "fecha no válida: %s\n", argv[i]);
                return 2;
            }
        } else if (!strcmp(a, "--out") && more) {
            s.dir = argv[++i];
        } else if (!strcmp(a, "--scale") && more) {
            s.scale = atoi(argv[++i]);
            if (s.scale < 1 || s.scale > IMG_MAX_SCALE)
                s.scale = 3;
        } else if (!strcmp(a, "--ppm")) {
            s.ppm = true;
        } else if (!strcmp(a, "--lcd")) {
            s.lcd = true;
        } else {
            fprintf(stderr, "opción desconocida: %s\n", a);
            return 2;
        }
    }

    s.now = start;
    s.ms = 1000;
    s.battery = 80;
    g_sim = &s;
    phrase_set_observer(on_said, &s);
    engine_init(&s.e, seed, s.now, s.ms);
    idle(&s, LLV_COLLAPSE_MS + 150);

    int rc = 0;
    for (; i < argc && rc == 0; i++) {
        const char *c = argv[i];
        bool m1 = i + 1 < argc, m2 = i + 2 < argc;
        int64_t v;
        if (!strcmp(c, "shot") && m1) {
            rc = do_shot(&s, argv[++i]);
        } else if (!strcmp(c, "sheet") && m2) {
            rc = do_sheet(&s, argv[i + 1], atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(c, "adv") && m1 && parse_duration(argv[++i], &v)) {
            advance(&s, v);
        } else if (!strcmp(c, "at") && m1) {
            if (!parse_date(argv[++i], &v) || v < s.now) {
                fprintf(stderr, "fecha no válida o en el pasado: %s\n", argv[i]);
                rc = 2;
            } else {
                advance(&s, v - s.now);
            }
        } else if (!strcmp(c, "style") && m1) {
            set_style(&s, atoi(argv[++i]) % 5);
        } else if (!strcmp(c, "world") && m1) {
            goto_world(&s, atoi(argv[++i]) % LLV_WORLD_COUNT);
        } else if ((!strcmp(c, "tap") || !strcmp(c, "hold")) && m1) {
            wake_if_needed(&s);
            uint8_t b = argv[++i][0] == 'b' ? BTN_B : BTN_A;
            if (c[0] == 't')
                tap(&s, b);
            else
                hold(&s, b);
        } else if (!strcmp(c, "shotheld") && m2) {
            rc = do_shot_held(&s, argv[i + 1], (uint32_t)atoi(argv[i + 2]));
            i += 2;
        } else if (!strcmp(c, "ab")) {
            wake_if_needed(&s);
            ab(&s);
        } else if (!strcmp(c, "shake")) {
            frame(&s, 20, 0, true);
            idle(&s, 200);
        } else if (!strcmp(c, "sleep")) {
            advance(&s, 0);
            idle(&s, LLV_SLEEP_AFTER_MS + 1000);
        } else if (!strcmp(c, "wake")) {
            wake_if_needed(&s);
        } else if (!strcmp(c, "wait") && m1) {
            idle(&s, (uint32_t)atoi(argv[++i]));
        } else if (!strcmp(c, "pet") && m1) {
            choose_pet(&s, atoi(argv[++i]));
        } else if (!strcmp(c, "give") && m1) {
            /* Solo para preparar escenas de prueba: mete ese objeto en la bolsa y lo deja preparado. */
            wake_if_needed(&s);
            ItemKind it = (ItemKind)(atoi(argv[++i]) % ITEM_COUNT);
            GameState *g = &s.e.gs;
            if (it != ITEM_NONE && !state_bag_add(g, it))
                g->bag[g->bag_count - 1] = (uint8_t)it;
            g->prepared = (uint8_t)it;
        } else if (!strcmp(c, "routine") && m1) {
            routine(&s, atoi(argv[++i]));
        } else if (!strcmp(c, "talk")) {
            talk_a(&s);
        } else if (!strcmp(c, "yes") || !strcmp(c, "no")) {
            answer(&s, c[0] == 'y');
        } else if (!strcmp(c, "drain")) {
            /* Gasta las conversaciones del día (contestando que sí) y pide una más: sale el relleno. */
            wake_if_needed(&s);
            for (int guard = 0; guard < 30; guard++) {
                const Sala *sa = &s.e.gs.sala;
                if (sa->stage == DLG_ASK)
                    answer(&s, true);
                else if (sa->stage == DLG_SAY && sa->scene > 0)
                    talk_a(&s);
                else if (s.e.gs.actions_used[WORLD_SALA] < LLV_SALA_ACTIONS)
                    talk_a(&s);
                else
                    break;
            }
            talk_a(&s);
        } else if (!strcmp(c, "pool")) {
            /* Cuántas frases de charla podrían salir ahora mismo, por personaje (para juzgar la variedad). */
            PhraseEnv env = sim_env(&s);
            PhraseCtx pc;
            phrase_ctx_make(&pc, &s.e.gs, &env);
            printf("charlas posibles ahora:");
            for (int spk = 1; spk < SPK_COUNT; spk++) {
                PhraseQuery pq = {PK_BIT(PK_CHARLA), SPK_BIT(spk), -1, 0, false, 0, false};
                int n = phrase_eligible(&pc, &pq);
                if (n)
                    printf(" %s %d", speaker_short(spk), n);
            }
            PhraseQuery cq = {PK_BIT(PK_COMENTARIO), pc.present, -1, 0, false, 0, false};
            printf(" | comentarios %d\n", phrase_eligible(&pc, &cq));
        } else if (!strcmp(c, "bell")) {
            bell(&s);
        } else if (!strcmp(c, "code")) {
            secret_code(&s);
        } else if (!strcmp(c, "visitor") && m1) {
            wake_if_needed(&s);
            int v = speaker_by_name(argv[++i]);
            s.e.gs.sala.visitor = (uint8_t)v;
            s.e.gs.sala.visitor_left = v ? 36 : 0;
        } else if (!strcmp(c, "surprise") && m1) {
            wake_if_needed(&s);
            int k = surprise_by_name(argv[++i]);
            int arg = 0;
            if (k == DS_AUSENCIA)
                arg = (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '5') ? atoi(argv[++i]) : WORLD_SALA;
            else if (k == DS_INVASION)
                arg = (WORLD_JARDIN << 4) | SPK_GOTICA;
            else if (k == DS_REBELDIA)
                arg = SPK_ABUELA;
            PhraseEnv env = sim_env(&s);
            director_force(&s.e.gs, &env, k, arg);
            idle(&s, 400);
        } else if (!strcmp(c, "say") && m1) {
            /* Fuerza una frase de ese personaje (para las capturas): say NOMBRE [pregunta|relleno|...] */
            wake_if_needed(&s);
            int spk = speaker_by_name(argv[++i]);
            uint32_t kinds = PK_BIT(PK_CHARLA);
            if (i + 1 < argc) {
                static const char *const KN[] = {"charla", "relleno", "sinacciones", "cuarta", "rebeldia", "timbre",
                                                 "invasion", "pregunta", "si", "no", "mentira", "retoma", "escena"};
                for (unsigned k = 0; k < sizeof(KN) / sizeof(KN[0]); k++)
                    if (!strcmp(argv[i + 1], KN[k])) {
                        kinds = PK_BIT(k);
                        i++;
                        break;
                    }
            }
            PhraseEnv env = sim_env(&s);
            talk_say(&s.e.gs, &env, kinds, SPK_BIT(spk), -1);
            idle(&s, TYPE_WAIT_MS);
        } else if (!strcmp(c, "ask") && m1) {
            wake_if_needed(&s);
            const char *qn = argv[++i];
            for (int q = 0; q < Q_COUNT; q++)
                if (!strcmp(qn, phrase_question_vocab(q))) {
                    PhraseEnv env = sim_env(&s);
                    PhraseCtx pc;
                    phrase_ctx_make(&pc, &s.e.gs, &env);
                    talk_say(&s.e.gs, &env, PK_BIT(PK_PREGUNTA), pc.present, q);
                }
            idle(&s, TYPE_WAIT_MS);
        } else if (!strcmp(c, "grandma") && m1) {
            i++;
            s.e.gs.flags = (uint8_t)(!strcmp(argv[i], "on") ? s.e.gs.flags | SF_GRANDMA : s.e.gs.flags & ~SF_GRANDMA);
        } else if (!strcmp(c, "habit") && m2) {
            static const char *const HVOC[HAB_COUNT] = {PV_HABITS(PV_NAME)};
            for (int h = 0; h < HAB_COUNT; h++)
                if (!strcmp(argv[i + 1], HVOC[h]))
                    habit_set_enabled(&s.e.gs, h, !strcmp(argv[i + 2], "on"));
            i += 2;
        } else if (!strcmp(c, "steps") && m1) {
            s.steps = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!strcmp(c, "battery") && m1) {
            s.battery = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(c, "streak") && m1) {
            s.e.gs.hab.sleep_streak = (uint8_t)atoi(argv[++i]);
        } else if (!strcmp(c, "news") && m1) {
            s.e.gs.news = (uint8_t)strtoul(argv[++i], NULL, 0);
            s.e.gs.rare_unseen = 0;
        } else if (!strcmp(c, "week") && m1) {
            s.rng = (Rng){0, 0};
            rng_seed(&s.rng, seed, 99);
            week_start = s.now;
            week(&s, atoi(argv[++i]));
        } else if (!strcmp(c, "dumpfrases") && m1) {
            dump_phrases(&s, argv[++i]);
        } else if (!strcmp(c, "salastats")) {
            sala_stats(&s, week_start ? week_start : start);
        } else if (!strcmp(c, "sala")) {
            sala_info(&s);
        } else if (!strcmp(c, "led")) {
            /* Con el llavero dormido, mira el LED de aviso en el instante en que parpadea. */
            if (!s.e.asleep) {
                advance(&s, 0);
                idle(&s, LLV_SLEEP_AFTER_MS + 1000);
            }
            s.ms += (5000 - s.ms % 5000) % 5000;
            frame(&s, 20, 0, false);
            printf("LED: %s (sucesos raros sin ver: %u)\n", s.out.led ? "ENCENDIDO" : "apagado",
                   (unsigned)s.e.gs.rare_unseen);
        } else if (!strcmp(c, "stats")) {
            stats(&s);
        } else if (!strcmp(c, "weather") && m1) {
            s.e.gs.weather = (uint8_t)(atoi(argv[++i]) & 3);
        } else if (!strcmp(c, "sky") && m1) {
            s.e.gs.sky = (uint8_t)(atoi(argv[++i]) % 3);
            s.e.gs.sky_left = s.e.gs.sky ? 30 : 0;
        } else if (!strcmp(c, "musicseed") && m1) {
            s.e.gs.mus.seed = (uint16_t)strtoul(argv[++i], NULL, 0);
            s.e.gs.mus.mut = 0;
        } else if (!strcmp(c, "music")) {
            const Music *m = &s.e.music;
            if (music_active(m))
                printf("música: #%04X +%u  %s  %s  %d BPM  %s / %s  paso %u (compás %u, vuelta %u)  ánimo %02X\n",
                       m->seed, m->mut, music_mode_name(m->style.mode), "", m->style.bpm,
                       music_prog_name(m->style.prog_a), music_prog_name(m->style.prog_b), (unsigned)m->pos,
                       (unsigned)m->pos / 16, (unsigned)m->loops, m->mood);
            else
                printf("música: callada (guardado: #%04X +%u ánimo %02X)\n", s.e.gs.mus.seed, s.e.gs.mus.mut,
                       s.e.gs.mus.mood);
        } else if (!strcmp(c, "rec") && m1) {
            free(s.rec);
            s.rec_cap = (size_t)REC_MAX_SECONDS * LLV_AUDIO_RATE;
            s.rec = malloc(s.rec_cap * sizeof(int16_t));
            s.rec_n = 0;
            snprintf(s.rec_path, sizeof(s.rec_path), "%s", argv[++i]);
            if (!s.rec) {
                rc = 1;
            } else {
                hal_audio_stub_set_sink(on_audio, &s);
            }
        } else if (!strcmp(c, "recstop")) {
            if (!s.rec) {
                fprintf(stderr, "recstop sin rec\n");
                rc = 2;
            } else {
                hal_audio_stub_set_sink(NULL, NULL);
                rc = wav_write(s.rec_path, s.rec, s.rec_n, LLV_AUDIO_RATE);
                printf("%s (%zu muestras, %.1f s)\n", s.rec_path, s.rec_n, (double)s.rec_n / LLV_AUDIO_RATE);
                free(s.rec);
                s.rec = NULL;
            }
        } else if (!strcmp(c, "info")) {
            info(&s);
        } else {
            fprintf(stderr, "comando no válido: %s\n", c);
            usage(argv[0]);
            rc = 2;
        }
    }
    return rc;
}
