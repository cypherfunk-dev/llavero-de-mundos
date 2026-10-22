#include "imgout.h"

#include <stdio.h>

void img_render(const Framebuffer *fb, int scale, bool lcd, uint32_t *out)
{
    if (scale < 1)
        scale = 1;
    int w = LLV_W * scale;
    bool grid = lcd && scale >= 3;
    for (int y = 0; y < LLV_H; y++) {
        for (int x = 0; x < LLV_W; x++) {
            uint32_t c = gfx_palette[fb->px[y * LLV_W + x]];
            uint32_t dark = ((((c >> 16) & 0xFF) * 200 >> 8) << 16) |
                            ((((c >> 8) & 0xFF) * 200 >> 8) << 8) | (((c & 0xFF) * 200) >> 8);
            for (int j = 0; j < scale; j++)
                for (int i = 0; i < scale; i++) {
                    bool gap = grid && (i == scale - 1 || j == scale - 1);
                    out[(y * scale + j) * w + x * scale + i] = gap ? dark : c;
                }
        }
    }
}

/* ---------- PNG ---------- */

static uint32_t crc_update(uint32_t crc, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Escribe un chunk completo (longitud, tipo, datos, CRC). */
static int chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len)
{
    uint8_t head[8];
    put32(head, len);
    for (int i = 0; i < 4; i++)
        head[4 + i] = (uint8_t)type[i];
    uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    crc = ~crc_update(crc, data, len);
    uint8_t tail[4];
    put32(tail, crc);
    if (fwrite(head, 1, 8, f) != 8 || (len && fwrite(data, 1, len, f) != len) ||
        fwrite(tail, 1, 4, f) != 4)
        return 1;
    return 0;
}

int img_write_png(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    int err = fwrite(SIG, 1, 8, f) != 8;

    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;  /* 8 bits por canal */
    ihdr[9] = 2;  /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    err |= chunk(f, "IHDR", ihdr, 13);

    /* Datos crudos (filtro 0 por fila) dentro de un zlib de bloques sin comprimir. */
    size_t row = 1 + (size_t)w * 3;
    size_t raw_len = row * (size_t)h;
    size_t nblocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + nblocks * 5 + 4;

    /* IDAT puede ser grande: se escribe en streaming calculando el CRC al vuelo. */
    uint8_t head[8];
    put32(head, (uint32_t)z_len);
    head[4] = 'I'; head[5] = 'D'; head[6] = 'A'; head[7] = 'T';
    err |= fwrite(head, 1, 8, f) != 8;
    uint32_t crc = crc_update(0xFFFFFFFFu, head + 4, 4);
    uint32_t a = 1, b = 0; /* Adler-32 */

#define EMIT(buf, n)                          \
    do {                                      \
        err |= fwrite((buf), 1, (n), f) != (n); \
        crc = crc_update(crc, (buf), (n));    \
    } while (0)

    static const uint8_t ZH[2] = {0x78, 0x01};
    EMIT(ZH, 2);

    uint8_t buf[65535];
    size_t done = 0, in_buf = 0, y = 0, x = 0;
    int stage = 0; /* 0 = byte de filtro, 1..3 = canales */
    while (done < raw_len) {
        size_t n = raw_len - done > 65535 ? 65535 : raw_len - done;
        for (in_buf = 0; in_buf < n; in_buf++) {
            uint8_t v;
            if (stage == 0) {
                v = 0;
            } else {
                uint32_t c = px[y * (size_t)w + x];
                v = (uint8_t)(c >> (stage == 1 ? 16 : stage == 2 ? 8 : 0));
            }
            buf[in_buf] = v;
            a = (a + v) % 65521u;
            b = (b + a) % 65521u;
            if (++stage == 4) {
                stage = 1;
                if (++x == (size_t)w) {
                    x = 0;
                    y++;
                    stage = 0;
                }
            }
        }
        done += n;
        uint8_t bh[5] = {done == raw_len ? 1 : 0, (uint8_t)n, (uint8_t)(n >> 8),
                         (uint8_t)~n, (uint8_t)(~n >> 8)};
        EMIT(bh, 5);
        EMIT(buf, n);
    }
    uint8_t ad[4];
    put32(ad, (b << 16) | a);
    EMIT(ad, 4);
#undef EMIT
    uint8_t tail[4];
    put32(tail, ~crc);
    err |= fwrite(tail, 1, 4, f) != 4;

    err |= chunk(f, "IEND", NULL, 0);
    err |= fclose(f) != 0;
    return err;
}

int img_write_ppm(const char *path, const uint32_t *px, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 1;
    int err = fprintf(f, "P6\n%d %d\n255\n", w, h) < 0;
    for (long i = 0; i < (long)w * h && !err; i++) {
        uint8_t rgb[3] = {(uint8_t)(px[i] >> 16), (uint8_t)(px[i] >> 8), (uint8_t)px[i]};
        err |= fwrite(rgb, 1, 3, f) != 3;
    }
    err |= fclose(f) != 0;
    return err;
}
