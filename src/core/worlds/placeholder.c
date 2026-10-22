/* Mundos aún sin construir: muestran su nombre y la fase en la que llegan. */
#include <stdio.h>

#include "../world.h"

typedef struct {
    const char *title;
    int phase;
    Color bg1, bg2, accent;
} Placeholder;

/* Indexado por la posición en el ciclo de B (el Reloj, índice 0, no es marcador). */
static const Placeholder INFO[LLV_WORLD_COUNT] = {
    [2] = {"LA SALA", 3, C_GROUND_D, C_GROUND, C_LEAF},
    [3] = {"MÚSICA", 4, C_VIOLET_D, C_DARK, C_MAGENTA},
    [4] = {"PESCA", 5, C_SKY_N3, C_SKY_N4, C_CYAN},
};

static void placeholder_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    (void)gs;
    const Placeholder *p = &INFO[ctx->index];
    gfx_dither(fb, 0, LLV_HUD_H, LLV_W, LLV_H - LLV_HUD_H, p->bg1, p->bg2);
    gfx_textbox(fb, 10, 22, 100, 34);
    gfx_text_center(fb, 28, p->title, C_DARK);
    char line[24];
    snprintf(line, sizeof(line), "llega en fase %d", p->phase);
    gfx_text_center(fb, 40, line, C_GREY);

    /* Un pequeño "latido" para que se note que el llavero está vivo. */
    int r = 3 + (int)((ctx->ms / 300) % 3);
    gfx_ellipse(fb, LLV_W / 2, 84, r, r, p->accent);
}

#define PLACEHOLDER(sym, nm)                                                              \
    const World sym = {.name = nm, .daily_actions = 0, .tick = NULL,                    \
                       .render = placeholder_render, .on_tap = NULL, .on_hold = NULL}

PLACEHOLDER(world_sala, "La Sala");
PLACEHOLDER(world_musica, "Música");
PLACEHOLDER(world_pesca, "Pesca");
