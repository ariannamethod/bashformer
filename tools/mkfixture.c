/* SPDX-License-Identifier: GPL-3.0-or-later
 * Deterministic BFW1 fixture generator. This is not a trained model; it exists
 * to exercise every Bashformer inference path before notorch enters the room.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define QSHIFT 12
#define QS (1 << QSHIFT)
#ifdef BF_PRODUCTION_GEOMETRY
#define V 96
#define D 32
#define LAYERS 2
#define QHEADS 4
#define KVHEADS 1
#define HEAD_DIM 8
#define FFN 64
#define CTX 64
#define UNK_ID 1
#else
#define V 8
#define D 8
#define LAYERS 2
#define QHEADS 2
#define KVHEADS 1
#define HEAD_DIM 4
#define FFN 8
#define CTX 16
#define UNK_ID 0
#endif
#define KV_DIM (KVHEADS * HEAD_DIM)
#define LUT_STEPS 64
#define EXP_MAX_INDEX (16 * LUT_STEPS)
#define SIG_MAX_INDEX (16 * LUT_STEPS)
#define ROPE_THETA 500000.0

static uint32_t rng_state = 0xB45F0A13u;

static int irand(int limit) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return (int)((rng_state >> 8) % (uint32_t)limit);
}

static void emit_tensor(FILE *f, const char *name, int n, const int *x) {
    fprintf(f, "T %s %d\n", name, n);
    for (int i = 0; i < n; i += 24) {
        fputs("D", f);
        int end = i + 24 < n ? i + 24 : n;
        for (int j = i; j < end; ++j) fprintf(f, " %d", x[j]);
        fputc('\n', f);
    }
    fputs("E\n", f);
}

static void fill_small(int *x, int n, int span) {
    for (int i = 0; i < n; ++i) x[i] = irand(span * 2 + 1) - span;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "weights/fixture.bfw";
    FILE *f = fopen(path, "w");
    if (!f) {
        perror(path);
        return 1;
    }

    fprintf(f, "BFW1\n");
    fprintf(f, "M QSHIFT %d\n", QSHIFT);
    fprintf(f, "M VOCAB %d\n", V);
    fprintf(f, "M DIM %d\n", D);
    fprintf(f, "M LAYERS %d\n", LAYERS);
    fprintf(f, "M QHEADS %d\n", QHEADS);
    fprintf(f, "M KVHEADS %d\n", KVHEADS);
    fprintf(f, "M HEAD_DIM %d\n", HEAD_DIM);
    fprintf(f, "M FFN %d\n", FFN);
    fprintf(f, "M CTX %d\n", CTX);
    fprintf(f, "M LUT_STEPS %d\n", LUT_STEPS);
    fprintf(f, "M UNK_ID %d\n", UNK_ID);
    fprintf(f, "M TIE_HEAD 1\n");
    fprintf(f, "M ATTN_SCALE_Q %d\n", (int)llround((1.0 / sqrt((double)HEAD_DIM)) * QS));
    fprintf(f, "M EPS_Q2 %d\n", (int)llround(1e-5 * QS * QS));
    fprintf(f, "M ROPE_THETA %d\n", (int)ROPE_THETA);
    fputs("V ", f);
#ifdef BF_PRODUCTION_GEOMETRY
    /* newline, space, then printable ASCII 33..126 */
    fputs("0a20", f);
    for (int c = 33; c <= 126; ++c) fprintf(f, "%02x", c);
#else
    /* space, A..F, newline */
    fputs("204142434445460a", f);
#endif
    fputc('\n', f);

    int wte[V * D];
    memset(wte, 0, sizeof(wte));
    for (int i = 0; i < V && i < D; ++i) wte[i * D + i] = QS;
    /* A small non-orthogonal tail makes the logits less trivial. */
    for (int i = 0; i < V * D; ++i) wte[i] += irand(81) - 40;
    emit_tensor(f, "WTE", V * D, wte);

    int rms[D];
    int wq[D * D], wk[KV_DIM * D], wv[KV_DIM * D], wo[D * D];
    int wg[FFN * D], wu[FFN * D], wd[D * FFN];
    for (int i = 0; i < D; ++i) rms[i] = QS;
    for (int l = 0; l < LAYERS; ++l) {
        char name[32];
        fill_small(wq, D * D, QS / 10);
        fill_small(wk, KV_DIM * D, QS / 10);
        fill_small(wv, KV_DIM * D, QS / 8);
        fill_small(wo, D * D, QS / 16);
        fill_small(wg, FFN * D, QS / 8);
        fill_small(wu, FFN * D, QS / 8);
        fill_small(wd, D * FFN, QS / 16);
#define EMIT_LAYER(field, suffix, count) do { \
            snprintf(name, sizeof(name), "L%d_%s", l, suffix); \
            emit_tensor(f, name, count, field); \
        } while (0)
        EMIT_LAYER(rms, "RMS1", D);
        EMIT_LAYER(wq, "WQ", D * D);
        EMIT_LAYER(wk, "WK", KV_DIM * D);
        EMIT_LAYER(wv, "WV", KV_DIM * D);
        EMIT_LAYER(wo, "WO", D * D);
        EMIT_LAYER(rms, "RMS2", D);
        EMIT_LAYER(wg, "WG", FFN * D);
        EMIT_LAYER(wu, "WU", FFN * D);
        EMIT_LAYER(wd, "WD", D * FFN);
#undef EMIT_LAYER
    }
    emit_tensor(f, "RMSF", D, rms);

    int rope_cos[CTX * (HEAD_DIM / 2)];
    int rope_sin[CTX * (HEAD_DIM / 2)];
    for (int p = 0; p < CTX; ++p) {
        for (int i = 0; i < HEAD_DIM / 2; ++i) {
            double freq = 1.0 / pow(ROPE_THETA, (2.0 * i) / HEAD_DIM);
            double a = p * freq;
            rope_cos[p * (HEAD_DIM / 2) + i] = (int)llround(cos(a) * QS);
            rope_sin[p * (HEAD_DIM / 2) + i] = (int)llround(sin(a) * QS);
        }
    }
    emit_tensor(f, "ROPE_COS", CTX * (HEAD_DIM / 2), rope_cos);
    emit_tensor(f, "ROPE_SIN", CTX * (HEAD_DIM / 2), rope_sin);

    int exp_lut[EXP_MAX_INDEX + 1];
    int sig_lut[SIG_MAX_INDEX + 1];
    for (int i = 0; i <= EXP_MAX_INDEX; ++i) {
        double x = -(double)i / LUT_STEPS;
        exp_lut[i] = (int)llround(exp(x) * QS);
    }
    for (int i = 0; i <= SIG_MAX_INDEX; ++i) {
        double x = -8.0 + (double)i / LUT_STEPS;
        sig_lut[i] = (int)llround((1.0 / (1.0 + exp(-x))) * QS);
    }
    emit_tensor(f, "EXP_NEG", EXP_MAX_INDEX + 1, exp_lut);
    emit_tensor(f, "SIGMOID", SIG_MAX_INDEX + 1, sig_lut);

    fputs("Z\n", f);
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}
