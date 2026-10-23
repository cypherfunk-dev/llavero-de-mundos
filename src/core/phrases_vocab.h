/* Vocabulario del sistema de frases (spec §5). Fuente ÚNICA de nombres e ids:
 * lo lee el motor (los enums salen de aquí) y lo lee tools/phrases2bin.py (que extrae
 * las filas X(...) con una expresión regular), así que cada fila debe ir en una sola
 * línea lógica y con ese formato exacto. Para añadir una pregunta, un evento o un hábito
 * se añade una fila aquí; los ids no se reutilizan (van dentro del binario). */
#ifndef LLV_PHRASES_VOCAB_H
#define LLV_PHRASES_VOCAB_H

#define PV_ENUM(sym, id, str) sym = (id),
#define PV_NAME(sym, id, str) [sym] = (str),

/* Quién habla. Los residentes siempre están; los visitantes, a ratos. */
#define PV_SPEAKERS(X) \
    X(SPK_ABUELA, 1, "abuela") X(SPK_ROBOT, 2, "robot") X(SPK_GOTICA, 3, "gotica") \
    X(SPK_GAMER, 4, "gamer") X(SPK_BOOMER, 5, "boomer") X(SPK_GATO, 6, "gato") \
    X(SPK_SISTEMA, 7, "sistema")
enum { SPK_NONE = 0, PV_SPEAKERS(PV_ENUM) SPK_END };
#define SPK_COUNT SPK_END

/* Para qué sirve una frase (una sola por frase; sin etiqueta es PK_CHARLA). */
#define PV_KINDS(X) \
    X(PK_CHARLA, 0, "charla") X(PK_RELLENO, 1, "relleno") X(PK_SINACCIONES, 2, "sinacciones") \
    X(PK_CUARTA_PARED, 3, "cuarta_pared") X(PK_REBELDIA, 4, "rebeldia") X(PK_TIMBRE, 5, "timbre") \
    X(PK_INVASION, 6, "invasion") X(PK_PREGUNTA, 7, "pregunta") X(PK_SI, 8, "si") \
    X(PK_NO, 9, "no") X(PK_MENTIRA, 10, "mentira") X(PK_RETOMA, 11, "retoma") \
    X(PK_ESCENA, 12, "escena") X(PK_COMENTARIO, 13, "comentario") X(PK_GLITCH, 14, "glitch") \
    X(PK_GLITCH_OK, 15, "glitch_ok") X(PK_CARTEL, 16, "cartel") X(PK_MONEDA, 17, "moneda")
enum { PV_KINDS(PV_ENUM) PK_END };
#define PK_COUNT PK_END

/* Tipos de condición (cada frase lleva una lista; deben cumplirse todas). */
#define PV_TAGKINDS(X) \
    X(PT_TOD, 1, "tod") X(PT_WDAY, 2, "wday") X(PT_WEEKEND, 3, "weekend") X(PT_SEASON, 4, "season") \
    X(PT_MOON, 5, "moon") X(PT_DATE, 6, "date") X(PT_ABS_GT, 7, "abs_gt") X(PT_ABS_LT, 8, "abs_lt") \
    X(PT_LOOKS_GT, 9, "looks_gt") X(PT_LOOKS_LT, 10, "looks_lt") X(PT_STREAK_GT, 11, "streak_gt") \
    X(PT_AFF, 12, "aff") X(PT_RESP, 13, "resp") X(PT_SHAKEN, 14, "shaken") X(PT_BATT, 15, "batt") \
    X(PT_CHARGING, 16, "charging") X(PT_EVENT, 17, "event") X(PT_WEATHER, 18, "weather") \
    X(PT_GARDEN, 19, "garden") X(PT_PET, 20, "pet") X(PT_COLL_GT, 21, "coll_gt") X(PT_BAG, 22, "bag") \
    X(PT_HABIT, 23, "habit")
enum { PV_TAGKINDS(PV_ENUM) PT_END };
#define PT_COUNT PT_END

/* Franjas del día (PT_TOD). "noche" incluye la madrugada. El día del llavero empieza a las 05:00. */
enum { TOD_MADRUGADA = 0, TOD_MANANA, TOD_MEDIODIA, TOD_TARDE, TOD_NOCHE };
/* Fechas especiales (PT_DATE). El cumpleaños se rellena en la fase 7: aquí queda el gancho. */
enum { DATE_NONE = 0, DATE_HALLOWEEN, DATE_VIERNES13, DATE_ANIO_NUEVO, DATE_NAVIDAD, DATE_CUMPLE };

/* Palabras sueltas: palabra, tipo de condición y argumento. */
#define PV_WORDS(X) \
    X("madrugada", PT_TOD, 0) X("manana", PT_TOD, 1) X("mediodia", PT_TOD, 2) X("tarde", PT_TOD, 3) \
    X("noche", PT_TOD, 4) \
    X("domingo", PT_WDAY, 0) X("lunes", PT_WDAY, 1) X("martes", PT_WDAY, 2) X("miercoles", PT_WDAY, 3) \
    X("jueves", PT_WDAY, 4) X("viernes", PT_WDAY, 5) X("sabado", PT_WDAY, 6) X("finde", PT_WEEKEND, 0) \
    X("primavera", PT_SEASON, 0) X("verano", PT_SEASON, 1) X("otono", PT_SEASON, 2) X("invierno", PT_SEASON, 3) \
    X("luna_llena", PT_MOON, 4) X("luna_nueva", PT_MOON, 0) \
    X("halloween", PT_DATE, 1) X("viernes13", PT_DATE, 2) X("anio_nuevo", PT_DATE, 3) \
    X("navidad", PT_DATE, 4) X("cumple", PT_DATE, 5) \
    X("agitar", PT_SHAKEN, 0) X("bateria_baja", PT_BATT, 0) X("cargando", PT_CHARGING, 0) \
    X("afin_alta", PT_AFF, 1) X("afin_baja", PT_AFF, 0) \
    X("despejado", PT_WEATHER, 0) X("lluvia", PT_WEATHER, 1) X("viento", PT_WEATHER, 2) X("nieve", PT_WEATHER, 3) \
    X("jardin_vacio", PT_GARDEN, 0) X("jardin_lleno", PT_GARDEN, 1) X("sin_mascota", PT_PET, 0) \
    X("bolsa_vacia", PT_BAG, 0) X("bolsa_llena", PT_BAG, 1)

/* Sucesos del diario que los personajes comentan: evento:NOMBRE (el motor los traduce a EventKind). */
#define PV_EVENTS(X) \
    X(PE_MANZANA_CAE, 0, "manzana_cae") X(PE_MANZANA_PODRIDA, 1, "manzana_podrida") X(PE_BROTE, 2, "brote") \
    X(PE_ARBOL, 3, "arbol") X(PE_HONGO, 4, "hongo") X(PE_FLOR_RARA, 5, "flor_rara") X(PE_PAJARO, 6, "pajaro") \
    X(PE_SEMILLA_PAJARO, 7, "semilla_pajaro") X(PE_ABEJA, 8, "abeja") X(PE_LOMBRIZ, 9, "lombriz") \
    X(PE_RANA, 10, "rana") X(PE_LLUVIA, 11, "lluvia") X(PE_VIENTO, 12, "viento") X(PE_NIEVE, 13, "nieve") \
    X(PE_ARCOIRIS, 14, "arcoiris") X(PE_ESTRELLA, 15, "estrella") X(PE_ABONO, 16, "abono") \
    X(PE_DESCUBRIMIENTO, 17, "descubrimiento") X(PE_MASCOTA_CRECE, 18, "mascota_crece") \
    X(PE_MASCOTA_NACE, 19, "mascota_nace") X(PE_MASCOTA_CUIDADA, 20, "mascota_cuidada") \
    X(PE_MASCOTA_PALABRA, 21, "mascota_palabra") X(PE_ESPORAS, 22, "esporas") \
    X(PE_COMPOSTERA_COME, 23, "compostera_come") X(PE_BOLSA_LLENA, 24, "bolsa_llena") \
    X(PE_OBJETO, 25, "objeto") X(PE_VISITA, 26, "visita") X(PE_HABITO, 27, "habito") \
    X(PE_PASEO, 28, "paseo") X(PE_PACIENCIA, 29, "paciencia")
enum { PV_EVENTS(PV_ENUM) PE_END };
#define PE_COUNT PE_END

/* Hábitos (spec §3.3). Se activan y desactivan con una máscara de bits. */
#define PV_HABITS(X) \
    X(HAB_AGUA, 0, "agua") X(HAB_DORMIR, 1, "dormir") X(HAB_PAUSA, 2, "pausa") X(HAB_PANTALLA, 3, "pantalla") \
    X(HAB_SOL, 4, "sol") X(HAB_LLAMAR, 5, "llamar") X(HAB_MOVERSE, 6, "moverse")
enum { PV_HABITS(PV_ENUM) HAB_END };
#define HAB_COUNT HAB_END

/* Preguntas de sí o no (memoria de respuestas). Las seis primeras son las de los hábitos
 * y comparten número con ellos; moverse no pregunta (lo mide el podómetro). Máximo 16. */
#define PV_QUESTIONS(X) \
    X(Q_AGUA, 0, "agua") X(Q_DORMIR, 1, "dormir") X(Q_PAUSA, 2, "pausa") X(Q_PANTALLA, 3, "pantalla") \
    X(Q_SOL, 4, "sol") X(Q_LLAMAR, 5, "llamar") X(Q_PLANTAS, 6, "plantas") X(Q_TE, 7, "te") \
    X(Q_ALMA, 8, "alma") X(Q_MADRUGAR, 9, "madrugar") X(Q_JUEGOS, 10, "juegos") X(Q_TRISTE, 11, "triste") \
    X(Q_CASETE, 12, "casete") X(Q_MASCOTA, 13, "mascota")
enum { PV_QUESTIONS(PV_ENUM) Q_END };
#define Q_COUNT Q_END
#define LLV_Q_MAX 16

/* Mascotas (PT_PET): los números son los de PetKind (se comprueba en phrases.c). */
#define PV_PETS(X) \
    X(PETV_HONGO, 1, "hongo") X(PETV_SEMILLA, 2, "semilla") X(PETV_PIEDRA, 3, "piedra") \
    X(PETV_COMPOSTERA, 4, "compostera")
enum { PV_PETS(PV_ENUM) PETV_END };

#endif
