/* HAL del simulador: SDL2 para pantalla, teclado y audio; archivo para el guardado. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../core/timeutil.h"
#include "../../hal/hal.h"
#include "../../util/imgout.h"
#include "sim.h"

#define AUDIO_RATE 22050
#define TONE_QUEUE 16

static SimConfig cfg;
static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static uint32_t *pixels; /* LLV_W*scale x LLV_H*scale, 0xRRGGBB */

/* Máquina del tiempo. */
static int64_t warp_secs;
static int64_t warp_pending;
static char warp_label[16];

/* Eventos entre llamadas. */
static bool shake_flag;
static bool shot_flag;

/* Cuerpo simulado (tecla W: camina 1000 pasos; tecla 4: alterna batería baja) y LED. */
static uint32_t sim_steps;
static bool sim_batt_low;
static bool led_on;

/* Audio: cola de tonos consumida desde el callback. */
static SDL_AudioDeviceID audio_dev;
static Tone queue[TONE_QUEUE];
static int q_head, q_count;
static uint32_t tone_left;  /* muestras que quedan del tono actual */
static uint32_t tone_total;
static uint32_t tone_phase; /* contador de fase en 1/65536 de ciclo */
static uint32_t tone_step;

static const SimConfig *cfg_of(void *arg) { return (const SimConfig *)arg; }

int64_t sim_warp_total(void) { return warp_secs; }

int64_t sim_take_warp(char *label, int n)
{
    int64_t w = warp_pending;
    warp_pending = 0;
    if (w && label)
        snprintf(label, (size_t)n, "%s", warp_label);
    return w;
}

/* ---------- audio ---------- */

static void audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int n = len / 2;
    for (int i = 0; i < n; i++) {
        if (tone_left == 0 && q_count > 0) {
            Tone t = queue[q_head];
            q_head = (q_head + 1) % TONE_QUEUE;
            q_count--;
            tone_total = tone_left = (uint32_t)t.ms * AUDIO_RATE / 1000u;
            tone_step = (uint32_t)(((uint64_t)t.freq << 16) / AUDIO_RATE);
        }
        if (tone_left == 0) {
            out[i] = 0;
            continue;
        }
        /* Onda cuadrada con un pequeño fundido final (sin clic). */
        int amp = 2600;
        uint32_t fade = AUDIO_RATE / 400; /* ~2,5 ms */
        if (tone_left < fade)
            amp = (int)(amp * tone_left / fade);
        tone_phase += tone_step;
        out[i] = (int16_t)((tone_phase & 0x8000u) ? amp : -amp);
        tone_left--;
    }
}

void hal_sound_play(const Tone *tones, int count)
{
    if (!audio_dev)
        return;
    SDL_LockAudioDevice(audio_dev);
    for (int i = 0; i < count && q_count < TONE_QUEUE; i++) {
        queue[(q_head + q_count) % TONE_QUEUE] = tones[i];
        q_count++;
    }
    SDL_UnlockAudioDevice(audio_dev);
}

/* ---------- ciclo de vida ---------- */

int hal_init(void *arg)
{
    cfg = *cfg_of(arg);
    if (cfg.scale < 1)
        cfg.scale = 4;
    if (cfg.scale > IMG_MAX_SCALE)
        cfg.scale = IMG_MAX_SCALE;

    Uint32 flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    if (SDL_Init(flags) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    int w = LLV_W * cfg.scale, h = LLV_H * cfg.scale;
    win = SDL_CreateWindow("Llavero de Mundos", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           w, h, SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    pixels = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!tex || !pixels) {
        fprintf(stderr, "no se pudo crear la textura\n");
        return 1;
    }

    /* El audio es opcional: sin dispositivo, el simulador sigue mudo. */
    if (!cfg.mute && SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
        SDL_AudioSpec want, have;
        SDL_zero(want);
        want.freq = AUDIO_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 512;
        want.callback = audio_cb;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (audio_dev)
            SDL_PauseAudioDevice(audio_dev, 0);
        else
            fprintf(stderr, "sin audio: %s\n", SDL_GetError());
    } else if (!cfg.mute) {
        fprintf(stderr, "sin audio: %s\n", SDL_GetError());
    }
    return 0;
}

void hal_shutdown(void)
{
    if (audio_dev)
        SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
    free(pixels);
    pixels = NULL;
    if (tex)
        SDL_DestroyTexture(tex);
    if (ren)
        SDL_DestroyRenderer(ren);
    if (win)
        SDL_DestroyWindow(win);
    tex = NULL;
    ren = NULL;
    win = NULL;
    SDL_Quit();
}

/* ---------- display ---------- */

void hal_display_present(const Framebuffer *fb, bool screen_on)
{
    int w = LLV_W * cfg.scale, h = LLV_H * cfg.scale;
    if (screen_on) {
        img_render(fb, cfg.scale, cfg.lcd, pixels);
        for (int i = 0; i < w * h; i++)
            pixels[i] |= 0xFF000000u;
    } else {
        memset(pixels, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    }
    /* El LED de aviso es un punto naranja en la esquina (se ve incluso con la pantalla apagada);
     * se guarda lo que tapa para que las capturas salgan limpias. */
    int led_r = cfg.scale * 2, led_x = w - cfg.scale * 6, led_y = cfg.scale * 6;
    static uint32_t under[1024];
    int led_n = 0;
    if (led_on && (2 * led_r + 1) * (2 * led_r + 1) <= 1024) {
        for (int j = -led_r; j <= led_r; j++)
            for (int i = -led_r; i <= led_r; i++, led_n++) {
                int idx = (led_y + j) * w + led_x + i;
                under[led_n] = pixels[idx];
                if (i * i + j * j <= led_r * led_r)
                    pixels[idx] = 0xFFF08020u;
            }
    }
    SDL_UpdateTexture(tex, NULL, pixels, w * (int)sizeof(uint32_t));
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    if (led_n) {
        led_n = 0;
        for (int j = -led_r; j <= led_r; j++)
            for (int i = -led_r; i <= led_r; i++, led_n++)
                pixels[(led_y + j) * w + led_x + i] = under[led_n];
    }

    if (shot_flag && screen_on) {
        shot_flag = false;
        char name[64];
        time_t t = time(NULL);
        struct tm *tm = localtime(&t);
        static int n;
        strftime(name, sizeof(name), "llavero-%Y%m%d-%H%M%S", tm);
        char path[96];
        snprintf(path, sizeof(path), "%s-%d.png", name, n++);
        /* El PNG no lleva el canal alfa de la textura. */
        for (int i = 0; i < w * h; i++)
            pixels[i] &= 0x00FFFFFFu;
        if (img_write_png(path, pixels, w, h) == 0)
            fprintf(stderr, "captura: %s\n", path);
        else
            fprintf(stderr, "no se pudo escribir %s\n", path);
    }
}

void sim_write_shot(const char *path)
{
    int w = LLV_W * cfg.scale, h = LLV_H * cfg.scale;
    if (!pixels)
        return;
    for (int i = 0; i < w * h; i++)
        pixels[i] &= 0x00FFFFFFu;
    if (img_write_png(path, pixels, w, h) != 0)
        fprintf(stderr, "no se pudo escribir %s\n", path);
}

/* ---------- input ---------- */

static void add_warp(int64_t secs, const char *label)
{
    warp_secs += secs;
    warp_pending += secs;
    snprintf(warp_label, sizeof(warp_label), "%s", label);
}

bool hal_input_poll(RawInput *in)
{
    bool run = true;
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            run = false;
        } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            switch (ev.key.keysym.scancode) {
            case SDL_SCANCODE_ESCAPE: run = false; break;
            case SDL_SCANCODE_S: shake_flag = true; break;
            case SDL_SCANCODE_W: sim_steps += 1000; break;
            case SDL_SCANCODE_4: sim_batt_low = !sim_batt_low; break;
            case SDL_SCANCODE_1: add_warp(3600, "+1 h"); break;
            case SDL_SCANCODE_2: add_warp(86400, "+1 día"); break;
            case SDL_SCANCODE_3: add_warp(7 * 86400, "+1 semana"); break;
            case SDL_SCANCODE_F12:
            case SDL_SCANCODE_P: shot_flag = true; break;
            case SDL_SCANCODE_L: cfg.lcd = !cfg.lcd; break;
            default: break;
            }
        }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    in->buttons = 0;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_J])
        in->buttons |= BTN_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_K])
        in->buttons |= BTN_B;
    in->shake = shake_flag;
    shake_flag = false;
    return run;
}

/* ---------- body y led ---------- */

void hal_body_read(uint8_t *battery, bool *charging, uint32_t *steps)
{
    *battery = sim_batt_low ? 12 : 87;
    *charging = false;
    *steps = sim_steps;
}

void hal_led_set(bool on)
{
    led_on = on;
}

/* ---------- clock ---------- */

int64_t hal_clock_local_seconds(void)
{
    time_t t = time(NULL);
    struct tm *l = localtime(&t);
    int64_t secs = time_from_civil(l->tm_year + 1900, l->tm_mon + 1, l->tm_mday, l->tm_hour,
                                   l->tm_min, l->tm_sec);
    return secs + warp_secs;
}

uint32_t hal_clock_ms(void) { return SDL_GetTicks(); }

void hal_clock_delay_ms(uint32_t ms) { SDL_Delay(ms); }

/* ---------- storage ---------- */

size_t hal_storage_load(uint8_t *buf, size_t cap)
{
    FILE *f = fopen(cfg.save_path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

bool hal_storage_save(const uint8_t *buf, size_t len)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", cfg.save_path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    bool ok = fwrite(buf, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
    if (ok)
        ok = rename(tmp, cfg.save_path) == 0;
    if (!ok)
        remove(tmp);
    return ok;
}
