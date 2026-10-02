/* Generate stereo 16-bit PCM WAV: left = sin, right = cos. */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static int u16(FILE *f, uint16_t v) {
    return fputc(v & 255, f) != EOF && fputc((v >> 8) & 255, f) != EOF;
}
static int u32(FILE *f, uint32_t v) {
    return u16(f, (uint16_t)v) && u16(f, (uint16_t)(v >> 16));
}
static int header(FILE *f, uint32_t fs, uint32_t frames) {
    uint32_t bytes = frames * 4;
    return fwrite("RIFF", 1, 4, f) == 4 && u32(f, 36 + bytes) &&
           fwrite("WAVEfmt ", 1, 8, f) == 8 && u32(f, 16) &&
           u16(f, 1) && u16(f, 2) && u32(f, fs) && u32(f, fs * 4) &&
           u16(f, 4) && u16(f, 16) &&
           fwrite("data", 1, 4, f) == 4 && u32(f, bytes);
}
static int number(const char *s, double *out) {
    char *end;
    errno = 0;
    *out = strtod(s, &end);
    return s != end && *end == '\0' && errno == 0 && isfinite(*out);
}

int main(int argc, char **argv) {
    double fs_arg, freq, seconds;
    if (argc != 5 || !number(argv[1], &fs_arg) ||
        !number(argv[2], &freq) || !number(argv[3], &seconds) ||
        fs_arg < 2 || fs_arg > 192000 || floor(fs_arg) != fs_arg ||
        freq < 0 || freq >= fs_arg / 2 || seconds <= 0 ||
        seconds * fs_arg > (UINT32_MAX - 36.0) / 4.0) {
        fprintf(stderr, "usage: %s fs f L out.wav\n"
                        "fs: integer 2..192000; 0 <= f < fs/2; L > 0\n", argv[0]);
        return 1;
    }
    uint32_t fs = (uint32_t)fs_arg;
    uint32_t frames = (uint32_t)llround(seconds * fs);
    if (!frames || frames > (UINT32_MAX - 36U) / 4U) {
        fprintf(stderr, "length is outside the WAV size limit\n"); return 1;
    }
    FILE *out = fopen(argv[4], "wb");
    if (!out) { perror(argv[4]); return 1; }
    if (!header(out, fs, frames)) { perror("WAV header"); fclose(out); return 1; }
    for (uint32_t n = 0; n < frames; ++n) {
        double phase = 2 * PI * freq * n / fs;
        int16_t left = (int16_t)lrint(32767.0 * 0.8 * sin(phase));
        int16_t right = (int16_t)lrint(32767.0 * 0.8 * cos(phase));
        if (!u16(out, (uint16_t)left) || !u16(out, (uint16_t)right)) {
            perror("write samples"); fclose(out); return 1;
        }
    }
    if (fclose(out) != 0) { perror("close WAV"); return 1; }
    printf("wrote %u frames, %u Hz, %.3f Hz, stereo PCM16\n", frames, fs, freq);
    return 0;
}
