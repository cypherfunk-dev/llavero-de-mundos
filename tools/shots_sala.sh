#!/bin/sh
# Capturas de la Sala (fase 3) y su semana simulada con la herramienta headless.
# Uso: tools/shots_sala.sh RUTA/llv_headless DIRECTORIO_DE_SALIDA
set -e
H=${1:-build/make/llv_headless}
OUT=${2:-capturas}
mkdir -p "$OUT"
run() { "$H" --out "$OUT" --scale 3 "$@"; }

# 1) La habitación en reposo, los dos residentes y los tres visitantes con su caja de texto.
run --date "2026-10-22 17:00" --seed 3 world 2 shot sala_reposo \
    say abuela shot sala_abuela say robot shot sala_robot \
    visitor gotica say gotica shot sala_gotica visitor gamer say gamer shot sala_gamer \
    visitor boomer say boomer shot sala_boomer sheet sala_personajes 4 > /dev/null

# 2) Preguntas de sí o no: la pregunta, la reacción y, días después, el recuerdo.
run --date "2026-10-22 17:00" --seed 3 world 2 ask plantas shot sala_pregunta no shot sala_reaccion \
    adv 3d world 2 wake wait 4000 talk shot sala_retoma sheet sala_preguntas 3 > /dev/null

# 3) Escena a dos voces, de madrugada en pijama y el modo "se acabaron las charlas".
run --date "2026-10-22 17:00" --seed 3 world 2 say abuela escena shot sala_escena1 talk shot sala_escena2 > /dev/null
run --date "2026-10-23 03:10" --seed 3 world 2 say robot shot sala_pijama > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 drain shot sala_relleno > /dev/null

# 4) Fechas especiales: Halloween, viernes 13, Año Nuevo y Navidad.
run --date "2026-10-31 18:00" --seed 3 world 2 visitor gotica say gotica shot sala_halloween > /dev/null
run --date "2027-08-13 18:00" --seed 3 world 2 visitor gato say gato shot sala_viernes13 > /dev/null
run --date "2027-01-01 12:00" --seed 3 world 2 visitor boomer say boomer shot sala_anio_nuevo > /dev/null
run --date "2026-12-25 12:00" --seed 3 world 2 say abuela shot sala_navidad > /dev/null

# 5) El Gato de Schrödinger con el código secreto B B A A B A B A (desde el Reloj).
run --date "2026-10-22 17:00" --seed 3 world 0 code shot sala_gato_codigo > /dev/null

# 6) Cosas inesperadas (spec §6): cartucho sucio, "salí", invasión, arranque falso, INSERT COIN, rebeldía.
run --date "2026-10-22 17:00" --seed 3 world 2 surprise glitch shot sorpresa_glitch > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise ausencia 2 wait 300 shot sorpresa_sali > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 1 surprise invasion wait 800 shot sorpresa_invasion > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise arranque wait 1000 shot sorpresa_arranque > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise moneda wait 350 shot sorpresa_moneda > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise rebeldia wait 200 tap b wait 3900 shot sorpresa_rebeldia > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise cuarta wait 3900 shot sorpresa_cuarta_pared > /dev/null
run --date "2026-10-22 17:00" --seed 3 world 2 surprise colapso wait 300 shot sorpresa_colapso > /dev/null

# 7) El HUD con ❗ en los mundos con novedades, el amanecer especial del Reloj y los ajustes de hábitos.
run --date "2026-10-22 17:00" --seed 3 pet 1 world 0 news 0x22 wait 3600 shot hud_novedades > /dev/null
run --date "2026-10-22 07:20" --seed 3 streak 4 world 0 wait 500 shot reloj_amanecer_especial > /dev/null
run --date "2026-10-22 12:00" --seed 3 pet 2 world 5 wait 3600 tap a tap a tap a tap a hold a wait 300 shot ajustes_habitos \
    tap a tap a hold a wait 200 shot ajustes_habitos2 > /dev/null

# 8) LED de aviso: tras una ausencia larga con el llavero dormido.
run --date "2026-10-22 12:00" --seed 11 pet 1 sleep adv 3d led sala > "$OUT/led_aviso.txt"
cat "$OUT/led_aviso.txt"

# 9) La semana del gate: 7 días con 4-7 miradas, charlas, preguntas y sorpresas.
run --date "2026-10-22 08:00" --seed 7 pet 1 week 7 salastats \
    dumpfrases "$OUT/semana_sala_frases.txt" shot semana_sala_fin > "$OUT/semana_sala_stats.txt"
cat "$OUT/semana_sala_stats.txt" | grep -v "^$OUT"
echo "capturas en $OUT/"
