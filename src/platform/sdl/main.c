/* Simulador SDL2 del llavero. Uso: llv_sim [archivo.sav] [opciones]; ver --help. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../hal/hal.h"
#include "sim.h"

static void usage(const char *prog)
{
    printf("Uso: %s [archivo.sav] [opciones]\n"
           "  --save F     archivo de guardado (por defecto llavero.sav)\n"
           "  --scale N    ampliación de la ventana, 1-8 (por defecto 4)\n"
           "  --seed N     semilla del universo si no hay guardado\n"
           "  --lcd        empieza con la rejilla LCD\n"
           "  --mute       sin sonido\n"
           "  --frames N   sale solo tras N fotogramas (pruebas)\n"
           "  --shot F     con --frames, escribe una captura PNG al salir\n"
           "Teclas: Z/J=A  X/K=B  S=agitar  1/2/3=+1h/+1d/+1sem  F12/P=captura\n"
           "        L=rejilla LCD  Esc=salir\n",
           prog);
}

int main(int argc, char **argv)
{
    SimConfig cfg = {"llavero.sav", 4, false, false, NULL};
    uint64_t seed = (uint64_t)time(NULL);
    long frames = -1;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        bool more = i + 1 < argc;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(a, "--save") && more) {
            cfg.save_path = argv[++i];
        } else if (!strcmp(a, "--scale") && more) {
            cfg.scale = atoi(argv[++i]);
        } else if (!strcmp(a, "--seed") && more) {
            seed = strtoull(argv[++i], NULL, 0);
        } else if (!strcmp(a, "--frames") && more) {
            frames = atol(argv[++i]);
        } else if (!strcmp(a, "--shot") && more) {
            cfg.shot_path = argv[++i];
        } else if (!strcmp(a, "--lcd")) {
            cfg.lcd = true;
        } else if (!strcmp(a, "--mute")) {
            cfg.mute = true;
        } else if (a[0] != '-') {
            cfg.save_path = a;
        } else {
            fprintf(stderr, "opción desconocida: %s\n", a);
            usage(argv[0]);
            return 2;
        }
    }

    if (hal_init(&cfg) != 0) {
        hal_shutdown();
        return 1;
    }

    static Engine engine;
    bool resumed = hal_boot_engine(&engine, seed);
    fprintf(stderr, "%s (%s)\n", resumed ? "guardado cargado" : "universo nuevo", cfg.save_path);

    long n = 0;
    while (frames < 0 || n < frames) {
        uint32_t t0 = hal_clock_ms();
        RawInput in;
        if (!hal_input_poll(&in))
            break;

        char label[16];
        if (sim_take_warp(label, sizeof(label)) && !engine.asleep)
            engine_toast(&engine, label, hal_clock_ms(), 1200);

        FrameOut out = engine_frame(&engine, in, hal_clock_local_seconds(), hal_clock_ms());
        if (out.tone_count)
            hal_sound_play(out.tones, out.tone_count);
        if (out.save_now && !hal_save_engine(&engine))
            fprintf(stderr, "no se pudo guardar\n");
        hal_display_present(&engine.fb, out.screen_on);
        n++;

        uint32_t dt = hal_clock_ms() - t0;
        if (dt < 33)
            hal_clock_delay_ms(33 - dt); /* ~30 fps */
    }

    if (cfg.shot_path && frames >= 0)
        sim_write_shot(cfg.shot_path);
    int rc = 0;
    if (!hal_save_engine(&engine)) {
        fprintf(stderr, "no se pudo guardar al salir\n");
        rc = 1;
    }
    hal_shutdown();
    return rc;
}
