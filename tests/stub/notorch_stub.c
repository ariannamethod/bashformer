/* Contract stub for the forge smoke test.
 *
 * It is deliberately not a neural-network implementation. Its job is to give
 * src/train.c the current notorch ABI shape, exercise checkpoint/export control
 * flow, and produce deterministic tensors that the BFW1 runtimes can load.
 */
#include "notorch.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static nt_tape g_tape;
static unsigned int g_rng = 1;
static int g_training = 1;

static void fail_alloc(void) { fputs("notorch-stub: out of memory\n", stderr); abort(); }

static nt_tensor *tensor_clone(const nt_tensor *src) {
    nt_tensor *t = (nt_tensor *)calloc(1, sizeof(*t));
    if (!t) fail_alloc();
    *t = *src;
    t->data = (float *)calloc((size_t)t->len, sizeof(float));
    if (!t->data) fail_alloc();
    memcpy(t->data, src->data, (size_t)t->len * sizeof(float));
    t->refcount = 1;
    return t;
}

static int record_owned(nt_tensor *t, int op, int p1, int p2) {
    if (g_tape.count >= 8192) abort();
    int i = g_tape.count++;
    memset(&g_tape.entries[i], 0, sizeof(g_tape.entries[i]));
    g_tape.entries[i].output = t;
    g_tape.entries[i].op = op;
    g_tape.entries[i].parent1 = p1;
    g_tape.entries[i].parent2 = p2;
    return i;
}

nt_tensor *nt_tensor_new(size_t len) {
    nt_tensor *t = (nt_tensor *)calloc(1, sizeof(*t));
    if (!t) fail_alloc();
    t->len = (int)len; t->ndim = 1; t->shape[0] = (int)len; t->stride[0] = 1; t->refcount = 1;
    t->data = (float *)calloc(len ? len : 1, sizeof(float));
    if (!t->data) fail_alloc();
    return t;
}

nt_tensor *nt_tensor_new2d(int rows, int cols) {
    nt_tensor *t = nt_tensor_new((size_t)rows * (size_t)cols);
    t->ndim = 2; t->shape[0] = rows; t->shape[1] = cols;
    t->stride[0] = cols; t->stride[1] = 1;
    return t;
}

void nt_tensor_free(nt_tensor *t) { if (t) { free(t->data); free(t); } }
void nt_tensor_fill(nt_tensor *t, float value) { for (int i = 0; i < t->len; ++i) t->data[i] = value; }

void nt_tensor_xavier(nt_tensor *t, int fan_in, int fan_out) {
    (void)fan_in; (void)fan_out;
    for (int i = 0; i < t->len; ++i) {
        g_rng = g_rng * 1664525u + 1013904223u;
        int v = (int)((g_rng >> 8) % 2001u) - 1000;
        t->data[i] = (float)v / 20000.0f;
    }
}

void nt_tensor_sync_cpu(nt_tensor *t) { (void)t; }
void nt_seed(unsigned int seed) { g_rng = seed ? seed : 1; }
void nt_train_mode(int on) { g_training = on; (void)g_training; }

void nt_tape_clear(void) {
    for (int i = 0; i < g_tape.count; ++i) {
        if (!g_tape.entries[i].is_param) nt_tensor_free(g_tape.entries[i].output);
        g_tape.entries[i].output = NULL;
    }
    g_tape.count = 0; g_tape.active = 0;
}

void nt_tape_start(void) {
    if (g_tape.count) nt_tape_clear();
    g_tape.active = 1;
}

nt_tape *nt_tape_get(void) { return &g_tape; }

int nt_tape_param(nt_tensor *t) {
    int i = record_owned(t, 0, -1, -1);
    g_tape.entries[i].is_param = 1;
    return i;
}

void nt_tape_no_decay(int idx) { if (idx >= 0 && idx < g_tape.count) g_tape.entries[idx].no_decay = 1; }

int nt_tape_record(nt_tensor *output, int op, int p1, int p2, float aux) {
    int i = record_owned(tensor_clone(output), op, p1, p2);
    g_tape.entries[i].aux = aux;
    return i;
}

void nt_tape_backward(int loss_idx) { (void)loss_idx; }
float nt_tape_clip_grads(float max_norm) { return max_norm; }
void nt_tape_chuck_step(float lr, float loss) { (void)lr; (void)loss; }

int nt_seq_embedding(int wte_i, int wpe_i, int tok_i, int T, int D) {
    (void)wpe_i;
    nt_tensor *w = g_tape.entries[wte_i].output;
    nt_tensor *tok = g_tape.entries[tok_i].output;
    nt_tensor *out = nt_tensor_new2d(T, D);
    int vocab = w->shape[0];
    for (int t = 0; t < T; ++t) {
        int id = (int)tok->data[t]; if (id < 0 || id >= vocab) id = 0;
        memcpy(out->data + (size_t)t * D, w->data + (size_t)id * D, (size_t)D * sizeof(float));
    }
    return record_owned(out, 11, wte_i, tok_i);
}

int nt_seq_rmsnorm(int x_i, int gamma_i, int T, int D) {
    (void)gamma_i; (void)T; (void)D;
    return record_owned(tensor_clone(g_tape.entries[x_i].output), 13, x_i, gamma_i);
}

int nt_seq_linear(int w_i, int x_i, int T) {
    nt_tensor *w = g_tape.entries[w_i].output;
    nt_tensor *x = g_tape.entries[x_i].output;
    int rows = w->shape[0], cols = w->shape[1];
    nt_tensor *out = nt_tensor_new2d(T, rows);
    for (int t = 0; t < T; ++t) {
        for (int r = 0; r < rows; ++r) {
            double sum = 0.0;
            for (int c = 0; c < cols; ++c) sum += w->data[r * cols + c] * x->data[t * cols + c];
            out->data[t * rows + r] = (float)sum;
        }
    }
    return record_owned(out, 12, w_i, x_i);
}

int nt_rope(int x_i, int T, int head_dim) { return nt_rope_freq(x_i, T, head_dim, 10000.0f); }
int nt_rope_freq(int x_i, int T, int head_dim, float freq_base) {
    (void)T; (void)head_dim; (void)freq_base;
    return record_owned(tensor_clone(g_tape.entries[x_i].output), 18, x_i, -1);
}

int nt_gqa_causal_attention(int q_i, int k_i, int v_i, int T, int head_dim, int n_heads, int n_kv_heads) {
    (void)k_i; (void)v_i; (void)T; (void)head_dim; (void)n_heads; (void)n_kv_heads;
    return record_owned(tensor_clone(g_tape.entries[q_i].output), 23, q_i, v_i);
}

int nt_add(int a_i, int b_i) {
    nt_tensor *a = g_tape.entries[a_i].output, *b = g_tape.entries[b_i].output;
    nt_tensor *out = tensor_clone(a);
    int n = out->len < b->len ? out->len : b->len;
    for (int i = 0; i < n; ++i) out->data[i] += b->data[i];
    return record_owned(out, 2, a_i, b_i);
}

int nt_mul(int a_i, int b_i) {
    nt_tensor *a = g_tape.entries[a_i].output, *b = g_tape.entries[b_i].output;
    nt_tensor *out = tensor_clone(a);
    int n = out->len < b->len ? out->len : b->len;
    for (int i = 0; i < n; ++i) out->data[i] *= b->data[i];
    return record_owned(out, 3, a_i, b_i);
}

int nt_silu(int x_i) {
    nt_tensor *out = tensor_clone(g_tape.entries[x_i].output);
    for (int i = 0; i < out->len; ++i) out->data[i] /= 1.0f + expf(-out->data[i]);
    return record_owned(out, 7, x_i, -1);
}

int nt_seq_cross_entropy(int logits_i, int targets_i, int T, int V) {
    (void)logits_i; (void)targets_i; (void)T; (void)V;
    nt_tensor *loss = nt_tensor_new(1); loss->data[0] = 4.5f;
    return record_owned(loss, 15, logits_i, targets_i);
}

int nt_save(const char *path, nt_tensor **params, int n) {
    FILE *f = fopen(path, "wb"); if (!f) return -1;
    const uint32_t magic = UINT32_C(0x4254534e);
    if (fwrite(&magic, sizeof(magic), 1, f) != 1 || fwrite(&n, sizeof(n), 1, f) != 1) { fclose(f); return -1; }
    for (int i = 0; i < n; ++i) {
        nt_tensor *t = params[i];
        if (fwrite(&t->ndim, sizeof(int), 1, f) != 1 || fwrite(t->shape, sizeof(int), 8, f) != 8 ||
            fwrite(&t->len, sizeof(int), 1, f) != 1 || fwrite(t->data, sizeof(float), (size_t)t->len, f) != (size_t)t->len) {
            fclose(f); return -1;
        }
    }
    return fclose(f) == 0 ? 0 : -1;
}

nt_tensor **nt_load(const char *path, int *out_n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    uint32_t magic = 0; int n = 0;
    if (fread(&magic, sizeof(magic), 1, f) != 1 || magic != UINT32_C(0x4254534e) || fread(&n, sizeof(n), 1, f) != 1 || n < 0) {
        fclose(f); return NULL;
    }
    nt_tensor **a = (nt_tensor **)calloc((size_t)n, sizeof(*a)); if (!a) fail_alloc();
    for (int i = 0; i < n; ++i) {
        int ndim, shape[8], len;
        if (fread(&ndim, sizeof(int), 1, f) != 1 || fread(shape, sizeof(int), 8, f) != 8 || fread(&len, sizeof(int), 1, f) != 1 || len < 0) {
            fclose(f); return NULL;
        }
        nt_tensor *t = nt_tensor_new((size_t)len); t->ndim = ndim; memcpy(t->shape, shape, sizeof(shape));
        if (fread(t->data, sizeof(float), (size_t)len, f) != (size_t)len) { fclose(f); return NULL; }
        a[i] = t;
    }
    fclose(f); *out_n = n; return a;
}

nt_schedule nt_schedule_cosine(float start_lr, int warmup, int total, float min_lr) {
    nt_schedule s; memset(&s, 0, sizeof(s));
    s.start_lr = start_lr; s.min_lr = min_lr; s.warmup_steps = warmup; s.total_steps = total;
    return s;
}

float nt_schedule_get_lr(nt_schedule *s) {
    float lr = s->start_lr;
    if (s->warmup_steps > 0 && s->current_step < s->warmup_steps)
        lr *= (float)(s->current_step + 1) / (float)s->warmup_steps;
    s->current_step++;
    return lr;
}

nt_nan_guard nt_nan_guard_new(void) { nt_nan_guard g; memset(&g, 0, sizeof(g)); g.loss_scale = 1.0f; return g; }
int nt_nan_guard_check(nt_nan_guard *g) { (void)g; return 1; }
