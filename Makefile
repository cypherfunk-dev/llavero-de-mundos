# Makefile sencillo (alternativa a CMake). Salida en build/make/.
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
B       := build/make

CORE := $(wildcard src/core/*.c) $(wildcard src/core/worlds/*.c)
UTIL := src/util/imgout.c
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
SHOTS ?= capturas

.PHONY: all test sim headless shots clean
all: test headless sim

$(B):
	mkdir -p $(B)

$(B)/llv_tests: tests/test_main.c $(CORE) $(wildcard src/core/*.h) | $(B)
	$(CC) $(CFLAGS) tests/test_main.c $(CORE) -lm -o $@

$(B)/llv_headless: src/platform/headless/main.c $(UTIL) $(CORE) $(wildcard src/core/*.h) | $(B)
	$(CC) $(CFLAGS) src/platform/headless/main.c $(UTIL) $(CORE) -lm -o $@

$(B)/llv_sim: src/platform/sdl/main.c src/platform/sdl/hal_sdl.c src/hal/boot.c $(UTIL) $(CORE) $(wildcard src/core/*.h src/hal/*.h src/platform/sdl/*.h) | $(B)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/platform/sdl/main.c src/platform/sdl/hal_sdl.c src/hal/boot.c $(UTIL) $(CORE) $(SDL_LIBS) -lm -o $@

test: $(B)/llv_tests
	$(B)/llv_tests

headless: $(B)/llv_headless
sim: $(B)/llv_sim

# El Reloj en sus 5 estilos a varias horas: una hoja de contacto por momento.
shots: $(B)/llv_headless
	mkdir -p $(SHOTS)
	$(B)/llv_headless --date "2024-06-12 12:00" --out $(SHOTS) --scale 3 \
	  style 0 shot dia_s0 style 1 shot dia_s1 style 2 shot dia_s2 style 3 shot dia_s3 \
	  style 4 shot dia_s4 sheet reloj_dia 5
	$(B)/llv_headless --date "2024-06-12 19:45" --out $(SHOTS) --scale 3 \
	  style 0 shot atardecer_s0 style 1 shot atardecer_s1 style 2 shot atardecer_s2 \
	  style 3 shot atardecer_s3 style 4 shot atardecer_s4 sheet reloj_atardecer 5
	$(B)/llv_headless --date "2024-01-25 23:30" --out $(SHOTS) --scale 3 \
	  style 0 shot noche_s0 style 1 shot noche_s1 style 2 shot noche_s2 style 3 shot noche_s3 \
	  style 4 shot lunallena_s4 sheet reloj_luna_llena 5
	$(B)/llv_headless --date "2024-06-12 06:45" --out $(SHOTS) --scale 3 \
	  style 0 shot dia_x1 at "2024-06-12 09:00" shot dia_x2 at "2024-06-12 12:00" shot dia_x3 \
	  at "2024-06-12 16:30" shot dia_x4 at "2024-06-12 21:00" shot dia_x5 \
	  at "2024-06-13 03:00" shot dia_x6 sheet reloj_ciclo_dia 6

clean:
	rm -rf build
