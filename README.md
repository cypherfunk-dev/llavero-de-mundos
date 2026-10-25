# Llavero de Mundos

Un llavero habitado: seis mundos pequeños (Reloj, Jardín, Sala, Música, Pesca y
Cobertizo) que comparten un mismo universo, con gráficos estilo Game Boy Color,
dos botones y un acelerómetro. Todo es offline y determinista. La especificación
completa está en [`llavero-mundos-spec.md`](llavero-mundos-spec.md).

Este repositorio contiene el motor en C11 portable (`src/core`), la capa HAL
(`src/hal`), el simulador SDL2 (`src/platform/sdl`), una herramienta headless
para generar capturas y simular semanas de uso (`src/platform/headless`), el
banco de frases (`data/frases`) con su conversor (`tools/phrases2bin.py`), la música
generativa (`src/core/music.c`, con su banco de pruebas `llv_music`) y su Estudio (`studio.c`), la pesca (`src/core/fish.c`,
`fish_data.c`, `fish_art.c` y `worlds/pesca.c`) y los tests (`tests/`).

> **Versión alfa.** Las fases 0 a 5 están hechas en cuanto a estructura: los seis mundos (Reloj, Jardín,
> Sala, Música, Pesca y Cobertizo) funcionan de principio a fin y hay una prueba de 14 días de uso por todos
> ellos (`make alpha`). Falta pulir contenido, arte propio y números 🧪, y el hardware real; ver
> [Estado de la alfa](#estado-de-la-alfa) y [Pendiente para la beta](#pendiente-para-la-beta).

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
make shots     # capturas del Reloj, del Jardín, de la Sala (con su semana simulada), de la Música y de la Pesca con su colección en capturas/
make music     # 200 semillas a WAV, informe de métricas, comparación y barrido de las 65.536 semillas, y el informe y los WAV del Estudio (capturas/musica/)
make alpha     # la alfa entera: tests + 14 días de uso por los 6 mundos (3 informes) + capturas de los 6 mundos y de la Pesca (capturas/alfa/, capturas/pesca/)
make phrases   # regenera src/core/phrases_data.c a partir de data/frases/*.txt
```

`make shots` llama a `make shots-reloj`, `make shots-jardin` (`tools/shots_jardin.sh`: una
semana de uso normal, las cuatro estaciones, un día de clima variado, los mensajes, la
elección de mascota y la ficha de las cuatro mascotas) y `make shots-sala`
(`tools/shots_sala.sh`: la Sala con cada personaje, las preguntas, las fechas especiales,
todas las sorpresas, el ❗ del HUD, los ajustes de hábitos y la semana de la fase 3 con su
volcado de frases). `make test` también comprueba que el binario de frases está al día y pasa los
tests de la música (`tests/test_music.c`, otro ejecutable: `llv_test_music`) y los del Estudio (`tests/test_studio.c`:
`llv_test_studio`); `ctest` ejecuta todos.
`make shots-musica` (`tools/shots_musica.sh`) son las capturas del mundo Música a distintas horas, con
lluvia, estrella fugaz y arcoíris, con varias semillas, mutando y con semilla nueva, más 30 s de audio
en WAV de tres momentos y, desde el Estudio, el panel en cada fila, con candados y valores largos, con voces silenciadas, las
escalas nuevas y el dado (`estudio_*.png`, `voces_*.png`, `escala_*.png`...) y dos WAV con ajustes (`audio_estudio_*.wav`).
`make shots-pesca` (`tools/shots_pesca.sh`) son las capturas de la Pesca y la colección: el estanque a todas
horas, con lluvia, viento, nieve y en las cuatro estaciones, una pesca de principio a fin (lanzar, esperar,
picar, pelear, tarjeta ganada y perdida), los mensajes con gracia, la tarjeta de cada pez y de cada basura,
el legendario con luna llena y las fichas de la colección. `make shots-alfa` (`tools/shots_alfa.sh`) son los
seis mundos a las 07:00, 12:30, 19:20 y 23:30 (una hoja de contacto por momento). `make test` ahora pasa tres
ejecutables (`llv_tests`, `llv_test_music` y `llv_test_pesca`) y los 14 días de la alfa; `ctest` ejecuta lo mismo
(con dos escenarios de la alfa: otoño y un invierno).

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
| + / - | Volumen del simulador (10 % por pulsación; empieza al 60 %) |
| M | Silencio (otra vez, vuelve el volumen) |
| Esc | Salir (guarda el estado) |

El llavero se duerme a los 20 s sin tocarlo: pantalla negra y guardado; se
despierta con A, B o S. Si mientras dormía pasó algo raro, un **punto naranja** en la esquina
de la ventana parpadea (el LED de aviso, spec §3.2). Opciones: `--save F`, `--scale N`, `--seed N`, `--lcd`,
`--mute`, `--frames N` (sale solo), `--shot F` (captura al salir) y `--world N` (abre ese mundo al
arrancar, p. ej. `--world 3` para la Música). Para probarlo sin pantalla ni altavoz:
`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/llv_sim --world 3 --frames 60`.

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

## La Música generativa (fase 4)

### 🎵 Controles

| Gesto | Qué hace |
|---|---|
| **A** (toque) | **Mutar**: cambia la melodía (motivo y variaciones) y conserva el estilo: misma escala, progresión, tempo y timbre. Aparece "+N" junto a la escala |
| **A** (mantener) | **Semilla nueva**: otra escala, otro tempo, otra progresión, otro timbre (`Semilla #4F2A`); **lo que hayas fijado en el Estudio no cambia** |
| **A A** (dos toques seguidos, < 0,3 s) | Abre el **Estudio** (v1.1), ver [más abajo](#-el-estudio-v11); la pista `A A: ESTUDIO` se ve bajo el osciloscopio |
| **B** | Cambia de mundo, como siempre. La música se desvanece (150 ms) y calla |
| `+` / `-` / `M` (simulador) | Volumen y silencio; se aplican a la música y a los pitidos |

**Límite: ninguno** (ni acciones diarias ni nada que gastar). La caja del mundo muestra la escala y
las mutaciones (`DÓRICA +3`) y el tempo y la semilla (`96 BPM · #4F2A`); debajo, el osciloscopio de las
últimas muestras, una barra por voz (con el destello de cada nota), los 16 pasos del compás, los 16
compases de la canción (cian A, magenta B) y el acorde que suena. El visualizador se calcula del mismo
generador que el audio, no del altavoz: se ve igual con `--mute` o sin dispositivo de audio.

### Cómo suena (y de qué depende)

- **Tres voces estilo NES**: dos ondas cuadradas con ciclo de trabajo de 12,5 / 25 / 50 % (la melodía y el
  bajo o arpegio) y un canal de ruido de 15 bits que hace de bombo (ruido grave que baja), caja y
  hi-hat (cerrado y abierto). Envolventes de ataque/decaimiento (órgano, pluck o suave), vibrato ligero
  en las notas largas y un filtro de un polo. Todo en enteros (fase acumulada de 32 bits, tabla de
  frecuencias de 16 bits), sin float ni malloc: `Music` ocupa 2,8 KB de RAM y ~14 KB de código en x86
  con `-Os`. Medido en el portátil: 60 s de audio en 7 ms (~5 ns por muestra, más de 8.000 veces el tiempo
  real); en un Cortex-M33 sin FPU, a 22.050 Hz, saldría por debajo del 5 % de la CPU, pero **es una estimación:
  no está medido en el chip**.
- **La semilla** es de 16 bits (`#4F2A`) y se guarda con las mutaciones (1 byte) y el ánimo (1 byte): **4 bytes**
  en `GameState.mus`, nada más. De (semilla, ánimo) sale el **estilo** y de (estilo, semilla, mutaciones) la
  **canción** de 16 compases, siempre igual (desde el Estudio, `GameState.mus` son 8 bytes y `SAVE_VERSION` 6: ver su sección).
  `SAVE_VERSION` era 4 en esta fase (los guardados de las fases 0, 2 y 3 se rechazaron y se creó un universo nuevo); `SAVE_FIELDS` sigue siendo la única fuente de verdad y el estado
  completo ocupa 1.154 bytes en el guardado (1.584 en memoria), muy por debajo de los 4 KB.
- **Escala y tempo según la hora y el clima del Jardín (§4.4)**: de día, pentatónica mayor; amanecer y
  atardecer, pentatónica o dórica; de noche, menor o dórica; **con lluvia, solo las dos menores** (pentatónica
  menor o menor natural) y un 15 % más despacio. Tempos: noche 66-88, amanecer 92-112, día 104-132,
  atardecer 84-104 negras por minuto. La hora (franjas: noche 22-5 h, amanecer 5-10, día 10-18, atardecer
  18-22), el clima, el cielo y la mascota forman el **ánimo** (1 byte). Es **sutil, no estridente**: una
  estrella fugaz da un temblor de vibrato, el arcoíris la anima un 4 % y la pasa a una cuadrada de 25 %
  (más brillante), la piedra la frena un 6 % y le deja la batería mínima, el hongo la vuelve algo nasal de día
  (cuadrada de 12,5 %), la semilla la acelera un 3 %. El ánimo se fija al entrar en el
  mundo; si empieza a llover mientras escuchas, el cambio entra al empezar la vuelta siguiente.
- **Progresiones (22)**: I-V-vi-IV, I-vi-IV-V, vi-IV-I-V, I-IV-V-IV, I-iii-IV-V, I-IV-vi-V, IV-I-V-vi, I-V-IV-I
  (mayores); i-VII-VI-VII, i-VI-III-VII, i-iv-VII-i, i-VI-VII-i, i-iv-v-i, i-III-VII-VI, VI-VII-i-i, i-VII-III-VII
  (menores); i-IV-i-VII, i-VII-IV-i, i-IV-VII-IV, i-III-VII-IV, i-v-IV-i, i-ii-IV-i (dóricas). La canción es
  **A A' B A''** (4 compases cada una, la B con otra progresión de la misma familia) y acaba con V (o ♭VII)
  y tónica.
- **Ritmos euclidianos** (Björklund con enteros): bombo E(2..5,16), hi-hat E(4..12,16), redoble de E(5,8) al final
  de las frases y las notas del bajo en E(3..5,16). Casos conocidos comprobados en los tests: E(3,8) `x..x..x.`,
  E(5,8) `x.xx.xx.`, E(7,16) `x..x.x.x..x.x.x.`, E(9,16) `x.xx.x.x.xx.x.x.`...
- **Melodía por cadenas de Markov** sobre los grados de la escala: una matriz de tendencias por escala (el 4.º baja
  al 3.º, el 7.º sube a la tónica...) por el tamaño del salto (sobre todo grados conjuntos y terceras; sextas,
  séptimas y octava no se saltan) y reglas que aproximan el orden 2 (tras un salto se vuelve en sentido contrario por
  grados conjuntos, nunca tres notas iguales seguidas). Rango de 17 semitonos desde la tónica -3 (cantable); en el
  primer y tercer tiempo solo suenan notas del acorde (así es consonante con el bajo); cada compás acaba cerca de
  donde empieza el siguiente. Los motivos se repiten (A' es A con las últimas notas re-tiradas y otra cadencia;
  A'' vuelve) y mutan poco a poco. **Mutar** vuelve a tirar la melodía (otro motivo y otros ritmos) con la misma
  semilla de estilo.
- **La música no es parte del tiempo del universo**: no avanza en `World.tick` ni en el catch-up, solo suena (por
  trozos de muestras) mientras estás en este mundo, y tiene su propio PCG32: no gasta ni un número de `gs->rng`. El
  test de 6 horas lo comprueba (el estado guardado coincide con el de un llavero que no escucha nada, salvo los
  4 bytes de la música y el mundo abierto).

### 🎛 El Estudio (v1.1)

*Petición del dueño tras probar la alfa: "que el generador de música aleatoria tuviese un poco más de control, no tanto como un IDE,
pero que el usuario pueda ser más creativo, que tenga un factor de más participación: cambiar la escala, el tipo de arpegio,
silenciar instrumentos, etc."* El modo rápido y aleatorio de arriba no cambia: el Estudio es una ventana opcional.

**Cómo se abre.** En el mundo Música, **dos toques seguidos de A** (con menos de 0,3 s entre soltar el primero y soltar el segundo,
`LLV_DOUBLE_TAP_MS`). Es barato de descubrir (A ya es "hacer algo" en este mundo, y la caja del visualizador lleva la pista discreta
`A A: ESTUDIO` bajo el osciloscopio) y no choca con A (toque: mutar) ni con A (mantener: semilla nueva), ni con A+B (el diario).
Abrirlo **no cambia nada del universo**: el primer toque ya había mutado la melodía como siempre y el segundo lo deshace antes de abrir el
panel (y un test comprueba que el estado guardado es idéntico, salvo los campos de la música, con el panel abierto o no). Para
mutar varias veces seguidas, deja pasar un instante (> 0,3 s) entre toque y toque.

**Qué hace cada botón con el panel abierto** (la música sigue sonando y el visualizador se ve en miniatura abajo):

| Gesto | Qué hace |
|---|---|
| **B** (toque) | Parámetro siguiente (cíclico). **B no cambia de mundo mientras el Estudio está abierto** (el título dice `ESTUDIO`; la pista de abajo dice `B:SIGUE  A:CAMBIA`) |
| **B** (mantener) | Parámetro anterior (añadido: con 11 filas ahorra vueltas) |
| **A** (toque) | Cambia el valor del parámetro seleccionado, al siguiente (cíclico; el último vuelve a `AUTO`). Un pitido corto (el mismo mecanismo de siempre: los pitidos mandan y la música baja un instante) |
| **A** (mantener) | **Cierra** el Estudio; sobre la fila `DADO`, en cambio, **tira el dado** (semilla nueva respetando los candados) y sigue abierto |
| A+B | Diario, como siempre |

Se cierra solo tras **9 s** sin pulsar nada (`LLV_STUDIO_TIMEOUT_MS`; una barra fina bajo el título lo muestra) y al dormir o si una
sorpresa te saca del mundo. Mientras está abierto, los gestos no cuentan para el código secreto; y si estás tecleando el código
(`B B A A ...`), dos A seguidas siguen siendo del código y no abren nada (tras unos B seguidos, espera 1,5 s).

**Parámetros** (11 filas, 6 a la vez con barra de desplazamiento; `AUTO` en gris, lo **fijo** en amarillo con un candado):

| Fila | Valores | Qué cambia |
|---|---|---|
| `ESCALA` | AUTO, PENT. MAYOR, PENT. MENOR, MAYOR, MENOR, DÓRICA, FRIGIA, LIDIA, MIXOLIDIA, BLUES, MENOR ARM. | La escala de la melodía, y con ella los acordes (ver abajo). AUTO = por hora y clima, como siempre |
| `TÓNICA` | AUTO, Do, Do#, Re, Re#, Mi, Fa, Fa#, Sol, Sol#, La, La#, Si | Transpone todo (melodía, bajo y acordes) |
| `TEMPO` | AUTO, 60, 68, 76 ... 140 BPM (de 8 en 8; 11 valores) | El tempo, sin la lluvia, la mascota ni el arcoíris |
| `ARPEGIO` | AUTO, APAGADO, SUBE, BAJA, SUBE-BAJA, AZAR, ROTO | El patrón de la voz de acompañamiento (ver abajo). AUTO = el estilo que sale de la semilla (euclidiano, octavas, arpegio o caminante) |
| `PROG` | AUTO + las 22 progresiones de la tabla (`I-V-vi-IV`, `i-VII-VI-VII`...) | La misma en las cuatro frases (A, A', B y A''); los acordes se ajustan a la escala |
| `TIMBRE` | AUTO, 12,5 %, 25 %, 50 % | Ciclo de trabajo de las dos cuadradas (melodía y bajo) |
| `RITMO` | AUTO, BAJO, MEDIO, ALTO | Densidad del canal de ruido: bombo E(2,16)/E(3-4,16)/E(4-5,16), hi-hat E(4)/E(6-8)/E(10-12) y redobles |
| `MELODÍA` `BAJO` `RUIDO` | SUENA / MUDA | Silencia o reactiva cada voz por separado |
| `DADO` | `MANTÉN A` | Acción: semilla nueva, respetando los candados |

**Candados: AUTO o FIJO.** Los siete parámetros de generación (escala, tónica, tempo, arpegio, progresión, timbre y ritmo) están en
`AUTO` (lo decide la semilla, la hora y el clima, como siempre) o `FIJO` (el valor elegido). **Lo fijo no lo cambian ni "mutar" ni la
"semilla nueva", solo tú**: así puedes fijar la escala y dejar que la melodía mute, o fijar el tempo y tirar el dado. Fuera del
panel, la caja de datos lo refleja: un asterisco tras la escala o el tempo fijos (`DÓRICA*`, `124* BPM`) y un candado en la esquina si
hay otros ajustes fijos. Silenciar una voz no es un candado (no se tiene en cuenta en esa esquina), pero se ve: su barra queda gris y tachada.

**Cómo lo aplica el motor** (`music.c`, sin romper el determinismo):

- **Con todo en AUTO, el audio es idéntico bit a bit al de la alfa.** La semilla hace *todas* sus tiradas en el orden de siempre y los
  ajustes fijos se aplican **después** por encima (por eso fijar un parámetro no mueve a los demás: fijar el valor que AUTO habría elegido da
  exactamente la misma canción). El test de regresión (`tests/test_studio.c`) lleva los hashes del PCM de 32 casos de 70 s y de 2.048
  canciones calculados con el código de la rama anterior, antes de tocar nada.
- **Misma semilla + mismos ajustes = mismo PCM byte a byte**, también en trozos irregulares (probado con cada parámetro fijo).
- **Escalas nuevas** (mayor, frigia, lidia, mixolidia, blues, menor armónica): cada una con su tabla de tendencias melódicas (la sensible sube
  a la tónica, la segunda bemol frigia baja a ella, la cuarta aumentada lidia sube a la quinta...) y su acorde dominante de cierre.
- **Acordes diatónicos.** Si fijas la escala o la progresión, los acordes de la tabla se **ajustan a la escala**: un acorde con todas sus notas
  en la escala (dos en las pentatónicas y el blues) se queda como está; si no, se busca el más cercano que sí (la otra tercera, o la fundamental
  uno o dos semitonos arriba o abajo). Así `I-V-vi-IV` en frigia en Do suena Do menor, La bemol, Si bemol menor y Fa menor (`i-bVI-bvii-iv`) en vez de chocar con la melodía, y en lidia el IV (que no
  es de la escala) pasa a iii. Con la escala
  y la progresión en AUTO no se toca nada. Además, con la escala o la progresión fijas se evitan más los choques con el bajo (la sensible de
  la menor armónica sobre la tónica era lo más áspero: de 3,5 % a 0,2 % de semillas con choques).
- **Arpegios** (corcheas sobre la octava de la fundamental, R fundamental, T tercera, F quinta, O octava): `SUBE` R T F O R T F O, `BAJA`
  O F T R O F T R, `SUBE-BAJA` R T F O F T R T, `ROTO` (bajo de Alberti) R F T F R F T F, `AZAR` primera nota la fundamental y las demás
  al azar del acorde sin repetir la anterior (sale del mismo generador de la semilla: siempre igual con esa semilla), `APAGADO` sin arpegio:
  la fundamental en dos blancas.
- **Silenciar una voz** es un fundido de 16 ms de su **salida** (8 ticks de control): lo que se genera y los osciladores, envolventes y el
  LFSR del ruido siguen su curso, así que no cambia nada de las otras voces y al reactivarla vuelve **sincronizada**. Se prueba con
  `music_render_voices()`, que deja ver cada voz por separado: las otras voces son idénticas muestra a muestra, la silenciada es cero y las
  rampas de fundido bajan en escalones de un octavo.
- **Cambios en marcha:** al cambiar un ajuste, la canción se rehace al instante sin perder el compás ni cortar la nota que suena (como mutar);
  una semilla nueva conserva los ajustes. Un valor no válido se ignora.

**Estado y guardado.** `GameState.mus` crece de 4 a 8 bytes: los ajustes van empaquetados en 27 bits (escala 4, tónica 4, tempo 4,
arpegio 3, progresión 5, timbre 2, ritmo 2 y voces 3; todo AUTO = 0). `SAVE_VERSION` pasa a **6** (los guardados de la alfa se rechazan
con limpieza y se crea un universo nuevo), `SAVE_FIELDS` sigue siendo la única fuente de verdad, se validan los rangos al cargar y el
estado completo ocupa 1.174 bytes guardados (1.608 en memoria) de los 4.096. El panel abierto es efímero (no se guarda). La música sigue sin
tocar `gs->rng` ni avanzar en `World.tick` ni en el catch-up. El código nuevo añade ~3,9 KB a `music.c` con `-Os` (de 14,4 a 18,3 KB, con las tablas) y `Music`
pasa de 2.816 a 2.840 bytes; el coste de renderizar no cambia (70 s de audio x 32 casos en 0,37 s en el portátil, antes 0,36 s).

**Generar WAV e informes con ajustes fijos** (`llv_music`; 0 o sin la opción = AUTO):

```sh
build/make/llv_music --scale 6 --arp 4 --key 3 --seeds 50 --out /tmp/m     # 50 semillas en frigia, arpegio sube y baja, en Re
build/make/llv_music --scale 9 --prog 20 --duty 1 --mute 4 --seeds 20       # blues con la progresión 20, cuadradas finas y sin ruido
build/make/llv_music --one 0x4F2A 0x22 --scale 10                          # volcado de notas de una semilla con la menor armónica
build/make/llv_music --matrix 100 --examples --out capturas/musica          # = lo que añade `make music`
```

Opciones: `--scale 1..10` (el orden de la lista: 1 pent. mayor, 2 pent. menor, 3 mayor, 4 menor, 5 dórica, 6 frigia, 7 lidia, 8 mixolidia,
9 blues, 10 menor armónica), `--key 1..12`, `--tempo 1..11`, `--arp 1..6`, `--prog 1..22`, `--duty 1..3`, `--rhythm 1..3` y
`--mute MÁSCARA` (1 melodía, 2 bajo, 4 ruido). `--matrix N` mide las 77 combinaciones de escala (AUTO + 10) x arpegio (AUTO + 6) con N semillas
cada una (notas y 12 s de audio, los mismos umbrales) y escribe `informe_estudio.txt` (una línea por combinación con qué criterios
incumple, resúmenes por escala, por arpegio y por criterio) y un piano roll por combinación (`estudio_ESCALA_ARPEGIO.png`);
`--examples` renderiza 14 WAV de 20 s y su piano roll (una por escala con un arpegio distinto y cuatro más con otra tónica, tempo, timbre y ritmo).
En el simulador headless, `studio open|close|clear|row N|set FILA VALOR` maneja el panel (`llv_headless --help`).

**Calidad con los ajustes fijos (los umbrales no se tocan)** (`make music`, 100 semillas por combinación, 12 s de audio juzgado):

| | Cumplen |
|---|---|
| **77 combinaciones de escala x arpegio** (7.700 semillas) | **7.376 (95,8 %)**; **las 77 combinaciones pasan el 90 %** (la peor: 91 %) |
| Por escala (todos los arpegios) | AUTO 95,1 %, pent. mayor 96,1 %, pent. menor 96,3 %, mayor 95,9 %, menor 95,1 %, dórica 94,0 %, frigia 97,1 %, lidia 95,1 %, mixolidia 94,9 %, blues 98,0 %, menor armónica 96,0 % |
| Por arpegio (todas las escalas) | AUTO 95,8 %, apagado 96,8 %, sube 96,1 %, baja 94,6 %, sube y baja 95,8 %, azar 95,6 %, roto 95,7 % |
| 10 escalas x 22 progresiones fijas (8.800 semillas) | 8.412 (95,6 %); la peor, 82 % (pent. menor con `i-ii-IV-i` y frigia con `VI-VII-i-i`) |
| Timbre, ritmo, tempo, tónica o voces fijos (100 semillas) | 98 % en todos; con bajo y ruido silenciados, 94 % |
| Música de siempre (AUTO), barrido de las 65.536 semillas | 96,2 %: no ha cambiado |

Incumplen sobre todo "grados conjuntos" (3,0 % de las semillas) y "salto máximo" (1,0 %), los mismos de siempre; los choques ásperos son el 0,1 %.
**Qué sonaba peor por construcción y cómo se arregló** (primera pasada, sin tocar umbrales): la **menor armónica** (89 %: la sensible sobre la
tónica es una séptima mayor contra el bajo y el VII bemol de las progresiones menores no existe en ella; se armonizan los acordes y se evitan más
los choques, y la tabla de tendencias favorece los grados conjuntos: 96 %) y la **frigia** (la segunda bemol; misma receta: de 93 % a 97 %).
Siguen siendo los más "raros" de oído: **la menor armónica con `SUBE` y la frigia con `BAJA`** son las que más hay que escuchar, y **cualquier
progresión fijada en una escala que no es suya** (p. ej. `I-V-vi-IV` en frigia) pasa por la armonización, que da acordes legales pero a veces
distintos de los que anuncia la fila (el nombre sigue siendo el de la tabla). Y lo de siempre: que cumpla los umbrales no dice que **guste**; el gate de
oído de las escalas nuevas (y de los 14 WAV de ejemplo) es tuyo.

### Audio, batería y pitidos

- **Solo suena en el mundo Música y con el llavero despierto.** Al cambiar de mundo o al dormirse se desvanece
  en 150 ms (`LLV_MUSIC_FADE_MS`) y calla (`engine_audio_render` devuelve 0 muestras: no se manda silencio, la salida
  duerme). Con una sorpresa de cartucho sucio o de "salí" en este mundo, también calla.
- **Se duerme a los 3 minutos sin tocarlo** (`LLV_SLEEP_MUSIC_MS`) en este mundo mientras suena, en vez de a los
  20 s de los demás: es un mundo para dejarlo en la mesa, y con 20 s no daría tiempo a juzgar ninguna canción.
  Sigue siendo "solo consume batería mientras estás aquí".
- **Quién manda: los pitidos de la interfaz.** Los `Tone` de `FrameOut` (un toque, B, etc.) siguen saliendo igual;
  la plataforma los mezcla por encima de la música y el motor baja la música al 44 % mientras suenan
  (`music_duck`, con rampa para que no haya escalones).
- **HAL** (`src/hal/hal.h`): `hal_audio_wanted()` (cuántas muestras produce falta) y `hal_audio_push(pcm, n)`; el bucle común
  `hal_audio_pump()` (`src/hal/audio.c`) llama a `engine_audio_render()`. El PCM es mono de 16 bits a
  `LLV_AUDIO_RATE` (22.050 Hz; vale entre 8.000 y 48.000 y el tempo es el mismo a cualquier tasa), para el PWM de la
  RP2350. En el simulador SDL2 el callback mezcla una cola circular de música con los pitidos (con
  `SDL_AUDIODRIVER=dummy` funciona sin sonido; sin dispositivo, `hal_audio_wanted` sigue el reloj y el visualizador
  sigue vivo). En headless hay un stub (`hal_audio_stub.c`) que no suena pero pasa por el mismo camino y se puede
  grabar: `llv_headless ... world 3 rec musica.wav wait 30000 recstop`.

### Generar WAV y el informe

```sh
make music    # = build/make/llv_music --seeds 200 --seconds 30 --out capturas/musica --baseline --sweep 65536 --roll 8
build/make/llv_music --seeds 50 --first 100 --out /tmp/m --no-wav        # solo medir
build/make/llv_music --one 0x4F2A 0x22                                   # volcado de notas de una semilla y un ánimo
```

Escribe `musica_XXXX.wav` (mono, 16 bits, 30 s), `informe_musica.txt` (por semilla: escala, tonalidad, tempo,
progresiones, todas las métricas, el veredicto y qué criterios incumple, más un resumen con los mínimos, medias y
máximos), `roll_XXXX.png` (piano roll: melodía cian, bajo magenta, bombo/caja/hi-hat, acordes y frases) y
`espectro_XXXX.png` (espectrograma de 0 a 5,5 kHz) de las primeras semillas. Cada semilla rota entre franjas del día,
lluvia, cielo y mascota (el ánimo sale de su posición: 120 combinaciones), así que las 200 las cubren todas. `--baseline` repite el
análisis con una **melodía ingenua** (notas al azar de la escala con ritmo al azar, el mismo acompañamiento) y
`--sweep N` mide solo las notas de N semillas sin renderizar (con 65.536 recorre todas).

### "Suena agradable 9 de cada 10 semillas": qué se mide y qué no

**No se puede oír desde el código.** Lo que sí se puede medir es si la música está *bien formada* según reglas sencillas
de teoría musical. Los umbrales se fijaron antes de medir y se razonan aquí; una semilla es "agradable" si cumple **todos**:

| Criterio | Umbral | Por qué |
|---|---|---|
| Notas en la escala | >= 95 % | Sin notas ajenas a la escala elegida |
| Rango | entre 7 y 17 semitonos | Menos de una quinta es monótono; más de octava y cuarta ya no es cantable |
| Salto máximo | <= 12 (una octava) | Un salto mayor que una octava rompe la línea |
| Salto medio | <= 4 semitonos | Una melodía se mueve por grados conjuntos |
| Grados conjuntos | >= 55 % de los intervalos <= 3 semitonos | En las melodías occidentales y de tradición oral la mayoría de los intervalos son grados conjuntos (en pentatónica, de 2 o 3 semitonos) |
| Saltos grandes | <= 10 % de los intervalos > 7 semitonos | Mayores que una quinta, raros |
| Notas repetidas | <= 3 iguales seguidas | La cuarta igual seguida suena a tartamudeo |
| Escalas largas | <= 7 notas seguidas subiendo (o bajando) | Más es un ejercicio de escala, no una melodía |
| Variedad | >= 4 clases de altura | Con menos, un ostinato |
| Motivo que vuelve | >= 30 % de semejanza entre compases a la mejor distancia (1 a 12) | Autocorrelación a nivel de compás: lo que se repite se reconoce (el azar da 10-20 %) |
| Densidad | entre 2 y 9 notas por compás | Menos es letárgico, más es frenético con semicorcheas a 60-140 BPM |
| Silencios en la melodía | <= 30 % de los pasos | Debe cantar casi todo el rato |
| Resuelve | la última nota es la tónica y dura >= una negra | La cadencia final |
| Consonancia en tiempos fuertes | >= 85 % de las ocasiones (pasos 0 y 8) | Entre melodía y bajo: unísono, terceras, cuarta, quinta y sextas; 2.as, 7.as y tritono cuentan como disonancias |
| Notas del acorde | >= 55 % de la duración de la melodía | La melodía se apoya en la armonía |
| Choques ásperos | < 8 % del tiempo con 2.ª menor, 7.ª mayor o tritono entre melodía y bajo | Los intervalos más ásperos pueden pasar, no quedarse |
| Tempo | 56 a 140 BPM | |
| Pico | < 32.000 y sin recortes | El máximo teórico es 18.500; sin saturar |
| Volumen | RMS entre -32 y -9 dBFS | Ni inaudible ni a tope |
| Componente continua | < 3 % del fondo de escala | |
| Silencio | < 15 % de ventanas de 20 ms mudas | Sin huecos largos |

**Resultado** (`make music`, semillas #0001-#00C8 con los 24 primeros ánimos en rotación, y barrido de todas las
semillas con todos los ánimos):

| | Cumplen |
|---|---|
| 200 semillas, 30 s de audio cada una | **194 de 200 (97,0 %)** |
| Barrido de las 65.536 semillas (solo notas) | **63.064 (96,2 %)** |
| Por franja del día y clima | entre 95,5 y 96,6 % en todas |
| Por escala | pentatónica 96,5 %, pentatónica menor 96,1 %, dórica 95,0 %, menor 96,2 % |
| Con 1 a 6 mutaciones | 95,9 a 96,3 % |
| **Melodía ingenua** (notas al azar de la escala) | **0 de 200 (0 %)** |

Las 6 que no cumplen de las 200 incumplen solo "grados conjuntos" (y una también "salto máximo"): #0038, #004D, #0072, #0096,
#00BF y #00C6 (casi todas en pentatónica menor o menor, donde las notas del acorde de VII o iv quedan a 5 semitonos). Los
umbrales no pasan por casualidad: el azar no los cumple nunca, y los tests (`gate: >= 90 %`) lo vigilan con 240 semillas
renderizadas, el barrido de 6.000 y la melodía ingenua.

**Qué hice para subirlo.** La primera versión del generador cumplía el 28,6 % (barrido). Cada paso se midió con el
barrido de las 65.536 semillas, y se afinó el generador con *conocimiento musical*, no moviendo umbrales:

1. Solo el primer y el tercer tiempo (y las notas que pasan por encima) exigen una nota del acorde; antes también las
   negras del 2.º y el 4.º, y la melodía salía hecha de arpegios (28,6 -> 61,8 %, junto con ampliar la métrica de
   motivos de 8 a 12 compases de distancia: A y A'' están a 12).
2. Ningún salto mayor que una quinta (salían octavas por azar): 68,6 %.
3. Cada compás acaba cerca de donde empieza el siguiente (los motivos copiados saltaban entre frases), los motivos copiados
   se desplazan por notas del acorde y no por semitonos (A' repetía), y nunca hay tres notas iguales seguidas: 83,7 %.
4. Pesos de los saltos más conservadores (terceras menos y quintas casi nunca): 92,7 %.
5. Ritmos con notas de paso incluso en las melodías escasas: 93,3 %.
6. Más peso aún a los grados conjuntos y una tendencia al centro más suave: 96,1 %.
7. Añadí un criterio más (choques ásperos), que bajó el resultado a 94,7 %, y las notas de paso pasaron a evitar los
   choques con el bajo: **96,2 %**.

**Una advertencia honesta:** un generador afinado contra unas métricas tiende a cumplirlas (ley de Goodhart); que cumpla los
umbrales demuestra que está bien formado, no que guste. Lo único que se cambió en las métricas fue el alcance de la de
motivos (8 a 12 compases, paso 1) y se añadió un criterio; ningún umbral se aflojó.

### Lo que no se puede validar sin oído humano

El gate real ("suena agradable 9 de cada 10 semillas") **se cierra escuchando**:

```sh
make sim && build/make/llv_sim              # B hasta Música (Reloj > Jardín > Sala > Música), o: build/make/llv_sim --world 3
# A: mutar    A (mantener): semilla nueva    + / -: volumen    1 / 2: máquina del tiempo (cambia la hora, la escala y el tempo)
```

o abre los WAV de `capturas/musica/` (`musica_0001.wav`...) y apunta cuáles te molestan: pasa 20 semillas y cuenta (18 o más =
gate). Esto **no** lo puede decir ninguna métrica: si el timbre de las cuadradas con el filtro cansa o es agradable, si el
bombo de ruido suena a bombo, si los alias de las cuadradas agudas (se ven en el espectrograma por encima de 5 kHz) molestan
en un altavoz de 15 mm, si 30 s de bucle ya se hacen pesados, si el vibrato es demasiado, el balance entre voces, ni cómo suena
por el PWM de la RP2350 (tasa, filtrado RC y el altavoz real). Material para afinar: las 6 semillas que no cumplen (arriba);
#0001, #0002, #0003 y #0006 (rotan dórica, pentatónica y menor, de las más y menos densas); y las extremas del informe:
#0091 (la más escasa: 39 notas), #0064 (la más densa: 88), #000D (56 BPM), #008B (137 BPM), #006C (la más baja de volumen) y
#0003 (la más alta), todas con su ánimo en `informe_musica.txt`. Las dos preguntas que más importan: ¿se nota que la lluvia la hace
más triste?, y ¿molesta que no haya ninguna variación de dinámica en 30 s?

Los gráficos (`roll_XXXX.png`, `espectro_XXXX.png`) y las capturas (`make shots`) se revisaron a ojo: el piano roll muestra
melodía dentro del rango, bajo en el registro grave, acordes por compás y las frases A, A', B y A''; el espectrograma, las
armónicas de las cuadradas y los golpes de ruido; el visualizador, barras, osciloscopio y pasos que se mueven con el compás.

## La Pesca (fase 5)

El estanque del jardín con un solo botón (spec §4.5). Antes de lanzar hace falta una **lombriz** 🪱 (el objeto de
la bolsa de rareza media; se consigue como cualquier objeto, con la lluvia o con la compostera).

### Controles

| Gesto | Qué hace |
|---|---|
| **A** (toque) | **Lanza la caña**: gasta una Lombriz de la bolsa (la **preparada** desde el Cobertizo cuenta si es lombriz; si lo preparado es otra cosa, se queda preparado para el Jardín). **2 lanzamientos al día** (se reponen a las 05:00, como las acciones del Jardín). Sin lanzamientos: *"El estanque descansa. Vuelve en 3 h 20 min."*; sin lombrices: *"Sin lombrices, la pesca es solo meditación."* (nunca se bloquea, y no gasta nada) |
| **A** (mantener) | Durante la pelea: **sube la barra**. Fuera de ella vale como un toque (lanza la caña o cierra la tarjeta) |
| **B** | Cambia de mundo, como siempre, y no actúa. Si había una pesca a medias se cancela: la lombriz gastada se pierde, el pez que había picado cuenta como escapado (queda en el diario) y se avisa ("Soltaste la caña: la lombriz se perdió."). Dormirse a mitad hace lo mismo y avisa al despertar. No queda ningún estado colgado |

### El minijuego

1. Un toque de A: el flotador vuela en arco hasta el agua (0,7 s).
2. **"Picando…"** (1,2 a 3,4 s: lo sortea el lanzamiento) y un **¡PICA!** de 0,7 s con el flotador hundiéndose.
3. **La pelea.** A la derecha hay una barra vertical con el pez (con un halo blanco) y tu **zona verde**. **Mantén A para subir
   la zona; al soltar, cae** (con un pequeño rebote en el fondo). El **medidor** (la columna de la derecha) sube mientras el pez
   está dentro de la zona y baja fuera: si se llena, **lo pescas**; si se vacía (o pasan 60 s), **se escapa**. En el agua, el
   flotador sigue al pez y su sombra nada bajo él.
4. La **tarjeta** del resultado (7 s o un toque de A): el pez a doble tamaño, ¡NUEVO! / ¡RÉCORD! / ¡LEYENDA!, nombre, centímetros y
   lo que trae (un objeto, una alegría para la mascota, restos para la compostera o, si no, su humor).

Todo va en **aritmética entera y a paso fijo de 20 ms** (`LLV_FISH_STEP_MS`): el resultado no depende de los fotogramas por
segundo (lo comprueba un test a 20, 33 y 100 ms por fotograma). Las posiciones son de 0 a 1000; la barra gana 5 por paso al
mantener y pierde 3 al soltar (velocidad entre -24 y +18); el medidor empieza en 300, sube 8 por paso en la zona y baja
3 + dificultad/2 fuera. La zona mide 270 - 8·dificultad (262 el pez más fácil, 190 el más difícil) y **el pez solo nada entre
294 y 706**: justo por encima de la zona de la barra en reposo y por debajo de la de la barra a tope, así que **quien no pulsa
nunca y quien mantiene A siempre pierden siempre, por construcción** (y un jugador perfecto simulado, `fishgame_ai`, gana siempre:
lo comprueban los tests con 300 semillas por especie).

El pez se mueve con su **temperamento** (tabla `TEMPER` de `fish.c`): *tranquilo* (cambia de sitio cada 1-2 s, despacio),
*vago* (lento, pocos saltos), *saltarín* (cambia cada 0,3-0,7 s), *caprichoso* (como el saltarín, con empujones al azar) y
*fugaz* (se queda quieto y de pronto se lanza lejos). Su movimiento sale de un **PCG32 propio del minijuego** que se siembra
**una sola vez** con `rng_next(&gs->rng)` al lanzar: el universo avanza igual con o sin pelea y la pelea es reproducible dado el
lanzamiento (misma semilla, mismo recorrido del pez; solo cambia lo que tú pulses). El mismo sorteo decide qué pica, de qué
tamaño es y cuánto tarda. **El minijuego en curso no se guarda** (es efímero).

Un humano simulado (reflejos de 200 ms y un 3 % de torpeza) gana a: Pez Lunes 100 %, Carpa 100 %, Trucha 99 %, Bagre 100 %,
Paraguas 98 % y **Koi Lunar 21 %** (números 🧪: el objetivo final debe costar).

### Especies y rareza

Tabla de datos en `src/core/fish_data.c` (nombre, peso/rareza, dificultad, temperamento, tamaño, condiciones y lo que trae); el
motor de selección (`fish_pick`) es genérico: suma pesos según el contexto y tira con el `Rng` que se le da. Con **lluvia
pican menos basuras y más peces raros** ("llueve, mejores peces"), para todos.

| Pez (hueco de la colección) | Peso | Dif. | Carácter | Cuándo pica | Tamaño | Trae |
|---|---|---|---|---|---|---|
| **Pez Lunes** (COLL_PEZ0) | 100 | 2 | tranquilo | siempre | 12-28 cm | nada |
| **Carpa Resignada** | 80 (x1,3 de día) | 3 | vago | siempre | 25-60 cm | nada |
| **Trucha de Escarcha** | 30 (x2 en invierno) | 5 | saltarín | otoño e invierno | 20-45 cm | Viento; alegra a la mascota |
| **Bagre Insomne** | 45 | 5 | caprichoso | de noche | 30-80 cm | Semilla; alegra a la mascota |
| **Pez Paraguas** | 40 | 4 | fugaz | con lluvia | 10-30 cm | Nube; alegra a la mascota |
| **Koi Lunar** (legendario) | 30 | 8 | caprichoso | **solo de noche con luna llena** | 80-140 cm | **Deseo**; alegra a la mascota |
| Bota Vieja, Lata Oxidada, Calcetín Solitario | 8 / 7 / 4 | 1 | | siempre | | nada (no cuentan en la colección) |
| Alga Enredada | 8 | 1 | | siempre | | **restos de pesca**: si la mascota es la Compostera, la alegra y devuelve una Lombriz |

Los nombres y las fichas tienen el humor de §5.3 (*"Pez Lunes: ojeroso. Pica los lunes. Los otros días también, con menos
ganas."*). El legendario además dispara su propio suceso del diario, frases de la Sala, destellos dorados sobre el agua, un halo
en la luna llena del cielo, una fanfarria y su tarjeta con borde dorado.

El estanque **cambia con la hora y el clima**: cielo y agua por franjas (noche, amanecer, día, ocaso) con reflejo del sol o de la
luna con dithering, ondas de 4 fotogramas, sombras de peces que pasan, nenúfares y juncos; la luna muestra su **fase real**; con
lluvia hay nubes grises y gotas que ondulan el agua, con nieve cae nieve y la orilla es blanca en invierno.

### Universo compartido

- El diario "mientras no mirabas…" (**A+B**) apunta *Lanzas la caña*, *Pesca Carpa*, *Huyó Bagre*, *Pesca Koi* (legendario), *Pesca Bota*
  y *Nuevo: Trucha* (nuevos `EventKind`: `EV_CAST`, `EV_FISH`, `EV_FISH_LOST`, `EV_FISH_LEGEND`, `EV_FISH_JUNK`).
- **La Sala los comenta** (16 frases nuevas, 725 en total: Abuela, Unidad-7, la Gótica, el Gamer y el Boomer; también charla de luna llena).
  El vocabulario de `phrases_vocab.h` ganó `evento:cebo`, `pez`, `pez_escapa`, `legendario` y `basura`.
- **El Director** marca ❗ en la Pesca cuando entra una **lombriz** en la bolsa (recarga natural, lluvia o compostera) y la mañana
  en que llega la **luna llena**, y abre la Pesca al despertar si no hay nada más importante (el orden es Jardín raro, Sala, Cobertizo, Pesca). También puede aparecer allí
  una invasión (la Gótica en la pesca) o el cartel de "Salí".
- **Cobertizo:** la compostera acepta los restos (alga) y los peces raros alegran a la mascota y dejan objetos si caben.

### La colección y las fichas (Cobertizo)

La estantería (16 huecos) ya enseña los 6 peces con su sprite reducido (silueta gris "???" hasta pescarlos; el Koi Lunar con marco
dorado). Con el cursor sobre **la colección** va nombrando lo que tienes y termina con *"Peces 3/6"*. Por cada pez que tengas hay
un **puesto más en el cursor** (entre la colección y la mascota: **A (toque)** avanza como siempre): al llegar a él se abre su
**ficha** de dos páginas que alternan solas (o con **A (mantener)**): **nombre, centímetros del récord de tamaño y número de
capturas** (y *LEGENDARIO* en el Koi) / **humor y comentario**. Sin peces, los puestos del cursor son los de antes.

### Estado, guardado y tests

`GameState.fish` (16 bytes): capturas por especie (se saturan en 255), récord de tamaño de cada una en cm, basuras, peces escapados
y lanzamientos de toda la vida. **`SAVE_VERSION` 5** (los guardados de las fases 0 a 4 se rechazan con limpieza); `SAVE_FIELDS`
sigue siendo la única fuente de verdad. El `GameState` ocupa **1.600 bytes en memoria y 1.170 en el guardado**. La validación al
cargar rechaza ahora también estados imposibles: capturas sin récord o sin hueco en la colección, o más acciones del día
que el límite de cada mundo.

`tests/test_pesca.c` (`llv_test_pesca`, 23 tests, ~1 millón de comprobaciones): tabla de especies y que todo lo dibujado cabe;
selección por contexto (noche, lluvia, estación, luna llena, con frecuencias que cuadran con los pesos a 4,5 sigmas y el
legendario **nunca** fuera de la luna llena en los 128 contextos x 4 estaciones); límite de 2 al día y reinicio a las 05:00;
gasto de lombriz y objeto preparado; una sola tirada de `gs->rng` por lanzamiento; minijuego reproducible, perfecto gana siempre,
sin pulsar o con A mantenida pierde siempre; colección sin duplicados y récords; regalos baratos; **la cadena lombriz → pez →
colección → diario por el motor (el gate)**; mensajes con gracia; B y dormir cancelan sin estados colgados; independencia de los
fotogramas por segundo; determinismo del universo con pesca (vivo, catch-up y guardado a mitad, byte a byte) y del motor con
fotogramas densos o escasos; guardado v5 y estados absurdos; novedades del Director; la Sala comenta cada suceso; el
diario; el dibujo en todas las horas, climas y fases; la colección y sus fichas, y que los 6 mundos son reales y van en el
orden de la spec. Con ASan y UBSan, limpios.

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

Para la fase 4: `weather N` y `sky N` (clima y cielo, que cambian la música), `musicseed S`, `music`
(describe lo que suena) y `rec ARCHIVO.wav` / `recstop` (graba lo que saldría por el altavoz mientras tanto):

```sh
build/make/llv_headless --date "2026-10-22 23:30" --seed 3 musicseed 17 weather 1 world 3 \
    rec musica_noche_lluvia.wav wait 30000 recstop shot musica
```

Para la fase 5 (`--help` los lista todos): `fish none|perfect|human` (lanza la caña y juega el minijuego: nunca pulsa, perfecto
o humano torpe), `fishshot SKILL NOMBRE` (igual, con capturas de la pelea y de la tarjeta), `forcefish N` (el próximo lanzamiento
pica ese pez, para las capturas), `catch N CM` (apunta un pez como pescado) y **`alpha DIAS [INFORME.txt] [stock]`**, la prueba
de la alfa (ver más abajo):

```sh
build/make/llv_headless --date "2026-10-24 22:30" --seed 3 --out capturas world 4 give 5 forcefish 5 fishshot perfect koi
build/make/llv_headless --date "2026-10-22 08:00" --seed 7 alpha 14 informe.txt stock
```

## Estado por fases (spec §14)

- [x] **Fase 0:** motor, HAL, simulador SDL, controles, estado con límites fijos,
  look GBC, Reloj y tests de determinismo.
- [ ] Fase 1: hardware (RP2350, botones, altavoz, IMU, sueño).
- [x] **Fase 2:** Jardín + bolsa + Cobertizo + mascota (ver abajo el gate de la semana
  simulada).
- [x] **Fase 3:** La Sala (personajes, preguntas, memoria, diario), Director, hábitos y bono
  de paciencia (ver el gate de la fase 3).
- [x] **Fase 4:** Música generativa: motor, mundo, HAL de audio y métricas hechos (97 % de las semillas cumple los
  umbrales, ver más arriba); **el gate de oído está pendiente de ti** (es el que cuenta: `make sim`, mundo Música).
- [x] **Estudio de la música (v1.1)**, a petición del dueño tras probar la alfa: dos toques de A abren un panel para fijar la escala, la
  tónica, el tempo, el arpegio, la progresión, el timbre y el ritmo (candados AUTO/FIJO) y silenciar voces, sin tocar el modo rápido
  (ver [El Estudio](#-el-estudio-v11)). El gate de oído también cuenta para las escalas y los arpegios nuevos.
- [x] **Fase 5:** Pesca y colección completa: especies, selección por contexto, minijuego, colección con fichas, diario, Sala
  y Director (ver el gate de la fase 5). Con ella, **los 6 mundos funcionan de principio a fin: versión alfa**.
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

## Gate de la fase 5: la cadena lombriz → pez → colección

*"La cadena lombriz → pez → colección funciona."* La comprueba de punta a punta el test `CADENA` de `tests/test_pesca.c`,
pasando por el motor y sus fotogramas: un toque de A gasta la lombriz y anota `EV_CAST`; tras esperar y picar, un jugador que
mantiene A gana la pelea; el pez entra en la colección (`COLL_PEZ0 + n`) con su récord y sus capturas, el diario lo cuenta
("Pesca Carpa") y la Sala lo comenta; el estado se guarda y se carga idéntico. Y la prueba de la alfa lo repite durante 14 días.

Con 8 semillas de 14 días (empezando el 22 de octubre, con la luna llena el 24-27) y una persona que pesca cuando tiene lombriz:
**sin ayudas** (solo las lombrices que da el universo) hace 3-10 lanzamientos en total (media 6,5) y reúne 2-5 especies
de las 6; **con una lombriz extra al día** (`stock`: lluvia y compostera generosas) hace 12-19 lanzamientos (media 16) y 3-5
especies. El Koi Lunar cayó en 1 de esas 16 ejecuciones. **Honestamente:** la pesca real depende de las lombrices:
la bolsa da ~3 objetos al día y la lombriz pesa 12 de 100 (0,36 lombrices al día, más lo que dé la lluvia, la compostera y
el Cobertizo), así que sin ayuda se pesca una vez cada dos o tres días. Es la tensión de la spec ("hay que decidir dónde usar cada
objeto"), pero es el primer número 🧪 que conviene tocar al jugar de verdad.

## Estado de la alfa

**Qué es:** el llavero entero en el simulador, sin hardware. Los 6 mundos del ciclo de B funcionan de principio a fin y comparten
un universo (la lombriz del jardín es el cebo, el pez queda en el diario, la Sala lo comenta, la lluvia mejora la pesca, la
compostera acepta los restos...). `make alpha` ejecuta los tests y **14 días de uso realista** (`llv_headless alpha 14`, que `ctest` y
`make test` también pasan):

- **Qué hace la persona simulada:** 1 a 4 sesiones al día (07:40, 12:50, 18:20, 22:40, con holgura, y un día de poco uso). En cada una
  recorre el Reloj (cambia de estilo y mira la fecha), el Jardín (acción del día y lupa), la Sala (charla, timbre, contesta
  preguntas de sí o no y sigue las escenas), la Música (muta, pide semillas y a veces abre el Estudio con dos toques, cambia valores, tira el dado y lo cierra a mano o lo deja
  que se cierre solo; con el audio corriendo por el mismo camino que en el simulador), la Pesca (con jugador perfecto, humano torpe o que no pulsa; a veces suelta con B a mitad de pelea o esperando; a
  veces el llavero se apaga con la caña en el agua), el Cobertizo (prepara la lombriz, cuida y respira con la mascota, mira las fichas de
  los peces con el cursor y mantiene A para cambiar de página), una vuelta entera con B (y B mantenido), gestos A+B, agitar y el código
  secreto. Hay un **día entero sin mirar y una semana fuera** (21 días naturales en total) y una **ausencia de 1 hora**.
- **Guardado a mitad:** el estado se guarda, se carga con `engine_resume` y se sigue (dos veces: a mitad y al final); el estado
  cargado es idéntico al guardado byte a byte.
- **Invariantes** (cada despertar, cada sesión y tras cada cancelación): índice de mundo válido, bolsa dentro de su capacidad y sin
  fantasmas ni cantidades negativas, acciones del día <= límite de cada mundo, <= 32 entidades, diario <= 64, mascota y sorpresa
  válidas, capturas y colección coherentes, nada de pesca a medias fuera de la Pesca ni con el llavero dormido, guardado < 4 KB y
  guardar-cargar-guardar idéntico, y todo píxel dentro de la paleta. Nada se cuelga: todos los bucles tienen tope y el escenario
  falla si tarda más de 240 s (tarda 0,8 s). Con ASan y UBSan no hay accesos fuera de límites.
- **Resultado** (semilla 7, 14 días, con lombriz extra; el informe completo sale en `capturas/alfa/informe_alfa_stock.txt`): 62.568
  fotogramas, 45 despertares, 96 comprobaciones, **0 violaciones**; 466 sucesos en el diario (el diario circular pisó 20); Pesca:
  17 lanzamientos, 16 pescados y 1 cancelado con B; 5 de los 6 peces y 15 de 16 huecos de la colección, incluido el **Koi Lunar de 134 cm
  con la luna llena**; la compostera creció y comió 3 manzanas; 8 sorpresas del Director (cartucho sucio, arranque falso, invasiones,
  rebeldía, colapso, INSERT COIN...); 50 frases distintas en la Sala (54 mostradas); guardado máximo **1.170 bytes** de 4.096. Sin lombriz extra
  (`informe_alfa.txt`): 8 lanzamientos, 7 peces y 14 de 16 huecos.
- **Capturas** de los 6 mundos a las 07:00, 12:30, 19:20 y 23:30 (`capturas/alfa/alfa_*.png`) y de la Pesca y la colección (`capturas/pesca/`),
  que se miraron una a una: el texto cabe, los sprites se leen (los peces a 16x12 y a la mitad en la estantería, las siluetas "???"),
  nada se solapa con el HUD.

**Qué está hecho:** motor, HAL (con audio), simulador SDL2 y herramienta headless; los 6 mundos; las 4 mascotas; el jardín como tabla de
datos; 725 frases con su validador; hábitos, Director de Sorpresas, bono de paciencia y LED; música generativa de 3 voces; pesca
con 6 peces y 4 basuras; colección con fichas; el **Estudio de la música** (v1.1: 10 escalas, 6 arpegios, candados y voces silenciables); guardado v6 con tamaño fijo;
162 tests (84 del motor, 26 de la música, 29 del Estudio, 23 de la pesca)
más la alfa, limpios con gcc y clang (`-Wall -Wextra -Wpedantic`) y con ASan/UBSan.

**Qué falta** (en bruto, no es de la alfa): todo lo de la lista siguiente.

## Pendiente para la beta

Por orden de prioridad (🧪 = número a afinar jugando):

1. **Hardware real (fase 1):** placa RP2350, botones, altavoz, pantalla, IMU, LED. Todo lo que hoy simula el HAL: el **podómetro** del IMU
   y el agitar, el **LED de aviso**, la **batería**, el **altavoz** por PWM, el **RTC** (RV-3028 o el temporizador AON) y el **sueño
   profundo** de la RP2350 con el despertar por botón. Medir el consumo (objetivo >= 4 días).
2. **Medir el coste en el Cortex-M33:** la música (estimado < 5 % de CPU a 22.050 Hz, sin medir), el dibujo de los mundos, el catch-up de
   7 días (~1.000 ticks) y el tamaño real del firmware y de la flash.
3. **Afinar los números 🧪 jugando una semana de verdad:** la **economía de lombrices** (0,36 al día sin ayuda: ¿subir el peso de la
   Lombriz o dar más con la lluvia?), la dificultad de cada pez (el Koi Lunar es 21 % para un humano de 200 ms) y la del minijuego
   (zona, velocidad, rebote), la rareza de cada especie, la velocidad del jardín, el ciclo de la mascota y las probabilidades
   del Director.
4. **Arte propio:** sustituir los dibujos hechos con formas y los sprites de relleno por sprites propios (Aseprite o Pixelorama):
   animar los peces (hoy son una imagen fija de 16x12), el pescador, las mascotas y los personajes de la Sala; más escenas de
   fondo.
5. **Contenido de frases:** 725 frases frente al objetivo de 150-300 por residente y 50-100 por visitante: más relleno, más
   variantes de las preguntas de hábitos, un comentario más por suceso y personaje (pesca incluida), más escenas, retomas, fechas
   y visitantes (ver el gate de la fase 3). La frase de "el estanque descansa" aún no la dicen los personajes (hoy es un mensaje del motor).
6. **Más peces:** 6 peces y 4 basuras es la estructura; la spec pide más variedad por hora, estación y clima (un pez por estación,
   uno para el viento, otros nocturnos) y más sprites.
7. **Carcasa y dos semanas de uso diario (fase 6):** impresión 3D, tornillos, anilla; y ver si el día 14 lo sigues mirando.
8. **Regalos (fase 7):** personalización por persona (nombre, mascota, visitantes favoritos), **mensajes con fecha de apertura**, el
   **cumpleaños** (la etiqueta `cumple` está en el vocabulario y nada la usa), el "cartucho" por USB y el modo apto para la abuela por regalo.
9. **Música:** el **gate de oído** de la fase 4 (nadie ha escuchado las semillas ni las 10 escalas y 6 arpegios del Estudio: empieza por los
   14 WAV de `capturas/musica/estudio_*.wav`), Unidad-7 comentando la música desde la Sala, orden 2 de Markov real (hoy se aproxima con reglas) y
   la tabla de alias de las cuadradas en el altavoz de 15 mm. Ideas para el Estudio que no entraron (el objetivo era "no tanto como un IDE"):
   **guardar presets** (3 o 4 huecos de ajustes con nombre, y compartir "semilla + ajustes" como un código corto, `#4F2A` + 4 bytes);
   **grabar la melodía a WAV desde el llavero** (hace falta flash o SD; hoy solo lo hace `llv_headless rec`); **mutar solo una voz** (el ritmo, el
   bajo o la melodía por separado); volumen por voz y "tap tempo"; más escalas (menor melódica, doble armónica, hexatónica) y más arpegios
   (por tresillos, con octavas, el acorde entero); fijar el motivo de la melodía y mutar solo las frases B; exportar la canción a MIDI por USB;
   que el Estudio "suene a lo que tocas" (una vista previa del valor antes de aceptarlo) y un modo ambiente que cambie un parámetro cada vuelta.
10. **Pulido:** el nombre del llavero y de las mascotas, la web con Emscripten, modo ambiente de la pantalla MIP (v2), efecto LCD
    del simulador en la tarjeta, sonido propio del minijuego (hoy son pitidos) y la tecla de máquina del tiempo también para la pesca.

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

### Decisiones de la fase 4

- **La semilla es de 16 bits** (`#4F2A`): cabe entera en la pantalla, se puede apuntar y 65.536 canciones de
  base (x ánimos x mutaciones) sobran. El estado de la música son 4 bytes: semilla, mutaciones (1 a 255; al llegar a 255 vuelve
  a 1) y el último ánimo. La semilla inicial sale de la del universo con un hash, sin gastar `gs->rng`; la nueva (A, mantener) sale
  de la hora y el instante del toque, tampoco usa `gs->rng`.
- **El sintetizador vive en el `Engine`, no en el `GameState`** (es efímero): `engine_audio_render()` lo avanza por trozos y
  el mundo lo lee por `WorldCtx.music` (solo lectura). El motor lo arranca al entrar en el mundo y lo apaga con un fundido al salir o
  dormir. El ánimo se toma al entrar (hora, lluvia, cielo y mascota de ese momento) y se guarda en `gs->mus.mood`.
- **El visualizador se calcula del generador** (envolventes y las últimas 128 muestras del propio sintetizador), no del
  dispositivo: igual con y sin audio. Mantener la gramática de botones: B cambia de mundo; A actúa.
- **Pitidos y música**: los pitidos mandan (se mezclan por encima y la música baja al 44 % mientras suenan). En el
  simulador, las dos cosas pasan por el mismo callback de SDL. El volumen y el silencio (`+`, `-`, `M`) son del simulador, no del estado.
- **Dormir**: con la música sonando, 3 minutos sin tocar (`LLV_SLEEP_MUSIC_MS`) en vez de 20 s. Sin música, nada cambia.
- **Mutar y semilla nueva**: mutar regenera la melodía y sigue por el mismo paso (la nota que suena termina); semilla nueva
  baja el volumen 40 ms, regenera todo y vuelve a empezar con entrada suave (no hay clics). Un cambio de ánimo (llueve) espera
  al principio de la vuelta (16 compases).
- **Ensayos que no entraron**: orden 2 real en las cadenas de Markov (se aproxima con las reglas de salto y repetición, más sencillo
  y suficiente); notas de paso cromáticas (todo es de la escala); más voces o arpegios rápidos al estilo NES (las dos cuadradas
  hacen bajo o arpegio de corcheas); el comentario de Unidad-7 sobre la música en la Sala (es secundario y habría tocado el
  vocabulario de frases: queda para cuando se amplíe el banco).

### Decisiones del Estudio (v1.1)

- **El gesto que lo abre: dos toques seguidos de A en Música (< 0,3 s).** Alternativas descartadas: A+B (es el diario), agitar (el llavero se
  agita al llevarlo en el bolso), mantener A más tiempo (el A mantenido ya saca una semilla a los 0,45 s) y una pulsación larga de B (es el
  mundo anterior). Para que abrirlo no deje ninguna mutación de más, **el primer toque actúa como siempre** (mutar al instante, sin la
  espera de 0,3 s que haría falta para saber si habrá un segundo) y **el segundo lo deshace** (`gs->mus.mut` vuelve a su valor) y abre el
  panel. Efecto secundario aceptado: dos mutaciones seguidas a menos de 0,3 s ya no son posibles. Si la secuencia encaja con el principio del código
  secreto (`B B A`), no se abre (el código manda; hay un test).
- **Un solo tipo de ajuste: valor entero con 0 = AUTO.** Todo es un `uint8_t` en `MusicStudio` y 27 bits en el guardado; `AUTO` y `FIJO` son
  "valor 0" y "distinto de 0": no hay un bit de candado aparte que pueda desincronizarse del valor. Ciclar los valores incluye `AUTO` al final, así
  que quitar un candado es dar la vuelta (o, en una lista larga como las progresiones, mantener B para ir hacia atrás por las filas y volver).
- **Lo fijo se aplica después de las tiradas de la semilla**, nunca en su lugar: así fijar un parámetro no desplaza a los demás (ni cambia su
  valor AUTO) y con todo en AUTO no hay diferencia con la alfa (el hash del PCM de 32 casos de 70 s y el de 2.048 canciones lo comprueban). Lo
  que sí hace falta volver a sortear (la familia de progresiones de otra escala, los golpes de un ritmo fijo) usa un segundo PCG32 sembrado con la
  semilla.
- **"Apagado" en el arpegio** es "sin arpegio": el bajo toca la fundamental en dos blancas (un pedal), no silencio (para eso están las voces).
- **El timbre fija las dos cuadradas** (melodía y bajo con el mismo ciclo de trabajo): fijarlo solo en una daba mezclas sin sentido y
  habría añadido otra fila. **Ritmo** son tres niveles (baja, media, alta) de la densidad del ruido, no un número de golpes: son los que ya
  usa el generador (la "intensidad de la batería" de 0 a 2) y suenan bien con cualquier semilla.
- **Silenciar es un fundido de la salida de la voz, no de la generación.** Lo contrario (no generar la voz) cambiaría el estado del LFSR del
  ruido o de las envolventes y la voz reactivada no volvería en su sitio; esto cumple "sin clics" (rampa de 16 ms), "sin alterar las
  otras voces" y "retomar sincronizado", y se comprueba muestra a muestra con `music_render_voices`.
- **Panel modal a casi toda la pantalla con un mini visualizador** (una barra horizontal por voz): los 6 renglones a 5x7 px y las progresiones
  más largas (`i-VII-III-VII`, 13 letras) no dejan sitio para ver el visualizador original alrededor (un test mide que cada fila cabe en 105 px
  con tres de hueco). La música sigue sonando y el panel late con ella. La fila `PROG` se llama así (no "PROGRESIÓN") por la misma razón.
- **Los avisos con el panel abierto salen abajo** (`Semilla #E8D5`, `Mantén A para el dado`): arriba taparían el título. Un toque sobre el dado no
  hace nada (solo avisa): mutar desde dentro del panel habría duplicado el dado.
- **Pitidos**: los de siempre (`FrameOut.tones`), que ya hacen que la música ceda; el Estudio no abre ningún camino de audio nuevo.
- **No entró:** guardar presets, grabar a WAV desde el llavero, mutar una sola voz y un número exacto de golpes del euclidiano por voz (ver la lista de "Pendiente para la beta").

### Decisiones de la fase 5

- **Dos ganchos nuevos en `World` en vez de simular dentro de `render`:** `frame` (cada fotograma con el mundo abierto, tras los gestos y
  con los botones pulsados) y `leave` (al salir del mundo por B, por una sorpresa que lo cambia o al dormirse; limpia lo efímero y
  devuelve el aviso). El minijuego necesita mover el estado cuando gana o pierde (colección, diario), cosa que `render` (que recibe el
  `GameState` como `const`) no puede, y necesita saber cuándo se deja a medias. Los demás mundos no los usan.
- **El lanzamiento hace TODOS los sorteos de una vez** (`fish_plan`: qué pica, de qué tamaño, cuánto tarda y la semilla de la pelea) a partir
  de **una** tirada de `gs->rng`. Es lo que el test `una sola tirada de gs->rng` verifica; así el universo avanza igual aunque la pelea
  dure más o menos, y se puede repetir un lanzamiento dada su semilla y el contexto.
- **Contexto de la pesca** (`fish_ctx`): hora, clima, luna y estación del *macro tick* actual del universo (no del reloj del fotograma),
  para que el estado decida, no el instante en que se pinta. **Luna llena = fase 4 de 8** (unas 3,7 noches seguidas cada 29,5 días); el
  legendario pide además que sea de noche.
- **El lanzamiento anota `EV_CAST` y no `EV_ITEM_USED`:** así la Sala puede comentar "la lombriz gastada" con su propia etiqueta y
  el diario no repite "Usaste Lombriz". La herramienta headless cuenta los `EV_CAST` como lombrices usadas. La preparada que no sea lombriz no se toca.
- **Cancelar:** B, una sorpresa que cambia de mundo o dormirse a mitad pierden la lombriz (ya estaba gastada) y, si el pez ya había picado
  (`BITE` o `FIGHT`), anotan `EV_FISH_LOST`. Si aún esperaba, no se anota ningún pez (nunca picó). El aviso es "Soltaste la caña: la
  lombriz se perdió." (al despertar, si fue el sueño). El código secreto deshace el lanzamiento junto con lo demás que hicieron sus A.
- **Quien no pulsa y quien mantiene A pierden siempre, por construcción** (rango del pez entre las zonas extremas de la barra, ver arriba): una
  primera versión con el pez en 420-940 dejaba ganar a quien mantenía A (el test lo destapó: 118 de 200 victorias) y se acotó el rango y las zonas.
- **Mantener A durante la pelea y A suelta dentro de los primeros 0,9 s de la tarjeta:** al soltar la tecla tras ganar, el toque no cierra la
  tarjeta sin querer. Tras una pausa larga entre dos fotogramas solo se recuperan 100 ms de pelea (no se "adelantan" pasos).
- **Sprites de 16x12** (en un recuadro de 16x16 con margen), 4 colores, uno por pez y basura; la estantería usa la versión reducida a 12x9 por vecino más
  cercano (se lee: el Koi sale con su borde dorado). Se añadieron 6 colores de agua y juncos al final de la paleta maestra.
- **Diario:** los textos de la pesca llevan una etiqueta corta del pez (`tag`, <= 8 letras) para caber junto a "hace 47 h"; y como "Nuevo:
  Estrella fugaz" (de la fase 2) ya se salía de la pantalla, **el diario recorta ahora lo que no cabe** (sin partir letras con tilde).
- **Colección:** los puestos de ficha se añaden al cursor solo con los peces que tienes; `goto_pet` del headless tiene un tope mayor (16).
  Hay un nuevo `cobertizo_on_fish` para los tests.
- **Director:** la Pesca queda tras la Sala y el Cobertizo al elegir qué abrir; las ausencias ("Salí") y las invasiones también pueden caer en
  ella. La spec (§6) ya pedía "la Gótica en la pesca".
- **Estado imposible = rechazado al cargar:** acciones del día por encima del límite del mundo y pesca incoherente se rechazan con el CRC
  bueno (como las demás invariantes de `state_valid`).
- **Frases:** 16 nuevas sobre la pesca; el test que renderiza cada frase con todos los huecos destapó dos que no cabían en la caja de texto y se acortaron.
- **`alpha` es un comando del headless** (y no un ejecutable aparte) para reutilizar sus gestos y relojes virtuales. Su opción `stock` (una
  lombriz extra cada mañana) es un atajo declarado para ejercitar la pesca sin esperar a que el universo dé lombrices; los números "sin ayuda" están al lado.
- **Bugs encontrados por el camino** (y arreglados): el mapa de sucesos de las frases no incluía los de pesca (el test de la Sala los vio mudos);
  el headless usaba un `Rng` sin sembrar (un bucle sin fin en `rng_below`) hasta que `fish` y `alpha` lo sembraron; un array de mi propio test de determinismo (40 acciones, hacían falta 48) se
  desbordaba y el test fallaba de forma rara (se agrandó); y el diario se salía de la pantalla con nombres largos (se recorta).
