/* RC low-pass for mono/stereo 16-bit PCM WAV, cutoff = 400 Hz. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define CUTOFF_HZ 400.0

static int u16(FILE *f, uint16_t v) {
    return fputc(v & 255, f) != EOF && fputc((v >> 8) & 255, f) != EOF;
}
static int u32(FILE *f, uint32_t v) {
    return u16(f, (uint16_t)v) && u16(f, (uint16_t)(v >> 16));
}
static int read_u16(FILE *f, uint16_t *v) {
    int a = fgetc(f), b = fgetc(f);
    if (a == EOF || b == EOF) return 0;
    *v = (uint16_t)(a | (b << 8)); return 1;
}
static int read_u32(FILE *f, uint32_t *v) {
    uint16_t a, b;
    if (!read_u16(f, &a) || !read_u16(f, &b)) return 0;
    *v = (uint32_t)a | ((uint32_t)b << 16); return 1;
}
static int header(FILE *f, uint32_t fs, uint16_t channels, uint32_t bytes) {
    uint16_t frame_bytes = channels * 2;
    return fwrite("RIFF", 1, 4, f) == 4 && u32(f, 36 + bytes) &&
           fwrite("WAVEfmt ", 1, 8, f) == 8 && u32(f, 16) &&
           u16(f, 1) && u16(f, channels) && u32(f, fs) &&
           u32(f, fs * frame_bytes) && u16(f, frame_bytes) && u16(f, 16) &&
           fwrite("data", 1, 4, f) == 4 && u32(f, bytes);
}
static int skip(FILE *f, uint32_t bytes) {
    return bytes <= 0x7fffffffU && fseek(f, (long)bytes, SEEK_CUR) == 0;
}
static int read_format(FILE *in, uint32_t *fs, uint16_t *channels,
                       uint32_t *data_bytes, long *data_pos) {
    char id[4], wave[4]; uint32_t riff_size;
    if (fread(id, 1, 4, in) != 4 || memcmp(id, "RIFF", 4) ||
        !read_u32(in, &riff_size) || fread(wave, 1, 4, in) != 4 ||
        memcmp(wave, "WAVE", 4)) return 0;
    int got_fmt = 0, got_data = 0;
    uint16_t tag = 0, align = 0, bits = 0;
    uint32_t rate = 0;
    while (fread(id, 1, 4, in) == 4) {
        uint32_t size;
        if (!read_u32(in, &size)) return 0;
        if (!memcmp(id, "fmt ", 4)) {
            if (size < 16 || !read_u16(in, &tag) ||
                !read_u16(in, channels) || !read_u32(in, fs) ||
                !read_u32(in, &rate) || !read_u16(in, &align) ||
                !read_u16(in, &bits) || !skip(in, size - 16 + (size & 1))) return 0;
            got_fmt = 1;
        } else if (!memcmp(id, "data", 4)) {
            *data_bytes = size; *data_pos = ftell(in); got_data = 1;
            break;
        } else if (size == UINT32_MAX || !skip(in, size + (size & 1))) return 0;
    }
    if (!got_fmt || !got_data || tag != 1 ||
        (*channels != 1 && *channels != 2) || bits != 16 ||
        *fs < 2 || *fs > 192000 || align != *channels * 2 ||
        rate != *fs * align || *data_bytes % align ||
        *data_bytes > UINT32_MAX - 36) return 0;
    if (fseek(in, 0, SEEK_END) != 0) return 0;
    long end = ftell(in);
    return end >= 0 && *data_pos >= 0 &&
           (unsigned long)(end - *data_pos) >= *data_bytes &&
           fseek(in, *data_pos, SEEK_SET) == 0;
}

int main(int argc, char **argv) {
    if (argc != 3 || strcmp(argv[1], argv[2]) == 0) {
        fprintf(stderr, "usage: %s in.wav out.wav (different paths)\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    uint32_t fs, bytes; uint16_t channels; long pos;
    if (!read_format(in, &fs, &channels, &bytes, &pos)) {
        fprintf(stderr, "unsupported or malformed WAV (use PCM16 mono/stereo)\n");
        fclose(in); return 1;
    }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); fclose(in); return 1; }
    if (!header(out, fs, channels, bytes)) {
        perror("WAV header"); fclose(in); fclose(out); return 1;
    }
    double rc = 1.0 / (2 * PI * CUTOFF_HZ);
    double tau = 1.0 / fs;
    double alpha = rc / (rc + tau), beta = tau / (rc + tau);
    double state[2] = {0, 0}; /* y[-1] = 0 for both channels */
    uint32_t frames = bytes / (channels * 2);
    for (uint32_t n = 0; n < frames; ++n) {
        for (uint16_t ch = 0; ch < channels; ++ch) {
            uint16_t raw;
            if (!read_u16(in, &raw)) { fprintf(stderr, "short input\n"); goto fail; }
            int16_t sample = (int16_t)raw;
            state[ch] = alpha * state[ch] + beta * (double)sample;
            long rounded = lrint(state[ch]);
            if (rounded > 32767) rounded = 32767;
            if (rounded < -32768) rounded = -32768;
            if (!u16(out, (uint16_t)(int16_t)rounded)) {
                perror("write samples"); goto fail;
            }
        }
    }
    int close_in = fclose(in), close_out = fclose(out);
    if (close_in != 0 || close_out != 0) { perror("close WAV"); return 1; }
    printf("filtered %u frames, %u Hz, %u channel(s); alpha=%.9f beta=%.9f\n",
           frames, fs, channels, alpha, beta);
    return 0;
fail:
    fclose(in); fclose(out); return 1;
}
