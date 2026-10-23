# Llavero de Mundos

Un llavero habitado: seis mundos pequeños (Reloj, Jardín, Sala, Música, Pesca y
Cobertizo) que comparten un mismo universo, con gráficos estilo Game Boy Color,
dos botones y un acelerómetro. Todo es offline y determinista. La especificación
completa está en [`llavero-mundos-spec.md`](llavero-mundos-spec.md).

Este repositorio contiene el motor en C11 portable (`src/core`), la capa HAL
(`src/hal`), el simulador SDL2 (`src/platform/sdl`), una herramienta headless
para generar capturas y simular semanas de uso (`src/platform/headless`), el
banco de frases (`data/frases`) con su conversor (`tools/phrases2bin.py`) y los
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
make shots     # capturas del Reloj, del Jardín y de la Sala (con su semana simulada) en capturas/
make phrases   # regenera src/core/phrases_data.c a partir de data/frases/*.txt
```

`make shots` llama a `make shots-reloj`, `make shots-jardin` (`tools/shots_jardin.sh`: una
semana de uso normal, las cuatro estaciones, un día de clima variado, los mensajes, la
elección de mascota y la ficha de las cuatro mascotas) y `make shots-sala`
(`tools/shots_sala.sh`: la Sala con cada personaje, las preguntas, las fechas especiales,
todas las sorpresas, el ❗ del HUD, los ajustes de hábitos y la semana de la fase 3 con su
volcado de frases). `make test` también comprueba que el binario de frases está al día.

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
| S | Agitar (despierta; arregla el cartucho sucio; en la Sala, alguien protesta) |
| W | Camina 1.000 pasos (el podómetro del IMU simulado) |
| 4 | Alterna batería baja / normal (simulada) |
| 1 / 2 / 3 | Máquina del tiempo: +1 hora / +1 día / +1 semana |
| F12 / P | Captura PNG en el directorio actual |
| L | Rejilla LCD |
| Esc | Salir (guarda el estado) |

El llavero se duerme a los 20 s sin tocarlo: pantalla negra y guardado; se
despierta con A, B o S. Si mientras dormía pasó algo raro, un **punto naranja** en la esquina
de la ventana parpadea (el LED de aviso, spec §3.2). Opciones: `--save F`, `--scale N`, `--seed N`, `--lcd`,
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

## La Sala (fase 3)

### 👵🤖 Controles

| Gesto | Qué hace |
|---|---|
| **A** (toque) | **Dar pie**: un personaje dice algo (o la siguiente línea de una escena a dos voces). Si estaban escribiendo, completa la frase. Con una pregunta en pantalla, es **sí** |
| **A** (mantener) | **Tocar el timbre**: entra otro visitante (Gótica, Gamer o Boomer). Con una pregunta en pantalla, es **no** |
| **Agitar** | Alguien protesta ("¿Puedes dejar de agitarme? Me mareo.") |
| **B B A A B A B A** | **Código secreto**: el Gato de Schrödinger (ver abajo) |

Máximo **2 conversaciones nuevas al día** (se reponen a las 05:00; se ven en el HUD como en
el Jardín). Seguir una escena o contestar una pregunta no gasta ninguna. Con el límite
gastado, A da una **frase de relleno** con gracia ("Ya te conté todo por hoy. Mañana más.",
"Mis reservas de pensamiento se han agotado. Como el universo, eventualmente.") y el
timbre suena a hueco: nunca se bloquea.

El texto sale **letra a letra** con un pitido por letra de **distinto tono por personaje**
(por `FrameOut.tones`: la Abuela grave y cálida, Unidad-7 metálico, la Gótica oscura, el
Gamer agudo...). Las caras cambian de expresión (hablando, contento, pícaro, sorprendido,
dormido) y la Sala se viste en **Halloween**, **viernes 13**, **Año Nuevo** y **Navidad**
(cada fecha trae a su visitante a media tarde). De madrugada (00:00–05:00) la Abuela y
Unidad-7 salen en pijama y con gorro y te mandan a la cama.

**Preguntas de sí o no** (toque = sí, mantener = no): se guardan en la memoria (2 bits por
pregunta, 14 preguntas) y los personajes se acuerdan: "¿Has regado tus plantas esta semana?"
→ no → tres días después "¿Y esas plantas que no regaste? Dime que siguen vivas. O que son
compost: ambas son dignas." Una pregunta de las de siempre no se repite antes de 20 días.

**Comentan el universo**: el diario marca los sucesos nuevos (manzana que cae o se pudre,
brote, hongo, rana, lluvia, arcoíris, bolsa llena, visitas, hábitos cumplidos...) y la
Abuela y Unidad-7 los comentan una vez ("{evento}. Para el compost, como todo en la vida.").

### 🎲 El Director de Sorpresas (spec §6 y §7)

En cada mirada (mínimo 10 minutos desde la anterior: mirar muchas veces seguidas no da nada
nuevo y los personajes lo notan, "Me miraste hace 2 minutos…") tira un dado con `gs->rng`:
**80 % normal, 15 % comentario del universo, 4 % sorpresa, 1 % algo muy raro**. Nunca dos
sorpresas seguidas; **seguro de pena**: si pasa un día entero sin sorpresa, la siguiente mirada
trae una. También **elige el mundo que se abre al despertar** (el que tiene novedades) y
marca con un **❗ rojo** del HUD los demás mundos con novedades (bajo la hora, un marcador por mundo).

| Sorpresa | Qué pasa |
|---|---|
| Cartucho sucio | Imagen rota y "CARTUCHO SUCIO. SOPLA.": los botones no hacen nada hasta que **agitas** (o a los 9 s) |
| Arranque falso | Una consola noventera arranca 2 s con su logo |
| Mundo ausente | La Sala, el Jardín o el Cobertizo vacíos con el cartel "Salí. Vuelvo en 5 min." |
| Invasión | Un visitante en el Jardín o el Cobertizo, con su bocadillo |
| Cuarta pared | "Me has mirado 15 veces hoy. Esto ya es obsesión." / "Son las 3:47. ¿Todo bien?" |
| Calendario | El visitante de la fecha (Halloween: la Gótica) habla en la Sala |
| Rebeldía | El primer B no cambia de mundo: "No. Me gusta aquí." |
| Colapso cuántico | Varias versiones del mundo superpuestas antes de quedarse con una |
| INSERT COIN (raro) | Pantalla de arcade: pulsa A y la moneda da un Deseo |
| Gato de Schrödinger (raro) | Asoma de su caja (≈ 1 de cada 300 miradas) |

**Código secreto `B B A A B A B A`** (toques de menos de 1,5 s entre sí, desde cualquier
mundo): el motor lo detecta sin tocar la gramática de gestos. Mientras tecleas, las B cambian
de mundo y las A hacen lo suyo; si el código se completa, el motor **deshace** lo que hicieron
las A (guarda el estado antes de la primera A del código): no se gasta ni un objeto ni una
conversación. Empieza en la Sala con el Gato diciendo algo ("Estoy aquí. O no. Depende de
quién mire.").

### ⏳ Bono de paciencia y LED de aviso (spec §3.2)

Mientras el llavero **duerme**, cada macro tick puede traer un suceso raro extra (un Deseo,
un arcoíris o una estrella fugaz, una flor rara, una rana o un hongo) con un riesgo que
**crece con la espera** hasta el tope de 12 h (`LLV_PATIENCE_*`): de media, 1 % tras 1 h,
12 % tras 4 h, 29 % tras 8 h y 36 % tras 12 h o más (esperar más no suma). Despierto, o mirando a
cada rato, no hay bono. Cuando pasa algo raro mientras duerme, `FrameOut.led` parpadea
(150 ms cada 5 s) y la HAL lo enciende (`hal_led_set`; en el simulador, un punto naranja en
la ventana); al mirar se apaga.

### 🌱 Buenos hábitos (spec §3.3)

Se preguntan con humor, **máximo 2 al día**, nunca el mismo dos veces el mismo día (llamar,
una vez por semana), **cumplirlos ayuda al universo y no cumplirlos no castiga nada**
(solo cambia la reacción), y **se puede mentir**: a veces ("Detecto una mentira. No tengo
sensores para eso, pero la detecto.") y, si dices que dormiste bien habiendo mirado el
llavero de madrugada, siempre.

| Hábito | Cómo lo sabe | Qué pasa si lo cumples |
|---|---|---|
| Beber agua | Pregunta (8–22 h) | **Llueve un poco** en el Jardín (2 h; nieva en invierno), sin gastar objetos |
| Dormir bien | Mirada de madrugada + pregunta por la mañana (5–12 h) | Racha de noches: con 3 seguidas, **amanecer especial en el Reloj** (06:00–08:30) |
| Respirar | Respirar con la mascota (3 respiraciones) o pregunta | La **mascota se alegra** (una vez al día) |
| Soltar el móvil | Pregunta (18–24 h) | Baja un pájaro al Jardín |
| Salir al sol | Pregunta (10–17 h) | Florecen dos capullos |
| Llamar a alguien | Pregunta semanal | La Abuela te manda una Semilla |
| Moverse | **Podómetro** del IMU (`HAL: steps`, simulado con W) | Un **paseo largo** (6.000 pasos) suma **un objeto extra** (máx. 1 al día) y alimenta la compostera |

**Activarlos y desactivarlos:** en el **Cobertizo**, lleva el cursor a la **colección**
(toca A hasta que se marque) y **mantén A**: se abre **AJUSTES**. Allí A (toque) baja de fila
y A (mantener) cambia la casilla; la última fila es "Salir". Hay una casilla por hábito y la
del **modo apto para la abuela** (solo humor blanco). Un hábito desactivado ni se pregunta ni
se menciona. Por defecto están todos activos.

### 📝 Frases: formato y cómo añadir más

Un `.txt` por personaje en `data/frases/` (`abuela`, `robot`, `gotica`, `gamer`, `boomer`,
`gato`, `sistema` y `escenas`), una frase por línea con etiquetas de contexto (spec §5.2):

```
[noche, abuela]                    A estas horas solo trabajan las lechuzas y los hongos.
[ausencia>3d, robot]               {ausencia} sin verme. He reflexionado sobre la soledad. Conclusión: 404.
[evento:manzana_podrida, abuela]   {evento}. Para el compost, como todo en la vida.
[luna_llena, gotica]               Esta noche no me esperes despierta.
[halloween, abuela, picaro]        He tallado una calabaza. Se parece a mi cuñado, pero sin la mala leche.
[pregunta:plantas, abuela]         ¿Has regado tus plantas esta semana?
[no:plantas, abuela]               Ay, criatura. Aquí no hay reproches, solo una regadera esperando.
[retoma:plantas, resp:plantas=no, abuela]   ¿Y esas plantas que no regaste?...
[escena:existir:1, robot]          Si nadie me mira, ¿existo?
[escena:existir:2, abuela]         Si nadie te mira, riegas los tomates. Vamos.
```

Cada frase lleva **un personaje**, como mucho **un propósito** (`relleno`, `sinacciones`,
`cuarta_pared`, `rebeldia`, `timbre`, `invasion`, `pregunta:Q`, `si:Q`, `no:Q`, `mentira:Q`,
`retoma:Q`, `escena:ID:N`, `glitch`, `cartel`...; sin propósito es charla, y con `evento:` es
comentario del diario) y las **condiciones** que quieras: franja (`madrugada`, `manana`,
`mediodia`, `tarde`, `noche`), día (`lunes`...`domingo`, `finde`), estación, `luna_llena`,
`luna_nueva`, fechas (`halloween`, `viernes13`, `anio_nuevo`, `navidad`; `cumple` queda para
la fase 7), relación (`ausencia>3d`, `ausencia<10m`, `miradas>8`, `racha>3d`, `afin_alta`,
`afin_baja`, `resp:Q=si|no`), cuerpo (`agitar`, `bateria_baja`, `cargando`), universo
(`evento:X`, `lluvia`, `jardin_lleno`, `mascota:hongo`, `coleccion>5`, `bolsa_llena`...) y
`habito:X` (la frase se calla si desactivas ese hábito). Se cumplen todas a la vez.
`picaro` marca el humor irónico (el modo apto-abuela lo omite). Huecos: `{ausencia}`,
`{evento}`, `{hora}`, `{miradas}`, `{racha}`, `{estacion}`, `{dia}`, `{mascota}`, `{pasos}`.
El vocabulario completo (con los ids) está en `src/core/phrases_vocab.h`.

Para añadir frases: edita los `.txt` y ejecuta `make phrases` (o
`python3 tools/phrases2bin.py`). El script valida todo (etiquetas, huecos que se puedan
rellenar, cada pregunta con su sí y su no, una versión blanca de cada grupo, escenas completas)
y escribe `src/core/phrases_data.c`, un array `const` que el firmware lee directamente de la
flash (XIP). `make check-phrases` o `ctest` comprueban que el generado está al día; los tests
del motor validan además que todo se dibuja con la fuente y cabe en la caja (6 líneas de 18
letras; las preguntas, 5).

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

Para la fase 3 (`--help` los lista todos):

```sh
# una semana de uso: 4-7 miradas al día, charlas, preguntas, sorpresas; estadísticas y
# volcado de todas las frases que salieron
build/llv_headless --date "2026-10-22 08:00" --seed 7 pet 1 week 7 salastats dumpfrases frases.txt
# escenas de prueba
build/llv_headless --date "2026-10-31 18:00" world 2 visitor gotica say gotica shot halloween
build/llv_headless world 0 code shot gato          # el código secreto
build/llv_headless world 2 surprise glitch shot sucio
```

`talk`, `yes`/`no`, `bell`, `drain`, `ask PREGUNTA`, `say PERSONAJE`, `visitor`, `surprise`,
`grandma`, `habit`, `steps`, `battery`, `streak`, `news`, `led`, `pool` y `sala` completan la herramienta.

## Estado por fases (spec §14)

- [x] **Fase 0:** motor, HAL, simulador SDL, controles, estado con límites fijos,
  look GBC, Reloj y tests de determinismo.
- [ ] Fase 1: hardware (RP2350, botones, altavoz, IMU, sueño).
- [x] **Fase 2:** Jardín + bolsa + Cobertizo + mascota (ver abajo el gate de la semana
  simulada).
- [x] **Fase 3:** La Sala (personajes, preguntas, memoria, diario), Director, hábitos y bono
  de paciencia (ver el gate de la fase 3).
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

## Gate de la fase 3: una semana en la Sala

*"Te hace reír al menos una vez al día, y alguna vez te hace beber agua."* La risa no se puede
medir offline; lo que sí se mide es si hay material para intentarlo y cómo se reparte.
`make shots-sala` simula 7 días de una persona con 4-7 miradas al día (algunas de madrugada,
un día casi sin mirar), que charla, contesta preguntas, agita el llavero y pulsa lo que le
pide cada sorpresa, y vuelca todo en `capturas/semana_sala_frases.txt` y
`capturas/semana_sala_stats.txt`. Sobre 8 semillas distintas (1-8):

- **Frases distintas vistas en 7 días: 45-57** (de 47-63 mostradas). Se repiten 2-13 (3-20 %),
  casi siempre frases de relleno o la pregunta de siempre; **dentro de las últimas 30 solo
  se repite alguna vez y solo cuando el banco no da para más** (una pregunta de hábito con
  pocos personajes presentes, o el texto fijo de una sorpresa).
- **Sorpresas: 3-6 por semana** (0,4-0,9 al día): el seguro de pena da una al menos cada 2
  días de uso. **Preguntas: ~1 al día** (0-2), nunca más de 2 de hábitos.
- **Mundo que abre el Director:** Sala 45-65 %, Jardín 20-40 %, Cobertizo 3-18 %, Reloj nunca
  (abre donde hay novedades; la Sala las tiene con cada visitante, comentario o sorpresa).
- **Picardía:** 11-22 % de lo que sale (el banco: 22 %).

**Honestamente:** una semana está bien servida (en cualquier momento pueden salir ~40 charlas
de cada residente y ~13-20 de cada visitante, y solo caben 2 conversaciones al día), pero el
banco tiene 709 frases y el objetivo final es 150-300 por residente: ahora la Abuela tiene
~120 de charla y ~250 en total y Unidad-7, ~115 y ~240, pero los visitantes solo 20-26 de charla
cada uno y el Gato 24. Con 2 conversaciones al día, en 3 meses se agotaría casi toda la charla
una vez, y la antirrepetición de 30 hashes (unas dos semanas) deja que vuelvan. Lo que más
falta: **más relleno** (ahora 17 por residente y 3-5 por visitante: es lo que más se repite si insistes), **más
variantes de las preguntas de hábitos** (cada una vuelve cada pocos días con 3 maneras de
decirla), **un comentario más por suceso y personaje** (cada suceso tiene una sola frase
por personaje), más escenas (12) y retomas (solo 4 preguntas se retoman), más líneas de
calendario y de estaciones, y 30-40 frases más para cada visitante. Lo del agua depende de ti:
la pregunta, la lluvia y la tos de la Abuela están, pero que beba agua de verdad no lo puede
comprobar un simulador.

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

### Decisiones de la fase 3

- **Frases:** un binario sin comprimir (709 frases, ~53 KB: cabe de sobra en los ~175 KB
  presupuestados) con tabla de desplazamientos, para acceso directo desde la flash. Ver
  `phrases.h` para el formato. El vocabulario (personajes, propósitos, condiciones, sucesos,
  preguntas, hábitos) vive en `phrases_vocab.h` y lo leen el motor y el conversor, así que
  añadir una pregunta o un suceso es una fila en un solo sitio. `phrases_data.c` se versiona
  ya generado (compilar no exige Python); `make test` y `ctest` verifican que está al día.
- **Selección:** gana más peso la frase con más condiciones (así las de Halloween o de la
  racha se notan), y el visitante que está en la sala habla más que los residentes. Una
  frase se descarta si su hash está entre los últimos 30; si no queda ninguna, sale la que más
  tiempo lleva sin salir.
- **La conversación vive en `GameState`** (frase, quién habla, huecos, escena, pregunta) y no
  en variables de los mundos: así se guarda, y las tiradas con `gs->rng` salen idénticas con
  los mismos gestos. Solo la máquina de escribir (cuánto va escrito) es visual. Un gesto
  durante la escritura solo termina la frase; por eso el momento de empezar a escribir se fija
  al gesto y no al fotograma.
- **Director:** la tirada solo ocurre si han pasado 10 minutos desde la mirada anterior (y no en
  la primera de todas); "nunca dos seguidas" cuenta entre tiradas. El mundo que se abre sigue
  este orden: el de la sorpresa; el Jardín si hubo algo raro; la Sala; el Cobertizo; el Jardín.
  Se queda donde estaba si no hay novedades. El seguro de pena usa días del llavero (05:00).
- **Novedades (❗):** Jardín (cae o se pudre una manzana, brote, árbol, hongo, flor rara, semilla
  de pájaro, rana, arcoíris, estrella, abono), Cobertizo (la mascota crece, nace, habla, suelta
  esporas o come) y Sala (llega un visitante). Se apagan al entrar al mundo.
- **Bono de paciencia:** el riesgo por tick crece linealmente con el tiempo dormido hasta
  12 h (1,2 % por tick al llegar al tope) y se corta ahí. Para que el catch-up siga siendo
  función del estado, el chip "se despierta" mientras duerme solo para avanzar el mundo
  (`engine_frame` dormido llama a `engine_advance`); en el RP2350 será el temporizador.
- **LED:** lo raro son arcoíris, estrella fugaz, rana, flor rara, nueva generación de la
  mascota y el propio bono. Parpadea 150 ms cada 5 s mientras haya algo sin ver.
- **HAL:** `RawInput` no cambia (los tests de las fases 0 y 2 siguen valiendo); el cuerpo
  (batería, cargando, pasos) entra con `engine_set_body()` y la HAL lo lee con
  `hal_body_read()`. El simulador inventa batería 87 % (tecla 4: 12 %), sin carga y pasos con
  la tecla W. El LED sale por `hal_led_set()`.
- **Hábitos:** todos activados por defecto; franjas, esperas y efectos en `habits.c`. "Respirar
  juntos" en el Cobertizo cambia lo que decía la fase 2 ("solo visual"): ahora, con 3
  respiraciones completas, la mascota se alegra, pero **una vez al día**, para que no se pueda
  "farmear" humor. Dormir mal o no responder solo reinicia un contador. El podómetro: si la
  bolsa está llena, el objeto del paseo espera a que haya sitio.
- **Estado:** `SAVE_VERSION` 3; `GameState` ocupa 1.584 bytes en memoria y el guardado 1.150
  (muy por debajo de los 4 KB de §10; `LLV_SAVE_MAX` sigue en 2.048). Los guardados de las
  fases 0 y 2 se rechazan y se crea un universo nuevo.
- **Arte:** los personajes son tres capas de sprites de 3 colores + transparente (cabeza, torso,
  piernas o falda) con dos frames y una cara dibujada encima según la expresión. Se añadieron
  colores al final de la paleta maestra (piel, pelo canoso, metal, crema, lámpara, calabaza...) y
  la `ö` a la fuente (para Schrödinger).
- **Bug encontrado por el test de determinismo:** `habit_steps` (que corre en cada fotograma)
  gastaba un dado con la bolsa llena; el azar dependía de cuántos fotogramas hubiera habido.
  Ahora comprueba antes si cabe y no consume azar si no.
