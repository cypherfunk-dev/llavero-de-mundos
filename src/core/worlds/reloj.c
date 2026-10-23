/* 🌙 Reloj Celeste (spec §4.1). Sin límite de acciones: no genera estado salvo el estilo elegido.
 * A (toque): cambia de estilo. A (mantener): ficha con fecha, estación y luna. */
#include <math.h>
#include <stdio.h>

#include "../timeutil.h"
#include "../world.h"

#define PI_F 3.14159265f
#define STYLE_COUNT 5
#define SKY_BOTTOM 96

enum { STYLE_CELESTE, STYLE_PALABRAS, STYLE_BINARIO, STYLE_CUANTICO, STYLE_CUENTA };

static const char *const STYLE_NAME[STYLE_COUNT] = {
    "CELESTE", "PALABRAS", "BINARIO", "CUÁNTICO", "CUENTA ATRÁS",
};

/* Efímero, no se guarda: solo afecta a lo que se ve unos segundos. */
static uint32_t style_label_until;
static uint32_t date_card_until;

/* ---------- textos ---------- */

static const char *const WDAY_ABBR[7] = {"DOM", "LUN", "MAR", "MIÉ", "JUE", "VIE", "SÁB"};
static const char *const WDAY_FULL[7] = {"Domingo", "Lunes", "Martes", "Miércoles",
                                         "Jueves", "Viernes", "Sábado"};
static const char *const MONTH_ABBR[12] = {"ENE", "FEB", "MAR", "ABR", "MAY", "JUN",
                                           "JUL", "AGO", "SEP", "OCT", "NOV", "DIC"};
static const char *const MONTH_FULL[12] = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                                           "julio", "agosto", "septiembre", "octubre",
                                           "noviembre", "diciembre"};
static const char *const SEASON_NAME[4] = {"Primavera", "Verano", "Otoño", "Invierno"};
static const char *const MOON_NAME[8] = {
    "Luna nueva", "Luna creciente", "Cuarto creciente", "Gibosa creciente",
    "Luna llena", "Gibosa menguante", "Cuarto menguante", "Luna menguante",
};

static const char *const HOUR_WORD[13] = {"", "una", "dos", "tres", "cuatro", "cinco", "seis",
                                          "siete", "ocho", "nueve", "diez", "once", "doce"};
static const char *const NUM_WORD[30] = {
    "", "un minuto", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve",
    "diez", "once", "doce", "trece", "catorce", "cuarto", "dieciséis", "diecisiete",
    "dieciocho", "diecinueve", "veinte", "veintiuno", "veintidós", "veintitrés",
    "veinticuatro", "veinticinco", "veintiséis", "veintisiete", "veintiocho", "veintinueve",
};

/* "Son las diez y veinticuatro de la mañana." */
void reloj_time_in_words(char *buf, size_t n, int hour, int minute)
{
    int h = hour;
    char mins[32];
    if (minute == 0)
        snprintf(mins, sizeof(mins), "en punto");
    else if (minute <= 30)
        snprintf(mins, sizeof(mins), "y %s", minute == 30 ? "media" : NUM_WORD[minute]);
    else {
        h = (hour + 1) % 24;
        snprintf(mins, sizeof(mins), "menos %s", NUM_WORD[60 - minute]);
    }
    const char *when = h == 0 ? "de la noche"
                     : h < 6  ? "de la madrugada"
                     : h < 12 ? "de la mañana"
                     : h < 14 ? "del mediodía"
                     : h < 21 ? "de la tarde"
                              : "de la noche";
    int h12 = h % 12 == 0 ? 12 : h % 12;
    snprintf(buf, n, "%s %s %s %s.", h12 == 1 ? "Es la" : "Son las", HOUR_WORD[h12], mins, when);
}

/* ---------- cielo y paisaje ---------- */

typedef enum { P_NIGHT, P_DAWN, P_DAY, P_DUSK } Period;

static Period period_of(int m)
{
    if (m < 330 || m >= 1230)
        return P_NIGHT;
    if (m < 450)
        return P_DAWN;
    if (m < 1080)
        return P_DAY;
    return P_DUSK;
}

static const Color SKY[4][5] = {
    {C_SKY_N0, C_SKY_N1, C_SKY_N2, C_SKY_N3, C_SKY_N4},
    {C_SKY_S0, C_SKY_S1, C_SKY_S2, C_SKY_S3, C_SKY_S4},
    {C_SKY_D0, C_SKY_D1, C_SKY_D2, C_SKY_D3, C_SKY_D4},
    {C_SKY_N2, C_SKY_S0, C_SKY_S1, C_SKY_S2, C_SKY_S3},
};

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1103515245u + 12345u;
    return (*s >> 8) & 0xFFFFu;
}

static void stars(Framebuffer *fb, uint32_t ms, int count)
{
    uint32_t s = 7;
    for (int i = 0; i < count; i++) {
        int x = (int)(lcg(&s) % LLV_W), y = LLV_HUD_H + (int)(lcg(&s) % 70);
        if ((i * 7 + ms / 400) % 11 == 0)
            continue; /* titila */
        gfx_pset(fb, x, y, i % 3 ? C_MOON : C_STAR_BLUE);
    }
}

static int hill_y(int x, int base, float amp, float freq, float phase)
{
    return base - (int)(amp * (0.6f * sinf(x * freq + phase) + 0.4f * sinf(x * freq * 2.3f + phase * 1.7f)));
}

static void landscape(Framebuffer *fb, Season season, bool night)
{
    static const Color NEAR[4] = {C_GREEN, C_GREEN_D, C_ORANGE, C_SNOW_D};
    static const Color DOTS[4] = {C_PINK, C_GREEN_L, C_LEAF, C_SNOW};
    for (int x = 0; x < LLV_W; x++) {
        int far = hill_y(x, 82, 6, 0.05f, 1.0f);
        gfx_rect(fb, x, far, 1, LLV_H - far, night ? C_HILL_FAR_N : C_HILL_FAR);
        if (season == SEASON_WINTER && !night)
            gfx_rect(fb, x, far, 1, 2, C_SNOW);
    }
    for (int x = 0; x < LLV_W; x++) {
        int near = hill_y(x, 92, 5, 0.08f, 2.5f);
        gfx_rect(fb, x, near, 1, LLV_H - near, night ? C_HILL_N1 : NEAR[season]);
    }
    if (!night) {
        uint32_t s = 11;
        for (int i = 0; i < 45; i++) {
            int x = (int)(lcg(&s) % LLV_W);
            int y = hill_y(x, 92, 5, 0.08f, 2.5f) + 1 + (int)(lcg(&s) % 10);
            gfx_pset(fb, x, y, DOTS[season]);
        }
    }
    /* La casita de la Abuela: de noche, con la ventana encendida. */
    int hy = hill_y(70, 92, 5, 0.08f, 2.5f) - 8;
    gfx_rect(fb, 64, hy, 12, 9, night ? C_HOUSE : C_GREY_L);
    for (int k = 0; k < 6; k++)
        gfx_rect(fb, 63 + k, hy - 1 - k, 14 - 2 * k, 1, night ? C_ROOF : C_RED);
    gfx_rect(fb, 68, hy + 3, 3, 3, night ? C_WINDOW : C_SKY_D2);
    gfx_rect(fb, 0, 106, LLV_W, LLV_H - 106, night ? C_HILL_N2 : C_GROUND_D);
    gfx_rect(fb, 0, 106, LLV_W, 1, night ? C_HILL_N1 : C_GROUND);
}

static void moon(Framebuffer *fb, int cx, int cy, int r, int64_t now, Color shadow)
{
    float theta = (float)time_moon_age(now) / (float)LLV_SYNODIC_SECONDS * 2.0f * PI_F;
    float k = cosf(theta);
    bool waxing = theta < PI_F;
    for (int dy = -r; dy <= r; dy++) {
        float ry = sqrtf((float)(r * r - dy * dy)) + 0.5f;
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy > r * r + r)
                continue;
            float fx = LLV_SOUTH_HEMISPHERE ? (float)-dx : (float)dx;
            bool lit = waxing ? fx >= ry * k : fx <= -ry * k;
            gfx_pset(fb, cx + dx, cy + dy, lit ? C_MOON : shadow);
        }
    }
}

static void sky_scene(Framebuffer *fb, const CivilTime *c, int64_t now, uint32_t ms)
{
    int m = c->hour * 60 + c->minute;
    Period p = period_of(m);
    gfx_bands(fb, LLV_HUD_H, SKY_BOTTOM, SKY[p], 5);
    if (p != P_DAY)
        stars(fb, ms, p == P_NIGHT ? 60 : 18);

    if (m >= LLV_SUNRISE_MIN && m < LLV_SUNSET_MIN) {
        float f = (float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
        int x = 6 + (int)(f * 108), y = 90 - (int)(sinf(PI_F * f) * 40); /* el arco queda por debajo de los dígitos */
        gfx_ellipse(fb, x, y, 7, 7, C_SUN);
        gfx_ellipse(fb, x - 1, y - 1, 4, 4, C_SUN_CORE);
    } else {
        int night_len = 1440 - (LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
        int nm = m >= LLV_SUNSET_MIN ? m - LLV_SUNSET_MIN : m + 1440 - LLV_SUNSET_MIN;
        float f = (float)nm / (float)night_len;
        int x = 6 + (int)(f * 108), y = 90 - (int)(sinf(PI_F * f) * 40);
        moon(fb, x, y, 6, now, SKY[p][1]);
    }
    landscape(fb, time_season(c->month, c->day, LLV_SOUTH_HEMISPHERE), p == P_NIGHT);
}

/* ---------- estilos ---------- */

static void big_time(Framebuffer *fb, const CivilTime *c, int y, Color col, Color shadow)
{
    char t[6];
    snprintf(t, sizeof(t), c->second % 2 ? "%02d %02d" : "%02d:%02d", c->hour, c->minute);
    int x = (LLV_W - gfx_digits_width("00:00", 4)) / 2; /* ancho estable aunque el ':' parpadee */
    gfx_digits(fb, x + 1, y + 1, t, shadow, 4);
    gfx_digits(fb, x, y, t, col, 4);
}

/* Amanecer especial (spec §3.3): una buena racha de sueño ilumina el amanecer con rayos y destellos. */
static bool good_sunrise(const GameState *gs, const CivilTime *c)
{
    int m = c->hour * 60 + c->minute;
    return gs->hab.sleep_streak >= LLV_SLEEP_STREAK_GOOD && m >= 6 * 60 && m < 8 * 60 + 30;
}

static void sunrise_special(Framebuffer *fb, const CivilTime *c, uint32_t ms, int nights)
{
    int m = c->hour * 60 + c->minute;
    float f = (float)(m - LLV_SUNRISE_MIN) / (float)(LLV_SUNSET_MIN - LLV_SUNRISE_MIN);
    if (f < 0.0f)
        f = 0.0f;
    int sx = 6 + (int)(f * 108), sy = 90 - (int)(sinf(PI_F * f) * 40);
    gfx_dither(fb, 0, 76, LLV_W, 14, C_SKY_S4, C_SKY_S3); /* el horizonte se enciende */
    float spin = (float)(ms % 6000) / 6000.0f * 2.0f * PI_F;
    for (int k = 0; k < 12; k++) { /* rayos que giran despacio */
        float a = spin + (float)k * PI_F / 6.0f;
        int len = (k & 1) ? 12 : 17;
        for (int r = 10; r < 10 + len; r++)
            gfx_pset(fb, sx + (int)(cosf(a) * (float)r), sy + (int)(sinf(a) * (float)r),
                     (k & 1) ? C_SUN_CORE : C_SUN);
    }
    gfx_ellipse(fb, sx, sy, 7, 7, C_SUN);
    gfx_ellipse(fb, sx - 1, sy - 1, 4, 4, C_SUN_CORE);
    for (int i = 0; i < 16; i++) { /* destellos */
        uint32_t h = (uint32_t)(i * 2654435761u);
        if ((ms / 250 + (uint32_t)i) % 4 == 0)
            continue;
        gfx_pset(fb, 4 + (int)(h % 112), LLV_HUD_H + 2 + (int)((h >> 9) % 60), i & 1 ? C_WHITE : C_SUN);
    }
    char chip[24];
    snprintf(chip, sizeof(chip), "Amanecer: %d noches", nights > 99 ? 99 : nights);
    int w = gfx_text_width(chip) + 8;
    gfx_rect(fb, (LLV_W - w) / 2, 10, w, 10, C_DARK);
    gfx_text_center(fb, 11, chip, C_SUN);
}

static void style_celeste(Framebuffer *fb, const GameState *gs, const CivilTime *c, int64_t now, uint32_t ms)
{
    sky_scene(fb, c, now, ms);
    if (good_sunrise(gs, c))
        sunrise_special(fb, c, ms, gs->hab.sleep_streak);
    bool night = period_of(c->hour * 60 + c->minute) == P_NIGHT;
    big_time(fb, c, 20, night ? C_MOON : C_WHITE, night ? C_SKY_N4 : C_BLUE_TXT);
    char date[20];
    snprintf(date, sizeof(date), "%s %d %s", WDAY_ABBR[c->wday], c->day, MONTH_ABBR[c->month - 1]);
    int w = gfx_text_width(date) + 12;
    gfx_textbox(fb, (LLV_W - w) / 2, 99, w, 15);
    gfx_text_center(fb, 103, date, C_DARK);
}

static void style_palabras(Framebuffer *fb, const CivilTime *c, int64_t now, uint32_t ms)
{
    sky_scene(fb, c, now, ms);
    char words[96];
    reloj_time_in_words(words, sizeof(words), c->hour, c->minute);
    int lines = gfx_text_wrap(NULL, 0, 0, 104, words, C_DARK);
    int h = lines * GFX_LINE_H + 9;
    gfx_textbox(fb, 3, LLV_H - 3 - h, 114, h);
    gfx_text_wrap(fb, 8, LLV_H - 3 - h + 5, 104, words, C_DARK);
}

static void led_row(Framebuffer *fb, int y, const char *label, int value, int bits, Color on)
{
    gfx_text(fb, 6, y - 3, label, C_GREY_L);
    for (int b = 0; b < bits; b++) {
        int x = 22 + (6 - bits + b) * 12;
        bool lit = value & (1 << (bits - 1 - b));
        gfx_ellipse(fb, x, y, 4, 4, lit ? on : C_LED_OFF);
        if (lit)
            gfx_pset(fb, x - 2, y - 2, C_WHITE);
    }
    char v[4];
    snprintf(v, sizeof(v), "%02d", value);
    gfx_text(fb, 104, y - 3, v, C_GREY_L);
}

static void style_binario(Framebuffer *fb, const CivilTime *c)
{
    gfx_dither(fb, 0, LLV_HUD_H, LLV_W, LLV_H - LLV_HUD_H, C_DARK, C_VIOLET_D);
    led_row(fb, 34, "H", c->hour, 5, C_MAGENTA);
    led_row(fb, 58, "M", c->minute, 6, C_CYAN);
    led_row(fb, 82, "S", c->second, 6, C_SUN);
    gfx_text_center(fb, 104, "32 16 8 4 2 1", C_GREY_D);
}

static void style_cuantico(Framebuffer *fb, const CivilTime *c, uint32_t ms)
{
    gfx_dither(fb, 0, LLV_HUD_H, LLV_W, LLV_H - LLV_HUD_H, C_VIOLET_D, C_DARK);
    uint32_t cycle = ms / 3000, phase = ms % 3000;
    int real = c->hour * 60 + c->minute;
    char t[16];
    int x = (LLV_W - gfx_digits_width("00:00", 4)) / 2;
    if (phase < 2000) {
        Rng r;
        rng_seed(&r, cycle, 3);
        static const Color GHOST[3] = {C_VIOLET, C_CYAN, C_MAGENTA};
        for (int k = 0; k < 3; k++) {
            int cand = (real + (int)rng_below(&r, 600) - 300 + 1440) % 1440;
            snprintf(t, sizeof(t), "%02d:%02d", cand / 60, cand % 60);
            int jx = (int)((ms / 90 + (uint32_t)k * 5) % 5) - 2;
            int jy = (int)((ms / 130 + (uint32_t)k * 3) % 3) - 1;
            gfx_digits(fb, x + jx, 38 + jy, t, GHOST[k], 4);
        }
        /* Transparencia por dithering: los fantasmas dejan ver el fondo. */
        for (int y = 30; y < 66; y++)
            for (int i = 0; i < LLV_W; i++) {
                Color px = gfx_get(fb, i, y);
                if ((i + y) % 2 && (px == C_VIOLET || px == C_CYAN || px == C_MAGENTA))
                    gfx_pset(fb, i, y, (i + y) % 4 == 1 ? C_VIOLET_D : C_DARK);
            }
        gfx_text_center(fb, 92, "Superposición…", C_GREY_L);
        gfx_text_center(fb, 102, "mírame un rato", C_GREY);
    } else {
        snprintf(t, sizeof(t), "%02d:%02d", c->hour, c->minute);
        gfx_digits(fb, x, 38, t, C_WHITE, 4);
        Rng r;
        rng_seed(&r, ms / 120, 5);
        int sparks = (int)(3000 - phase) / 40;
        for (int i = 0; i < sparks; i++)
            gfx_pset(fb, 10 + (int)rng_below(&r, 100), 28 + (int)rng_below(&r, 40), C_SUN);
        gfx_text_center(fb, 92, "Colapsó.", C_WHITE);
    }
}

static void countdown_text(char *buf, size_t n, int64_t secs)
{
    int64_t d = secs / 86400, h = secs % 86400 / 3600;
    if (d > 0)
        snprintf(buf, n, "%lldd %lldh", (long long)d, (long long)h);
    else
        snprintf(buf, n, "%lldh %lldm", (long long)h, (long long)(secs % 3600 / 60));
}

static void style_cuenta(Framebuffer *fb, const CivilTime *c, int64_t now, uint32_t ms)
{
    gfx_bands(fb, LLV_HUD_H, LLV_H, SKY[P_NIGHT], 5);
    stars(fb, ms, 40);
    moon(fb, 60, 38, 17, now, C_SKY_N3);

    char l1[64], l2[64], l3[64], t[48];
    int64_t full = time_until_full_moon(now);
    int64_t since_full = LLV_SYNODIC_SECONDS - full;
    if (full < 12 * 3600 || since_full < 12 * 3600) {
        snprintf(l1, sizeof(l1), "¡Luna llena hoy!");
    } else {
        countdown_text(t, sizeof(t), full);
        snprintf(l1, sizeof(l1), "Llena: %s", t);
    }

    if (c->wday == 0 || c->wday == 6) {
        snprintf(l2, sizeof(l2), "¡Es finde!");
    } else {
        int64_t secs = (int64_t)(6 - c->wday) * 86400 - (c->hour * 3600 + c->minute * 60 + c->second);
        countdown_text(t, sizeof(t), secs);
        snprintf(l2, sizeof(l2), "Finde: %s", t);
    }

    /* Días hasta el próximo cambio de estación (20/3, 21/6, 23/9, 21/12). */
    static const int STARTS[4][2] = {{3, 20}, {6, 21}, {9, 23}, {12, 21}};
    int64_t today = time_days_from_civil(c->year, c->month, c->day);
    int64_t best = 400;
    int next = 0;
    for (int y = c->year; y <= c->year + 1; y++)
        for (int s = 0; s < 4; s++) {
            int64_t dd = time_days_from_civil(y, STARTS[s][0], STARTS[s][1]) - today;
            if (dd > 0 && dd < best) {
                best = dd;
                next = s;
            }
        }
    Season ns = time_season(STARTS[next][0], STARTS[next][1], LLV_SOUTH_HEMISPHERE);
    snprintf(l3, sizeof(l3), "%s: %lldd", SEASON_NAME[ns], (long long)best);

    gfx_textbox(fb, 3, 76, 114, 41);
    gfx_text(fb, 9, 81, l1, C_DARK);
    gfx_text(fb, 9, 91, l2, C_DARK);
    gfx_text(fb, 9, 101, l3, C_DARK);
}

static void date_card(Framebuffer *fb, const CivilTime *c, int64_t now)
{
    char l2[32], l3[32];
    snprintf(l2, sizeof(l2), "%d de %s", c->day, MONTH_FULL[c->month - 1]);
    snprintf(l3, sizeof(l3), "%d · %s", c->year,
             SEASON_NAME[time_season(c->month, c->day, LLV_SOUTH_HEMISPHERE)]);
    gfx_textbox(fb, 3, 30, 114, 52);
    gfx_text(fb, 10, 36, WDAY_FULL[c->wday], C_BLUE_TXT);
    gfx_text(fb, 10, 47, l2, C_DARK);
    gfx_text(fb, 10, 57, l3, C_DARK);
    gfx_text(fb, 10, 68, MOON_NAME[time_moon_phase(now)], C_GREY);
}

/* ---------- interfaz World ---------- */

static void reloj_render(const GameState *gs, const WorldCtx *ctx, Framebuffer *fb)
{
    CivilTime c = time_to_civil(ctx->now);
    switch (gs->reloj_style % STYLE_COUNT) {
    case STYLE_CELESTE: style_celeste(fb, gs, &c, ctx->now, ctx->ms); break;
    case STYLE_PALABRAS: style_palabras(fb, &c, ctx->now, ctx->ms); break;
    case STYLE_BINARIO: style_binario(fb, &c); break;
    case STYLE_CUANTICO: style_cuantico(fb, &c, ctx->ms); break;
    case STYLE_CUENTA: style_cuenta(fb, &c, ctx->now, ctx->ms); break;
    }
    if (ms_pending(ctx->ms, date_card_until)) {
        date_card(fb, &c, ctx->now);
    } else if (ms_pending(ctx->ms, style_label_until)) {
        const char *name = STYLE_NAME[gs->reloj_style % STYLE_COUNT];
        int w = gfx_text_width(name) + 8;
        gfx_rect(fb, (LLV_W - w) / 2, 11, w, 11, C_DARK);
        gfx_text_center(fb, 13, name, C_SUN);
    }
}

static void reloj_tap(GameState *gs, const WorldCtx *ctx)
{
    gs->reloj_style = (uint8_t)((gs->reloj_style + 1) % STYLE_COUNT);
    style_label_until = ctx->ms + 1200;
    date_card_until = ctx->ms;
}

static void reloj_hold(GameState *gs, const WorldCtx *ctx)
{
    (void)gs;
    date_card_until = ctx->ms + 3500;
}

const World world_reloj = {
    .name = "Reloj Celeste",
    .daily_actions = 0,
    .tick = NULL,
    .render = reloj_render,
    .on_tap = reloj_tap,
    .on_hold = reloj_hold,
};
