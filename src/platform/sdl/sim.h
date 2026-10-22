/* Parte específica del simulador SDL2 (lo que no es HAL): configuración, máquina del
 * tiempo, capturas y efecto LCD. */
#ifndef LLV_SIM_H
#define LLV_SIM_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *save_path; /* archivo de guardado */
    int scale;             /* ampliación de la ventana */
    bool lcd;              /* rejilla LCD (tecla L) */
    bool mute;             /* sin sonido */
    const char *shot_path; /* captura al terminar (con --frames) */
} SimConfig;

/* Desfase acumulado (máquina del tiempo, spec §9.1) sobre la hora real, en segundos. */
int64_t sim_warp_total(void);
/* Segundos añadidos desde la última llamada (0 si ninguno); 'label' recibe un texto corto. */
int64_t sim_take_warp(char *label, int n);
/* Escribe la captura pendiente (tecla F12/P), si la hay, con el último fotograma. */
void sim_write_shot(const char *path);

#endif
