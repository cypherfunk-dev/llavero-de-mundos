# Makefile sencillo (alternativa a CMake). Salida en build/make/.
CC      ?= cc
CFLAGS  ?= -O2
CFLAGS  += -std=c11 -Wall -Wextra -Wpedantic
B       := build/make

CORE := $(sort $(wildcard src/core/*.c) $(wildcard src/core/worlds/*.c) src/core/phrases_data.c)
UTIL := src/util/imgout.c src/util/wav.c src/util/musicstats.c
HEADLESS_HAL := src/hal/audio.c src/platform/headless/hal_audio_stub.c
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
SHOTS ?= capturas
PYTHON ?= python3

# El banco de frases (spec §5): los .txt de data/frases se convierten en un array const que se
# compila en el firmware. Se versiona ya generado; si no hay python3, se usa el que está.
PHRASE_INPUTS := $(wildcard data/frases/*.txt) src/core/phrases_vocab.h tools/phrases2bin.py
src/core/phrases_data.c: $(PHRASE_INPUTS)
	@if command -v $(PYTHON) >/dev/null 2>&1; then $(PYTHON) tools/phrases2bin.py; \
	 else echo "aviso: no hay $(PYTHON); se usa phrases_data.c tal como está"; touch $@; fi

phrases:
	$(PYTHON) tools/phrases2bin.py
check-phrases:
	$(PYTHON) tools/phrases2bin.py --selftest
	$(PYTHON) tools/phrases2bin.py --check

.PHONY: all test sim headless music-tool shots shots-reloj shots-jardin shots-sala shots-musica shots-pesca shots-alfa music alpha phrases check-phrases clean
all: test headless sim

$(B):
	mkdir -p $(B)

$(B)/llv_tests: tests/test_main.c $(CORE) $(wildcard src/core/*.h) | $(B)
	$(CC) $(CFLAGS) tests/test_main.c $(CORE) -lm -o $@

$(B)/llv_test_music: tests/test_music.c $(UTIL) $(CORE) $(wildcard src/core/*.h src/util/*.h) | $(B)
	$(CC) $(CFLAGS) tests/test_music.c $(UTIL) $(CORE) -lm -o $@

$(B)/llv_test_studio: tests/test_studio.c $(UTIL) $(CORE) $(wildcard src/core/*.h src/util/*.h) | $(B)
	$(CC) $(CFLAGS) tests/test_studio.c $(UTIL) $(CORE) -lm -o $@

$(B)/llv_test_pesca: tests/test_pesca.c $(CORE) $(wildcard src/core/*.h) | $(B)
	$(CC) $(CFLAGS) tests/test_pesca.c $(CORE) -lm -o $@

$(B)/llv_headless: src/platform/headless/main.c $(HEADLESS_HAL) $(UTIL) $(CORE) $(wildcard src/core/*.h src/util/*.h src/hal/*.h src/platform/headless/*.h) | $(B)
	$(CC) $(CFLAGS) src/platform/headless/main.c $(HEADLESS_HAL) $(UTIL) $(CORE) -lm -o $@

$(B)/llv_music: src/platform/headless/music_tool.c $(UTIL) $(CORE) $(wildcard src/core/*.h src/util/*.h) | $(B)
	$(CC) $(CFLAGS) src/platform/headless/music_tool.c $(UTIL) $(CORE) -lm -o $@

$(B)/llv_sim: src/platform/sdl/main.c src/platform/sdl/hal_sdl.c src/hal/boot.c src/hal/audio.c $(UTIL) $(CORE) $(wildcard src/core/*.h src/hal/*.h src/platform/sdl/*.h) | $(B)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) src/platform/sdl/main.c src/platform/sdl/hal_sdl.c src/hal/boot.c src/hal/audio.c $(UTIL) $(CORE) $(SDL_LIBS) -lm -o $@

test: $(B)/llv_tests $(B)/llv_test_music $(B)/llv_test_studio $(B)/llv_test_pesca $(B)/llv_headless
	$(B)/llv_tests
	$(B)/llv_test_music
	$(B)/llv_test_studio
	$(B)/llv_test_pesca
	$(B)/llv_headless --date "2026-10-22 08:00" --seed 7 --out $(B) alpha 14 $(B)/informe_alfa.txt stock > $(B)/alfa.log
	@echo "alfa de 14 días: 0 violaciones ($(B)/informe_alfa.txt)"
	@if command -v $(PYTHON) >/dev/null 2>&1; then $(MAKE) --no-print-directory check-phrases; fi

headless: $(B)/llv_headless
sim: $(B)/llv_sim

music-tool: $(B)/llv_music

# Música (fase 4): 200 semillas a WAV de 30 s, informe con las métricas y los umbrales de "agradable",
# la comparación con una melodía ingenua, un barrido de las 65.536 semillas y piano rolls y
# espectrogramas de las primeras. Todo en capturas/musica/ (informe_musica.txt, musica_XXXX.wav...).
# Estudio (v1.1): el informe de las 77 combinaciones de escala x arpegio (100 semillas cada una, con audio),
# el piano roll de cada combinación y los WAV de ejemplo (informe_estudio.txt, estudio_*.png, estudio_*.wav).
music: $(B)/llv_music
	mkdir -p $(SHOTS)/musica
	$(B)/llv_music --seeds 200 --seconds 30 --out $(SHOTS)/musica --baseline --sweep 65536 --roll 8
	$(B)/llv_music --matrix 100 --examples --first 1000 --out $(SHOTS)/musica --report informe_estudio.txt

# Capturas del Reloj, del Jardín/Cobertizo (fase 2), de la Sala con su semana simulada (fase 3), de la Música (fase 4)
# y de la Pesca con la colección (fase 5).
shots: shots-reloj shots-jardin shots-sala shots-musica shots-pesca

shots-pesca: $(B)/llv_headless
	tools/shots_pesca.sh $(B)/llv_headless $(SHOTS)

shots-alfa: $(B)/llv_headless
	tools/shots_alfa.sh $(B)/llv_headless $(SHOTS)

shots-musica: $(B)/llv_headless
	tools/shots_musica.sh $(B)/llv_headless $(SHOTS)

shots-sala: $(B)/llv_headless
	tools/shots_sala.sh $(B)/llv_headless $(SHOTS)

shots-jardin: $(B)/llv_headless
	tools/shots_jardin.sh $(B)/llv_headless $(SHOTS)

# El Reloj en sus 5 estilos a varias horas: una hoja de contacto por momento.
shots-reloj: $(B)/llv_headless
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

# La alfa entera (fase 5): los tests, 14 días de uso por los 6 mundos (con y sin lombriz extra diaria), el
# informe y las capturas de los seis mundos en cuatro momentos del día. Todo en capturas/alfa/.
alpha: test $(B)/llv_headless
	mkdir -p $(SHOTS)/alfa
	$(B)/llv_headless --date "2026-10-22 08:00" --seed 7 --out $(SHOTS)/alfa alpha 14 $(SHOTS)/alfa/informe_alfa.txt
	$(B)/llv_headless --date "2026-10-22 08:00" --seed 7 --out $(SHOTS)/alfa alpha 14 $(SHOTS)/alfa/informe_alfa_stock.txt stock
	$(B)/llv_headless --date "2027-02-03 08:00" --seed 21 --out $(SHOTS)/alfa alpha 14 $(SHOTS)/alfa/informe_alfa_invierno.txt stock
	tools/shots_alfa.sh $(B)/llv_headless $(SHOTS)
	tools/shots_pesca.sh $(B)/llv_headless $(SHOTS)
