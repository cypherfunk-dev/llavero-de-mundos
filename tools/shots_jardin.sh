#!/bin/sh
# Capturas del Jardín, el Cobertizo y la mascota con la herramienta headless.
# Uso: tools/shots_jardin.sh RUTA/llv_headless DIRECTORIO_DE_SALIDA
set -e
H=${1:-build/make/llv_headless}
OUT=${2:-capturas}
mkdir -p "$OUT"
run() { "$H" --out "$OUT" --scale 3 "$@"; }

# 1) La semana del gate: día 0, 1, 3 y 7 con uso normal (4 miradas al día).
#    Cada pareja es Jardín + Cobertizo a las 11:00.
run --date "2026-10-22 11:00" --seed 7 \
    pet 1 world 1 wait 3000 shot semana_d0_jardin world 5 wait 3000 shot semana_d0_cobertizo \
    routine 1 at "2026-10-23 11:00" \
    world 1 wait 3000 shot semana_d1_jardin world 5 wait 3000 shot semana_d1_cobertizo \
    routine 2 at "2026-10-25 11:00" \
    world 1 wait 3000 shot semana_d3_jardin world 5 wait 3000 shot semana_d3_cobertizo \
    routine 4 at "2026-10-29 11:00" \
    world 1 wait 3000 shot semana_d7_jardin world 5 wait 3000 shot semana_d7_cobertizo \
    sheet semana 4 stats > "$OUT/semana_stats.txt"
cat "$OUT/semana_stats.txt" | head -3

# 2) Las cuatro estaciones (a mediodía, tras unos días de uso).
run --date "2026-01-20 10:00" --seed 11 pet 2 routine 3 \
    at "2026-01-24 12:00" world 1 wait 3000 shot estacion_invierno \
    at "2026-04-20 12:00" routine 3 at "2026-04-24 12:00" world 1 wait 3000 shot estacion_primavera \
    at "2026-07-15 12:00" routine 3 at "2026-07-19 12:00" world 1 wait 3000 shot estacion_verano \
    at "2026-10-22 12:00" routine 3 at "2026-10-26 12:00" world 1 wait 3000 shot estacion_otono \
    sheet estaciones 2 > /dev/null

# 3) Un par de días de otoño: amanecer, arcoíris, lluvia, ocaso, viento y estrella fugaz.
run --date "2026-10-22 06:50" --seed 5 pet 3 \
    world 1 wait 3000 shot dia_amanecer \
    at "2026-10-22 09:00" give 6 world 1 tap a wait 3500 shot dia_arcoiris \
    at "2026-10-22 12:00" give 1 world 1 tap a wait 3200 shot dia_lluvia \
    at "2026-10-22 19:12" world 1 wait 3500 shot dia_ocaso \
    at "2026-10-23 12:00" give 2 world 1 tap a wait 500 shot dia_viento \
    at "2026-10-23 23:30" give 6 world 1 tap a wait 3500 shot dia_estrella \
    sheet momentos 3 > /dev/null

# 4) Mensajes del Jardín: objeto sacado, "descansa" y la lupa.
run --date "2026-10-22 12:00" --seed 3 pet 3 give 5 world 1 tap a shot msg_sacaste \
    give 3 tap a wait 3500 tap a shot msg_descansa wait 3500 hold a wait 300 shot msg_lupa \
    sheet mensajes 3 > /dev/null

# 5) La mascota de cada tipo: ficha (dos páginas) y respirar juntos.
for p in 1 2 3 4; do
  run --date "2026-10-22 13:00" --seed 5 pet $p wait 3000 shot mascota${p}_ficha_a wait 3600 shot mascota${p}_ficha_b \
      tap a wait 300 shotheld mascota${p}_respirar 2800 sheet mascota${p} 3 > /dev/null
done

# 6) Primer uso: la pantalla de elección y la bolsa con la colección.
run --date "2026-10-22 13:00" --seed 5 world 5 wait 3000 shot elegir_1 tap a wait 300 shot elegir_2 \
    tap a wait 300 shot elegir_3 tap a wait 300 shot elegir_4 sheet elegir 4 > /dev/null
echo "capturas en $OUT/"
