#!/bin/sh
# Capturas de la Pesca y de la colección de peces (fase 5) con la herramienta headless.
# Uso: tools/shots_pesca.sh RUTA/llv_headless DIRECTORIO_DE_SALIDA
set -e
H=${1:-build/make/llv_headless}
OUT=${2:-capturas}
mkdir -p "$OUT/pesca"
P="$OUT/pesca"
run() { "$H" --out "$P" --scale 3 "$@"; }

# 1) El estanque a lo largo del día (otoño): noche, amanecer, mañana, mediodía, tarde, ocaso y noche cerrada.
run --date "2026-10-22 03:30" --seed 3 world 4 shot estanque_0330 \
    at "2026-10-22 06:50" world 4 shot estanque_0650 at "2026-10-22 09:30" world 4 shot estanque_0930 \
    at "2026-10-22 13:00" world 4 shot estanque_1300 at "2026-10-22 17:30" world 4 shot estanque_1730 \
    at "2026-10-22 19:20" world 4 shot estanque_1920 at "2026-10-22 21:30" world 4 shot estanque_2130 \
    sheet estanque_dia 4 > /dev/null

# 2) Clima y estación: lluvia, viento, nieve (invierno), verano y primavera.
run --date "2026-10-22 13:00" --seed 3 world 4 weather 1 shot estanque_lluvia weather 2 shot estanque_viento \
    weather 0 > /dev/null
run --date "2027-01-15 13:00" --seed 3 world 4 weather 3 shot estanque_nieve > /dev/null
run --date "2026-07-15 13:00" --seed 3 world 4 shot estanque_verano > /dev/null
run --date "2027-04-10 13:00" --seed 3 world 4 shot estanque_primavera > /dev/null

# 3) Una pesca de principio a fin: lanzar, esperar, que pique, la pelea y la tarjeta (ganada y perdida).
run --date "2026-10-22 15:00" --seed 3 world 4 shot pesca_reposo give 5 tap a wait 250 shot pesca_lanza \
    wait 900 shot pesca_espera wait 800 shot pesca_espera2 sheet pesca_lanzar 4 > /dev/null
run --date "2026-10-22 15:00" --seed 3 give 5 world 4 fishshot perfect pesca_ganar \
    give 5 fishshot none pesca_perder sheet pesca_ganar_perder 4 > /dev/null

# 4) Mensajes con gracia: sin lombrices, y el estanque que descansa tras dos lanzamientos.
run --date "2026-10-22 15:00" --seed 3 world 4 tap a wait 300 shot pesca_sin_lombrices > /dev/null
run --date "2026-10-22 15:00" --seed 3 give 5 world 4 fish perfect give 5 fish perfect give 5 tap a wait 300 shot pesca_descansa > /dev/null

# 5) Cada pez en su tarjeta y las basuras simpáticas (dos lanzamientos al día: se pasa a otro día cada dos).
run --date "2026-10-24 22:30" --seed 3 world 4 \
    give 5 forcefish 0 fishshot perfect pez_0 give 5 forcefish 1 fishshot perfect pez_1 adv 1d \
    give 5 forcefish 2 fishshot perfect pez_2 give 5 forcefish 3 fishshot perfect pez_3 adv 1d \
    give 5 forcefish 4 fishshot perfect pez_4 give 5 forcefish 5 fishshot perfect pez_5 adv 1d \
    give 5 forcefish 6 fishshot perfect basura_6 give 5 forcefish 7 fishshot perfect basura_7 adv 1d \
    give 5 forcefish 8 fishshot perfect basura_8 give 5 forcefish 9 fishshot perfect basura_9 \
    sheet pez_tarjetas 4 > /dev/null

# 6) El legendario en luna llena: la noche, la pelea con destellos y la tarjeta dorada.
run --date "2026-10-25 23:30" --seed 3 give 5 world 4 shot luna_llena_estanque forcefish 5 fishshot perfect legendario > /dev/null

# 7) La colección en el Cobertizo: vacía, con los peces y completa; las fichas de cada uno.
run --date "2026-10-22 15:00" --seed 3 pet 1 world 5 wait 3600 shot coleccion_vacia > /dev/null
run --date "2026-10-22 15:00" --seed 3 pet 1 catch 0 22 catch 1 41 catch 4 18 world 5 wait 3600 \
    tap a tap a shot coleccion_parcial tap a shot ficha_pez_a hold a wait 200 shot ficha_pez_b > /dev/null
run --date "2026-10-22 15:00" --seed 3 pet 1 catch 0 22 catch 1 41 catch 2 33 catch 3 55 catch 4 18 catch 5 120 \
    world 5 wait 3600 tap a tap a shot coleccion_completa \
    tap a shot ficha_0 tap a shot ficha_1 tap a shot ficha_2 tap a shot ficha_3 tap a shot ficha_4 \
    tap a shot ficha_5 hold a wait 200 shot ficha_5_pagina2 sheet fichas_peces 3 > /dev/null
echo "capturas de la pesca en $P/"
