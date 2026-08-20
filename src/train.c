/* SPDX-License-Identifier: GPL-3.0-or-later
 * bashformer training/export forge.
 *
 * Training runs through an installed upstream notorch. The generated BFW1 file
 * is consumed by bashformer.sh, whose runtime uses GNU Bash builtins only.
 */
#include "notorch.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#ifndef BF_VOCAB
#define BF_VOCAB 96
#endif
#ifndef BF_DIM
#define BF_DIM 256
#endif
#ifndef BF_LAYERS
#define BF_LAYERS 12
#endif
#ifndef BF_QHEADS
#define BF_QHEADS 8
#endif
#ifndef BF_KVHEADS
#define BF_KVHEADS 2
#endif
#define BF_HEAD_DIM (BF_DIM / BF_QHEADS)
#define BF_KV_DIM (BF_KVHEADS * BF_HEAD_DIM)
#ifndef BF_FFN
#define BF_FFN 768
#endif
#ifndef BF_CTX
#define BF_CTX 256
#endif
#ifndef BF_QSHIFT
#define BF_QSHIFT 12
#endif
#define BF_QS (1 << BF_QSHIFT)
#ifndef BF_LUT_STEPS
#define BF_LUT_STEPS 64
#endif
#define BF_ROPE_THETA 500000.0
#define BF_EVAL_SEQS 32
#define BF_LOG_EVERY 100
#define BF_CKPT_EVERY 1000
/* The runtime contract, restated where the weights are born. bashformer.sh
   refuses any loaded value beyond BF_VALUE_LIMIT, and refuses any activation
   beyond the bound derived from this geometry. A forge that ignores both can
   spend a whole run and hand back a file its own runtime will not execute. */
#define BF_VALUE_LIMIT 67108864

#ifndef NT_OP_NONE
#define NT_OP_NONE 0
#endif

typedef struct {
    nt_tensor *rms1, *wq, *wk, *wv, *wo;
    nt_tensor *rms2, *wg, *wu, *wd;
} Layer;

typedef struct {
    nt_tensor *wte;
    Layer layer[BF_LAYERS];
    nt_tensor *rmsf;
} Model;

typedef struct {
    const char *corpus;
    const char *out_prefix;
    int steps;
    float lr;
    int resume;
    int probe;
    const char *probe_weights;
    const char *probe_prompt;
} Options;

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

static void die(const char *message) {
    fprintf(stderr, "bashformer-train: %s\n", message);
    exit(1);
}

static int byte_to_id(unsigned char c) {
    if (c == '\n') return 0;
    if (c == ' ') return 1;
    if (c >= 33 && c <= 126) return 2 + (int)c - 33;
    return 1;
}

static unsigned char id_to_byte(int id) {
    if (id == 0) return '\n';
    if (id == 1) return ' ';
    if (id >= 2 && id < BF_VOCAB) return (unsigned char)(33 + id - 2);
    return ' ';
}

static Model *model_new(void) {
    Model *m = (Model *)calloc(1, sizeof(*m));
    if (!m) die("out of memory");

    m->wte = nt_tensor_new2d(BF_VOCAB, BF_DIM);
    nt_tensor_xavier(m->wte, BF_VOCAB, BF_DIM);

    const float residual_scale = 0.02f / sqrtf(2.0f * BF_LAYERS);
    for (int l = 0; l < BF_LAYERS; ++l) {
        Layer *x = &m->layer[l];
        x->rms1 = nt_tensor_new(BF_DIM); nt_tensor_fill(x->rms1, 1.0f);
        x->wq = nt_tensor_new2d(BF_DIM, BF_DIM); nt_tensor_xavier(x->wq, BF_DIM, BF_DIM);
        x->wk = nt_tensor_new2d(BF_KV_DIM, BF_DIM); nt_tensor_xavier(x->wk, BF_DIM, BF_KV_DIM);
        x->wv = nt_tensor_new2d(BF_KV_DIM, BF_DIM); nt_tensor_xavier(x->wv, BF_DIM, BF_KV_DIM);
        x->wo = nt_tensor_new2d(BF_DIM, BF_DIM); nt_tensor_xavier(x->wo, BF_DIM, BF_DIM);
        for (int i = 0; i < x->wo->len; ++i) x->wo->data[i] *= residual_scale / 0.1f;
        x->rms2 = nt_tensor_new(BF_DIM); nt_tensor_fill(x->rms2, 1.0f);
        x->wg = nt_tensor_new2d(BF_FFN, BF_DIM); nt_tensor_xavier(x->wg, BF_DIM, BF_FFN);
        x->wu = nt_tensor_new2d(BF_FFN, BF_DIM); nt_tensor_xavier(x->wu, BF_DIM, BF_FFN);
        x->wd = nt_tensor_new2d(BF_DIM, BF_FFN); nt_tensor_xavier(x->wd, BF_FFN, BF_DIM);
        for (int i = 0; i < x->wd->len; ++i) x->wd->data[i] *= residual_scale / 0.1f;
    }
    m->rmsf = nt_tensor_new(BF_DIM); nt_tensor_fill(m->rmsf, 1.0f);
    return m;
}

static void model_free(Model *m) {
    if (!m) return;
    nt_tensor_free(m->wte);
    for (int l = 0; l < BF_LAYERS; ++l) {
        Layer *x = &m->layer[l];
        nt_tensor_free(x->rms1); nt_tensor_free(x->wq); nt_tensor_free(x->wk);
        nt_tensor_free(x->wv); nt_tensor_free(x->wo); nt_tensor_free(x->rms2);
        nt_tensor_free(x->wg); nt_tensor_free(x->wu); nt_tensor_free(x->wd);
    }
    nt_tensor_free(m->rmsf);
    free(m);
}

static int model_tensor_count(void) { return 1 + BF_LAYERS * 9 + 1; }

static nt_tensor **model_tensors(Model *m) {
    nt_tensor **p = (nt_tensor **)calloc((size_t)model_tensor_count(), sizeof(*p));
    if (!p) die("out of memory");
    int i = 0;
    p[i++] = m->wte;
    for (int l = 0; l < BF_LAYERS; ++l) {
        Layer *x = &m->layer[l];
        p[i++] = x->rms1; p[i++] = x->wq; p[i++] = x->wk; p[i++] = x->wv;
        p[i++] = x->wo; p[i++] = x->rms2; p[i++] = x->wg; p[i++] = x->wu; p[i++] = x->wd;
    }
    p[i++] = m->rmsf;
    return p;
}

static long model_param_count(Model *m) {
    nt_tensor **p = model_tensors(m);
    long n = 0;
    for (int i = 0; i < model_tensor_count(); ++i) n += p[i]->len;
    free(p);
    return n;
}

static int forward(Model *m, const int *tokens, const int *targets, int want_logits) {
    int wte = nt_tape_param(m->wte);
    nt_tape_no_decay(wte);
    int li[BF_LAYERS][9];
    for (int l = 0; l < BF_LAYERS; ++l) {
        Layer *x = &m->layer[l];
        li[l][0] = nt_tape_param(x->rms1); li[l][1] = nt_tape_param(x->wq);
        li[l][2] = nt_tape_param(x->wk);   li[l][3] = nt_tape_param(x->wv);
        li[l][4] = nt_tape_param(x->wo);   li[l][5] = nt_tape_param(x->rms2);
        li[l][6] = nt_tape_param(x->wg);   li[l][7] = nt_tape_param(x->wu);
        li[l][8] = nt_tape_param(x->wd);
    }
    int rmsf = nt_tape_param(m->rmsf);

    nt_tensor *tok = nt_tensor_new(BF_CTX);
    nt_tensor *tgt = nt_tensor_new(BF_CTX);
    for (int i = 0; i < BF_CTX; ++i) {
        tok->data[i] = (float)tokens[i];
        tgt->data[i] = (float)targets[i];
    }
    int tok_i = nt_tape_record(tok, NT_OP_NONE, -1, -1, 0.0f);
    int tgt_i = nt_tape_record(tgt, NT_OP_NONE, -1, -1, 0.0f);
    nt_tensor_free(tok); nt_tensor_free(tgt);

    int h = nt_seq_embedding(wte, -1, tok_i, BF_CTX, BF_DIM);
    for (int l = 0; l < BF_LAYERS; ++l) {
        int xn = nt_seq_rmsnorm(h, li[l][0], BF_CTX, BF_DIM);
        int q = nt_seq_linear(li[l][1], xn, BF_CTX);
        int k = nt_seq_linear(li[l][2], xn, BF_CTX);
        int v = nt_seq_linear(li[l][3], xn, BF_CTX);
        q = nt_rope_freq(q, BF_CTX, BF_HEAD_DIM, (float)BF_ROPE_THETA);
        k = nt_rope_freq(k, BF_CTX, BF_HEAD_DIM, (float)BF_ROPE_THETA);
        int a = nt_gqa_causal_attention(q, k, v, BF_CTX, BF_HEAD_DIM, BF_QHEADS, BF_KVHEADS);
        h = nt_add(h, nt_seq_linear(li[l][4], a, BF_CTX));

        xn = nt_seq_rmsnorm(h, li[l][5], BF_CTX, BF_DIM);
        int gate = nt_silu(nt_seq_linear(li[l][6], xn, BF_CTX));
        int up = nt_seq_linear(li[l][7], xn, BF_CTX);
        h = nt_add(h, nt_seq_linear(li[l][8], nt_mul(gate, up), BF_CTX));
    }
    int hf = nt_seq_rmsnorm(h, rmsf, BF_CTX, BF_DIM);
    int logits = nt_seq_linear(wte, hf, BF_CTX); /* tied lm_head */
    if (want_logits) return logits;
    return nt_seq_cross_entropy(logits, tgt_i, BF_CTX, BF_VOCAB);
}

/* --out names a path prefix whose directory may not exist. Create it before the
   run starts: a checkpoint that fails after ten thousand steps costs the whole
   run, and the failure used to say only "final save failed". */
static void ensure_parent_dir(const char *path) {
    char buf[4096];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof buf) return;
    memcpy(buf, path, n + 1);
    char *slash = strrchr(buf, '/');
    if (!slash || slash == buf) return;
    *slash = '\0';
    for (char *p = buf + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        mkdir(buf, 0755);
        *p = '/';
    }
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "bashformer-train: cannot create %s: %s\n", buf, strerror(errno));
        exit(1);
    }
}

static int save_model(Model *m, const char *path) {
    nt_tensor **p = model_tensors(m);
    int rc = nt_save(path, p, model_tensor_count());
    free(p);
    return rc;
}

static int load_model(Model *m, const char *path) {
    int n = 0;
    nt_tensor **loaded = nt_load(path, &n);
    if (!loaded) return -1;
    if (n != model_tensor_count()) {
        for (int i = 0; i < n; ++i) nt_tensor_free(loaded[i]);
        free(loaded);
        return -1;
    }
    nt_tensor **dst = model_tensors(m);
    for (int i = 0; i < n; ++i) {
        if (loaded[i]->len != dst[i]->len) {
            for (int j = 0; j < n; ++j) nt_tensor_free(loaded[j]);
            free(loaded); free(dst); return -1;
        }
        memcpy(dst[i]->data, loaded[i]->data, (size_t)dst[i]->len * sizeof(float));
        nt_tensor_free(loaded[i]);
    }
    free(loaded); free(dst);
    return 0;
}

static int *read_corpus(const char *path, long *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    unsigned char *raw = (unsigned char *)malloc((size_t)n);
    int *encoded = (int *)malloc((size_t)n * sizeof(int));
    if (!raw || !encoded) die("out of memory");
    if (fread(raw, 1, (size_t)n, f) != (size_t)n) die("failed to read corpus");
    fclose(f);
    for (long i = 0; i < n; ++i) encoded[i] = byte_to_id(raw[i]);
    free(raw);
    *out_n = n;
    return encoded;
}

static float eval_loss(Model *m, const int *data, long begin, long end) {
    float sum = 0.0f; int count = 0;
    long span = end - begin;
    long stride = span / BF_EVAL_SEQS;
    if (stride < BF_CTX + 1) stride = BF_CTX + 1;
    for (int s = 0; s < BF_EVAL_SEQS; ++s) {
        long off = begin + s * stride;
        if (off + BF_CTX + 1 > end) break;
        nt_tape_start(); nt_train_mode(0);
        int loss = forward(m, data + off, data + off + 1, 0);
        sum += nt_tape_get()->entries[loss].output->data[0];
        ++count;
        nt_tape_clear(); nt_train_mode(1);
    }
    return count ? sum / count : 99.0f;
}

static int quantize(float x) {
    double q = llround((double)x * BF_QS);
    if (q > INT_MAX) return INT_MAX;
    if (q < INT_MIN) return INT_MIN;
    return (int)q;
}

static int64_t bf_isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t x = n, y = (x + 1) / 2;
    while (y < x) { x = y; y = (x + n / x) / 2; }
    return x;
}

/* Mirror of bf_activation_limit in bashformer.sh: the tightest bound over every
   reduction the runtime performs, against 2^62 for headroom. */
static int64_t bf_activation_limit(void) {
    int64_t cap = (int64_t)1 << 62, qs = BF_QS;
    int64_t cols = BF_DIM > BF_FFN ? BF_DIM : BF_FFN;
    int64_t attn_scale_q = llround((1.0 / sqrt((double)BF_HEAD_DIM)) * BF_QS);
    int64_t best = cap / (cols * (int64_t)BF_VALUE_LIMIT);
    int64_t lim = bf_isqrt64(cap / BF_DIM);
    if (lim < best) best = lim;
    lim = bf_isqrt64(cap / ((int64_t)BF_HEAD_DIM * attn_scale_q));
    if (lim < best) best = lim;
    lim = cap / ((int64_t)BF_CTX * qs);
    if (lim < best) best = lim;
    lim = cap / qs;
    if (lim < best) best = lim;
    return best;
}

static void emit_int_tensor(FILE *f, const char *name, const int *values, int n) {
    /* WTE is both a weight and the first activation: it is copied straight into
       the residual stream, so it answers to the tighter of the two bounds. */
    int64_t bound = BF_VALUE_LIMIT;
    int64_t act = bf_activation_limit();
    if (!strcmp(name, "WTE") && act < bound) bound = act;
    for (int i = 0; i < n; ++i)
        if (values[i] > bound || values[i] < -bound) {
            fprintf(stderr, "bashformer-train: %s[%d] = %d exceeds the runtime contract "
                            "(limit %" PRId64 "); this checkpoint would not load\n",
                    name, i, values[i], bound);
            exit(1);
        }
    fprintf(f, "T %s %d\n", name, n);
    for (int i = 0; i < n; i += 24) {
        fputc('D', f);
        int end = i + 24 < n ? i + 24 : n;
        for (int j = i; j < end; ++j) fprintf(f, " %d", values[j]);
        fputc('\n', f);
    }
    fputs("E\n", f);
}

static void emit_float_tensor(FILE *f, const char *name, nt_tensor *t) {
    nt_tensor_sync_cpu(t);
    int *q = (int *)malloc((size_t)t->len * sizeof(int));
    if (!q) die("out of memory");
    for (int i = 0; i < t->len; ++i) q[i] = quantize(t->data[i]);
    emit_int_tensor(f, name, q, t->len);
    free(q);
}

static void export_bfw(Model *m, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "BFW1\n");
    fprintf(f, "M QSHIFT %d\n", BF_QSHIFT);
    fprintf(f, "M VOCAB %d\n", BF_VOCAB);
    fprintf(f, "M DIM %d\n", BF_DIM);
    fprintf(f, "M LAYERS %d\n", BF_LAYERS);
    fprintf(f, "M QHEADS %d\n", BF_QHEADS);
    fprintf(f, "M KVHEADS %d\n", BF_KVHEADS);
    fprintf(f, "M HEAD_DIM %d\n", BF_HEAD_DIM);
    fprintf(f, "M FFN %d\n", BF_FFN);
    fprintf(f, "M CTX %d\n", BF_CTX);
    fprintf(f, "M LUT_STEPS %d\n", BF_LUT_STEPS);
    fprintf(f, "M UNK_ID 1\n");
    fprintf(f, "M TIE_HEAD 1\n");
    fprintf(f, "M ATTN_SCALE_Q %d\n", (int)llround((1.0 / sqrt((double)BF_HEAD_DIM)) * BF_QS));
    fprintf(f, "M EPS_Q2 %d\n", (int)llround(1e-5 * BF_QS * BF_QS));
    fprintf(f, "M ROPE_THETA %d\n", (int)BF_ROPE_THETA);
    fputs("V ", f);
    for (int i = 0; i < BF_VOCAB; ++i) fprintf(f, "%02x", id_to_byte(i));
    fputc('\n', f);

    emit_float_tensor(f, "WTE", m->wte);
    for (int l = 0; l < BF_LAYERS; ++l) {
        char name[32]; Layer *x = &m->layer[l];
#define EMIT(field, suffix) do { snprintf(name, sizeof(name), "L%d_%s", l, suffix); emit_float_tensor(f, name, x->field); } while (0)
        EMIT(rms1, "RMS1"); EMIT(wq, "WQ"); EMIT(wk, "WK"); EMIT(wv, "WV");
        EMIT(wo, "WO"); EMIT(rms2, "RMS2"); EMIT(wg, "WG"); EMIT(wu, "WU"); EMIT(wd, "WD");
#undef EMIT
    }
    emit_float_tensor(f, "RMSF", m->rmsf);

    const int pairs = BF_HEAD_DIM / 2;
    int *cos_q = (int *)malloc((size_t)BF_CTX * pairs * sizeof(int));
    int *sin_q = (int *)malloc((size_t)BF_CTX * pairs * sizeof(int));
    for (int p = 0; p < BF_CTX; ++p) {
        for (int i = 0; i < pairs; ++i) {
            double freq = 1.0 / pow(BF_ROPE_THETA, (2.0 * i) / BF_HEAD_DIM);
            double angle = p * freq;
            cos_q[p * pairs + i] = (int)llround(cos(angle) * BF_QS);
            sin_q[p * pairs + i] = (int)llround(sin(angle) * BF_QS);
        }
    }
    emit_int_tensor(f, "ROPE_COS", cos_q, BF_CTX * pairs);
    emit_int_tensor(f, "ROPE_SIN", sin_q, BF_CTX * pairs);
    free(cos_q); free(sin_q);

    const int lut_n = 16 * BF_LUT_STEPS + 1;
    int *exp_q = (int *)malloc((size_t)lut_n * sizeof(int));
    int *sig_q = (int *)malloc((size_t)lut_n * sizeof(int));
    for (int i = 0; i < lut_n; ++i) {
        exp_q[i] = (int)llround(exp(-(double)i / BF_LUT_STEPS) * BF_QS);
        double x = -8.0 + (double)i / BF_LUT_STEPS;
        sig_q[i] = (int)llround((1.0 / (1.0 + exp(-x))) * BF_QS);
    }
    emit_int_tensor(f, "EXP_NEG", exp_q, lut_n);
    emit_int_tensor(f, "SIGMOID", sig_q, lut_n);
    free(exp_q); free(sig_q);
    fputs("Z\n", f);
    fclose(f);
    printf("exported %s\n", path);
}

static int probe(Model *m, const char *prompt) {
    int tokens[BF_CTX], targets[BF_CTX];
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)prompt; *p && n < BF_CTX; ++p) tokens[n++] = byte_to_id(*p);
    if (!n) tokens[n++] = 1;
    int pad = tokens[n - 1];
    for (int i = n; i < BF_CTX; ++i) tokens[i] = pad;
    memset(targets, 0, sizeof(targets));

    nt_tape_start(); nt_train_mode(0);
    int logits_i = forward(m, tokens, targets, 1);
    float *logits = nt_tape_get()->entries[logits_i].output->data + (n - 1) * BF_VOCAB;
    int top[5] = {-1, -1, -1, -1, -1};
    for (int id = 0; id < BF_VOCAB; ++id) {
        for (int k = 0; k < 5; ++k) {
            if (top[k] < 0 || logits[id] > logits[top[k]]) {
                for (int j = 4; j > k; --j) top[j] = top[j - 1];
                top[k] = id;
                break;
            }
        }
    }
    printf("top5");
    for (int k = 0; k < 5; ++k) printf(" %d", top[k]);
    printf("\n");
    nt_tape_clear(); nt_train_mode(1);
    return top[0];
}

static Options parse_options(int argc, char **argv) {
    Options o = {"data/dracula.txt", "weights/bashformer", 20000, 3e-4f, 0, 0, NULL, NULL};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--corpus") && i + 1 < argc) o.corpus = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) o.out_prefix = argv[++i];
        else if (!strcmp(argv[i], "--steps") && i + 1 < argc) o.steps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lr") && i + 1 < argc) o.lr = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--resume")) o.resume = 1;
        else if (!strcmp(argv[i], "--probe") && i + 2 < argc) {
            o.probe = 1; o.probe_weights = argv[++i]; o.probe_prompt = argv[++i];
        } else {
            fprintf(stderr, "unknown/incomplete option: %s\n", argv[i]);
            exit(2);
        }
    }
    return o;
}

int main(int argc, char **argv) {
    Options o = parse_options(argc, argv);
    if (o.steps <= 0) die("--steps must be positive");
    if (!(o.lr > 0.0f) || !isfinite(o.lr)) die("--lr must be finite and positive");
    if (!o.out_prefix || !*o.out_prefix) die("--out must not be empty");
    ensure_parent_dir(o.out_prefix);
    nt_seed(42);
    srand(42);
    Model *m = model_new();

    if (o.probe) {
        if (load_model(m, o.probe_weights) != 0) die("cannot load probe checkpoint");
        int id = probe(m, o.probe_prompt);
        printf("greedy %d %02x\n", id, id_to_byte(id));
        model_free(m);
        return 0;
    }

    long n = 0;
    int *data = read_corpus(o.corpus, &n);
    if (!data || n < BF_CTX * 4) die("corpus too small");
    long train_end = (long)(n * 0.9);
    if (train_end <= BF_CTX + 1) die("corpus too small after split");

    char bin_path[512], bfw_path[512], meta_path[512];
    snprintf(bin_path, sizeof(bin_path), "%s.bin", o.out_prefix);
    snprintf(bfw_path, sizeof(bfw_path), "%s.bfw", o.out_prefix);
    snprintf(meta_path, sizeof(meta_path), "%s.meta", o.out_prefix);

    int start = 0; float best = 99.0f;
    if (o.resume && load_model(m, bin_path) == 0) {
        FILE *mf = fopen(meta_path, "r");
        if (mf) { (void)fscanf(mf, "%d\n%f", &start, &best); fclose(mf); }
        printf("resumed %s at step %d\n", bin_path, start);
    }
    if (start > o.steps) die("checkpoint step is beyond requested --steps");

    long params = model_param_count(m);
    printf("bashformer forge: %.3fM params, D=%d L=%d Q=%d KV=%d FFN=%d CTX=%d V=%d\n",
           params / 1e6, BF_DIM, BF_LAYERS, BF_QHEADS, BF_KVHEADS, BF_FFN, BF_CTX, BF_VOCAB);
    printf("corpus: %ld bytes; train=%ld val=%ld\n", n, train_end, n - train_end);

    nt_schedule schedule = nt_schedule_cosine(o.lr, o.steps / 10, o.steps, o.lr * 0.1f);
    schedule.current_step = start;
    nt_nan_guard guard = nt_nan_guard_new();
    double t0 = now_ms();

    for (int step = start; step < o.steps; ++step) {
        float lr = nt_schedule_get_lr(&schedule);
        long off = rand() % (int)(train_end - BF_CTX - 1);
        nt_tape_start(); nt_train_mode(1);
        int loss_i = forward(m, data + off, data + off + 1, 0);
        float loss = nt_tape_get()->entries[loss_i].output->data[0];
        if (loss < best) best = loss;
        nt_tape_backward(loss_i);
        if (nt_nan_guard_check(&guard)) {
            nt_tape_clip_grads(1.0f);
            nt_tape_chuck_step(lr, loss);
        }
        nt_tape_clear();

        if (step == start || (step + 1) % BF_LOG_EVERY == 0) {
            printf("step %6d | loss %.4f | best %.4f | lr %.2e | %.1fs\n",
                   step + 1, loss, best, lr, (now_ms() - t0) / 1000.0);
            fflush(stdout);
        }
        if ((step + 1) % BF_CKPT_EVERY == 0) {
            float val = eval_loss(m, data, train_end, n);
            printf("checkpoint %d | val %.4f\n", step + 1, val);
            if (save_model(m, bin_path) != 0) die("checkpoint save failed");
            FILE *mf = fopen(meta_path, "w");
            if (mf) { fprintf(mf, "%d\n%.9g\n", step + 1, best); fclose(mf); }
            export_bfw(m, bfw_path);
        }
    }

    float val = eval_loss(m, data, train_end, n);
    printf("final val %.4f; nans %d\n", val, guard.total_nan_count);
    if (save_model(m, bin_path) != 0) {
        fprintf(stderr, "bashformer-train: final save failed: %s\n", bin_path);
        exit(1);
    }
    export_bfw(m, bfw_path);
    FILE *mf = fopen(meta_path, "w");
    if (mf) { fprintf(mf, "%d\n%.9g\n", o.steps, best); fclose(mf); }
    probe(m, "The blood was ");

    model_free(m);
    free(data);
    return 0;
}
