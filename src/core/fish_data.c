/* Las especies de la Pesca como tabla de datos (spec §4.5, §5.3): la lógica (fish.c) no sabe de peces
 * concretos. Para añadir uno, se añade una fila aquí, su sprite en fish_art.c y su hueco en la
 * colección (state.h) si es pez de verdad. Los números marcados 🧪 se afinan jugando.
 *
 * Rareza (peso) y condiciones: noche, lluvia, estación y luna llena. Con lluvia, además, salen menos
 * basuras y más peces raros (regla general de fish_weight: "llueve, mejores peces"). */
#include "fish.h"

#include "garden.h"

#define OTONO_INV (SEAS(SEASON_AUTUMN) | SEAS(SEASON_WINTER))

const FishInfo FISH_TABLE[FISH_TOTAL] = {
    /* ---- peces: los 6 huecos de la colección (COLL_PEZ0 + n) ---- */
    {.name = "Pez Lunes", .tag = "P. Lunes", .humor = "ojeroso",
     .quip = "Pica los lunes. Los otros días también, con menos ganas.",
     .weight = 100, .diff = 2, .temper = FT_CALM, .smin = 12, .smax = 28, .seasons = SEAS_ALL,
     .boost_pct = 100},
    {.name = "Carpa Resignada", .tag = "Carpa", .humor = "zen sin querer",
     .quip = "Lleva años esperando el anzuelo. Se rindió con mucha elegancia.",
     .weight = 80, .diff = 3, .temper = FT_LAZY, .smin = 25, .smax = 60, .seasons = SEAS_ALL,
     .boost_if = FC_DAY, .boost_pct = 130},
    {.name = "Trucha de Escarcha", .tag = "Trucha", .humor = "friolera",
     .quip = "Solo sale con fresco. El calor, dice, es de gente sin carácter.",
     .weight = 30, .diff = 5, .temper = FT_JUMPY, .smin = 20, .smax = 45, .seasons = OTONO_INV,
     .boost_if = FC_WINTER, .boost_pct = 200, .gift_item = ITEM_VIENTO, .gift = FG_JOY},
    {.name = "Bagre Insomne", .tag = "Bagre", .humor = "trasnochador",
     .quip = "A las tres de la mañana está más despierto que tú. Y sin café.",
     .weight = 45, .diff = 5, .temper = FT_ERRATIC, .smin = 30, .smax = 80, .need = FC_NIGHT,
     .seasons = SEAS_ALL, .boost_pct = 100, .gift_item = ITEM_SEMILLA, .gift = FG_JOY},
    {.name = "Pez Paraguas", .tag = "Paraguas", .humor = "previsor",
     .quip = "Sale con lluvia y lleva paraguas. Nadie le dijo que ya está mojado.",
     .weight = 40, .diff = 4, .temper = FT_DARTER, .smin = 10, .smax = 30, .need = FC_RAIN,
     .seasons = SEAS_ALL, .boost_pct = 100, .gift_item = ITEM_NUBE, .gift = FG_JOY},
    {.name = "Koi Lunar", .tag = "Koi", .humor = "poético",
     .quip = "Solo nada con luna llena. Concede deseos, casi nunca el tuyo.",
     .weight = 30, .diff = 8, .temper = FT_ERRATIC, .smin = 80, .smax = 140,
     .need = FC_NIGHT | FC_FULLMOON, .seasons = SEAS_ALL, .boost_pct = 100,
     .gift_item = ITEM_DESEO, .gift = FG_JOY},
    /* ---- basuras simpáticas: no cuentan en la colección ---- */
    {.name = "Bota Vieja", .tag = "Bota", .humor = "extraviada", .junk = 1,
     .quip = "Una bota. Del pie izquierdo. Siempre es del izquierdo.",
     .weight = 8, .diff = 1, .temper = FT_LAZY, .seasons = SEAS_ALL, .boost_if = FC_RAIN, .boost_pct = 100},
    {.name = "Lata Oxidada", .tag = "Lata", .humor = "con historia", .junk = 1,
     .quip = "Una lata con pasado. Nadie quiere contar cuál.",
     .weight = 7, .diff = 1, .temper = FT_LAZY, .seasons = SEAS_ALL, .boost_pct = 100},
    {.name = "Alga Enredada", .tag = "Alga", .humor = "enredada", .junk = 1,
     .quip = "Un alga. Para la compostera es un manjar; para ti, un lío.",
     .weight = 8, .diff = 1, .temper = FT_CALM, .seasons = SEAS_ALL, .boost_pct = 100, .gift = FG_COMPOST},
    {.name = "Calcetín Solitario", .tag = "Calcetín", .humor = "desparejado", .junk = 1,
     .quip = "Por fin lo encuentras. Su pareja sigue en la lavadora.",
     .weight = 4, .diff = 1, .temper = FT_CALM, .seasons = SEAS_ALL, .boost_pct = 100},
};
