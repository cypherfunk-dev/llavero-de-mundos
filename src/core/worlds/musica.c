/* 🎵 Música generativa (spec §4.4): chiptune de tres voces con paleta neón sobre fondo oscuro (§12.2).
 * A (toque): mutar. Cambia la melodía (motivo y variaciones) y conserva el estilo: misma escala,
 *   progresión y tempo.
 * A (mantener): nueva semilla. Otra escala, otro tempo, otro todo.
 * Límite: ninguno. Solo consume batería mientras estás en este mundo (el motor calla la música al
 * salir o dormir). El estado guardado son 4 bytes (GameState.mus): semilla, mutaciones y ánimo.
 *
 * El visualizador no mira al altavoz: dibuja lo que calcula el mismo generador (music.c), así que se
 * ve igual sin audio. Todo lo de aquí que no es GameState es visual y efímero. */
#include <stdio.h>

#include "../music.h"
#include "../world.h"

static const Color NEON[MV_COUNT] = {C_CYAN, C_MAGENTA, C_SUN};
static const char *const VOICE_NAME[MV_COUNT] = {"VOZ 1", "VOZ 2", "RUIDO"};

/* Efímero: máximos que caen despacio en las barras. */
static uint8_t peak[MV_COUNT];
static uint32_t peak_ms;

static uint32_t hash32(uint32_t v)
{
    v ^= v >> 16;
    v *= 0x7feb352du;
    v ^= v >> 15;
    v *= 0x846ca68bu;
    v ^= v >> 16;
    return v;
}

static uint16_t new_seed(const GameState *gs, const WorldCtx *ctx)
{
    uint64_t h = ((uint64_t)gs->mus.seed << 32) ^ ((uint64_t)ctx->now * 0x9E3779B97F4A7C15ull) ^
                 ((uint64_t)ctx->ms * 0xBF58476D1CE4E5B9ull);
    h ^= h >> 31;
    h *= 0x94D049BB133111EBull;
    h ^= h >> 29;
    uint16_t s = (uint16_t)(h >> 24);
    return s == gs->mus.seed ? (uint16_t)(s + 1) : s;
}

static void musica_tap(GameState *gs, const WorldCtx *ctx)
{
    gs->mus.mut = gs->mus.mut == 255 ? 1 : (uint8_t)(gs->mus.mut + 1);
    world_toast(ctx, "Melodía mutada", 1000);
}

static void musica_hold(GameState *gs, const WorldCtx *ctx)
{
    gs->mus.seed = new_seed(gs, ctx);
    gs->mus.mut = 0;
    gs->mus.mood = music_mood(gs, ctx->now);
    char msg[32];
    snprintf(msg, sizeof(msg), "Semilla nueva #%04X", gs->mus.seed);
    world_toast(ctx, msg, 1400);
}

/* Los dos renglones de la caja: "DÓRICA +3" (escala y mutaciones) y "96 BPM · #4F2A" (tempo y semilla).
 * Caben en 106 px (17 letras) con cualquier valor: lo comprueba tests/test_music.c. */
void musica_info_text(const MusicStyle *st, const MusicSave *ms, char *l1, size_t n1, char *l2, size_t n2)
{
    if (ms->mut)
        snprintf(l1, n1, "%s +%u", music_mode_name(st->mode), (unsigned)ms->mut);
    else
        snprintf(l1, n1, "%s", music_mode_name(st->mode));
    snprintf(l2, n2, "%d BPM · #%04X", st->bpm, ms->seed);
}

static const char *const NOTE_NAME[12] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};

/* Una barra de 8 segmentos que se enciende según el nivel (0..255) con su máximo marcado. */
static void draw_meter(Framebuffer *fb, int x, int base_y, int w, int level, int pk, Color c)
{
    int lit = (level * 8 + 127) / 255;
    int pks = (pk * 8 + 127) / 255;
    for (int s = 0; s < 8; s++) {
        int y = base_y - (s + 1) * 4 + 1;
        Color col = C_VIOLET_D;
        if (s < lit)
            col = s >= 7 ? C_WHITE : c;
        else if (s == pks - 1 && pks > 0)
            col = C_WHITE;
        gfx_rect(fb, x, y, w, 3, col);
        if (s >= lit && !(s == pks - 1 && pks > 0))
            gfx_frame(fb, x, y, w, 3, C_SKY_S0);
    }
}

static void musica_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    const Music *m = ctx->music;
    bool live = m && music_active(m) && m->steps_played > 0;
    MusicStyle idle;
    const MusicStyle *st = live ? &m->style : &idle;
    if (!live)
        music_style(&idle, gs->mus.seed, music_mood(gs, ctx->now));
    int beat = live ? m->pos / 4 : 0;

    /* Fondo: del violeta casi negro al gris oscuro, con estrellitas que parpadean (3 frames). */
    gfx_dither(fb, 0, LLV_HUD_H, LLV_W, LLV_H - LLV_HUD_H, C_VIOLET_D, C_DARK);
    gfx_rect(fb, 0, 62, LLV_W, LLV_H - 62, C_VIOLET_D);
    gfx_dither(fb, 0, 100, LLV_W, 20, C_VIOLET_D, C_DARK);
    for (int i = 0; i < 26; i++) {
        uint32_t h = hash32((uint32_t)i * 2654435761u + 77);
        int x = (int)(h % LLV_W), y = LLV_HUD_H + 2 + (int)((h >> 8) % (LLV_H - LLV_HUD_H - 4));
        int f = (int)((ctx->ms / 260 + h) % 3);
        gfx_pset(fb, x, y, f == 0 ? C_SKY_S0 : f == 1 ? C_SKY_S1 : C_VIOLET);
    }

    /* La caja de datos: escala, tempo, semilla (con el número de mutaciones). El borde late con el pulso. */
    Color border = (beat & 1) ? C_MAGENTA : C_CYAN;
    gfx_rect(fb, 4, 12, 112, 21, C_DARK);
    gfx_frame(fb, 4, 12, 112, 21, border);
    gfx_frame(fb, 5, 13, 110, 19, C_VIOLET_D);
    char l1[32], l2[32];
    musica_info_text(st, &gs->mus, l1, sizeof(l1), l2, sizeof(l2));
    gfx_text_center(fb, 15, l1, C_WHITE);
    gfx_text_center(fb, 24, l2, C_PINK);

    /* El osciloscopio: las últimas muestras del generador. */
    gfx_rect(fb, 4, 36, 112, 23, C_DARK);
    gfx_frame(fb, 4, 36, 112, 23, C_SKY_S0);
    gfx_rect(fb, 6, 47, 108, 1, C_SKY_N3);
    if (live) {
        int prev_y = 47;
        for (int x = 0; x < 108; x++) {
            int idx = ((int)m->scope_pos + MUSIC_SCOPE - 108 + x) % MUSIC_SCOPE;
            int y = 47 - m->scope[idx] / 1700;
            y = y < 38 ? 38 : y > 56 ? 56 : y;
            int y0 = prev_y < y ? prev_y : y, y1 = prev_y < y ? y : prev_y;
            if (x == 0)
                y0 = y1 = y;
            for (int yy = y0; yy <= y1; yy++)
                gfx_pset(fb, 6 + x, yy, C_CYAN);
            prev_y = y;
        }
    }

    /* Una barra por voz, con máximos que caen despacio. */
    if (ctx->ms - peak_ms >= 90 || ctx->ms < peak_ms) {
        for (int v = 0; v < MV_COUNT; v++)
            peak[v] = peak[v] > 12 ? (uint8_t)(peak[v] - 12) : 0;
        peak_ms = ctx->ms;
    }
    for (int v = 0; v < MV_COUNT; v++) {
        int lvl = live ? m->level[v] : 0;
        if (lvl > peak[v])
            peak[v] = (uint8_t)lvl;
        int x = 6 + v * 40;
        draw_meter(fb, x, 94, 28, lvl, peak[v], NEON[v]);
        gfx_text(fb, x + 14 - gfx_text_width(VOICE_NAME[v]) / 2, 96, VOICE_NAME[v], C_GREY);
    }

    /* Los 16 pasos del compás y los 16 compases (A A' B A''). */
    int step = live ? m->pos % MUSIC_BAR_STEPS : 0, bar = live ? m->pos / MUSIC_BAR_STEPS : 0;
    for (int s = 0; s < 16; s++) {
        int x = 4 + s * 7;
        Color c = s == step && live ? C_WHITE : (s % 4 == 0 ? C_VIOLET : C_SKY_S0);
        if (live && s == step)
            gfx_rect(fb, x, 106, 5, 5, c);
        else
            gfx_rect(fb, x, 107, 5, 3, c);
    }
    for (int b = 0; b < MUSIC_BARS; b++) {
        Color c = b / 4 == 2 ? C_MAGENTA : C_CYAN;
        if (!(live && b == bar))
            c = b < bar && live ? c : C_SKY_S0;
        else
            c = C_WHITE;
        gfx_rect(fb, 52 + b * 4, 115, 3, 3, c);
    }
    if (live) {
        const MusicChord *ch = &m->song.chord[bar];
        char name[8];
        snprintf(name, sizeof(name), "%s%s", NOTE_NAME[ch->root], ch->minor ? "m" : "");
        gfx_text(fb, 5, 113, name, bar / 4 == 2 ? C_MAGENTA : C_CYAN);
    }
}

const World world_musica = {
    .name = "Música",
    .daily_actions = 0,
    .tick = NULL, /* la música no avanza con el tiempo del universo: solo suena mientras la oyes */
    .render = musica_render,
    .on_tap = musica_tap,
    .on_hold = musica_hold,
    .on_shake = NULL,
};
