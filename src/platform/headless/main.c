/* Herramienta headless: corre el motor sin ventana con reloj virtual, simula gestos y
 * tiempo, y escribe capturas PNG/PPM. Pensada para revisar el Reloj a distintas horas
 * y para scripts de humo. Ver --help. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/director.h"
#include "../../core/engine.h"
#include "../../core/fish.h"
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
    int force_fish_p1;        /* (capturas) el próximo lanzamiento pica este pez + 1; 0 = el sorteado */
} Sim;

static Sim *g_sim; /* para el observador de frases */
static int do_grab(Sim *s, const char *name);

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
        if (d->kind == EV_CAST) /* cada lanzamiento gasta una lombriz */
            s->items_used[ITEM_LOMBRIZ]++;
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

static uint32_t g_frames; /* fotogramas simulados: para vigilar bucles sin fin */

static void frame(Sim *s, uint32_t dt, uint8_t buttons, bool shake)
{
    g_frames++;
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
    int64_t guard = (s->e.gs.current_world == WORLD_MUSICA ? LLV_SLEEP_MUSIC_MS : LLV_SLEEP_AFTER_MS) / 1000 + 2;
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
    for (int guard = 0; guard < 16 && !cobertizo_on_pet(&s->e.gs); guard++)
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

/* ---------- la Pesca (fase 5) ---------- */

typedef enum { SKILL_NONE, SKILL_PERFECT, SKILL_HUMAN } FishSkill;

/* Lanza la caña (A) y juega el minijuego como lo haría esa jugadora. Devuelve 1 si pescó algo, 0 si
 * se escapó y -1 si no se pudo lanzar (sin lombriz o sin lanzamientos). Si 'shot' no es NULL, hace
 * dos capturas: a mitad de pelea (NOMBRE_pelea) y con la tarjeta del resultado (NOMBRE_tarjeta). */
static int fish_play(Sim *s, FishSkill skill, const char *shot)
{
    goto_world(s, WORLD_PESCA);
    for (int guard = 0; guard < 400 && pesca_phase() == PESCA_RESULT; guard++)
        frame(s, 20, 0, false);
    uint32_t casts = s->e.gs.fish.casts;
    tap(s, BTN_A);
    if (s->e.gs.fish.casts == casts)
        return -1;
    if (s->force_fish_p1) {
        pesca_debug_force(s->force_fish_p1 - 1, 0);
        s->force_fish_p1 = 0;
    }
    FishGame hist[16];
    int n = 0;
    bool shot_fight = false;
    for (int guard = 0; guard < 12000; guard++) {
        const FishGame *g = pesca_game();
        bool hold = false;
        if (g && skill != SKILL_NONE) {
            hist[n++ % 16] = *g;
            const FishGame *view = skill == SKILL_HUMAN && n > 10 ? &hist[(n - 10) % 16] : g;
            hold = fishgame_ai(view);
            if (skill == SKILL_HUMAN && rng_below(&s->rng, 100) < 3)
                hold = !hold;
        }
        if (g && shot && !shot_fight && n == 80) {
            char name[96];
            snprintf(name, sizeof(name), "%s_pelea", shot);
            do_grab(s, name);
            shot_fight = true;
        }
        frame(s, 20, hold ? BTN_A : 0, false);
        if (pesca_phase() == PESCA_RESULT || (pesca_phase() == PESCA_IDLE && guard > 5))
            break;
    }
    int won = pesca_phase() == PESCA_RESULT ? pesca_result()->won : 0;
    if (pesca_phase() == PESCA_RESULT) {
        idle(s, 700);
        if (shot) {
            char name[96];
            snprintf(name, sizeof(name), "%s_tarjeta", shot);
            do_grab(s, name);
        }
        tap(s, BTN_A); /* cierra la tarjeta */
    }
    return won;
}

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
    [EV_PATIENCE] = "bono de paciencia", [EV_CAST] = "lanzamientos de caña", [EV_FISH] = "peces pescados",
    [EV_FISH_LOST] = "peces escapados", [EV_FISH_LEGEND] = "peces legendarios", [EV_FISH_JUNK] = "basuras pescadas",
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

/* Atiende la sorpresa de esta mirada como lo haría una persona (agita el cartucho, mete la moneda...). */
static void attend_surprise(Sim *s)
{
    GameState *g = &s->e.gs;
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
    attend_surprise(s);
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

/* ---------- la alfa (fase 5): 14 días de uso realista por los 6 mundos ---------- */

#define ALPHA_MAX_VIO 24
#define ALPHA_REPORT_MAX 49152

typedef struct {
    int days;
    bool stock;                 /* una lombriz extra cada mañana: simula la lluvia y la compostera generosas */
    uint32_t sessions, saves, loads, checks, violations;
    uint32_t visits[LLV_WORLD_COUNT];
    uint32_t a_taps[LLV_WORLD_COUNT], a_holds[LLV_WORLD_COUNT];
    uint32_t casts, no_worm, no_actions, fish_won, fish_lost, cancel_b, cancel_sleep, legend;
    uint32_t music_mut, music_seeds, qs_yes, qs_no, ab_taps, shakes, codes, diaries;
    uint32_t absences[3];       /* 1 h, 1 día, 1 semana */
    uint32_t max_garden, max_diary, max_bag, max_save, frames_start;
    uint32_t bag_full_lost, discoveries;
    char vio[ALPHA_MAX_VIO][200];
    char *rep;                  /* informe en construcción */
    size_t rep_n;
    char daily[4096];           /* una línea por día */
    size_t daily_n;
} Alpha;

static void rp(Alpha *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void rp(Alpha *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (a->rep_n < ALPHA_REPORT_MAX - 1)
        a->rep_n += (size_t)vsnprintf(a->rep + a->rep_n, ALPHA_REPORT_MAX - a->rep_n, fmt, ap);
    va_end(ap);
}

static void vio(Alpha *a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void vio(Alpha *a, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (a->violations < ALPHA_MAX_VIO)
        vsnprintf(a->vio[a->violations], sizeof(a->vio[0]), fmt, ap);
    va_end(ap);
    a->violations++;
}

#define REQUIRE(c, msg)                                           \
    do {                                                          \
        if (!(c))                                                 \
            vio(a, "%s: %s [%s]", when, msg, #c);                 \
    } while (0)

/* Las invariantes del llavero: nada de estados imposibles (spec §10). */
static void alpha_check(const Sim *s, Alpha *a, const char *when)
{
    const GameState *g = &s->e.gs;
    static const uint8_t LIMIT[LLV_WORLD_COUNT] = {
        [WORLD_JARDIN] = LLV_GARDEN_ACTIONS, [WORLD_SALA] = LLV_SALA_ACTIONS,
        [WORLD_PESCA] = LLV_PESCA_ACTIONS, [WORLD_COBERTIZO] = LLV_PET_ACTIONS,
    };
    a->checks++;
    REQUIRE(g->current_world < LLV_WORLD_COUNT, "índice de mundo inválido");
    REQUIRE(g->bag_count <= LLV_BAG_SIZE, "bolsa por encima de su capacidad");
    for (int i = 0; i < LLV_BAG_SIZE; i++) {
        REQUIRE(g->bag[i] < ITEM_COUNT, "objeto de bolsa inválido");
        REQUIRE((i < g->bag_count) == (g->bag[i] != ITEM_NONE), "la bolsa tiene huecos o fantasmas");
    }
    REQUIRE(g->prepared < ITEM_COUNT, "objeto preparado inválido");
    for (int w = 0; w < LLV_WORLD_COUNT; w++)
        REQUIRE(g->actions_used[w] <= LIMIT[w], "más acciones que el límite del día");
    int n = garden_count(g);
    REQUIRE(n <= LLV_GARDEN_MAX, "más de 32 entidades");
    if ((uint32_t)n > a->max_garden)
        a->max_garden = (uint32_t)n;
    REQUIRE(g->diary_count <= LLV_DIARY_SIZE && g->diary_head < LLV_DIARY_SIZE, "diario fuera de límites");
    if (g->diary_count > a->max_diary)
        a->max_diary = g->diary_count;
    if (g->bag_count > a->max_bag)
        a->max_bag = g->bag_count;
    REQUIRE(g->pet.kind < PET_COUNT, "mascota inválida");
    REQUIRE(g->dir.cur < DS_COUNT, "sorpresa inválida");
    for (int i = 0; i < FISH_SPECIES; i++) {
        REQUIRE((g->fish.caught[i] > 0) == (g->fish.best[i] > 0), "capturas y récord no coinciden");
        REQUIRE(g->fish.caught[i] == 0 || ((g->collection >> (COLL_PEZ0 + i)) & 1), "pez pescado fuera de la colección");
    }
    REQUIRE(g->current_world == WORLD_PESCA || pesca_phase() == PESCA_IDLE, "pesca a medias fuera de la Pesca");
    if (s->e.asleep)
        REQUIRE(pesca_phase() == PESCA_IDLE, "pesca a medias con el llavero dormido");
    /* el guardado: cabe, carga y es idéntico */
    uint8_t buf[LLV_SAVE_MAX], again[LLV_SAVE_MAX];
    size_t len = state_serialize(g, buf, sizeof(buf));
    REQUIRE(len > 0 && len < 4096, "el guardado no cabe en 4 KB");
    if (len > a->max_save)
        a->max_save = (uint32_t)len;
    static GameState back;
    if (len > 0) {
        int rc = state_deserialize(&back, buf, len);
        REQUIRE(rc == 0, "el guardado no vuelve a cargar");
        size_t len2 = rc == 0 ? state_serialize(&back, again, sizeof(again)) : 0;
        REQUIRE(len2 == len && memcmp(buf, again, len) == 0, "guardar-cargar-guardar no es idéntico");
    }
    for (int i = 0; i < LLV_W * LLV_H; i++)
        if (s->e.fb.px[i] >= C_COUNT) {
            vio(a, "%s: píxel fuera de la paleta", when);
            break;
        }
}

/* Una mirada: lo que el Director abre y lo que pasa al despertar. */
static void alpha_wake(Sim *s, Alpha *a)
{
    bool was_asleep = s->e.asleep;
    wake_if_needed(s);
    if (!was_asleep)
        return;
    GameState *g = &s->e.gs;
    a->sessions++;
    s->looks_sim++;
    s->world_open[g->current_world]++;
    s->surprise_hist[g->dir.cur]++;
    s->steps += rng_below(&s->rng, 1800);
    s->battery = (uint8_t)(8 + rng_below(&s->rng, 90));
    s->charging = rng_below(&s->rng, 10) == 0;
    idle(s, 600);
    attend_surprise(s);
    alpha_check(s, a, "al despertar");
}

static void alpha_goto(Sim *s, Alpha *a, int w)
{
    alpha_wake(s, a);
    if (s->e.gs.current_world != w)
        goto_world(s, w);
    a->visits[w]++;
    idle(s, 400);
}

static void a_tap(Sim *s, Alpha *a)
{
    a->a_taps[s->e.gs.current_world]++;
    tap(s, BTN_A);
}

static void a_hold(Sim *s, Alpha *a)
{
    a->a_holds[s->e.gs.current_world]++;
    hold(s, BTN_A);
}

static int worms_in_bag(const GameState *g)
{
    int n = 0;
    for (int i = 0; i < g->bag_count; i++)
        n += g->bag[i] == ITEM_LOMBRIZ;
    return n;
}

/* Una conversación en la Sala: da pie, contesta lo que le pregunten y sigue las escenas. */
static void alpha_talk(Sim *s, Alpha *a, int convs)
{
    alpha_goto(s, a, WORLD_SALA);
    for (int c = 0; c < convs; c++) {
        a->a_taps[WORLD_SALA]++;
        if (rng_below(&s->rng, 100) < 15)
            bell(s);
        else
            talk_a(s);
        for (int guard = 0; guard < 8; guard++) {
            const Sala *sa = &s->e.gs.sala;
            if (sa->stage == DLG_ASK) {
                bool yes = rng_below(&s->rng, 100) < 65;
                answer(s, yes);
                if (yes)
                    a->qs_yes++;
                else
                    a->qs_no++;
            } else if (sa->stage == DLG_SAY && sa->scene > 0) {
                talk_a(s);
            } else {
                break;
            }
        }
    }
}

/* Cebo del día: si hay una lombriz en la bolsa, el jugador la prepara en el Cobertizo (spec §4.6). */
static void alpha_prepare_worm(Sim *s, Alpha *a)
{
    GameState *g = &s->e.gs;
    int idx = -1;
    for (int i = 0; i < g->bag_count && idx < 0; i++)
        if (g->bag[i] == ITEM_LOMBRIZ)
            idx = i;
    if (idx < 0 || g->prepared == ITEM_LOMBRIZ)
        return;
    alpha_goto(s, a, WORLD_COBERTIZO);
    if (s->e.gs.pet.kind == PET_NONE)
        choose_pet(s, 1 + (int)rng_below(&s->rng, 4));
    cobertizo_ui_reset();
    for (int k = 0; k < idx; k++)
        a_tap(s, a);
    a_hold(s, a); /* prepara la lombriz bajo el cursor */
    idle(s, 600);
    cobertizo_ui_reset();
}

/* Una sesión de pesca: lanza y juega con esa habilidad. mode 0 = juega, 1 = B a mitad de pelea,
 * 2 = B esperando, 3 = el llavero se apaga a mitad de la espera. */
static void alpha_fish(Sim *s, Alpha *a, FishSkill skill, int mode)
{
    GameState *g = &s->e.gs;
    if (worms_in_bag(g) == 0 && g->prepared != ITEM_LOMBRIZ && rng_below(&s->rng, 100) < 65)
        return; /* sin cebo casi nadie se acerca al estanque (y a veces va a comprobarlo) */
    alpha_goto(s, a, WORLD_PESCA);
    g = &s->e.gs;
    uint32_t casts = g->fish.casts;
    if (g->actions_used[WORLD_PESCA] >= LLV_PESCA_ACTIONS) {
        a_tap(s, a); /* "El estanque descansa" */
        a->no_actions++;
        idle(s, 500);
        return;
    }
    if (worms_in_bag(g) == 0 && g->prepared != ITEM_LOMBRIZ) {
        a_tap(s, a); /* "Sin lombrices, la pesca es solo meditación" */
        a->no_worm++;
        idle(s, 500);
        return;
    }
    a->casts++;
    if (mode == 0) {
        int r = fish_play(s, skill, NULL);
        if (r > 0)
            a->fish_won++;
        else if (r == 0)
            a->fish_lost++;
        if (g->fish.casts == casts)
            vio(a, "pesca: no se pudo lanzar con lombriz y lanzamientos disponibles");
        return;
    }
    a_tap(s, a);
    if (mode == 1) { /* espera a que pique y empiece la pelea; entonces suelta con B */
        for (int guard = 0; guard < 2000 && pesca_phase() != PESCA_FIGHT && pesca_phase() != PESCA_RESULT; guard++)
            frame(s, 20, 0, false);
        idle(s, 300);
        tap(s, BTN_B);
        a->cancel_b++;
    } else if (mode == 2) {
        idle(s, 700);
        tap(s, BTN_B);
        a->cancel_b++;
    } else {
        idle(s, 1000);
        engine_sleep(&s->e, s->now); /* el llavero se apaga con la caña en el agua */
        a->cancel_sleep++;
    }
    if (pesca_phase() != PESCA_IDLE)
        vio(a, "pesca: tras cancelar quedó una pesca a medias");
    alpha_check(s, a, "tras cancelar la pesca");
}

/* El Cobertizo: la bolsa, la colección con sus fichas de pez y la mascota. */
static void alpha_cobertizo(Sim *s, Alpha *a, bool deep)
{
    alpha_goto(s, a, WORLD_COBERTIZO);
    if (s->e.gs.pet.kind == PET_NONE)
        choose_pet(s, 1 + (int)rng_below(&s->rng, 4));
    cobertizo_ui_reset();
    int stops = deep ? 14 : 5;
    for (int k = 0; k < stops && !cobertizo_on_pet(&s->e.gs); k++) {
        idle(s, 700);
        if (cobertizo_on_fish(&s->e.gs) >= 0) { /* la ficha del pez: la otra página */
            a_hold(s, a);
            idle(s, 700);
        }
        a_tap(s, a);
    }
    if (cobertizo_on_pet(&s->e.gs)) {
        idle(s, 800);
        a_hold(s, a); /* cuidado del día (una vez) */
        idle(s, 700);
        if (deep) {
            a_tap(s, a); /* respirar juntos */
            press(s, BTN_A, 3600);
            idle(s, 3600);
            press(s, BTN_A, 3600);
            idle(s, 2500);
            a_tap(s, a);
        }
    }
    cobertizo_ui_reset();
}

/* La música: mutar, nueva semilla y unos segundos de audio. */
static void alpha_musica(Sim *s, Alpha *a)
{
    alpha_goto(s, a, WORLD_MUSICA);
    idle(s, 2500);
    int muts = 1 + (int)rng_below(&s->rng, 3);
    for (int k = 0; k < muts; k++) {
        a_tap(s, a);
        a->music_mut++;
        idle(s, 1500);
    }
    if (rng_below(&s->rng, 100) < 40) {
        a_hold(s, a);
        a->music_seeds++;
        idle(s, 2500);
    }
    idle(s, 4000);
}

static void alpha_reloj(Sim *s, Alpha *a)
{
    alpha_goto(s, a, WORLD_RELOJ);
    idle(s, 1500);
    int styles = 1 + (int)rng_below(&s->rng, 3);
    for (int k = 0; k < styles; k++) {
        a_tap(s, a);
        idle(s, 1800);
    }
    a_hold(s, a); /* fecha y estación */
    idle(s, 2500);
}

static void alpha_jardin(Sim *s, Alpha *a, bool act)
{
    alpha_goto(s, a, WORLD_JARDIN);
    idle(s, 1500);
    if (act) {
        a_tap(s, a);
        idle(s, 3500);
    }
    int looks = 1 + (int)rng_below(&s->rng, 3);
    for (int k = 0; k < looks; k++) {
        a_hold(s, a); /* la lupa */
        idle(s, 1200);
    }
}

/* Una vuelta entera por los seis mundos con B, en el orden de la spec (spec §2). */
static void alpha_b_cycle(Sim *s, Alpha *a)
{
    alpha_wake(s, a);
    int start = s->e.gs.current_world;
    for (int k = 1; k <= LLV_WORLD_COUNT; k++) {
        tap(s, BTN_B);
        int want = (start + k) % LLV_WORLD_COUNT;
        if (s->e.gs.current_world != want && s->e.gs.dir.armed == 0)
            vio(a, "B no siguió el orden de los mundos (esperaba %d, hay %d)", want, s->e.gs.current_world);
        a->visits[s->e.gs.current_world]++;
        idle(s, 350);
    }
    hold(s, BTN_B); /* mantener B: mundo anterior */
    idle(s, 300);
    tap(s, BTN_B);
    idle(s, 300);
}

static void alpha_gestures(Sim *s, Alpha *a)
{
    alpha_wake(s, a);
    ab(s); /* A+B: el diario "mientras no mirabas…" */
    a->diaries++;
    idle(s, 1500);
    if (rng_below(&s->rng, 100) < 50) {
        frame(s, 20, 0, true);
        a->shakes++;
        idle(s, 800);
    }
}

/* Guardar a mitad, cargar y seguir: el estado de después debe ser idéntico byte a byte. */
static void alpha_save_load(Sim *s, Alpha *a)
{
    GameState *g = &s->e.gs;
    advance(s, 0);
    idle(s, LLV_SLEEP_AFTER_MS + 1000);
    for (int guard = 0; guard < 4000 && !s->e.asleep; guard++)
        frame(s, 50, 0, false);
    uint8_t buf[LLV_SAVE_MAX], buf2[LLV_SAVE_MAX];
    size_t n = state_serialize(g, buf, sizeof(buf));
    GameState loaded;
    int rc = n ? state_deserialize(&loaded, buf, n) : -1;
    if (rc != 0) {
        vio(a, "guardar a mitad: no se pudo cargar (rc %d)", rc);
        return;
    }
    size_t n2 = state_serialize(&loaded, buf2, sizeof(buf2));
    if (n2 != n || memcmp(buf, buf2, n) != 0)
        vio(a, "guardar a mitad: el estado cargado no es idéntico al guardado");
    a->saves++;
    /* Un llavero nuevo que despierta del guardado, como al reiniciar. */
    s->ms += 4000;
    s->now += 5;
    engine_resume(&s->e, &loaded, s->now, s->ms);
    a->loads++;
    idle(s, LLV_COLLAPSE_MS + 150);
    alpha_check(s, a, "tras cargar el guardado");
    /* y sigue vivo: un vistazo a la Pesca y al Cobertizo con lo cargado */
    alpha_goto(s, a, WORLD_COBERTIZO);
    idle(s, 800);
    alpha_check(s, a, "tras reanudar");
}

typedef struct { int d_prev; uint32_t casts, won, lost, coll; uint8_t garden, bag; } DayMark;

static void alpha_day_line(Sim *s, Alpha *a, int day, int sessions_today)
{
    const GameState *g = &s->e.gs;
    CivilTime c = time_to_civil(s->now);
    char *dst = a->daily + a->daily_n;
    size_t room = sizeof(a->daily) - a->daily_n;
    a->daily_n += (size_t)snprintf(dst, room, "  día %2d  %04d-%02d-%02d  sesiones %d | jardín %2d cosas | bolsa %d | colección %2d/16 | peces %d (%d tipos) "
          "| lanzamientos %u | mascota %s %s\n",
       day + 1, c.year, c.month, c.day, sessions_today, garden_count(g), g->bag_count, bits(g->collection),
       fish_total_caught(g), fish_collected(g), (unsigned)g->fish.casts,
       g->pet.kind ? pet_name((PetKind)g->pet.kind) : "-", g->pet.kind ? pet_stage_name(&g->pet) : "");
    if (a->daily_n >= sizeof(a->daily))
        a->daily_n = sizeof(a->daily) - 1;
}

static int alpha_run(Sim *s, int days, bool stock, const char *report_path, uint64_t seed)
{
    static char rep[ALPHA_REPORT_MAX];
    static Alpha a;
    memset(&a, 0, sizeof(a));
    a.days = days;
    a.stock = stock;
    a.rep = rep;
    rep[0] = '\0';
    clock_t t_start = clock();
    int64_t t_begin = s->now;
    uint32_t frames0 = g_frames;
    s->rng = (Rng){0, 0};
    rng_seed(&s->rng, seed, 4242);
    int64_t day0 = time_floor_div(s->now, LLV_SECONDS_PER_DAY) * LLV_SECONDS_PER_DAY;
    static const int SLOT[4] = {7 * 60 + 40, 12 * 60 + 50, 18 * 60 + 20, 22 * 60 + 40};
    bool did_code = false;
    uint32_t ev_before[EV_KIND_COUNT];
    memcpy(ev_before, s->ev_hist, sizeof(ev_before));
    uint32_t said_before = (uint32_t)s->said_n;
    alpha_check(s, &a, "inicio");

    int cal_day = 0; /* día natural en curso (con las ausencias) */
    for (int d = 0; d < days; d++) {
        if (d > 0)
            cal_day++;
        if (d == 5) { /* un día entero sin mirar */
            cal_day++;
            a.absences[1]++;
        } else if (d == 10) { /* una semana fuera */
            cal_day += 7;
            a.absences[2]++;
        }
        int64_t today = day0 + (int64_t)cal_day * LLV_SECONDS_PER_DAY;
        int plan[4], np = 0;
        if (d == 3) { /* un día de poco uso: solo se asoma por la noche */
            plan[np++] = 3;
        } else {
            plan[np++] = 0;
            if (rng_below(&s->rng, 100) < 70)
                plan[np++] = 1;
            if (rng_below(&s->rng, 100) < 70)
                plan[np++] = 2;
            plan[np++] = 3;
        }
        int sessions = 0;
        for (int k = 0; k < np; k++) {
            int slot = plan[k];
            int64_t at = today + (int64_t)(SLOT[slot] + (int)rng_below(&s->rng, 50)) * 60;
            if (at <= s->now) /* el primer día empieza a las 08:00: lo de antes ya pasó */
                continue;
            advance(s, at - s->now);
            alpha_wake(s, &a);
            sessions++;
            if (stock && sessions == 1) /* una lombriz cada mañana (lluvia, compostera...) */
                if (!state_bag_add(&s->e.gs, ITEM_LOMBRIZ) && s->e.gs.bag_count > 0)
                    s->e.gs.bag[s->e.gs.bag_count - 1] = ITEM_LOMBRIZ;
            if (d == 0 && sessions == 1)
                alpha_cobertizo(s, &a, false); /* elige mascota */
            switch (slot) {
            case 0: /* mañana: la hora, el jardín y charlar */
                alpha_reloj(s, &a);
                alpha_jardin(s, &a, true);
                alpha_talk(s, &a, 1 + (int)rng_below(&s->rng, 2));
                break;
            case 1: /* mediodía: música, cobertizo y una pesca con la lombriz preparada */
                alpha_musica(s, &a);
                alpha_prepare_worm(s, &a);
                alpha_fish(s, &a, d % 3 == 0 ? SKILL_PERFECT : SKILL_HUMAN, d == 7 ? 1 : 0);
                alpha_cobertizo(s, &a, d % 4 == 1);
                break;
            case 2: /* tarde: jardín, charla y otra pesca */
                alpha_jardin(s, &a, true);
                alpha_talk(s, &a, 1);
                alpha_fish(s, &a, d % 4 == 3 ? SKILL_NONE : SKILL_HUMAN, d == 8 ? 2 : (d == 12 ? 3 : 0));
                break;
            default: /* noche: la hora, pesca nocturna (luna llena), cobertizo, gestos y diario */
                alpha_reloj(s, &a);
                alpha_prepare_worm(s, &a);
                alpha_fish(s, &a, d % 5 == 4 ? SKILL_NONE : SKILL_HUMAN, 0);
                alpha_cobertizo(s, &a, d % 3 == 0);
                alpha_gestures(s, &a);
                break;
            }
            if (sessions == 1 && d % 2 == 0)
                alpha_b_cycle(s, &a);
            if (d == 4 && sessions == 1 && !did_code) { /* el código secreto, una vez */
                alpha_wake(s, &a);
                secret_code(s);
                a.codes++;
                did_code = true;
            }
            if (d == 2 && sessions == 2) { /* ausencia de 1 hora */
                advance(s, 3600);
                a.absences[0]++;
                alpha_wake(s, &a);
                alpha_cobertizo(s, &a, false);
            }
            if (d == 6 && sessions == 1) /* guardar, cargar y seguir, a mitad de la 2.ª semana */
                alpha_save_load(s, &a);
            alpha_check(s, &a, "tras la sesión");
        }
        alpha_day_line(s, &a, d, sessions);
    }
    /* Última comprobación y guardado final. */
    alpha_save_load(s, &a);
    alpha_check(s, &a, "final");

    /* ---- informe ---- */
    CivilTime c0 = time_to_civil(t_begin), c1 = time_to_civil(s->now);
    double secs = (double)(clock() - t_start) / CLOCKS_PER_SEC;
    const GameState *g = &s->e.gs;
    static const char *const WN[LLV_WORLD_COUNT] = {"Reloj", "Jardín", "Sala", "Música", "Pesca", "Cobertizo"};
    char head[2048];
    snprintf(head, sizeof(head),
        "# INFORME DE LA ALFA: %d días de uso por los 6 mundos\n"
        "inicio %04d-%02d-%02d %02d:%02d, fin %04d-%02d-%02d %02d:%02d (%d días naturales), semilla %llu, lombriz diaria extra: %s\n"
        "fotogramas simulados: %u (tiempo real de la simulación: %.1f s), despertares: %u, guardados y cargas a mitad: %u/%u\n"
        "ausencias: %u de 1 h, %u de 1 día, %u de 1 semana\n\n",
        days, c0.year, c0.month, c0.day, c0.hour, c0.minute, c1.year, c1.month, c1.day, c1.hour, c1.minute,
        (int)((s->now - t_begin) / 86400), (unsigned long long)seed, stock ? "sí" : "no", g_frames - frames0, secs,
        a.sessions, a.saves, a.loads, a.absences[0], a.absences[1], a.absences[2]);
    rp(&a, "%s", head);
    rp(&a, "## Por días\n%s\n", a.daily);
    rp(&a, "## Qué pasó en cada mundo\n");
    rp(&a, "visitas (cambios de mundo hasta él): ");
    for (int w = 0; w < LLV_WORLD_COUNT; w++)
        rp(&a, "%s %u%s", WN[w], a.visits[w], w + 1 < LLV_WORLD_COUNT ? ", " : "\n");
    rp(&a, "mundo que abrió el Director al despertar: ");
    for (int w = 0; w < LLV_WORLD_COUNT; w++)
        rp(&a, "%s %u%s", WN[w], s->world_open[w], w + 1 < LLV_WORLD_COUNT ? ", " : "\n");
    rp(&a, "pulsaciones de A (toque/mantener): ");
    for (int w = 0; w < LLV_WORLD_COUNT; w++)
        rp(&a, "%s %u/%u%s", WN[w], a.a_taps[w], a.a_holds[w], w + 1 < LLV_WORLD_COUNT ? ", " : "\n");
    rp(&a, "\nReloj: %u estilos cambiados y fechas mostradas; estilo final %u.\n", a.a_taps[WORLD_RELOJ], g->reloj_style);
    rp(&a, "Jardín: %u acciones, %u lupas; entidades al final %d (máximo %u de %d); clima %d.\n",
       a.a_taps[WORLD_JARDIN], a.a_holds[WORLD_JARDIN], garden_count(g), a.max_garden, LLV_GARDEN_MAX, g->weather);
    rp(&a, "Sala: %u preguntas contestadas (%u sí, %u no), frases mostradas %d.\n", a.qs_yes + a.qs_no, a.qs_yes,
       a.qs_no, s->said_n - (int)said_before);
    rp(&a, "Música: %u mutaciones, %u semillas nuevas, semilla final #%04X (+%u).\n", a.music_mut, a.music_seeds,
       g->mus.seed, g->mus.mut);
    rp(&a, "Cobertizo: mascota %s gen %u día %d etapa '%s', humor %s.\n",
       g->pet.kind ? pet_name((PetKind)g->pet.kind) : "ninguna", g->pet.gen, g->pet.kind ? pet_day(&g->pet) : 0,
       g->pet.kind ? pet_stage_name(&g->pet) : "-", g->pet.kind ? pet_mood(&g->pet) : "-");
    rp(&a, "Pesca: %u lanzamientos jugados (+%u sin lombriz, +%u sin lanzamientos), %u pescados, %u escapados, "
           "%u cancelados con B, %u por apagarse; lanzamientos totales %u.\n",
       a.casts, a.no_worm, a.no_actions, a.fish_won, a.fish_lost, a.cancel_b, a.cancel_sleep, (unsigned)g->fish.casts);
    for (int i = 0; i < FISH_SPECIES; i++)
        rp(&a, "    %-20s capturas %3u  récord %3u cm  %s\n", FISH_TABLE[i].name, g->fish.caught[i], g->fish.best[i],
           (g->collection >> (COLL_PEZ0 + i)) & 1 ? "en la colección" : "???");
    rp(&a, "    basuras simpáticas %u, peces escapados %u\n", g->fish.junk, g->fish.fled);
    rp(&a, "Colección: %d/16 (%d de peces).\n", bits(g->collection), fish_collected(g));

    rp(&a, "\n## Diario (sucesos de estos días)\n");
    uint32_t total = 0;
    for (int k = 1; k < EV_KIND_COUNT; k++) {
        uint32_t dlt = s->ev_hist[k] - ev_before[k];
        total += dlt;
        if (dlt)
            rp(&a, "  %-26s %u\n", EV_LABEL[k] ? EV_LABEL[k] : "?", dlt);
    }
    rp(&a, "  total de sucesos: %u (diario circular de %d: pisados %u)\n", total, LLV_DIARY_SIZE, s->ev_lost);

    /* frases */
    int distinct = 0, per[SPK_COUNT] = {0};
    for (int i = (int)said_before; i < s->said_n; i++) {
        bool seen = false;
        for (int j = (int)said_before; j < i && !seen; j++)
            seen = s->said[j].hash == s->said[i].hash && s->said[j].speaker == s->said[i].speaker;
        distinct += !seen;
        per[s->said[i].speaker]++;
    }
    rp(&a, "\n## Frases\n  mostradas %d, distintas %d; por personaje:", s->said_n - (int)said_before, distinct);
    for (int i = 1; i < SPK_COUNT; i++)
        if (per[i])
            rp(&a, " %s %d", i == SPK_SISTEMA ? "Sistema" : speaker_short(i), per[i]);
    int pesca_phr = 0;
    for (int i = (int)said_before; i < s->said_n; i++)
        if (strstr(s->said[i].text, "pez") || strstr(s->said[i].text, "Pez") || strstr(s->said[i].text, "ombriz") ||
            strstr(s->said[i].text, "Pescaste") || strstr(s->said[i].text, "escapó"))
            pesca_phr++;
    rp(&a, "\n  de ellas, hablando de peces o lombrices: %d\n", pesca_phr);
    rp(&a, "  sorpresas (por tipo):");
    uint32_t surp = 0;
    for (int i = 1; i < DS_COUNT; i++) {
        surp += s->surprise_hist[i];
        if (s->surprise_hist[i])
            rp(&a, " %s %u", surprise_name(i), s->surprise_hist[i]);
    }
    rp(&a, " (total %u, tiradas del Director %u)\n", surp, (unsigned)g->dir.rolls);
    rp(&a, "  hábitos cumplidos %u, paseos largos %u, racha de días %u, racha de sueño %u\n",
       s->ev_hist[EV_HABIT] - ev_before[EV_HABIT], s->ev_hist[EV_WALK] - ev_before[EV_WALK], g->streak,
       g->hab.sleep_streak);

    rp(&a, "\n## Memoria y límites\n");
    rp(&a, "  GameState: %zu bytes en memoria; guardado máximo %u bytes (límite spec 4096)\n", sizeof(GameState), a.max_save);
    rp(&a, "  entidades del jardín: máximo %u de %d | diario: máximo %u de %d | bolsa: máximo %u de %d\n", a.max_garden,
       LLV_GARDEN_MAX, a.max_diary, LLV_DIARY_SIZE, a.max_bag, LLV_BAG_SIZE);
    rp(&a, "  invariantes: %u comprobaciones, %u violaciones\n", a.checks, a.violations);
    for (uint32_t i = 0; i < a.violations && i < ALPHA_MAX_VIO; i++)
        rp(&a, "    !! %s\n", a.vio[i]);

    rp(&a, "\n## Sorpresas y curiosidades\n");
    if (g->fish.caught[FISH_LEGEND])
        rp(&a, "  * ¡Se pescó el Koi Lunar (%u cm) con la luna llena!\n", g->fish.best[FISH_LEGEND]);
    else
        rp(&a, "  * El Koi Lunar no picó en estos días (necesita noche de luna llena y suerte).\n");
    if (a.cancel_b + a.cancel_sleep)
        rp(&a, "  * %u pescas se cancelaron a medias (B o apagado) y no quedó nada colgado.\n", a.cancel_b + a.cancel_sleep);
    if (s->ev_hist[EV_BAG_FULL_LOST] - ev_before[EV_BAG_FULL_LOST])
        rp(&a, "  * La bolsa estuvo llena %u veces y se perdieron objetos (sin culpa).\n",
           s->ev_hist[EV_BAG_FULL_LOST] - ev_before[EV_BAG_FULL_LOST]);
    if (s->ev_hist[EV_PET_GEN] - ev_before[EV_PET_GEN])
        rp(&a, "  * Nació una nueva generación de la mascota.\n");
    rp(&a, "  * Tiempo de la simulación: %.1f s para %d días (%.0f fotogramas/s simulados).\n", secs, days,
       secs > 0 ? (double)(g_frames - frames0) / secs : 0.0);

    printf("%s", rep);
    if (report_path) {
        FILE *f = fopen(report_path, "w");
        if (f) {
            fputs(rep, f);
            fclose(f);
            printf("\n%s\n", report_path);
        } else {
            fprintf(stderr, "no se pudo escribir %s\n", report_path);
        }
    }
    if (secs > 240.0)
        a.violations++; /* demasiado lento: algo se cuelga */
    return a.violations ? 1 : 0;
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
           "Pesca (fase 5):\n"
           "  alpha DIAS [INFORME.txt] [stock]  la alfa: DIAS de uso realista por los 6 mundos, con ausencias, guardado,\n"
           "                              pesca y comprobación de invariantes; devuelve 1 si algo se rompe\n"
           "  fish none|perfect|human     lanza la caña y juega el minijuego (nunca pulsa / perfecto / humano torpe)\n"
           "  forcefish IDX               (capturas) el próximo fish/fishshot pesca ese pez (0-5, 6-9 basuras)\n"
           "  catch IDX CM                (pruebas) apunta ese pez (0-5, 6-9 basuras) con ese tamaño como pescado\n"
           "  fishshot SKILL NOMBRE       igual, y captura NOMBRE_pelea y NOMBRE_tarjeta\n"
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
    rng_seed(&s.rng, seed, 99); /* la "persona" que usa el llavero (week y fish la reinician o la usan) */
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
            for (int guard = 0; guard < 4000 && !s.e.asleep; guard++) /* el mundo Música tarda más en dormirse */
                frame(&s, 50, 0, false);
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
        } else if (!strcmp(c, "forcefish") && m1) {
            s.force_fish_p1 = atoi(argv[++i]) % FISH_TOTAL + 1; /* (capturas) el próximo lanzamiento pica ese pez */
        } else if (!strcmp(c, "catch") && m2) {
            /* (pruebas) apunta ese pez con ese tamaño como si lo hubieras pescado: catch IDX CM */
            wake_if_needed(&s);
            FishPlan pl = {(uint8_t)(atoi(argv[i + 1]) % FISH_TOTAL), (uint8_t)atoi(argv[i + 2]), 0, 0};
            fish_resolve(&s.e.gs, &pl, true, NULL);
            i += 2;
        } else if ((!strcmp(c, "fish") && m1) || (!strcmp(c, "fishshot") && m2)) {
            /* fish none|perfect|human: lanza la caña y juega. fishshot SKILL NOMBRE además captura. */
            bool with_shot = c[4] == 's';
            const char *sk_name = argv[++i];
            FishSkill sk = !strcmp(sk_name, "none") ? SKILL_NONE : !strcmp(sk_name, "human") ? SKILL_HUMAN : SKILL_PERFECT;
            int r = fish_play(&s, sk, with_shot ? argv[++i] : NULL);
            printf("pesca: %s\n", r < 0 ? "no se pudo lanzar" : r ? "pescado" : "se escapó");
        } else if (!strcmp(c, "alpha") && m1) {
            /* alpha DIAS [INFORME.txt] [stock]: la alfa entera (14 días de uso por los 6 mundos) */
            int days = atoi(argv[++i]);
            const char *rep_path = NULL;
            bool stock = false;
            while (i + 1 < argc && (!strcmp(argv[i + 1], "stock") || strstr(argv[i + 1], ".txt"))) {
                if (!strcmp(argv[i + 1], "stock"))
                    stock = true;
                else
                    rep_path = argv[i + 1];
                i++;
            }
            rc = alpha_run(&s, days < 1 ? 1 : days, stock, rep_path, seed);
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
                for (int guard = 0; guard < 4000 && !s.e.asleep; guard++)
                    frame(&s, 50, 0, false);
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
