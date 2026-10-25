/* El Estudio de la música: modelo de la tabla de parámetros. Ver studio.h. */
#include "studio.h"

#include <stdio.h>

static const char *const ROW_NAME[SR_COUNT] = {
    "ESCALA", "TÓNICA", "TEMPO", "ARPEGIO", "PROG.", "TIMBRE", "RITMO", "MELODÍA", "BAJO", "RUIDO", "DADO",
};
static const char *const NOTE_NAME[12] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa", "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
static const char *const DUTY_TEXT[STUDIO_DUTIES] = {"12,5 %", "25 %", "50 %"};
static const char *const RHYTHM_TEXT[STUDIO_RHYTHMS] = {"BAJO", "MEDIO", "ALTO"};

const char *studio_row_name(int row) { return row >= 0 && row < SR_COUNT ? ROW_NAME[row] : ""; }
const char *studio_note_name(int pc) { return NOTE_NAME[((pc % 12) + 12) % 12]; }

bool studio_row_is_switch(int row) { return row >= SR_LEAD && row <= SR_NOISE; }
bool studio_row_is_action(int row) { return row == SR_DICE; }

int studio_row_values(int row)
{
    switch (row) {
    case SR_SCALE: return 1 + STUDIO_SCALES;
    case SR_KEY: return 1 + 12;
    case SR_TEMPO: return 1 + STUDIO_TEMPOS;
    case SR_ARP: return ARP_COUNT;
    case SR_PROG: return 1 + MUSIC_NPROG;
    case SR_DUTY: return 1 + STUDIO_DUTIES;
    case SR_RHYTHM: return 1 + STUDIO_RHYTHMS;
    case SR_LEAD: case SR_BASS: case SR_NOISE: return 2;
    default: return 1;
    }
}

int studio_row_value(const MusicStudio *s, int row)
{
    switch (row) {
    case SR_SCALE: return s->scale;
    case SR_KEY: return s->key;
    case SR_TEMPO: return s->tempo;
    case SR_ARP: return s->arp;
    case SR_PROG: return s->prog;
    case SR_DUTY: return s->duty;
    case SR_RHYTHM: return s->rhythm;
    case SR_LEAD: case SR_BASS: case SR_NOISE: return s->mute >> (row - SR_LEAD) & 1;
    default: return 0;
    }
}

bool studio_row_locked(const MusicStudio *s, int row)
{
    return row >= SR_SCALE && row <= SR_RHYTHM && studio_row_value(s, row) != 0;
}

void studio_text_of(char *buf, size_t n, int row, int v)
{
    if (row >= SR_SCALE && row <= SR_RHYTHM && v == 0) {
        snprintf(buf, n, "AUTO");
        return;
    }
    switch (row) {
    case SR_SCALE: snprintf(buf, n, "%s", music_mode_name(music_studio_mode(v))); break;
    case SR_KEY: snprintf(buf, n, "%s", NOTE_NAME[(v - 1) % 12]); break;
    case SR_TEMPO: snprintf(buf, n, "%d BPM", music_studio_bpm(v)); break;
    case SR_ARP: snprintf(buf, n, "%s", music_arp_name(v)); break;
    case SR_PROG: snprintf(buf, n, "%s", music_prog_name(v - 1)); break;
    case SR_DUTY: snprintf(buf, n, "%s", DUTY_TEXT[(v - 1) % STUDIO_DUTIES]); break;
    case SR_RHYTHM: snprintf(buf, n, "%s", RHYTHM_TEXT[(v - 1) % STUDIO_RHYTHMS]); break;
    case SR_LEAD: case SR_BASS: case SR_NOISE: snprintf(buf, n, "%s", v ? "MUDA" : "SUENA"); break;
    case SR_DICE: snprintf(buf, n, "MANTÉN A"); break;
    default: snprintf(buf, n, "?"); break;
    }
}

void studio_value_text(char *buf, size_t n, const MusicStudio *s, int row)
{
    studio_text_of(buf, n, row, studio_row_value(s, row));
}

void studio_set(MusicStudio *s, int row, int v)
{
    int n = studio_row_values(row);
    if (v < 0 || v >= n)
        return;
    switch (row) {
    case SR_SCALE: s->scale = (uint8_t)v; break;
    case SR_KEY: s->key = (uint8_t)v; break;
    case SR_TEMPO: s->tempo = (uint8_t)v; break;
    case SR_ARP: s->arp = (uint8_t)v; break;
    case SR_PROG: s->prog = (uint8_t)v; break;
    case SR_DUTY: s->duty = (uint8_t)v; break;
    case SR_RHYTHM: s->rhythm = (uint8_t)v; break;
    case SR_LEAD: case SR_BASS: case SR_NOISE:
        s->mute = (uint8_t)((s->mute & ~(1u << (row - SR_LEAD))) | (unsigned)v << (row - SR_LEAD));
        break;
    default: break;
    }
}

void studio_cycle(MusicStudio *s, int row)
{
    int n = studio_row_values(row);
    if (n > 1)
        studio_set(s, row, (studio_row_value(s, row) + 1) % n);
}

int studio_first_row(int sel, int count, int visible)
{
    if (count <= visible)
        return 0;
    int first = sel - visible / 2 + 1;   /* con 6 visibles, la seleccionada queda en la 3.ª */
    if (first < 0)
        first = 0;
    if (first > count - visible)
        first = count - visible;
    return first;
}
