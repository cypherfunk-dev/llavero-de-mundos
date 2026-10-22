# Llavero de Mundos

Un llavero habitado: seis mundos pequeños (Reloj, Jardín, Sala, Música, Pesca y
Cobertizo) que comparten un mismo universo, con gráficos estilo Game Boy Color,
dos botones y un acelerómetro. Todo es offline y determinista. La especificación
completa está en [`llavero-mundos-spec.md`](llavero-mundos-spec.md).

Este repositorio contiene el motor en C11 portable (`src/core`), la capa HAL
(`src/hal`), el simulador SDL2 (`src/platform/sdl`), una herramienta headless
para generar capturas y simular semanas de uso (`src/platform/headless`) y los
tests (`tests/`).

## Compilar

Con CMake:

```sh
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

O con make (salida en `build/make/`):

```sh
make test      # compila y pasa los tests
make sim       # simulador SDL2 (necesita SDL2: pkg-config sdl2)
make headless  # herramienta sin SDL
make shots     # capturas del Reloj y de la semana simulada del Jardín en capturas/
```

`make shots` llama a `make shots-reloj` y `make shots-jardin` (este último ejecuta
`tools/shots_jardin.sh`: una semana de uso normal, las cuatro estaciones, un día de
clima variado, los mensajes, la elección de mascota y la ficha de las cuatro mascotas).

## Simulador

```sh
build/llv_sim                 # guarda en llavero.sav
build/llv_sim otra.sav --scale 6 --seed 42
```

| Tecla | Acción |
|---|---|
| Z / J | Botón A (toque: acción; mantener: acción secundaria) |
| X / K | Botón B (toque: siguiente mundo; mantener: anterior) |
| Z+X | A+B: diario "mientras no mirabas…" |
| S | Agitar (despierta) |
| 1 / 2 / 3 | Máquina del tiempo: +1 hora / +1 día / +1 semana |
| F12 / P | Captura PNG en el directorio actual |
| L | Rejilla LCD |
| Esc | Salir (guarda el estado) |

El llavero se duerme a los 20 s sin tocarlo: pantalla negra y guardado; se
despierta con A, B o S. Opciones: `--save F`, `--scale N`, `--seed N`, `--lcd`,
`--mute`, `--frames N` (sale solo) y `--shot F` (captura al salir). Para probarlo
sin pantalla: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/llv_sim --frames 60`.

## Jardín y Cobertizo (fase 2)

Los controles son los de la spec (§2): **B** cambia de mundo y nunca modifica la escena;
**A** actúa dentro del mundo. El orden del ciclo es Reloj, **Jardín**, Sala, Música,
Pesca y **Cobertizo**.

### 🌳 Jardín

| Gesto | Qué hace |
|---|---|
| **A** (toque) | Usa un objeto de la bolsa: el que has preparado en el Cobertizo o, si no, uno al azar (`Sacaste: Viento`). **2 acciones al día** (se reponen a las 05:00). Sin acciones: *"El jardín descansa. Vuelve en 3 h 20 min."*; sin objetos: *"La bolsa está vacía. +1 en…"* |
| **A** (mantener) | **Lupa**: salta de entidad en entidad y dice qué es y cuándo cambia (*"Manzana · se pudre en ~5 h"*). No cuesta nada y apunta en la colección lo que ves |

Objetos: **Nube** (lluvia 6 h: todo crece x4, charcos), **Viento** (cae la fruta, vuelan
semillas), **Semilla** (planta una semilla, una flor o un brote), **Alpiste** (baja un
pájaro que come y deja semillas), **Lombriz** (abono: las plantas esperan la mitad) y
**Deseo** (arcoíris de día, estrella fugaz de noche, y florece una flor rara).

El jardín vive solo, también con el llavero dormido: estaciones según la fecha real,
lluvia, viento, nieve, animales que pasan, frutas que caen, se pudren y germinan...
Las reglas son una tabla de datos (`src/core/garden_data.c`).

### 🏚 Cobertizo

| Gesto | Qué hace |
|---|---|
| **A** (toque) | Mueve el cursor: objetos de la bolsa, colección, mascota. Sobre la mascota empieza **respirar juntos** |
| **A** (mantener) sobre un objeto | **Prepara** ese objeto para la próxima acción del Jardín (otra vez para quitarlo) |
| **A** (mantener) sobre la mascota | La **cuida** (1 al día): le ofreces el objeto preparado o la saludas |
| Respirar juntos | Mantener **A** inhala (la mascota se infla), soltar exhala; un toque sale. Sin límite |

**Elegir mascota (primer uso).** Hasta que eliges, el Cobertizo muestra la pantalla de
elección: un **toque de A** pasa al siguiente (Hongo, Semilla, Piedra, Compostera) y
**mantener A** confirma. Se hace aquí y no en una pantalla aparte porque B ya cambia de
mundo.

La **ficha solemne** de la mascota (con el cursor sobre ella) alterna cada pocos
segundos entre nombre, generación, día y altura, y humor y último pensamiento. El ciclo
dura 100 días reales (`LLV_PET_CYCLE_DAYS`), al acabar nace la generación siguiente y
nunca muere. El hongo suelta esporas y nacen hongos en el Jardín; la semilla da una
Semilla a la bolsa a veces; la compostera se come las manzanas podridas y devuelve una
lombriz; la piedra jamás dice nada.

## Herramienta headless

Reloj virtual, gestos y capturas sin dependencias:

```sh
build/llv_headless --date "2024-01-25 23:30" --out capturas \
    style 4 shot luna_llena adv 6h shot seis_horas_despues
```

`build/llv_headless --help` lista los comandos (`shot`, `sheet`, `adv`, `at`,
`style`, `world`, `tap`, `hold`, `ab`, `shake`, `sleep`, `info`...). Para la fase 2:

```sh
# una semana de uso normal (4 miradas al día: jardín, objeto preparado, mascota,
# respirar), con un resumen por día y las estadísticas al final
build/llv_headless --date "2026-10-22 08:00" --seed 7 pet 1 routine 7 stats
```

`pet N` elige mascota, `routine DIAS` simula el uso de una persona normal pasando por el
motor entero (gestos, sueño, catch-up), `stats` cuenta cosas por especie, eventos del
diario y objetos usados, `give N` (solo para escenas de prueba) deja un objeto preparado
y `shotheld NOMBRE MS` captura con A pulsado.

## Estado por fases (spec §14)

- [x] **Fase 0:** motor, HAL, simulador SDL, controles, estado con límites fijos,
  look GBC, Reloj y tests de determinismo.
- [ ] Fase 1: hardware (RP2350, botones, altavoz, IMU, sueño).
- [x] **Fase 2:** Jardín + bolsa + Cobertizo + mascota (ver abajo el gate de la semana
  simulada).
- [ ] Fase 3: La Sala, Director, hábitos.
- [ ] Fase 4: Música generativa.
- [ ] Fase 5: Pesca y colección.
- [ ] Fase 6: carcasa y ajuste de números.
- [ ] Fase 7: regalos.

## Gate de la fase 2: una semana simulada

*"Tras una semana en el simulador, el jardín sigue sorprendiendo."* Medido con
`build/llv_headless ... pet 1 routine 7 stats` sobre 30 semillas del universo, empezando el
22 de octubre (otoño), con 4 miradas al día y las 2 acciones diarias gastadas:

- El jardín tiene de media **15 cosas** (máximo visto 24 de 32): ni vacío ni saturado.
- Pasan **9 sucesos no rutinarios al día** (cae una manzana, llega un pájaro, brota algo,
  llueve...); solo el 4 % de los días tiene 2 o menos.
- Lo raro llega con cuentagotas por semana: hongo 1,8 · rana 1,8 · arcoíris 0,6 · estrella
  fugaz 0,5 · flor rara 0,4 (sin contar los Deseos que gastes).
- La colección del Cobertizo (10 huecos posibles; los 6 de peces esperan a la fase 5) sube
  a 2 el día 1, 6 el día 3 y 8 el día 7: lo común se descubre rápido y lo raro tarda más.
- En 400 días de uso normal, el jardín se queda entre 10 y 27 cosas (media 17); en 1.200
  días con 6 usos diarios (el triple de lo permitido) llega a 32 y no pasa de ahí (tests).

**Honestamente:** la primera semana funciona. El riesgo está más allá: el jardín converge a
una "madurez" (unos 5 árboles, flores, piedras) donde casi todo son ciclos que ya se han
visto, y a partir de la tercera o cuarta semana ya habrás completado la colección salvo lo
rarísimo. Lo que mantiene el interés a largo plazo serán las estaciones (cada 3 meses), la
renovación de árboles viejos (~200 días), los peces de la fase 5 y los comentarios de la
Sala (fase 3). Los números 🧪 de `config.h` y `garden_data.c` están para seguir ajustándose.

## Decisiones de implementación

Cosas que la spec deja abiertas y se resolvieron por lo más sencillo:

- **Rejilla del Jardín:** 7x6 casillas de 16 px (112x96) bajo el HUD, una entidad por
  casilla, con una valla arriba y una barra de estado abajo (clima y estación).
- **Escala de tiempo:** semilla (14 h) → brote (36 h) → joven (48 h) → árbol: ~4 días.
  Fruta a los 2,5 días, cuelga hasta 4 más si no hay viento. Manzana fresca 2 días →
  podrida 1 día → brote 40 % / hongo 30 % / nada 30 % (con ±25 % de variación de cada
  cuenta atrás). La lluvia multiplica por 4 (`LLV_WET_MULT`); en invierno las plantas
  avanzan a un cuarto de velocidad.
- **Topes:** además de las 32 entidades, cada especie tiene su tope y los árboles (brote,
  joven, adulto) comparten 5. Los objetos pueden pasarse del tope de lo que se acaba solo
  (flores, lombrices...), nunca del de los árboles. Los árboles viejos se vuelven tocón
  (hongo, brote o nada) y el jardín se renueva.
- **Objetos que dan objetos:** el pájaro que se come una manzana (o come alpiste) deja una
  Semilla en la bolsa; la lombriz que sale con la lluvia, una Lombriz. Si no caben, no
  caen. Estos y los cuidados de la mascota se anotan en el diario.
- **Colección:** 16 huecos (bitset): 10 del Jardín y 6 para peces (fase 5). Se rellenan
  cuando algo aparece por primera vez o al mirarlo con la lupa; el jardín inicial no cuenta
  hasta que lo miras. El gancho para pesca es `state_collect(gs, COLL_PEZ0 + n)`.
- **Mascota:** la elección, el cuidado y la respiración viven en el Cobertizo. Respirar es
  solo visual (la mascota se infla, sonríe y suelta corazones), no cambia el estado: así
  no hay forma de "farmear" humor y el estado sigue siendo función del tiempo y de las
  acciones. El humor baja despacio hasta "pensativo" como mínimo. Crece 0,1 px al día
  (el hongo del ejemplo de la spec mide 7 px el día 43).
- **Gustos de las mascotas** (`pet.c`, tabla `LIKE`): al hongo le encanta la Nube (la
  humedad) y la Lombriz le da igual; a la compostera le encanta la Lombriz; a la semilla la
  Nube y el Deseo (otra Semilla o el Alpiste la miran raro); a la piedra le gustan la Nube y el
  Viento. Ofrecer algo que no gusta nunca hace daño: solo anima menos.
- **Estado y guardado:** `SAVE_VERSION` 2: el `GameState` ocupa 1.320 bytes en memoria y
  el guardado 909 (muy por debajo de los 4 KB de §10; tope `LLV_SAVE_MAX` 2048, ajustable
  al compilar). Un guardado de
  la fase 0 se rechaza y el simulador arranca un universo nuevo. Los campos están en un
  único listado `SAVE_FIELDS`, que ahora incluye también los arrays.
- **Estado visual fuera del `GameState`:** el cursor del Cobertizo, la lupa y las
  animaciones son variables estáticas de cada mundo (`jardin_ui_reset()` y
  `cobertizo_ui_reset()` las reinician en los tests). Los mensajes largos salen abajo para
  no tapar la escena.
- **Un bug heredado de la fase 0, arreglado:** `ms_pending(ms, 0)` daba verdadero cuando el
  contador de milisegundos pasaba de 2^31 (~25 días encendido), y entonces aparecían el
  diario, el mensaje y la tarjeta del Reloj sin que nadie los pidiera. Ahora un temporizador
  a cero significa "nada pendiente".
