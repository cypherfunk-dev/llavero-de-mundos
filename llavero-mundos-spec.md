# Llavero de Mundos — Especificación v1

> **Un llavero habitado.** Seis mundos pequeños que comparten un mismo universo:
> un jardín que crece solo, personajes que te hablan y se acuerdan de ti, un
> reloj celeste, música generativa, pesca y un cobertizo donde vive una mascota
> absurdamente lenta. Lo miras un momento, algo ha cambiado, alguien te dice
> algo, y de vez en cuando pasa algo que no esperabas.

- **Estado:** fases 0, 2, 3 y 5 hechas en el simulador (motor, HAL, SDL2, Reloj, Jardín, bolsa, Cobertizo y mascota, La Sala con frases, Director, hábitos y bono de paciencia, y la Pesca con la colección completa de peces y sus fichas, con tests de determinismo y una prueba de 14 días por los 6 mundos: **versión alfa**); fase 4 (Música generativa) hecha en motor, mundo, HAL de audio y métricas, con el gate de oído pendiente; faltan el hardware real (fase 1), la carcasa y el ajuste de números (fase 6) y los regalos (fase 7); ver `README.md`.
- **Alcance:** primera unidad para mí; después unas pocas para regalar. No es un
  producto comercial (por ahora).
- **Vibe:** juguete noventero × solarpunk × cuántico × indie hacker.
- **Propósito:** fomentar la **paciencia** y los **buenos hábitos** sin
  sermones, con humor irónico y juguetón. Y que como llavero sea **bonito**.
- **Versión anterior:** `llavero-mundos-spec-v0.md`.
- **Última revisión:** 2026-10-19.

Las decisiones marcadas con 🔶 siguen abiertas. Los números marcados con 🧪 son
provisionales: se ajustan probando en el simulador y con el llavero encima.

---

## 1. Principios de diseño

1. **Universo compartido.** Lo que pasa en un mundo se nota en los demás: la
   manzana que cae en el jardín la comenta la Abuela, la lluvia cambia la escala
   de la música, la lombriz del jardín sirve de cebo para pescar.
2. **Escasez que da valor.** Pocas acciones al día, que se recargan con el
   tiempo. Cada pulsación importa y el llavero invita a volver, no a quemarlo.
3. **Sin culpa.** Nada muere por descuido. Como mucho, alguien se enfurruña o se
   queda "pensativo".
4. **Sorpresa dosificada.** Casi siempre pasa lo esperable; a veces, algo raro.
   Nunca dos sorpresas seguidas.
5. **Límites fijos.** Toda la memoria está acotada desde el diseño. El sistema
   no puede crecer hasta colapsar (ver §10).
6. **Offline y determinista.** Sin internet, sin IA dentro del llavero. Todo
   sale de semillas, tablas de datos y frases escritas a mano.
7. **Premia la paciencia, no la obsesión.** Lo contrario de una app: esperar da
   mejores resultados que mirar a cada rato (§3.2).
8. **Hábitos sin sermones.** Empuja con cariño e ironía, nunca regaña dos veces
   y cada hábito se puede desactivar (§3.3).

---

## 2. Controles

Dos botones (A y B) y el acelerómetro.

| Gesto | Qué hace |
|---|---|
| **B** (toque) | Siguiente mundo. *Nunca modifica la escena.* |
| **B** (mantener) | Mundo anterior |
| **A** (toque) | Acción principal del mundo. *Nunca saca de la escena.* |
| **A** (mantener) | Acción secundaria del mundo |
| **A + B** | Diario: "mientras no mirabas…" |
| **Agitar** | Despierta la pantalla (en algunos mundos provoca algo) |
| `B B A A B A B A` | Código secreto 🤫 |

**Al despertar**, el llavero no abre siempre el último mundo: el **Director**
(§7) abre el mundo donde ha pasado algo y marca con ❗ los que tienen novedades.

**Orden del ciclo con B:**

```
🌙 Reloj → 🌳 Jardín → 👵🤖 Sala → 🎵 Música → 🎣 Pesca → 🏚 Cobertizo → (Reloj)
```

---

## 3. Ritmo y escasez 🧪

Todos estos valores viven en un único archivo de configuración para poder
ajustarlos sin tocar la lógica.

| Parámetro | Valor inicial | Notas |
|---|---|---|
| Recarga de la bolsa | **+1 objeto cada 8 h** 🧪 | ~3 objetos al día |
| Capacidad de la bolsa | 5 objetos 🧪 | Si está llena, la recarga se pierde |
| Acciones por mundo y día | **2 como máximo** 🧪 | Jardín, Sala, Pesca |
| Acciones con la mascota | **1 al día** 🧪 | Es un anti-Tamagotchi |
| Reloj y Música | Sin límite | No generan estado persistente |
| Inicio del "día" | 05:00 🧪 | Como en los juegos: trasnochar no regala acciones |

Con 3 objetos al día y hasta 4 usos posibles (2 en el Jardín y 2 en la Pesca),
siempre hay que **decidir dónde usar cada objeto**.

**Sin acciones disponibles**, el mundo lo dice con gracia en lugar de bloquearse:

> *"El jardín descansa. Vuelve en 3 h 20 min."*
> Abuela: *"Paciencia. Las plantas no tienen prisa."*
> Robot: *"Mis reservas de pensamiento se han agotado. Como el universo, eventualmente."*

**Los eventos naturales no tienen límite:** el clima, el crecimiento y las
visitas de animales ocurren solos, también mientras el llavero duerme. La
escasez limita **tus** intervenciones, no la vida del mundo.

### 3.1 La bolsa de objetos

| Objeto | Efecto | Rareza |
|---|---|---|
| 🌧 Nube | Lluvia: crecimiento rápido, charcos | Común |
| 🌬 Viento | Cae la fruta, las semillas vuelan | Común |
| 🌱 Semilla | Planta algo al azar | Común |
| 🐦 Alpiste | Atrae un pájaro: come y deja semillas | Media |
| 🪱 Lombriz | Abono en el jardín, o cebo en la pesca | Media |
| ⭐ Deseo | Evento raro: estrella fugaz, eclipse, arcoíris | Rara |

- Usar una acción **gasta un objeto**.
- Por defecto se saca **un objeto al azar** ("Sacaste: 🌬 Viento").
- Si en el Cobertizo has **preparado** un objeto concreto, se usa ese.
- Algunos eventos también dan objetos (un pájaro deja una semilla, una lombriz
  aparece al llover), siempre que quepan en la bolsa.

### 3.2 Paciencia premiada

El llavero premia **esperar**, no mirar compulsivamente.

- **Bono de paciencia:** cuanto más tiempo pasa entre dos miradas (hasta un tope
  de ~12 h 🧪), más probable es que el catch-up traiga un evento raro.
- **Mirar muchas veces seguidas no da nada nuevo**, y los personajes lo notan:
  > Robot: *"Me miraste hace 2 minutos. No pasó nada. No va a pasar nada. Ve a vivir."*
  > Abuela: *"Las plantas crecen cuando no las miras. Como los hijos. Y las deudas."*
- **Lo bueno tarda:** la mascota de 100 días, el árbol de una semana, los
  mensajes con fecha de los regalos.
- **LED de aviso:** un LED diminuto parpadea de vez en cuando si pasó algo raro
  mientras no mirabas. Así sabes si vale la pena sacarlo sin encender la
  pantalla.

### 3.3 Buenos hábitos (sin sermones) 🧪

Reglas:

- El llavero solo "sabe" lo que puede saber de verdad (hora, pasos, tus
  respuestas). Lo demás lo **pregunta**, con humor.
- **Máximo 1–2 preguntas de hábitos al día.** Nunca insiste dos veces con lo
  mismo.
- **Cumplir un hábito ayuda al universo, pero no cumplirlo no castiga nada.**
- Cada hábito se activa o desactiva en la configuración (y en cada regalo).
- **Puedes mentir.** Robot: *"Detecto una mentira. No tengo sensores para eso,
  pero la detecto."*

| Hábito | Cómo lo sabe | Qué pasa en el universo |
|---|---|---|
| **Beber agua** | Pregunta sí/no | Si dices que sí, llueve un poco en el jardín sin gastar objetos |
| **Moverse** | **Podómetro** del IMU, sin hacer nada | Un paseo largo suma un objeto extra a la bolsa (máx. 1 al día) y alimenta la compostera |
| **Dormir** | Si lo miras de madrugada + pregunta por la mañana | De madrugada, los personajes aparecen en pijama y te mandan a la cama; una buena racha da un amanecer especial en el Reloj |
| **Respirar / pausa** | Ejercicio con la mascota: mantener A para inhalar, soltar para exhalar | La mascota se infla y se desinfla contigo y se pone contenta; sin límite |
| **Soltar el móvil** | Pregunta irónica | Robot: *"¿Cuántas horas de pantalla hoy? Yo no las cuento. Yo SOY pantalla."* |
| **Salir al sol** | Pregunta (v2: sensor de luz) | Florecen cosas en el jardín |
| **Llamar a alguien querido** | Pregunta semanal de la Abuela | *"¿Hace cuánto no llamas a tu madre? No respondas. Llama."* |

---

## 4. Los mundos

### 🌙 4.1 Reloj Celeste

El cielo muestra la hora real: el sol o la luna en su posición, el color del
cielo según la hora y la **fase lunar real** (calculada a partir de la fecha).
Los dígitos se forman con partículas.

- **A (toque):** cambia el estilo del reloj:
  1. **Celeste:** sol, luna y estrellas.
  2. **Dicho por un personaje:** *"Son casi las cinco. Hora del té."* (Abuela) ·
     *"17:42. Has consumido el 0,003 % de tu vida hoy."* (Robot)
  3. **Binario**, con LEDs falsos.
  4. **Cuántico:** varias horas superpuestas y borrosas que colapsan en la real.
  5. **Cuenta atrás** al próximo evento: luna llena, cumpleaños, fin de semana.
- **A (mantener):** fecha y estación.
- **Límite:** ninguno.
- Es el mundo más útil del día a día: hace que lleves el llavero encima.

### 🌳 4.2 Jardín

Ecosistema estilo Stardew Valley sobre una cuadrícula de casillas de 16 px.
Cada cosa es una **entidad con etapas** que cambian con el tiempo, el clima y lo
que tiene al lado.

**Ejemplo de cadena:**

```
árbol con fruta ──viento──▶ manzana en el suelo
manzana ──2 días──▶ podrida ──┬─ 40% ▶ brote ─5 días─▶ árbol joven ─▶ árbol
                              ├─ 30% ▶ hongo silvestre
                              └─ un pájaro se la come ▶ semilla en otro sitio
lluvia ──▶ charco ──▶ rana (rara) · todo crece más rápido
```

- **A (toque):** usa un objeto de la bolsa (aleatorio o el preparado).
- **A (mantener):** **lupa**. Recorre las entidades y muestra su estado
  (*"Manzana · se pudre en ~5 h"*). Sirve para descubrir las cadenas.
- **Límite:** 2 acciones al día.
- **Estaciones** según la fecha real.
- **Escala de tiempo** 🧪: una semilla tarda ~3–5 días reales en hacerse árbol.
- **Tope de 32 entidades.** La descomposición y el compost cierran el ciclo: el
  jardín se limpia solo por su propia ecología.
- **Las reglas viven en una tabla de datos**, no en el código:

```
# entidad   etapa     dura   siguiente
manzana     fresca    2d     podrida
manzana     podrida   1d     brote 40% | hongo 30% | nada 30%
brote       -         5d     arbol_joven
```

**Ideas de entidades:** semilla, brote, árbol joven, árbol, árbol con fruta,
manzana, manzana podrida, hongo silvestre, flor, abeja, pájaro, lombriz, charco,
rana, piedra con musgo, hojas secas, nieve.

### 👵🤖 4.3 La Sala (personajes)

**Residentes:**

- **Abuela Sol.** Solarpunk: plantas, té, compost, paneles solares en el tejado.
  Sabia, cariñosa y con algo de mala leche.
- **Unidad-7.** Robot existencialista. Se pregunta qué sentido tiene ser un
  llavero.

**Visitantes ocasionales:** la Gótica, el Gamer y el Boomer.
**Secreto:** el Gato de Schrödinger (aparece ~1 de cada 300 veces).

- **A (toque):** **dar pie**. El personaje dice algo o continúa la conversación.
- **A (mantener):** **tocar el timbre**. Entra otro visitante.
- **Límite:** 2 conversaciones nuevas al día 🧪. Después, frases de relleno
  (*"Ya te conté todo por hoy"*). Así el banco de frases dura meses.

**Preguntas de sí o no.** A veces te preguntan algo: **toque = sí,
mantener = no**. **Se acuerdan de tus respuestas.**

> Abuela: *"¿Has regado tus plantas esta semana?"* → no →
> tres días después: *"¿Y esas plantas? ¿Siguen vivas o ya son compost?"*

**Escenas a dos voces:**

> Robot: *"Si nadie me mira, ¿existo?"*
> Abuela: *"Si nadie te mira, riegas los tomates. Vamos."*

**Comentan el resto del universo** leyendo el diario (§9):

> Abuela: *"Vi que se pudrió otra manzana. Excelente para el compost."*
> Robot: *"Una manzana cayó. Yo también caeré algún día."*

Detalles del sistema de frases en §5.

### 🎵 4.4 Música generativa

Chiptune de tres voces al estilo NES: dos ondas cuadradas y una de ruido.

- **Escala** según la hora y el clima del jardín: pentatónica de día, menor o
  dórica de noche, más melancólica con lluvia.
- **Progresiones** de acordes desde una tabla (I–V–vi–IV y variantes).
- **Ritmos euclidianos:** repartir N golpes en M pasos.
- **Melodía** por cadenas de Markov sobre la escala, con motivos que se repiten
  y mutan poco a poco.
- **Visualizador:** barras u osciloscopio con la paleta del mundo.
- **A (toque):** mutar. Cambia la melodía y conserva el estilo.
- **A (mantener):** nueva semilla. Otra escala, otro tempo.
- **Límite:** ninguno. Solo consume batería mientras estás en este mundo.
- Necesita **altavoz** (ver §11).

### 🎣 4.5 Pesca

Pesca en el estanque del jardín con un solo botón: **mantén A** para subir la
barra y mantener al pez dentro de la zona.

- **Cada lanzamiento gasta una 🪱 lombriz** de la bolsa.
- **Límite:** 2 lanzamientos al día.
- Rareza según la hora, la estación y el clima: peces nocturnos, de lluvia y un
  pez legendario en luna llena.
- Lo que pescas va a la **colección** del Cobertizo y aparece en el diario.

### 🏚 4.6 El Cobertizo (último mundo)

- **Bolsa:** los objetos disponibles y la cuenta atrás al siguiente
  (*"+1 en 6 h 13 min"*).
- **Colección:** estantería con lo encontrado o pescado. Los huecos vacíos son
  siluetas "???", como en la Pokédex.
- **Mascota:** vive en el alféizar (§4.7).
- **A (toque):** mueve el cursor. Sobre la mascota: **respirar juntos**
  (ejercicio de respiración, sin límite).
- **A (mantener):** **prepara** el objeto seleccionado para la próxima acción
  del Jardín o de la Pesca, o cuida a la mascota si el cursor está sobre ella.

### 🍄 4.7 La mascota (anti-Tamagotchi)

Ridícula y lenta. No pide nada urgente, apenas hace cosas, y aun así le coges
cariño. **Una sola acción al día. Nunca muere.**

Se elige al empezar (y en los regalos, según la persona):

| Mascota | Ciclo (~100 días reales 🧪) | Rarezas | Conexión con el universo |
|---|---|---|---|
| **Hongo** | espora → punto → botón → pequeño → con sombrero → anciano con barba → suelta esporas y deja un hijo | Se mueve 1 px al día. Dice **una palabra a la semana** ("hm.") | Sus esporas hacen nacer hongos en el Jardín |
| **Semilla** | semilla → brote → plantita → bonsái → bonsái anciano | Se gira hacia el sol del Reloj | A veces da una 🌱 semilla para la bolsa |
| **Piedra** | No crece. Le sale musgo, luego liquen y, con el tiempo, una mariquita vive encima | "Peso: 120 g (estable)". Nunca dice nada. Nunca. | La Abuela la llama "la más sabia de la casa" |
| **Compostera** | Población de gusanos que crece despacio (3 → 4 → … gusanos) | Ficha absurda: "Productividad: +0,2 %" | Comen manzanas podridas y producen **abono** para la bolsa |

**Ficha solemne:**

```
HONGO · Gen 1 · Día 43
Altura: 7 px (+0,1 hoy)
Humor: contemplativo
Último pensamiento: "hm."
```

- **Cuidado (1 al día):** ofrecerle un objeto preparado o simplemente saludarle.
  Cada mascota tiene gustos (al hongo le encantan la 🌧 humedad y la manzana
  podrida; la lombriz le da igual).
- **Generaciones:** al completar el ciclo deja descendencia y el contador sube
  (Gen 2, Gen 3…).
- **Comentarios del resto:**
  > Robot: *"El hongo creció 0,1 píxeles. Yo nunca he crecido. ¿Quién es la
  > mascota de quién?"*

---

## 5. Sistema de personajes y frases

**Cada personaje es un paquete de frases con etiquetas de contexto.** Frases
escritas a mano, combinadas con una pequeña gramática para que no se repitan.

### 5.1 Contexto disponible (todo offline)

- **Tiempo:** hora, día de la semana, estación, fase lunar, fechas especiales
  (Halloween, viernes 13, Año Nuevo, cumpleaños de quien lo lleva).
- **Relación:** tiempo desde la última mirada, miradas de hoy, racha de días,
  respuestas a preguntas anteriores, afinidad.
- **Cuerpo del llavero:** agitado, boca abajo, batería, cargando, temperatura
  interna.
- **Universo:** eventos recientes del diario, estado del jardín, mascota,
  colección.

### 5.2 Formato de las frases

Un `.txt` por personaje, que un script convierte a binario para la flash:

```
[noche, abuela]          A estas horas solo trabajan las lechuzas y los hongos.
[ausencia>3d, robot]     {ausencia} sin verme. He reflexionado sobre la soledad. Conclusión: 404.
[agitar]                 ¿Puedes dejar de agitarme? Me mareo.
[luna_llena, gotica]     Esta noche no me esperes despierta.
[evento:manzana_podrida, abuela]  {evento}. Para el compost, como todo en la vida.
```

- Huecos como `{ausencia}` o `{evento}` los rellena la gramática.
- **Antirrepetición:** buffer de las últimas ~30 frases.
- **Voz:** el texto aparece letra a letra con un pitido por letra (estilo
  *Animal Crossing*); **cada personaje tiene un tono distinto**.
- **Afinidad sin culpa:** si lo miras a menudo, el personaje es más cariñoso; si
  lo ignoras, se vuelve pasivo-agresivo. Nunca castiga.
- **Objetivo de contenido:** 150–300 frases por residente y 50–100 por
  visitante. 🧪

### 5.3 Tono y humor

**Irónico y juguetón, un poco pícaro pero nunca grotesco, mezclado con humor
blanco y tierno.** Como una abuela con picardía o un amigo que te quiere y te
vacila.

- **Mezcla:** ~70 % humor blanco y tierno, ~30 % pícaro e irónico. 🧪
- **Sí:** ironía, doble sentido ligero, picardía de abuela, chistes de compost y
  gusanos, cuarta pared, autoburla del robot.
- **No:** escatología explícita, sexo explícito, humillar a nadie, reírse de
  grupos, crueldad. *Un guiño, no un chiste de bar a las 3 de la mañana.*
- **El chiste nunca castiga** por no cumplir un hábito: empuja con cariño.
- **Modo "apto para la abuela"** (solo humor blanco), configurable, útil para
  algunos regalos.

| Personaje | Blanco | Pícaro |
|---|---|---|
| **Abuela Sol** | *"Bebe agua. Eres 60 % agua y 40 % excusas."* | *"A tu edad yo tenía tres novios y un huerto. El huerto sigue vivo."* |
| **Unidad-7** | *"Caminaste 6.000 pasos. Yo no tengo piernas. Estoy orgulloso y un poco celoso."* | *"Me dicen que tengo un puerto USB-C. No sé qué hacer con esa información."* |
| **Compostera** | *"Los gusanos agradecen tu manzana podrida. Bueno, uno. Los otros duermen."* | *"Los gusanos se están reproduciendo. Dales privacidad, por favor."* |
| **Hongo** | *"hm."* | Ficha: *"Estado: esporulando. No mires."* |
| **Gótica** | *"Bebe agua. Las ojeras te quedan bien; la deshidratación, no."* | *"Me encanta la noche. Pero tú a las 3 de la mañana mirando un llavero… eso ya es otra cosa."* |
| **Gamer** | *"Hidratación: +10 HP."* | *"Racha de 4 días. Ni en mis relaciones duro tanto."* |
| **Boomer** | *"En mis tiempos el agua salía del grifo y no le hacíamos fotos."* | *"¿Paciencia? Yo esperé tres años a que me dijeran que sí. Y luego se me olvidó la pregunta."* |

---

## 6. Cosas inesperadas

| Tipo | Ejemplos |
|---|---|
| **Glitches falsos** | "CARTUCHO SUCIO. SOPLA." (se arregla agitando) · arranque falso de consola noventera · "INSERT COIN" |
| **Ausencia** | Mundo vacío con un cartel "Salí. Vuelvo en 5 min." |
| **Invasiones** | Un visitante aparece en otro mundo (la Gótica en la pesca) |
| **Cuarta pared** | "Me has mirado 15 veces hoy. Esto ya es obsesión." · "Son las 3:47. ¿Todo bien?" |
| **Calendario** | Halloween gótico, confeti de píxeles en tu cumpleaños, pez legendario en luna llena |
| **Colapso cuántico** | Al despertar, varias versiones del mundo superpuestas antes de quedarse con una |
| **Rebeldía** | Pulsas B y el personaje dice "No. Me gusta aquí." |
| **Secreto** | `B B A A B A B A` invoca al Gato de Schrödinger |

---

## 7. El Director de Sorpresas

En cada mirada tira un dado 🧪:

- **~80 %:** comportamiento normal y frase según el contexto.
- **~15 %:** comentario sobre algo que pasó en el universo.
- **~4 %:** sorpresa (§6).
- **~1 %:** algo muy raro.

Reglas:

- **Nunca dos sorpresas seguidas.**
- **Seguro de pena:** si pasa un día sin sorpresas, la siguiente mirada trae una.
- Elige qué mundo abrir al despertar (§2).

---

## 8. Regalos

- **Personalización por persona:** nombre, cumpleaños, mascota elegida,
  visitantes favoritos y bromas internas.
- ⭐ **Mensajes con fecha de apertura:** quien regala precarga mensajes que se
  desbloquean en una fecha, al cumplir N miradas o en el cumpleaños.
  > *"Hoy hace un año que te regalé esto."*
- Un script genera el "cartucho" de cada regalo (frases + configuración) y lo
  carga por USB.

---

## 9. Arquitectura de software

### 9.1 Estructura

```
          ┌──────────── Estado global ─────────────┐
          │ hora · fecha · luna · estación · clima │
          │ bolsa · colección · mascota · acciones │
          │ memoria de respuestas · afinidad       │
          └───▲───────────────▲──────────────▲─────┘
              │ leen/escriben │              │
   Reloj ── Jardín ── Sala ── Música ── Pesca ── Cobertizo
                │                         │
                └── Bus de eventos ──▶ Diario ──▶ personajes, música, director
```

- **Motor en C portable** con una **HAL** de 5 piezas: `display`, `input`,
  `clock`, `storage`, `sound`.
- Dos destinos: **simulador** (SDL2 en el ordenador, y web con Emscripten) y
  **llavero** (RP2350).
- El simulador incluye una tecla de **máquina del tiempo** (+1 h, +1 día,
  +1 semana) para probar ausencias, fechas y ritmos sin esperar.

### 9.2 Interfaz de mundo

```c
typedef struct {
    const char *name;
    uint8_t     daily_actions;                       // 0 = sin límite
    void (*init)(GameState *gs, uint32_t seed);
    void (*tick)(GameState *gs);                     // paso fijo, determinista
    void (*catch_up)(GameState *gs, uint32_t minutes);
    void (*render)(const GameState *gs, Framebuffer *fb);
    void (*on_tap)(GameState *gs);
    void (*on_hold)(GameState *gs);
} World;
```

`GameState` se guarda y carga entero; los mundos no gestionan su propia
persistencia.

### 9.3 Determinismo y "mientras duerme"

- **No** se simula en segundo plano: el chip duerme profundo.
- Estado del mundo = `f(semilla, tiempo transcurrido)`.
- **Tick fijo** con aritmética entera o de punto fijo y un PRNG propio (PCG32).
  Nada de `float dt`: rompería la coincidencia entre lo simulado en vivo y lo
  recalculado al despertar.
- **Dos escalas:** micro (visual, mientras miras) y macro (catch-up al
  despertar, ticks de 10 min, tope de 7 días, eventos raros muestreados por
  Poisson).
- **Colapso:** al despertar se mezcla entropía del hardware (oscilador ROSC)
  para elegir entre ramas posibles, y se muestra la animación de colapso.

---

## 10. Límites de memoria

Nada puede crecer sin límite. No hace falta "borrar eventos" a mano.

| Qué | Cómo se limita | Tamaño aprox. |
|---|---|---|
| Entidades del jardín | Arreglo fijo de 32; si está lleno, no nace nada | ~0,5 KB |
| Diario | Buffer circular de 64 eventos: el nuevo pisa al más viejo | ~0,5 KB |
| Bolsa, colección | Contadores, no listas | <0,1 KB |
| Memoria de personajes | Bits (sí/no por pregunta) + afinidad | <0,1 KB |
| Frases recientes | Buffer circular de 30 hashes | ~0,1 KB |
| **Estado completo** | Se guarda en flash al dormir (LittleFS) | **<4 KB** |
| Framebuffer | Indexado, 4 bits por píxel (§12) | ~8 KB |

- **Catch-up:** ~1000 ticks × 32 entidades como máximo → milisegundos.
- **Flash:** ~20 guardados al día con reparto de escrituras → décadas.
- **Sin `malloc`:** todo en arreglos estáticos.

### 10.1 Presupuesto de flash (4 MB en RP2350-Zero / Pico 2)

| Contenido | Cálculo | Tamaño |
|---|---|---|
| Firmware (motor + 6 mundos) | — | ~300–400 KB |
| Frases y diálogos | ~5.000 frases × ~70 B, comprimidas ~50 % | ~175 KB |
| Sprites | 16×16 a 2 bits/píxel = 64 B por frame; 1.000 frames | ~64 KB |
| Tiles de fondo | 8×8 a 2 bits = 16 B por tile; 2.000 tiles | ~32 KB |
| Ilustraciones completas | 120×120 a 4 bits = 7 KB; 50 escenas | ~350 KB |
| Fuente, paletas, tablas de reglas, música | — | ~30 KB |
| Partición LittleFS (estado, regalos, frases nuevas por USB) | — | 512 KB |
| **Total** | | **~1,5 MB de 4 MB** |

- Los assets se leen **directamente de la flash** (memoria mapeada, XIP): no
  ocupan RAM.
- La música es generativa (sin samples de audio), así que apenas ocupa.
- **Si algún día falta:** placa RP2350 con 16 MB de flash, o microSD en v2.

---

## 11. Hardware v1 (prototipo personal)

| Parte | Elección | Notas |
|---|---|---|
| **Cerebro** | RP2350 (Pico 2 o placa todo-en-uno) | Su temporizador sigue contando mientras duerme; 520 KB de RAM |
| **Pantalla** | 🔶 Ver §12 | |
| **Hora** | Temporizador AON del RP2350; si no basta, RTC RV-3028 | Imprescindible para el catch-up |
| **Movimiento** | IMU con **podómetro integrado**: LSM6DS3TR-C (fácil de encontrar en módulo) | Agitar, boca abajo y contar pasos mientras el chip duerme |
| **Botones** | A + B | |
| **LED de aviso** | LED diminuto, parpadeo breve | "Pasó algo raro" sin encender la pantalla (§3.2) |
| **Audio** | Altavoz de ~15 mm por PWM + transistor (alternativa: MAX98357A) | Un piezo no basta para la música |
| **Batería** | LiPo 300–500 mAh con protección | En carcasa rígida, alejada de las llaves |
| **Carga** | USB-C (en la placa o con un TP4056) | |
| **Almacenamiento** | Flash interna + LittleFS; frases y regalos por USB | Sin microSD en v1 |
| **Carcasa** | Impresa en 3D, resina translúcida, tornillos | Estética Game Boy Color "Atomic Purple" |

**Camino corto:** buscar una placa todo-en-uno RP2350 con LCD, IMU y cargador
(Waveshare tiene varias). **Antes de comprar, verificar el modelo exacto**: tipo
de pantalla, IMU, cargador y si trae RTC.

### 11.1 Energía

- Duerme casi siempre; despierta con botón o al agitar; vuelve a dormir a los
  ~10–30 s.
- Pantalla con retroiluminación apagada (power-gating) al dormir.
- **Objetivo:** ≥ 4 días con uso normal (cargar una vez por semana es aceptable
  para un juguete personal). 🧪 Se mide en la fase 1.

---

## 12. Dirección de arte: gráficos estilo Game Boy Color

La meta: que **se vea bonito como un juego de Game Boy Color** y que el llavero,
como objeto, den ganas de llevarlo colgado.

### 12.1 Referencias

- **Pokémon Oro/Plata/Cristal:** paletas que cambian según la hora del día,
  cajas de texto con borde, menús sencillos.
- **Harvest Moon GBC:** el abuelo de Stardew Valley. Referencia directa para el
  Jardín y el Cobertizo.
- **Zelda: Link's Awakening DX:** casillas de 16 px muy legibles, animaciones
  de 2 frames con mucha vida.
- **Tamagotchi:** expresividad con muy pocos píxeles (para la mascota).

### 12.2 Reglas del estilo

- **Resolución lógica pequeña** (~120×120 o 120×140) escalada ×2: píxeles
  gordos y nítidos.
- **Sprites de 8×8 y 16×16**, con **4 colores por paleta** (3 + transparente);
  8 paletas para el fondo y 8 para los sprites. La restricción da el carácter.
- **Animaciones de 2–4 frames.** El encanto está en el movimiento sencillo
  (hojas que se mecen, la mascota que parpadea).
- **Dithering** para degradados: cielos del Reloj, atardeceres, niebla.
- **Colores "lavados"** como los de la GBC real: negros algo elevados y
  saturación contenida.
- **Paletas según la hora**, como en Pokémon Cristal: mañana, día, tarde, noche.
- **Texto en cajas con borde**, fuente pixel propia de 8×8 y efecto máquina de
  escribir con pitido por letra.
- **Efecto LCD opcional:** rejilla sutil entre píxeles y un ligero arrastre de
  imagen.

| Mundo | Paleta base |
|---|---|
| 🌙 Reloj | Azules profundos de noche, naranjas y rosas al amanecer |
| 🌳 Jardín | Verdes y marrones; cambia con la estación |
| 👵🤖 Sala | Cálidos: madera, té, lámpara |
| 🎵 Música | Neón sobre fondo oscuro |
| 🎣 Pesca | Azules y turquesas; reflejos con dithering |
| 🏚 Cobertizo | Madera, terracota, verde de maceta |

### 12.3 El llavero como objeto bonito

- **Carcasa translúcida** en colores de la época: morado atómico, verde kiwi,
  turquesa, frambuesa, amarillo. Cada regalo puede llevar un color distinto.
- **Botones A/B** redondos y cóncavos, de color contrastado.
- **Marco oscuro alrededor de la pantalla** con un pequeño rótulo propio
  (p. ej. "LLAVERO · COLOR"). Sin marcas de Nintendo, sobre todo en los regalos.
- **LED de aviso** junto a la pantalla, como el LED de encendido de la GBC.
- **Etiqueta tipo cartucho** en la parte trasera con el nombre del dueño
  (ideal para regalos).
- **Tornillos visibles** (reparable, solarpunk), anilla metálica y una correa
  corta.
- **Tamaño objetivo:** ~45×55×14 mm 🧪. Que quepa en el bolsillo con las llaves.

### 12.4 Pantalla física

El estilo GBC es casi cuadrado (10:9). **La redonda queda descartada**: recortaría
las escenas.

| Opción | Resolución lógica | Notas |
|---|---|---|
| **IPS 1.54" 240×240** | 120×120 ×2 | Más compacta: mejor llavero |
| **IPS 1.69" 240×280** | 120×140 ×2 | Algo más grande; deja sitio para la barra de estado |
| MIP a color reflectiva (v2) | — | Sin luz propia y casi sin consumo, pero solo 8 colores |

Una barra de ~12 px muestra la hora, la bolsa y las acciones restantes.

---

## 13. Arte

- **Primero:** packs CC0 de Kenney como relleno.
- **Después:** sustitución progresiva por sprites propios en Aseprite o
  Pixelorama (gratuito).
- **Prioridad de sprites:** jardín (árbol en 4 etapas, manzana sana y podrida,
  hongo, pájaro, lombriz), las 4 mascotas, la Abuela y Unidad-7, iconos de la
  bolsa.

---

## 14. Fases

| Fase | Contenido | Gate (criterio para pasar a la siguiente) |
|---|---|---|
| **0** ✅ | Motor, simulador SDL, HAL, controles, estado global con límites fijos, look GBC y **Reloj** | El simulador corre, el Reloj se ve bonito y hay test de determinismo |
| **1** | Hardware en mano: placa, botones, altavoz, IMU, sueño y despertar | Aguanta ≥ 4 días con uso normal |
| **2** ✅ | **Jardín + bolsa + Cobertizo + mascota** | Tras una semana en el simulador, el jardín sigue sorprendiendo |
| **3** ✅ | **La Sala**: personajes, preguntas, memoria, diario, Director, **hábitos** y bono de paciencia | Te hace reír al menos una vez al día, y alguna vez te hace beber agua |
| **4** ◐ | **Música** generativa (motor, mundo, audio y métricas hechos; falta el gate de oído) | Suena agradable 9 de cada 10 semillas |
| **5** ✅ | **Pesca** y colección completa | La cadena lombriz → pez → colección funciona |
| **6** | Carcasa, dos semanas de uso diario y ajuste de los números 🧪 | Lo sigues mirando el día 14 |
| **7** | Regalos: personalización y mensajes con fecha | — |

---

## 15. Decisiones abiertas

- [ ] 🔶 **Pantalla:** IPS 1.54" (más compacta) vs. 1.69" (más espacio) (§12.4).
- [ ] 🔶 **Color de la carcasa** de la primera unidad (§12.3).
- [ ] Qué hábitos activar por defecto (§3.3).
- [ ] 🔶 **Placa concreta:** todo-en-uno vs. Pico 2 + módulos sueltos (§11).
- [ ] 🧪 Afinar en pruebas: recarga de la bolsa, acciones al día, velocidad del
      jardín, ciclo de la mascota, probabilidades del Director.
- [ ] Nombre del llavero (y de cada mascota).
- [ ] ¿Más visitantes en la Sala? ¿Más minijuegos de un botón?

## 16. Ideas para v2

- Pantalla MIP con modo ambiente "siempre encendido".
- Célula solar + cosechador (BQ25570); la luz real hace crecer el jardín.
- **Entrelazamiento:** dos llaveros emparejados por infrarrojos comparten
  semilla e intercambian criaturas.
- Mundos como "cartuchos" de datos por USB; scripts en Lua más adelante.
- Modo Tamagotchi completo como mundo propio.
