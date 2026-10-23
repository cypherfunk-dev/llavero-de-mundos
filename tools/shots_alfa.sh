#!/bin/sh
# Capturas de los 6 mundos en varios momentos del día (fase 5, versión alfa).
# Uso: tools/shots_alfa.sh RUTA/llv_headless DIRECTORIO_DE_SALIDA
set -e
H=${1:-build/make/llv_headless}
OUT=${2:-capturas}
mkdir -p "$OUT/alfa"
A="$OUT/alfa"
run() { "$H" --out "$A" --scale 3 "$@"; }

# Un universo con vida: mascota, peces en la colección y unos días de jardín.
common="--seed 7"
for t in "0700:2026-10-26 07:00" "1230:2026-10-26 12:30" "1920:2026-10-26 19:20" "2330:2026-10-26 23:30"; do
  tag=${t%%:*}
  date=${t#*:}
  run --date "$date" $common pet 4 wait 3400 catch 0 22 catch 1 41 catch 4 18 \
      world 0 shot ${tag}_0_reloj \
      world 1 shot ${tag}_1_jardin \
      world 2 say abuela shot ${tag}_2_sala \
      world 3 wait 2500 shot ${tag}_3_musica \
      world 4 shot ${tag}_4_pesca \
      world 5 wait 3600 tap a tap a shot ${tag}_5_cobertizo \
      sheet alfa_$tag 3 > /dev/null
done
echo "capturas de la alfa en $A/"
