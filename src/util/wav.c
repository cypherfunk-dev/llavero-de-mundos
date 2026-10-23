#include "wav.h"

#include <stdio.h>

static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int wav_write(const char *path, const int16_t *pcm, size_t n, uint32_t rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                     16, 0, 0, 0, 1, 0, 1, 0};
    uint32_t bytes = (uint32_t)(n * 2);
    le32(h + 4, 36 + bytes);
    le32(h + 24, rate);
    le32(h + 28, rate * 2);
    le16(h + 32, 2);
    le16(h + 34, 16);
    h[36] = 'd'; h[37] = 'a'; h[38] = 't'; h[39] = 'a';
    le32(h + 40, bytes);
    int ok = fwrite(h, 1, 44, f) == 44;
    for (size_t i = 0; i < n && ok; i++) {
        uint8_t b[2];
        le16(b, (uint16_t)pcm[i]);
        ok = fwrite(b, 1, 2, f) == 2;
    }
    return (fclose(f) == 0 && ok) ? 0 : 1;
}
