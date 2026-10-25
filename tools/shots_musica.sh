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
#    (Dos toques seguidos de A en menos de 0,3 s abren el Estudio: entre toque y toque se espera.)
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 shot musica_antes \
    tap a shot musica_mutar wait 500 tap a wait 500 tap a wait 500 tap a shot musica_mutar4 \
    hold a shot musica_semilla_nueva sheet musica_acciones 5 > /dev/null

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

# 7) El Estudio (v1.1). Se abre con dos toques de A seguidos (aquí `tap a tap a`, 200 ms entre uno y otro).
#    a) El mundo Música con la pista "A A: ESTUDIO" y el Estudio recién abierto, todo en AUTO.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2600 shot estudio_pista \
    tap a tap a wait 300 shot estudio_abierto > /dev/null
#    b) Cada fila del panel, todo en AUTO (el desplazamiento mantiene visible la seleccionada).
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 studio open wait 300 \
    studio row 0 shot fila00 studio row 1 shot fila01 studio row 2 shot fila02 studio row 3 shot fila03 \
    studio row 4 shot fila04 studio row 5 shot fila05 studio row 6 shot fila06 studio row 7 shot fila07 \
    studio row 8 shot fila08 studio row 9 shot fila09 studio row 10 shot fila10 \
    sheet estudio_filas_auto 4 > /dev/null
#    c) Con candados (lo fijo en amarillo) y valores largos: PENT. MENOR, i-VII-III-VII, SUBE-BAJA, 140 BPM...
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 studio open \
    studio set 0 2 studio set 1 11 studio set 2 11 studio set 3 4 studio set 4 16 studio set 5 1 studio set 6 3 wait 300 \
    studio row 0 shot fijo00 studio row 3 shot fijo03 studio row 4 shot fijo04 studio row 8 shot fijo08 \
    studio row 10 shot fijo10 sheet estudio_candados 5 > /dev/null
#    d) Voces silenciadas: panel y mundo (barras grises tachadas), y la caja de datos con asterisco y candado.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 studio open \
    studio set 0 5 studio set 7 1 studio set 8 1 studio set 9 1 wait 400 studio row 7 shot voces_panel \
    studio close wait 400 shot voces_mundo \
    studio set 7 0 studio set 8 0 studio set 9 0 studio set 2 9 studio set 3 3 wait 400 shot caja_fija \
    sheet estudio_voces 4 > /dev/null
#    e) Las escalas del Estudio y el arpegio en la caja de datos (la misma semilla).
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 wait 2000 studio open \
    studio set 0 3 wait 300 studio close shot escala_mayor studio set 0 6 wait 300 shot escala_frigia \
    studio set 0 7 wait 300 shot escala_lidia studio set 0 8 wait 300 shot escala_mixolidia \
    studio set 0 9 wait 300 shot escala_blues studio set 0 10 wait 300 shot escala_armonica \
    sheet estudio_escalas 3 > /dev/null
#    f) De noche y con el panel desplazado al final.
run --date "2026-10-22 23:30" --seed 3 musicseed 900 world 3 wait 2000 studio open studio set 0 6 studio set 8 1 wait 300 \
    studio row 8 shot noche_panel studio row 10 shot noche_dado hold a shot noche_dado_semilla > /dev/null
rm -f "$OUT"/fila??.png "$OUT"/fijo??.png

# 8) Audio del Estudio: la misma semilla con tres ajustes, 20 s cada una.
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 studio open studio set 0 6 studio set 3 3 \
    studio close rec "$OUT/audio_estudio_frigia_baja.wav" wait 20000 recstop > /dev/null
run --date "2026-10-22 12:00" --seed 3 musicseed 17 world 3 studio open studio set 0 9 studio set 3 6 studio set 7 1 \
    studio close rec "$OUT/audio_estudio_blues_roto_sin_melodia.wav" wait 20000 recstop > /dev/null
