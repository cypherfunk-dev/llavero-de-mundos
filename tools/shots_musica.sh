#!/bin/sh
# Capturas del mundo Música (fase 4) y su audio con la herramienta headless: de día y de noche, con
# lluvia, con estrella fugaz y arcoíris, con varias semillas, mutando y con semilla nueva, y los WAV de
# cada momento (lo que sonaría; el visualizador sale del mismo generador).
# Uso: tools/shots_musica.sh RUTA/llv_headless DIRECTORIO_DE_SALIDA
set -e
H=${1:-build/make/llv_headless}
OUT=${2:-capturas}
mkdir -p "$OUT"
run() { "$H" --out "$OUT" --scale 3 "$@"; }

# 1) Franjas del día con la misma semilla (la escala, el tempo y el timbre cambian con la hora).
run --date "2026-10-22 08:00" --seed 3 musicseed 17 world 3 wait 2600 shot musica_amanecer \
    at "2026-10-22 12:00" world 3 wait 2600 shot musica_dia \
    at "2026-10-22 19:30" world 3 wait 2600 shot musica_atardecer \
    at "2026-10-22 23:30" world 3 wait 2600 shot musica_noche \
    sheet musica_franjas 4 > /dev/null

# 2) Clima y cielo: la lluvia del Jardín vuelve la música más melancólica; la estrella y el arcoíris la tiñen.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2600 shot musica_dia \
    weather 1 world 0 world 3 wait 2600 shot musica_dia_lluvia \
    weather 0 sky 2 world 0 world 3 wait 2600 shot musica_arcoiris \
    at "2026-10-22 23:30" sky 0 world 3 wait 2600 shot musica_noche \
    weather 1 world 0 world 3 wait 2600 shot musica_noche_lluvia \
    weather 0 sky 1 world 0 world 3 wait 2600 shot musica_estrella_fugaz \
    sheet musica_clima 3 > /dev/null

# 3) Semillas distintas a la misma hora.
for s in 4 900 20300 65535 2863 777; do
    run --date "2026-10-22 12:00" --seed 3 musicseed $s world 3 wait 3100 shot musica_semilla_$s > /dev/null
done

# 4) Mutar (A, toque) y semilla nueva (A, mantener): el mensaje y la caja con el contador de mutaciones.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 shot musica_antes \
    tap a shot musica_mutar tap a tap a tap a shot musica_mutar4 hold a shot musica_semilla_nueva \
    sheet musica_acciones 5 > /dev/null

# 5) Animación: seis fotogramas seguidos (barras, osciloscopio y pasos).
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2400 \
    shot anim1 wait 100 shot anim2 wait 100 shot anim3 wait 100 shot anim4 wait 100 shot anim5 \
    wait 100 shot anim6 sheet musica_animacion 3 > /dev/null
rm -f "$OUT"/anim?.png

# 6) El audio (lo que saldría por el altavoz del simulador): 30 s de cada momento, en WAV.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 rec "$OUT/audio_dia.wav" wait 30000 recstop > /dev/null
run --date "2026-10-22 23:30" --seed 3 musicseed 17 world 3 rec "$OUT/audio_noche.wav" wait 30000 recstop > /dev/null
run --date "2026-10-22 23:30" --seed 3 musicseed 17 weather 1 world 3 rec "$OUT/audio_noche_lluvia.wav" \
    wait 30000 recstop > /dev/null
