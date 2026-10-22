# Llavero de Mundos Generativos — Especificación v0

> Prototipo personal. Un llavero con pantalla que no es un juego, sino una
> **"consola de juguetes generativos"**: varios mundos vivos que hacen su cosa
> solos y que miras de vez en cuando. Como un cartucho de PICO-8 o un demoscene
> de bolsillo.

Estado: **planificación**. Este documento recoge lo decidido y lo que queda
abierto. Las decisiones marcadas con 🔶 están pendientes.

---

## 1. Concepto

- No hay "partida" ni control directo: son mundos autónomos que observas.
- La gracia está en **cambiar de mundo** (rejugable para siempre) y en que
  cuando vuelves a uno **ha cambiado** respecto a la última vez.
- Estética: **pixel art** y paisajes.

### Los mundos (ampliable)

| Mundo        | Qué ves                                             | Botón A (acción)              |
|--------------|-----------------------------------------------------|-------------------------------|
| **Vida**     | Personaje camina, habla con NPC, juega (vive)       | Interactuar / darle un toque  |
| **Música**   | Visualizador + chiptune generativo                  | Regenerar melodía (semilla)   |
| **Figuras**  | Formas abstractas moviéndose, paleta cambiante      | Nuevo patrón / explotarlas    |
| **Paisaje**  | Escena generada, ciclo día-noche, clima             | Forzar un evento              |

Primer mundo a implementar: **Figuras** (el más fácil, se ve vivo enseguida).
Luego **Vida**. La **Música** al final (depende del audio, ver §6).

---

## 2. Gramática de los dos botones

Regla de oro para que se entienda sin manual:

- **Botón B — "cambiar mundo"**: siempre lo mismo, salta entre escenas.
  *Nunca modifica la escena.*
- **Botón A — "acción"**: contextual, cambia de significado según el mundo.
  *Nunca saca de la escena.*

(Opcional) **Agitar** para despertar la pantalla — ver §5.

---

## 3. Arquitectura de software

Cada mundo es un **módulo intercambiable** que implementa una interfaz común:

```c
typedef struct {
    void (*init)(uint32_t seed);   // nace el mundo
    void (*update)(float dt);      // avanza la simulación
    void (*render)(void);          // dibuja el frame
    void (*on_action)(void);       // botón A
} World;
```

- Un array de `World` + un índice que avanza con el botón B.
- Permite añadir mundos sin tocar el resto. Igual que PICO-8 por dentro.

### Simulación "mientras duerme" = fast-forward determinista

**No** se simula en segundo plano (eso obligaría a despertar la CPU y mata la
batería). En su lugar:

- Estado del mundo = `f(semilla, tiempo_transcurrido)`, **determinista**.
- Al despertar se calcula de golpe qué habría pasado desde la última vez.
- Resultado: se duerme profundo (µA) y aun así "ha cambiado todo" al volver.

---

## 4. Autonomía: objetivo 4–7 días

La pantalla a color encendida es el gran consumidor; lo que manda es **cuánto
tiempo está encendida**, no el tamaño de la batería.

Cuentas aproximadas:

- **Despierto** (CPU + pantalla + retro): ~60–80 mA.
- **Dormido** (RP2040 `dormant`, bien diseñado): ~0,3–2 mA.
- Uso ~20 veces/día × 20 s ≈ **7 mAh/día** despierto.
- Dormido 24 h a 1 mA ≈ **24 mAh/día**. → **El sueño domina.**

Con LiPo ~500–700 mAh y sueño < ~2 mA, los 7 días salen con holgura.

**Consecuencia:** diseño "mayormente dormido", despierta por botón o por agitar,
y se vuelve a dormir a los ~10–30 s.

---

## 5. 🔶 DECISIÓN CLAVE: pantalla

Esta decisión condiciona el resto del proyecto.

| Opción | Pros | Contras |
|---|---|---|
| **ST7789 / GC9A01 color IPS** (1.3–1.69", 240×240/280) | Color, nítida, barata (~3–6 €), la RP2040 la mueve de sobra por SPI | La retro se come la batería → obliga a dormir agresivo |
| **Sharp Memory LCD** (la de la Playdate) | Mantiene imagen a ~10 µA, reflectiva (sin retro), refresca para animar, estética preciosa; autonomía de semanas casi gratis | **Monocroma** (la de color es cara/rara) |
| **E-ink** | Consumo casi nulo parado | Refresco 0,3–2 s + parpadeo → mata la sensación de "vivo". **Descartada** para animación |

- Variante bonita de color: **GC9A01 redonda (1.28")** → mundo circular tipo
  ventanita/ojo.
- La Sharp Memory LCD encaja clavada con el pixel art, a cambio del color.

**Pendiente:** color IPS + dormir agresivo  **vs.**  Sharp Memory LCD mono.

---

## 6. 🔶 Audio (mundo Música)

Los otros mundos son solo píxeles; la música obliga a sacar sonido y toca el
hardware del llavero.

- **Chiptune por PWM** (piezo / micro-altavoz) → ondas cuadradas estilo Game
  Boy. Barato, encaja con el pixel art. **Recomendado.**
- **DAC I2S** (MAX98357) → suena de verdad, pero añade chip, altavoz y consumo.

Para no castigar la batería: sonido **corto y a demanda** (suena al pulsar A),
no en bucle. O que la música sea más visualizador que "pita a veces".

**Pendiente:** ¿altavoz propio sí/no? ¿o solo visualizador?

---

## 7. Almacenamiento: tarjeta microSD

Para **sprites, tilesets, paletas, diálogos, definiciones de mundo, patrones de
música**. Cambiar de mundo = cargar assets de otra carpeta.

- Librería: **FatFS** (ChaN). Comparte bus SPI con la pantalla (CS distinto; a
  la RP2040 le sobran buses + PIO).
- **"Lógica de negocio" en la SD**, dos niveles:
  1. **Datos que describen comportamiento** (máquinas de estados,
     probabilidades, tablas de eventos en JSON/binario) que el motor en C
     interpreta. → *Empezar por aquí.*
  2. **Scripts reales**: embeber **Lua** (hay port a RP2040) o un mini-VM
     propio → añadir mundos sin reflashear. → *Evolución futura.*
- ⚠️ La SD tira 20–100 mA al acceder y algunas fugan en reposo →
  **power-gate (transistor) y desmontar al dormir**. Nunca energizada durmiendo.

---

## 8. Boceto de hardware (prototipo, sin miniaturizar)

| Parte | Elección |
|---|---|
| **Cerebro** | Pico / módulo RP2040 + breakout con headers |
| **Pantalla** | ST7789 color *o* Sharp Memory LCD (ver §5) |
| **Batería** | LiPo ~500–700 mAh |
| **Carga** | TP4056 (USB-C) o PowerBoost con protección |
| **Botones** | A (acción) + B (mundo) |
| **Despertar** | Acelerómetro LIS3DH *o* tilt-switch (agitar) |
| **SD** | Módulo microSD + transistor para power-gate |
| **Audio** | Piezo/altavoz por PWM (si se mantiene, §6) |

Notas RP2040: doble Cortex-M0+ @ 133 MHz (hasta ~250), 264 KB RAM, USB nativo,
PIO. Sin WiFi/BT. RAM y CPU sobran para sprites + simulación.

---

## 9. Plan por fases

**Mundo Figuras**
- v0: formas generadas por semilla moviéndose + cambio de paleta. Render en
  pantalla, sistema de mundos + botón B funcionando.

**Mundo Vida**
- Personaje (sprite) sobre tiles generados por semilla, ciclo día/noche.
- Máquina de estados (deambula, duerme, come, huye, explora) con transiciones
  probabilísticas → la "historia" emerge.
- Eventos raros de baja probabilidad por tick (meteoritos, aparece otra
  criatura, cambia estación, encuentra objeto, eclipse).

**Persistencia + sueño**
- Guardar estado en flash/SD al dormir. Fast-forward determinista al despertar
  (§3). Despertar por agitar/botón.

**Música** (si procede, según §6).

---

## 10. Decisiones abiertas (resumen)

- [ ] 🔶 **Pantalla**: color IPS vs. Sharp Memory LCD mono (§5).
- [ ] 🔶 **Audio**: altavoz propio vs. solo visualizador (§6).
- [ ] Lógica en SD: datos primero; ¿Lua más adelante? (§7).
- [ ] Factor forma / tamaño final del llavero (tras validar el proto).
- [ ] Set inicial de mundos más allá de los cuatro base.
