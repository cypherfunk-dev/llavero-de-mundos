/* Herramienta headless: corre el motor sin ventana con reloj virtual, simula gestos y
 * tiempo, y escribe capturas PNG/PPM. Pensada para revisar el Reloj a distintas horas
 * y para scripts de humo. Ver --help. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/engine.h"
#include "../../core/timeutil.h"
#include "../../util/imgout.h"

#define MAX_GRAB 48

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
} Sim;

static void frame(Sim *s, uint32_t dt, uint8_t buttons, bool shake)
{
    s->ms += dt;
    s->sub_ms += dt;
    s->now += s->sub_ms / 1000;
    s->sub_ms %= 1000;
    RawInput in = {buttons, shake};
    s->out = engine_frame(&s->e, in, s->now, s->ms);
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

static int do_shot(Sim *s, const char *name)
{
    wake_if_needed(s);
    idle(s, 100);
    if (s->grab_count < MAX_GRAB)
        s->grabbed[s->grab_count++] = s->e.fb;
    img_render(&s->e.fb, s->scale, s->lcd, pixbuf);
    return write_img(s, name, pixbuf, LLV_W * s->scale, LLV_H * s->scale);
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
           "  sheet NOMBRE C hoja de contacto con las capturas previas en C columnas\n"
           "  adv DUR        pasa el tiempo (90s 30m 6h 2d 1w); se duerme solo\n"
           "  at FECHA       salta a esa fecha (hacia delante)\n"
           "  style N        Reloj en el estilo 0-4 (0 celeste 1 palabras 2 binario 3 cuántico 4 cuenta)\n"
           "  world N        va al mundo 0-5 con B\n"
           "  tap a|b  hold a|b  ab  shake  sleep  wake  wait MS\n"
           "  info           imprime un resumen del estado\n",
           p);
}

int main(int argc, char **argv)
{
    static Sim s;
    uint64_t seed = 1;
    int64_t start;
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
