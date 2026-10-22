#include "engine.h"

#include <stdio.h>
#include <string.h>

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
    if (out->tone_count < ENGINE_MAX_TONES)
        out->tones[out->tone_count++] = (Tone){freq, ms};
}

void engine_toast(Engine *e, const char *text, uint32_t ms, uint32_t duration)
{
    snprintf(e->toast, sizeof(e->toast), "%s", text);
    e->toast_until_ms = ms + duration;
}

static void wake(Engine *e, int64_t now, uint32_t ms, FrameOut *out)
{
    int64_t slept = now - e->gs.last_seen;
    engine_advance(&e->gs, now);
    int64_t hours = slept > 0 ? slept / 3600 : 0;
    state_log(&e->gs, EV_WAKE, (uint8_t)(hours > 255 ? 255 : hours));
    e->gs.looks_today++;
    e->gs.looks_total++;
    e->gs.last_seen = now;
    e->asleep = false;
    e->last_activity_ms = ms;
    e->collapse_until_ms = ms + LLV_COLLAPSE_MS;
    rng_seed(&e->fx, (uint64_t)now ^ ms, 7);
    if (out) {
        tone(out, 523, 40);
        tone(out, 659, 40);
        tone(out, 784, 60);
    }
}

void engine_init(Engine *e, uint64_t seed, int64_t now, uint32_t ms)
{
    memset(e, 0, sizeof(*e));
    state_init(&e->gs, seed, now);
    input_init(&e->in);
    wake(e, now, ms, NULL);
}

void engine_resume(Engine *e, const GameState *loaded, int64_t now, uint32_t ms)
{
    memset(e, 0, sizeof(*e));
    e->gs = *loaded;
    input_init(&e->in);
    wake(e, now, ms, NULL);
}

void engine_sleep(Engine *e, int64_t now)
{
    e->asleep = true;
    e->gs.last_seen = now;
    e->diary_until_ms = 0;
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

static void draw_hud(const Engine *e, int64_t now, Framebuffer *fb)
{
    const GameState *gs = &e->gs;
    gfx_rect(fb, 0, 0, LLV_W, LLV_HUD_H, C_DARK);
    CivilTime c = time_to_civil(now);
    char hhmm[6];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", c.hour, c.minute);
    gfx_digits(fb, 3, 2, hhmm, C_WHITE, 1);

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
    int y = 42;
    for (int i = 0; i < gs->diary_count && y < 106; i++) {
        const DiaryEntry *d = state_diary_get(gs, i);
        char what[24], when[24];
        switch ((EventKind)d->kind) {
        case EV_BAG_REFILL: snprintf(what, sizeof(what), "+1 %s", state_item_name(d->a)); break;
        case EV_BAG_FULL_LOST: snprintf(what, sizeof(what), "%s: no cupo", state_item_name(d->a)); break;
        case EV_NEW_DAY: snprintf(what, sizeof(what), "Nuevo día"); break;
        case EV_WAKE: snprintf(what, sizeof(what), "Dormí %uh", (unsigned)d->a); break;
        default: continue;
        }
        if (d->kind == EV_WAKE && d->a == 0)
            continue; /* siestas de minutos: ruido */
        ago(when, sizeof(when), gs->tick - d->tick);
        gfx_text(fb, 8, y, what, C_DARK);
        gfx_text(fb, 112 - gfx_text_width(when), y, when, C_GREY);
        y += GFX_LINE_H + 1;
    }
}

static void draw_toast(const Engine *e, Framebuffer *fb)
{
    int w = gfx_text_width(e->toast) + 8;
    int x = (LLV_W - w) / 2;
    gfx_rect(fb, x, 12, w, 12, C_DARK);
    gfx_text(fb, x + 4, 14, e->toast, C_WHITE);
}

/* "Colapso": al despertar, el mundo tiembla entre versiones antes de fijarse (spec §9.3). */
static void collapse_fx(Engine *e, uint32_t ms, Framebuffer *fb)
{
    uint32_t left = e->collapse_until_ms - ms;
    int strength = (int)(left * 5 / LLV_COLLAPSE_MS);
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

/* ---------- frame ---------- */

FrameOut engine_frame(Engine *e, RawInput in, int64_t now, uint32_t ms)
{
    FrameOut out;
    memset(&out, 0, sizeof(out));
    bool touched = in.buttons != 0 || in.shake;

    if (e->asleep) {
        if (!touched)
            return out; /* pantalla apagada: nada que dibujar */
        wake(e, now, ms, &out);
        input_update(&e->in, in.buttons, ms);
        input_swallow(&e->in); /* el botón que despierta no actúa */
    } else {
        engine_advance(&e->gs, now);
        Gesture g = input_update(&e->in, in.buttons, ms);
        if (touched)
            e->last_activity_ms = ms;

        GameState *gs = &e->gs;
        WorldCtx ctx = {now, ms, gs->current_world};
        const World *w = WORLDS[gs->current_world];
        if (g != GEST_NONE && ms_pending(ms, e->diary_until_ms)) {
            e->diary_until_ms = 0; /* cualquier gesto cierra el diario */
            g = GEST_NONE;
        }
        switch (g) {
        case GEST_B_TAP:
            gs->current_world = (uint8_t)((gs->current_world + 1) % LLV_WORLD_COUNT);
            tone(&out, 880, 30);
            break;
        case GEST_B_HOLD:
            gs->current_world = (uint8_t)((gs->current_world + LLV_WORLD_COUNT - 1) % LLV_WORLD_COUNT);
            tone(&out, 587, 50);
            break;
        case GEST_A_TAP:
            if (w->on_tap) {
                w->on_tap(gs, &ctx);
                tone(&out, 660, 35);
            }
            break;
        case GEST_A_HOLD:
            if (w->on_hold) {
                w->on_hold(gs, &ctx);
                tone(&out, 440, 60);
            }
            break;
        case GEST_AB:
            e->diary_until_ms = ms + 6000;
            tone(&out, 988, 40);
            tone(&out, 1319, 60);
            break;
        case GEST_NONE:
            break;
        }

        if (ms - e->last_activity_ms > LLV_SLEEP_AFTER_MS && in.buttons == 0) {
            engine_sleep(e, now);
            out.save_now = true;
            tone(&out, 523, 40);
            tone(&out, 392, 70);
            return out;
        }
    }

    WorldCtx ctx = {now, ms, e->gs.current_world};
    WORLDS[e->gs.current_world]->render(&e->gs, &ctx, &e->fb);
    draw_hud(e, now, &e->fb);
    if (ms_pending(ms, e->diary_until_ms))
        draw_diary(e, &e->fb);
    else if (ms_pending(ms, e->toast_until_ms))
        draw_toast(e, &e->fb);
    if (ms_pending(ms, e->collapse_until_ms))
        collapse_fx(e, ms, &e->fb);
    out.screen_on = true;
    return out;
}
