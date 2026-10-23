#include "engine.h"

#include <stdio.h>
#include <string.h>

#include "cast.h"
#include "director.h"
#include "habits.h"
#include "overlays.h"
#include "phrases.h"
#include "talk.h"
#include "timeutil.h"

static const World *const WORLDS[LLV_WORLD_COUNT] = {
    &world_reloj, &world_jardin, &world_sala, &world_musica, &world_pesca, &world_cobertizo,
};

const World *engine_world(int index)
{
    return WORLDS[index];
}

int64_t engine_advance(GameState *gs, int64_t now)
{
    int64_t target = time_floor_div(now, LLV_TICK_SECONDS);
    if (target <= gs->tick)
        return 0;
    if (target - gs->tick > LLV_CATCHUP_MAX_TICKS)
        gs->tick = target - LLV_CATCHUP_MAX_TICKS; /* más allá del tope, el mundo "no recuerda" */
    int64_t ran = 0;
    while (gs->tick < target) {
        state_tick(gs);
        for (int w = 0; w < LLV_WORLD_COUNT; w++)
            if (WORLDS[w]->tick)
                WORLDS[w]->tick(gs);
        ran++;
    }
    return ran;
}

static void tone(FrameOut *out, uint16_t freq, uint16_t ms)
{
    if (out && out->tone_count < ENGINE_MAX_TONES)
        out->tones[out->tone_count++] = (Tone){freq, ms};
}

void engine_toast(Engine *e, const char *text, uint32_t ms, uint32_t duration)
{
    snprintf(e->toast, sizeof(e->toast), "%s", text);
    e->toast_until_ms = ms + duration;
}

/* Puente para que los mundos (que no conocen el Engine) puedan lanzar mensajes y pitidos. */
static void toast_cb(void *user, const char *text, uint32_t ms, uint32_t duration)
{
    engine_toast((Engine *)user, text, ms, duration);
}

static void tone_cb(void *user, uint16_t freq, uint16_t ms)
{
    tone(((Engine *)user)->out, freq, ms);
}

static WorldCtx make_ctx(Engine *e, int64_t now, uint32_t ms)
{
    WorldCtx ctx = {0};
    const GameState *gs = &e->gs;
    ctx.now = now;
    ctx.ms = ms;
    ctx.index = gs->current_world;
    ctx.buttons = e->in.down;
    ctx.shaken = ms_pending(ms, e->shake_until_ms);
    ctx.battery = e->battery;
    ctx.charging = e->charging;
    ctx.absent = gs->dir.cur == DS_AUSENCIA && gs->dir.arg == gs->current_world;
    ctx.music = &e->music;
    ctx.toast = toast_cb;
    ctx.tone = tone_cb;
    ctx.user = e;
    return ctx;
}

static PhraseEnv phrase_env(const Engine *e, int64_t now, uint32_t ms)
{
    PhraseEnv env = {now, ms_pending(ms, e->shake_until_ms), e->battery, e->charging};
    return env;
}

static void wake(Engine *e, int64_t now, uint32_t ms, FrameOut *out)
{
    GameState *gs = &e->gs;
    bool first = gs->looks_total == 0;
    int64_t slept = first ? 0 : now - gs->last_seen;
    if (slept < 0)
        slept = 0;
    engine_advance(gs, now); /* con SF_ASLEEP aún puesto: el bono de paciencia corre mientras duerme */
    gs->flags = (uint8_t)(gs->flags & ~SF_ASLEEP);
    gs->unseen_ticks = 0;
    int64_t hours = slept / 3600;
    state_log(gs, EV_WAKE, (uint8_t)(hours > 255 ? 255 : hours));
    gs->looks_today++;
    gs->looks_total++;
    gs->gap_min = (uint16_t)(slept / 60 > 65535 ? 65535 : slept / 60);
    gs->last_seen = now;
    e->asleep = false;
    e->last_activity_ms = ms;
    e->collapse_until_ms = ms + LLV_COLLAPSE_MS;
    e->collapse_total_ms = LLV_COLLAPSE_MS;
    rng_seed(&e->fx, (uint64_t)now ^ ms, 7);

    /* La racha: días seguidos mirándolo. */
    uint16_t day = (uint16_t)gs->day_key;
    if (gs->streak == 0)
        gs->streak = 1;
    else if (day == (uint16_t)(gs->last_look_day + 1))
        gs->streak = gs->streak < 255 ? (uint8_t)(gs->streak + 1) : 255;
    else if (day != gs->last_look_day)
        gs->streak = 1;
    gs->last_look_day = day;

    /* Fase 3: hábitos, afinidad (solo cambia el tono), Director y mundo que se abre. */
    habit_steps(gs, e->steps);
    habits_on_look(gs, now);
    if (gs->looks_today <= 3 && !first)
        for (int s = SPK_ABUELA; s <= SPK_ROBOT; s++)
            gs->sala.affinity[s] = (uint8_t)(gs->sala.affinity[s] > 247 ? 255 : gs->sala.affinity[s] + 8);
    talk_on_wake(gs, slept);
    PhraseEnv env = phrase_env(e, now, ms);
    env.shaken = false;
    director_wake(gs, &env, slept, first);
    gs->rare_unseen = 0;
    e->seen_cur = 0; /* el motor se entera de la sorpresa en el primer fotograma */
    e->code_n = e->code_prog = 0;
    e->code_snap_valid = false;
    e->glitch_until_ms = e->boot_until_ms = e->coin_until_ms = 0;
    if (e->wake_msg[0]) {
        engine_toast(e, e->wake_msg, ms, 3200);
        e->wake_msg[0] = '\0';
    }
    if (out) {
        tone(out, 523, 40);
        tone(out, 659, 40);
        tone(out, 784, 60);
    }
}

void engine_init(Engine *e, uint64_t seed, int64_t now, uint32_t ms)
{
    memset(e, 0, sizeof(*e));
    pesca_ui_reset(); /* lo efímero de los mundos no sobrevive a un llavero nuevo */
    state_init(&e->gs, seed, now);
    input_init(&e->in);
    wake(e, now, ms, NULL);
}

void engine_resume(Engine *e, const GameState *loaded, int64_t now, uint32_t ms)
{
    memset(e, 0, sizeof(*e));
    pesca_ui_reset();
    e->gs = *loaded;
    input_init(&e->in);
    wake(e, now, ms, NULL);
}

void engine_sleep(Engine *e, int64_t now)
{
    /* Una pesca a medias se corta al dormir: la lombriz se pierde y se avisa al despertar. */
    const World *cur = WORLDS[e->gs.current_world];
    const char *left = cur->leave ? cur->leave(&e->gs) : NULL;
    if (left)
        snprintf(e->wake_msg, sizeof(e->wake_msg), "%s", left);
    e->asleep = true;
    e->gs.flags |= SF_ASLEEP;
    e->gs.last_seen = now;
    e->diary_until_ms = 0;
    /* Las sorpresas duran lo que dura una mirada. */
    e->gs.dir.cur = DS_NONE;
    e->gs.dir.armed = 0;
    e->seen_cur = 0;
    e->glitch_until_ms = e->boot_until_ms = e->coin_until_ms = 0;
    e->code_n = e->code_prog = 0;
    e->code_snap_valid = false;
    music_stop(&e->music, LLV_MUSIC_FADE_MS); /* dormir calla la música con un fundido corto */
}

/* ---------- música (fase 4) ---------- */

/* Pone el sintetizador de acuerdo con el estado: suena solo en el mundo Música, despierto y sin que
 * el mundo esté "ausente" ni haya un cartucho sucio. La semilla y las mutaciones las cambia el
 * propio mundo en gs->mus (A: mutar / nueva semilla); aquí se aplican. Solo toca gs->mus, nunca
 * gs->rng ni el resto del universo. */
static void music_sync(Engine *e, int64_t now)
{
    GameState *gs = &e->gs;
    Music *m = &e->music;
    bool want = !e->asleep && gs->current_world == WORLD_MUSICA && gs->dir.cur != DS_GLITCH &&
                !(gs->dir.cur == DS_AUSENCIA && gs->dir.arg == WORLD_MUSICA);
    if (!want) {
        if (music_active(m) && !m->stopping)
            music_stop(m, LLV_MUSIC_FADE_MS);
        return;
    }
    if (!music_active(m) || m->stopping) {
        /* Al entrar suena con el ánimo de ahora (hora, lluvia, cielo, mascota). */
        gs->mus.mood = music_mood(gs, now);
        music_start(m, gs->mus.seed, gs->mus.mut, gs->mus.mood, LLV_AUDIO_RATE);
        return;
    }
    if (m->restart) /* una semilla nueva ya está en camino */
        return;
    if (gs->mus.seed != m->seed)
        music_reseed(m, gs->mus.seed, gs->mus.mut, gs->mus.mood);
    else if (gs->mus.mut != m->mut)
        music_mutate(m, gs->mus.mut);
    /* Si llueve (o amanece...) mientras escuchas, el estilo cambia al empezar la vuelta siguiente. */
    if (gs->mus.seed == m->seed) {
        music_set_mood(m, music_mood(gs, now));
        gs->mus.mood = m->mood;
    }
}

size_t engine_audio_render(Engine *e, int16_t *buf, size_t max)
{
    return music_active(&e->music) ? music_render(&e->music, buf, max) : 0;
}

/* ---------- HUD y superposiciones ---------- */

static Color item_color(uint8_t item)
{
    switch (item) {
    case ITEM_NUBE: return C_ITEM_NUBE;
    case ITEM_VIENTO: return C_SKY_D2;
    case ITEM_SEMILLA: return C_ITEM_SEMILLA;
    case ITEM_ALPISTE: return C_ITEM_ALPISTE;
    case ITEM_LOMBRIZ: return C_ITEM_LOMBRIZ;
    case ITEM_DESEO: return C_SUN;
    default: return C_GREY_D;
    }
}

/* Un marcador por mundo: punto gris, blanco el actual y un ❗ rojo si tiene novedades (spec §2). */
static void draw_world_marks(const GameState *gs, uint32_t ms, Framebuffer *fb)
{
    for (int w = 0; w < LLV_WORLD_COUNT; w++) {
        int x = 30 + w * 5;
        bool news = (gs->news >> w & 1) && w != gs->current_world;
        if (news) {
            Color c = ((ms / 450) & 1) ? C_RED : C_ORANGE;
            gfx_rect(fb, x + 1, 2, 1, 3, c);
            gfx_pset(fb, x + 1, 6, c);
        } else if (w == gs->current_world) {
            gfx_rect(fb, x, 3, 3, 3, C_WHITE);
        } else {
            gfx_pset(fb, x + 1, 4, C_GREY_D);
        }
    }
}

static void draw_hud(const Engine *e, int64_t now, uint32_t ms, Framebuffer *fb)
{
    const GameState *gs = &e->gs;
    gfx_rect(fb, 0, 0, LLV_W, LLV_HUD_H, C_DARK);
    CivilTime c = time_to_civil(now);
    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", c.hour, c.minute);
    gfx_digits(fb, 3, 2, hhmm, C_WHITE, 1);
    draw_world_marks(gs, ms, fb);

    const World *w = WORLDS[gs->current_world];
    for (int k = 0; k < w->daily_actions; k++) {
        Color col = k < w->daily_actions - gs->actions_used[gs->current_world] ? C_SUN : C_GREY_D;
        int cx = 64 + k * 6;
        gfx_pset(fb, cx, 3, col);
        gfx_rect(fb, cx - 1, 4, 3, 1, col);
        gfx_pset(fb, cx, 5, col);
    }
    for (int k = 0; k < LLV_BAG_SIZE; k++) {
        int x = 82 + k * 7;
        if (k < gs->bag_count) {
            gfx_rect(fb, x, 2, 5, 5, item_color(gs->bag[k]));
            gfx_pset(fb, x, 2, C_WHITE);
        } else {
            gfx_frame(fb, x, 2, 5, 5, C_GREY_D);
        }
    }
}

static void ago(char *buf, size_t n, int64_t ticks)
{
    int64_t min = ticks * (LLV_TICK_SECONDS / 60);
    if (min < 60)
        snprintf(buf, n, "%lldm", (long long)min);
    else if (min < 48 * 60)
        snprintf(buf, n, "%lldh", (long long)(min / 60));
    else
        snprintf(buf, n, "%lldd", (long long)(min / 1440));
}

static void draw_diary(const Engine *e, Framebuffer *fb)
{
    const GameState *gs = &e->gs;
    gfx_textbox(fb, 3, 14, 114, 102);
    gfx_text(fb, 8, 19, "MIENTRAS NO", C_BLUE_TXT);
    gfx_text(fb, 8, 28, "MIRABAS…", C_BLUE_TXT);
    if (gs->diary_count == 0) {
        gfx_text_wrap(fb, 8, 44, 104, "Nada todavía. Paciencia.", C_DARK);
        return;
    }
    int y = 42, shown = 0;
    for (int i = 0; i < gs->diary_count && y < 110; i++) {
        const DiaryEntry *d = state_diary_get(gs, i);
        char what[24], when[24];
        if (!state_event_text(what, sizeof(what), d))
            continue;
        ago(when, sizeof(when), gs->tick - d->tick);
        /* Si el texto no cabe junto al "hace 3 h", se recorta (sin partir una letra con tilde). */
        int room = 104 - gfx_text_width(when) - 6;
        for (size_t len = strlen(what); len > 0 && gfx_text_width(what) > room;) {
            len--;
            while (len > 0 && ((unsigned char)what[len] & 0xC0) == 0x80)
                len--;
            what[len] = '\0';
        }
        gfx_text(fb, 8, y, what, C_DARK);
        gfx_text(fb, 112 - gfx_text_width(when), y, when, C_GREY);
        y += GFX_LINE_H + 1;
        shown++;
    }
    if (shown == 0)
        gfx_text_wrap(fb, 8, 44, 104, "Nada todavía. Paciencia.", C_DARK);
}

static void draw_toast(const Engine *e, Framebuffer *fb)
{
    if (!strchr(e->toast, '\n') && gfx_text_width(e->toast) <= 108) {
        int w = gfx_text_width(e->toast) + 8;
        int x = (LLV_W - w) / 2;
        gfx_rect(fb, x, 12, w, 12, C_DARK);
        gfx_text(fb, x + 4, 14, e->toast, C_WHITE);
        return;
    }
    /* mensajes largos: caja de varias líneas */
    int lines = gfx_text_wrap(NULL, 0, 0, 100, e->toast, C_WHITE);
    int h = lines * GFX_LINE_H + 5;
    int y = LLV_H - h - 3; /* abajo: no tapa lo importante de la escena */
    gfx_rect(fb, 4, y, 112, h, C_DARK);
    gfx_frame(fb, 4, y, 112, h, C_GREY);
    gfx_text_wrap(fb, 10, y + 3, 100, e->toast, C_WHITE);
}

/* "Colapso": al despertar, el mundo tiembla entre versiones antes de fijarse (spec §9.3). */
static void collapse_fx(Engine *e, uint32_t ms, Framebuffer *fb)
{
    uint32_t left = e->collapse_until_ms - ms;
    uint32_t total = e->collapse_total_ms ? e->collapse_total_ms : LLV_COLLAPSE_MS;
    int strength = (int)(left * 5 / total);
    static uint8_t row[LLV_W];
    Framebuffer src = *fb;
    for (int y = LLV_HUD_H; y < LLV_H; y++) {
        int sy = y;
        if (strength > 2 && rng_below(&e->fx, 4) == 0)
            sy = y + (int)rng_below(&e->fx, 7) - 3;
        if (sy < 0 || sy >= LLV_H)
            sy = y;
        int dx = (int)rng_below(&e->fx, (uint32_t)(2 * strength + 1)) - strength;
        for (int x = 0; x < LLV_W; x++) {
            int sx = x + dx;
            row[x] = (sx >= 0 && sx < LLV_W) ? src.px[sy * LLV_W + sx] : src.px[sy * LLV_W + x];
        }
        memcpy(&fb->px[y * LLV_W], row, LLV_W);
    }
}

/* ---------- sorpresas (spec §6): el motor reacciona a lo que decide el Director ---------- */

static void say_system(Engine *e, uint32_t ms, uint32_t kinds, uint32_t dur)
{
    int idx = director_phrase(&e->gs, kinds, SPK_SISTEMA);
    if (idx < 0)
        return;
    PhraseFill f = {{0, 0, 0}};
    char buf[128];
    phrase_render(&e->gs, idx, &f, buf, sizeof(buf));
    engine_toast(e, buf, ms, dur);
}

static void surprise_started(Engine *e, uint32_t ms, FrameOut *out)
{
    const GameState *gs = &e->gs;
    char buf[160];
    PhraseFill f = {{0, 0, 0}};
    int idx;
    switch (gs->dir.cur) {
    case DS_GLITCH:
        e->glitch_until_ms = ms + LLV_GLITCH_MS;
        for (int k = 0; k < 4; k++)
            tone(out, (uint16_t)(150 + 90 * ((k * 7) % 5)), 30);
        if ((idx = director_phrase(gs, PK_BIT(PK_GLITCH), SPK_SISTEMA)) >= 0) {
            phrase_render(gs, idx, &f, buf, sizeof(buf));
            phrase_notify(SPK_SISTEMA, PK_GLITCH, false, buf);
        }
        break;
    case DS_ARRANQUE:
        e->boot_start_ms = ms;
        e->boot_until_ms = ms + 1800;
        tone(out, 392, 90);
        tone(out, 523, 90);
        tone(out, 659, 90);
        tone(out, 1047, 220);
        break;
    case DS_COLAPSO:
        e->collapse_total_ms = 1600;
        e->collapse_until_ms = ms + 1600;
        tone(out, 330, 60);
        tone(out, 494, 60);
        tone(out, 740, 100);
        break;
    case DS_MONEDA:
        e->coin_until_ms = ms + 14000;
        tone(out, 1319, 60);
        tone(out, 1760, 160);
        if ((idx = director_phrase(gs, PK_BIT(PK_MONEDA), SPK_SISTEMA)) >= 0) {
            phrase_render(gs, idx, &f, buf, sizeof(buf));
            phrase_notify(SPK_SISTEMA, PK_MONEDA, false, buf);
        }
        break;
    case DS_AUSENCIA:
        if ((idx = director_phrase(gs, PK_BIT(PK_CARTEL), SPK_SISTEMA)) >= 0) {
            phrase_render(gs, idx, &f, buf, sizeof(buf));
            phrase_notify(SPK_SISTEMA, PK_CARTEL, false, buf);
        }
        break;
    case DS_INVASION:
        tone(out, 262, 70);
        tone(out, 196, 120);
        if ((idx = director_phrase(gs, PK_BIT(PK_INVASION), gs->dir.arg & 15)) >= 0) {
            phrase_render(gs, idx, &f, buf, sizeof(buf));
            phrase_notify(gs->dir.arg & 15, PK_INVASION, false, buf);
        }
        break;
    case DS_GATO:
        tone(out, 988, 50);
        tone(out, 740, 80);
        tone(out, 880, 120);
        break;
    default:
        break;
    }
}

/* El motor se entera de una sorpresa nueva (la del despertar o una forzada) y vigila lo que dura un
 * tiempo: el cartucho sucio se arregla solo e INSERT COIN caduca. Se llama antes de procesar los
 * gestos, así lo que hace un toque no depende de cuántos fotogramas haya habido. */
static void surprise_tick(Engine *e, uint32_t ms, FrameOut *out)
{
    GameState *gs = &e->gs;
    if (gs->dir.cur != e->seen_cur) {
        e->seen_cur = gs->dir.cur;
        surprise_started(e, ms, out);
    }
    if (gs->dir.cur == DS_GLITCH && !ms_pending(ms, e->glitch_until_ms)) {
        gs->dir.cur = DS_NONE;
        e->seen_cur = DS_NONE;
        say_system(e, ms, PK_BIT(PK_GLITCH_OK), 2200);
    }
    if (gs->dir.cur == DS_MONEDA && !ms_pending(ms, e->coin_until_ms)) {
        gs->dir.cur = DS_NONE;
        e->seen_cur = DS_NONE;
    }
}

static void render_world_into(Engine *e, int index, int64_t now, uint32_t ms, Framebuffer *fb)
{
    WorldCtx ctx = make_ctx(e, now, ms);
    ctx.index = (uint8_t)index;
    ctx.absent = false;
    WORLDS[index]->render(&e->gs, &ctx, fb);
}

static void draw_sorpresas(Engine *e, int64_t now, uint32_t ms, Framebuffer *fb)
{
    const GameState *gs = &e->gs;
    char buf[160];
    PhraseFill f = {{0, 0, 0}};
    int idx;
    if (gs->dir.cur == DS_AUSENCIA && gs->dir.arg == gs->current_world &&
        (idx = director_phrase(gs, PK_BIT(PK_CARTEL), SPK_SISTEMA)) >= 0) {
        phrase_render(gs, idx, &f, buf, sizeof(buf));
        overlay_absent(fb, buf);
    }
    if (gs->dir.cur == DS_INVASION && (gs->dir.arg >> 4) == gs->current_world &&
        (idx = director_phrase(gs, PK_BIT(PK_INVASION), gs->dir.arg & 15)) >= 0) {
        phrase_render(gs, idx, &f, buf, sizeof(buf));
        overlay_invasion(fb, gs->dir.arg & 15, buf, ms);
    }
    (void)now;
}

/* ---------- el código secreto (spec §2): B B A A B A B A ---------- */

static const char SECRET[8] = {'B', 'B', 'A', 'A', 'B', 'A', 'B', 'A'};

/* Mira cada toque sin cambiar la gramática de gestos: las acciones se ejecutan igual. Si el código
 * se completa, deshace lo que hicieron las A (con una copia del estado tomada antes de la
 * primera) para que ni se gaste un objeto ni una conversación. true = código completo. */
static bool code_feed(Engine *e, Gesture g, uint32_t ms)
{
    if (g == GEST_NONE)
        return false;
    if (g != GEST_A_TAP && g != GEST_B_TAP) {
        e->code_n = e->code_prog = 0;
        e->code_snap_valid = false;
        return false;
    }
    if (e->code_n && ms - e->code_last_ms > LLV_CODE_GAP_MS) {
        e->code_n = e->code_prog = 0;
        e->code_snap_valid = false;
    }
    e->code_last_ms = ms;
    if (e->code_n == 8) {
        memmove(e->code_hist, e->code_hist + 1, 7);
        e->code_n = 7;
    }
    e->code_hist[e->code_n++] = g == GEST_A_TAP ? 'A' : 'B';
    int best = 0;
    for (int k = e->code_n; k >= 1 && !best; k--)
        if (memcmp(e->code_hist + e->code_n - k, SECRET, (size_t)k) == 0)
            best = k;
    e->code_prog = (uint8_t)best;
    if (best < 3)
        e->code_snap_valid = false; /* lo hecho hasta aquí era una acción de verdad */
    else if (g == GEST_A_TAP && !e->code_snap_valid && best == 3) {
        e->code_snap = e->gs; /* justo antes de la primera A del código */
        e->code_snap_valid = true;
    }
    if (best == 8) {
        e->code_n = e->code_prog = 0;
        return true;
    }
    return false;
}

/* ---------- frame ---------- */

static void frame_impl(Engine *e, RawInput in, int64_t now, uint32_t ms, FrameOut *out)
{
    bool touched = in.buttons != 0 || in.shake;

    if (e->asleep) {
        if (!touched) {
            /* El chip despierta de vez en cuando con el temporizador: pone el mundo al día y, si
             * pasó algo raro, parpadea el LED sin encender la pantalla (spec §3.2). */
            engine_advance(&e->gs, now);
            out->led = e->gs.rare_unseen > 0 && ms % 5000 < 150;
            return;
        }
        wake(e, now, ms, out);
        input_update(&e->in, in.buttons, ms);
        input_swallow(&e->in); /* el botón que despierta no actúa */
    } else {
        engine_advance(&e->gs, now);
        habit_steps(&e->gs, e->steps);
        surprise_tick(e, ms, out);
        Gesture g = input_update(&e->in, in.buttons, ms);
        if (touched)
            e->last_activity_ms = ms;

        GameState *gs = &e->gs;
        WorldCtx ctx = make_ctx(e, now, ms);
        const World *w = WORLDS[gs->current_world];
        const uint8_t world_before = gs->current_world;
        PhraseEnv env = phrase_env(e, now, ms);

        if (in.shake) {
            e->shake_until_ms = ms + 1500;
            if (gs->dir.cur == DS_GLITCH) { /* agitar arregla el cartucho sucio */
                gs->dir.cur = DS_NONE;
                e->seen_cur = DS_NONE;
                e->glitch_until_ms = 0;
                say_system(e, ms, PK_BIT(PK_GLITCH_OK), 2200);
                tone(out, 659, 50);
                tone(out, 988, 90);
            } else if (w->on_shake) {
                ctx.shaken = true;
                w->on_shake(gs, &ctx);
            }
        }

        if (g != GEST_NONE && ms_pending(ms, e->diary_until_ms)) {
            e->diary_until_ms = 0; /* cualquier gesto cierra el diario */
            g = GEST_NONE;
        }
        /* Las pantallas de las sorpresas se llevan los gestos mientras duran. */
        if (g != GEST_NONE && gs->dir.cur == DS_GLITCH)
            g = GEST_NONE; /* hay que agitar */
        if (g != GEST_NONE && gs->dir.cur == DS_ARRANQUE && ms_pending(ms, e->boot_until_ms))
            g = GEST_NONE;
        if (gs->dir.cur == DS_MONEDA && g != GEST_NONE) {
            if (g == GEST_A_TAP) {
                bool got = state_bag_add(gs, ITEM_DESEO);
                if (got)
                    state_log(gs, EV_BAG_REFILL, ITEM_DESEO);
                engine_toast(e, got ? "¡Crédito! +1 Deseo" : "¡Crédito! (bolsa llena)", ms, 2400);
                gs->dir.cur = DS_NONE;
                e->seen_cur = DS_NONE;
                e->coin_until_ms = 0;
                tone(out, 1319, 60);
                tone(out, 1760, 60);
                tone(out, 2093, 140);
            }
            g = GEST_NONE;
        }
        if (g == GEST_B_TAP && gs->dir.armed) { /* rebeldía: "No. Me gusta aquí." */
            int spk = gs->dir.arg ? gs->dir.arg : SPK_ABUELA;
            gs->dir.armed = 0;
            if (gs->current_world == WORLD_SALA) {
                talk_say(gs, &env, PK_BIT(PK_REBELDIA), SPK_BIT(spk), -1);
            } else {
                int idx = director_phrase(gs, PK_BIT(PK_REBELDIA), spk);
                if (idx >= 0) {
                    PhraseFill f = {{0, 0, 0}};
                    char text[128], msg[160];
                    phrase_render(gs, idx, &f, text, sizeof(text));
                    snprintf(msg, sizeof(msg), "%s: %s", speaker_name(spk), text);
                    engine_toast(e, msg, ms, 2600);
                    phrase_notify(spk, PK_REBELDIA, false, text);
                }
            }
            tone(out, 220, 80);
            g = GEST_NONE;
        }

        if (code_feed(e, g, ms)) { /* B B A A B A B A: el Gato de Schrödinger */
            if (e->code_snap_valid) {
                pesca_ui_reset(); /* se deshace todo lo de las A del código, también un lanzamiento */
                *gs = e->code_snap;
                engine_advance(gs, now);
                e->code_snap_valid = false;
            }
            director_force(gs, &env, DS_GATO, 0);
            g = GEST_NONE;
            tone(out, 988, 50);
            tone(out, 1319, 50);
            tone(out, 988, 50);
            tone(out, 1568, 140);
        }

        switch (g) {
        case GEST_B_TAP:
            gs->current_world = (uint8_t)((gs->current_world + 1) % LLV_WORLD_COUNT);
            tone(out, 880, 30);
            break;
        case GEST_B_HOLD:
            gs->current_world = (uint8_t)((gs->current_world + LLV_WORLD_COUNT - 1) % LLV_WORLD_COUNT);
            tone(out, 587, 50);
            break;
        case GEST_A_TAP:
            if (w->on_tap) {
                w->on_tap(gs, &ctx);
                tone(out, 660, 35);
            }
            break;
        case GEST_A_HOLD:
            if (w->on_hold) {
                w->on_hold(gs, &ctx);
                tone(out, 440, 60);
            }
            break;
        case GEST_AB:
            e->diary_until_ms = ms + 6000;
            tone(out, 988, 40);
            tone(out, 1319, 60);
            break;
        case GEST_NONE:
            break;
        }

        /* Si el mundo cambió (B, una sorpresa...), el que se deja limpia lo efímero y, si había algo a
         * medias, avisa. Si no, corre lo que el mundo hace en cada fotograma (el minijuego de la Pesca). */
        if (gs->current_world != world_before) {
            const char *left = w->leave ? w->leave(gs) : NULL;
            if (left)
                engine_toast(e, left, ms, 3000);
        } else if (w->frame && !ms_pending(ms, e->diary_until_ms)) {
            WorldCtx fctx = make_ctx(e, now, ms);
            w->frame(gs, &fctx);
        }

        surprise_tick(e, ms, out); /* una sorpresa forzada por un gesto (el código secreto, la rebeldía) */
        music_sync(e, now);
        gs->news = (uint8_t)(gs->news & ~(1u << gs->current_world)); /* el mundo en el que estás ya no tiene novedades */

        /* Con la música sonando se aguanta despierto más rato (es un mundo para dejarlo en la mesa). */
        uint32_t sleep_after = gs->current_world == WORLD_MUSICA && music_active(&e->music) && !e->music.stopping
                                   ? LLV_SLEEP_MUSIC_MS : LLV_SLEEP_AFTER_MS;
        if (ms - e->last_activity_ms > sleep_after && in.buttons == 0) {
            engine_sleep(e, now);
            out->save_now = true;
            tone(out, 523, 40);
            tone(out, 392, 70);
            return;
        }
    }

    GameState *gs = &e->gs;
    if (gs->dir.cur != e->seen_cur) { /* tras despertar: la sorpresa de esta mirada */
        e->seen_cur = gs->dir.cur;
        surprise_started(e, ms, out);
    }
    gs->news = (uint8_t)(gs->news & ~(1u << gs->current_world));

    out->screen_on = true;
    if (gs->dir.cur == DS_ARRANQUE && ms_pending(ms, e->boot_until_ms)) {
        overlay_boot(&e->fb, ms - e->boot_start_ms);
        return;
    }
    if (gs->dir.cur == DS_MONEDA) {
        char buf[64];
        PhraseFill f = {{0, 0, 0}};
        int idx = director_phrase(gs, PK_BIT(PK_MONEDA), SPK_SISTEMA);
        if (idx >= 0)
            phrase_render(gs, idx, &f, buf, sizeof(buf));
        else
            snprintf(buf, sizeof(buf), "INSERT COIN");
        overlay_coin(&e->fb, buf, ms);
        return;
    }

    WorldCtx ctx = make_ctx(e, now, ms);
    WORLDS[gs->current_world]->render(gs, &ctx, &e->fb);
    draw_sorpresas(e, now, ms, &e->fb);
    draw_hud(e, now, ms, &e->fb);
    if (ms_pending(ms, e->diary_until_ms))
        draw_diary(e, &e->fb);
    else if (ms_pending(ms, e->toast_until_ms))
        draw_toast(e, &e->fb);
    if (gs->dir.cur == DS_GLITCH) {
        char buf[128];
        PhraseFill f = {{0, 0, 0}};
        int idx = director_phrase(gs, PK_BIT(PK_GLITCH), SPK_SISTEMA);
        if (idx >= 0)
            phrase_render(gs, idx, &f, buf, sizeof(buf));
        else
            snprintf(buf, sizeof(buf), "CARTUCHO SUCIO. SOPLA.");
        overlay_glitch(&e->fb, buf, ms, &e->fx);
    }
    if (ms_pending(ms, e->collapse_until_ms)) {
        /* Colapso cuántico: varias versiones del mundo superpuestas antes de quedarse con una. */
        if (gs->dir.cur == DS_COLAPSO) {
            static Framebuffer alt;
            uint32_t left = e->collapse_until_ms - ms;
            int other = (gs->current_world + 1 + (int)((ms / 120) % 4)) % LLV_WORLD_COUNT;
            if (left * 10 > e->collapse_total_ms * 3 && other != gs->current_world) {
                render_world_into(e, other, now, ms, &alt);
                for (int y = LLV_HUD_H; y < LLV_H; y++)
                    for (int x = 0; x < LLV_W; x++)
                        if (((x + y) & 1) != 0)
                            e->fb.px[y * LLV_W + x] = alt.px[y * LLV_W + x];
            }
        }
        collapse_fx(e, ms, &e->fb);
    }
}

void engine_set_body(Engine *e, uint8_t battery, bool charging, uint32_t steps)
{
    e->battery = battery;
    e->charging = charging;
    e->steps = steps;
}

FrameOut engine_frame(Engine *e, RawInput in, int64_t now, uint32_t ms)
{
    FrameOut out;
    memset(&out, 0, sizeof(out));
    e->out = &out;
    frame_impl(e, in, now, ms, &out);
    e->out = NULL;
    if (out.tone_count && music_active(&e->music)) { /* los pitidos mandan: la música les cede el sitio */
        unsigned total = 0;
        for (int i = 0; i < out.tone_count; i++)
            total += out.tones[i].ms;
        music_duck(&e->music, (uint16_t)(total + 40));
    }
    return out;
}
