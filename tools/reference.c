/* SPDX-License-Identifier: GPL-3.0-or-later
 * Integer reference runtime for BFW1. It mirrors bashformer.sh exactly and is
 * used as an oracle for fixed-point parity; it is not the production runtime.
 */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_CAP 262144

typedef struct { int *v; int n; } Tensor;
typedef struct {
    Tensor rms1, wq, wk, wv, wo, rms2, wg, wu, wd;
} Layer;
typedef struct {
    int qshift, vocab, dim, layers, qheads, kvheads, head_dim, ffn, ctx;
    int lut_steps, unk_id, tie_head, attn_scale_q, eps_q2;
    uint8_t *id_to_byte;
    int byte_to_id[256];
    Tensor wte, head, rmsf, rope_cos, rope_sin, exp_neg, sigmoid;
    Layer *layer;
} Model;

typedef struct {
    int **kcache, **vcache;
    int *h, *xn, *q, *k, *v, *attn, *proj, *gate, *up, *act, *down, *hf, *logits;
    int next;
} Runtime;

typedef struct {
    int enabled;
    char velocity[16];
    int velocity_q;
    int prophecy;
    int destiny_q, pain_q, focus_q, spread_q;
    int entropy_floor_q, resonance_ceiling_q;
    int debt_q, debt_decay_q, last_debt_q;
    int recovery_threshold_q, debt_cap_q;
    int field_steps, recoveries, state_loaded;
    int hebbian, learn;
    int64_t *cooc;          /* dense [vocab,vocab], QSHIFT counts */
    int cooc_total;
    int ring[8], ring_n;
    char plasticity[16];
    int plasticity_updates, plasticity_last_gain_q;
    int spa, spa_alpha_q, spa_strength_q, spa_history_max, spa_history_n;
    int spa_sentences, spa_last_conn_q, spa_last_temp_q, spa_pending_boundary;
    int *spa_history;      /* flattened [spa_history_max, dim], QSHIFT */
    int *spa_current;      /* token ids, max ctx */
    int spa_current_n;
    int spa_scale_q;
    const char *soma_path;
} MethodField;

static void die(const char *msg) { fprintf(stderr, "reference: %s\n", msg); exit(1); }
static void *xcalloc(size_t n, size_t z) { void *p = calloc(n, z); if (!p) die("out of memory"); return p; }

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Tensor *tensor_slot(Model *m, const char *name) {
    if (!strcmp(name, "WTE")) return &m->wte;
    if (!strcmp(name, "HEAD")) return &m->head;
    if (!strcmp(name, "RMSF")) return &m->rmsf;
    if (!strcmp(name, "ROPE_COS")) return &m->rope_cos;
    if (!strcmp(name, "ROPE_SIN")) return &m->rope_sin;
    if (!strcmp(name, "EXP_NEG")) return &m->exp_neg;
    if (!strcmp(name, "SIGMOID")) return &m->sigmoid;
    int l = -1; char field[32] = {0};
    if (sscanf(name, "L%d_%31s", &l, field) == 2 && l >= 0 && l < m->layers) {
        Layer *x = &m->layer[l];
        if (!strcmp(field, "RMS1")) return &x->rms1;
        if (!strcmp(field, "WQ")) return &x->wq;
        if (!strcmp(field, "WK")) return &x->wk;
        if (!strcmp(field, "WV")) return &x->wv;
        if (!strcmp(field, "WO")) return &x->wo;
        if (!strcmp(field, "RMS2")) return &x->rms2;
        if (!strcmp(field, "WG")) return &x->wg;
        if (!strcmp(field, "WU")) return &x->wu;
        if (!strcmp(field, "WD")) return &x->wd;
    }
    return NULL;
}

static void set_meta(Model *m, const char *key, int v) {
    if (!strcmp(key, "QSHIFT")) m->qshift = v;
    else if (!strcmp(key, "VOCAB")) m->vocab = v;
    else if (!strcmp(key, "DIM")) m->dim = v;
    else if (!strcmp(key, "LAYERS")) m->layers = v;
    else if (!strcmp(key, "QHEADS")) m->qheads = v;
    else if (!strcmp(key, "KVHEADS")) m->kvheads = v;
    else if (!strcmp(key, "HEAD_DIM")) m->head_dim = v;
    else if (!strcmp(key, "FFN")) m->ffn = v;
    else if (!strcmp(key, "CTX")) m->ctx = v;
    else if (!strcmp(key, "LUT_STEPS")) m->lut_steps = v;
    else if (!strcmp(key, "UNK_ID")) m->unk_id = v;
    else if (!strcmp(key, "TIE_HEAD")) m->tie_head = v;
    else if (!strcmp(key, "ATTN_SCALE_Q")) m->attn_scale_q = v;
    else if (!strcmp(key, "EPS_Q2")) m->eps_q2 = v;
}

static Model load_model(const char *path) {
    Model m; memset(&m, 0, sizeof(m));
    for (int i = 0; i < 256; ++i) m.byte_to_id[i] = -1;
    FILE *f = fopen(path, "r"); if (!f) { perror(path); exit(1); }
    char *line = xcalloc(LINE_CAP, 1); int header = 0;
    Tensor *current = NULL; int at = 0;
    while (fgets(line, LINE_CAP, f)) {
        size_t n = strlen(line); while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
        if (!n || line[0] == '#') continue;
        if (!strcmp(line, "BFW1")) { header++; continue; }
        if (!strncmp(line, "M ", 2)) {
            char key[64]; int v;
            if (sscanf(line, "M %63s %d", key, &v) != 2) die("bad metadata");
            set_meta(&m, key, v);
            if (!strcmp(key, "LAYERS")) m.layer = xcalloc((size_t)v, sizeof(Layer));
            continue;
        }
        if (!strncmp(line, "V ", 2)) {
            const char *hex = line + 2;
            if (m.vocab <= 0 || strlen(hex) != (size_t)m.vocab * 2) die("bad vocabulary");
            m.id_to_byte = xcalloc((size_t)m.vocab, 1);
            for (int i = 0; i < m.vocab; ++i) {
                int a=hexval(hex[2*i]), b=hexval(hex[2*i+1]); if (a < 0 || b < 0) die("bad vocab hex");
                int byte = (a << 4) | b; m.id_to_byte[i] = (uint8_t)byte; m.byte_to_id[byte] = i;
            }
            continue;
        }
        if (!strncmp(line, "T ", 2)) {
            char name[64]; int count = 0;
            if (current || sscanf(line, "T %63s %d", name, &count) != 2 || count < 0) die("bad tensor header");
            current = tensor_slot(&m, name); if (!current) die("unknown tensor name");
            current->n = count; current->v = xcalloc((size_t)count, sizeof(int)); at = 0; continue;
        }
        if (!strncmp(line, "D", 1)) {
            if (!current) die("data outside tensor");
            char *p = line + 1, *end;
            while (*p) {
                while (*p == ' ' || *p == '\t') ++p;
                if (!*p) break;
                errno = 0; long v = strtol(p, &end, 10);
                if (errno || end == p || at >= current->n) die("bad tensor data");
                current->v[at++] = (int)v; p = end;
            }
            continue;
        }
        if (!strcmp(line, "E")) { if (!current || at != current->n) die("bad tensor length"); current=NULL; at=0; continue; }
        if (!strcmp(line, "Z")) break;
        die("unknown record");
    }
    free(line); fclose(f);
    if (header != 1 || current) die("incomplete BFW1");
    if (!m.layer || !m.id_to_byte) die("missing model metadata");
    if (m.dim != m.qheads * m.head_dim || m.qheads % m.kvheads) die("bad head geometry");
    return m;
}

static int qshift(int64_t x, int qshift) {
    int64_t half = (int64_t)1 << (qshift - 1);
    return x >= 0 ? (int)((x + half) >> qshift) : (int)(-(((-x) + half) >> qshift));
}
static int64_t isqrt64(int64_t n) {
    if (n <= 0) return 0;
    int64_t x=n, y=(x+1)/2;
    while (y < x) { x=y; y=(x+n/x)/2; }
    return x;
}
static void matvec(int *out, const int *w, int rows, int cols, const int *x, int qbits) {
    for (int r=0; r<rows; ++r) {
        int64_t s=0;
        for (int c=0;c<cols;++c) s+=(int64_t)w[r*cols+c]*x[c];
        out[r]=qshift(s,qbits);
    }
}
static void rmsnorm(int *out,const int *x,const int *g,int n,int eps) {
    int64_t ss=0; for(int i=0;i<n;++i) ss+=(int64_t)x[i]*x[i]; int64_t rms=isqrt64(ss/n+eps); if(rms<1)rms=1;
    for(int i=0;i<n;++i){int64_t p=(int64_t)x[i]*g[i];out[i]=p>=0?(int)((p+rms/2)/rms):(int)(-(((-p)+rms/2)/rms));}
}
static void rope(int *a,int heads,int pos,const Model*m){int pairs=m->head_dim/2;for(int h=0;h<heads;++h)for(int p=0;p<pairs;++p){int i0=h*m->head_dim+2*p,i1=i0+1,ti=pos*pairs+p;int x0=a[i0],x1=a[i1],co=m->rope_cos.v[ti],si=m->rope_sin.v[ti];a[i0]=qshift((int64_t)x0*co-(int64_t)x1*si,m->qshift);a[i1]=qshift((int64_t)x0*si+(int64_t)x1*co,m->qshift);}}
static int64_t checksum(const int*a,int n){uint64_t s=UINT64_C(1469598103934665603);for(int i=0;i<n;++i){s^=(uint64_t)((int64_t)a[i]+(int64_t)i*1315423911LL);s*=UINT64_C(1099511628211);}return(int64_t)s;}

static Runtime runtime_new(const Model*m){Runtime r;memset(&r,0,sizeof(r));r.kcache=xcalloc(m->layers,sizeof(int*));r.vcache=xcalloc(m->layers,sizeof(int*));int kv=m->kvheads*m->head_dim;for(int l=0;l<m->layers;++l){r.kcache[l]=xcalloc((size_t)m->ctx*kv,sizeof(int));r.vcache[l]=xcalloc((size_t)m->ctx*kv,sizeof(int));}r.h=xcalloc(m->dim,sizeof(int));r.xn=xcalloc(m->dim,sizeof(int));r.q=xcalloc(m->dim,sizeof(int));r.k=xcalloc(kv,sizeof(int));r.v=xcalloc(kv,sizeof(int));r.attn=xcalloc(m->dim,sizeof(int));r.proj=xcalloc(m->dim,sizeof(int));r.gate=xcalloc(m->ffn,sizeof(int));r.up=xcalloc(m->ffn,sizeof(int));r.act=xcalloc(m->ffn,sizeof(int));r.down=xcalloc(m->dim,sizeof(int));r.hf=xcalloc(m->dim,sizeof(int));r.logits=xcalloc(m->vocab,sizeof(int));return r;}

static void tensor_free(Tensor *t) { free(t->v); t->v = NULL; t->n = 0; }

static void model_free(Model *m) {
    tensor_free(&m->wte); tensor_free(&m->head); tensor_free(&m->rmsf);
    tensor_free(&m->rope_cos); tensor_free(&m->rope_sin);
    tensor_free(&m->exp_neg); tensor_free(&m->sigmoid);
    for (int l = 0; l < m->layers; ++l) {
        Layer *x = &m->layer[l];
        tensor_free(&x->rms1); tensor_free(&x->wq); tensor_free(&x->wk);
        tensor_free(&x->wv); tensor_free(&x->wo); tensor_free(&x->rms2);
        tensor_free(&x->wg); tensor_free(&x->wu); tensor_free(&x->wd);
    }
    free(m->layer); m->layer = NULL;
    free(m->id_to_byte); m->id_to_byte = NULL;
}

static void runtime_free(Runtime *r, const Model *m) {
    for (int l = 0; l < m->layers; ++l) {
        free(r->kcache[l]); free(r->vcache[l]);
    }
    free(r->kcache); free(r->vcache);
    free(r->h); free(r->xn); free(r->q); free(r->k); free(r->v);
    free(r->attn); free(r->proj); free(r->gate); free(r->up);
    free(r->act); free(r->down); free(r->hf); free(r->logits);
    memset(r, 0, sizeof(*r));
}

static void attention(int*out,const int*q,int layer,int pos,const Model*m,Runtime*r){int kvdim=m->kvheads*m->head_dim,qpk=m->qheads/m->kvheads,qs=1<<m->qshift,qhalf=qs>>1,expmax=16*m->lut_steps;int*scores=xcalloc(pos+1,sizeof(int)),*exps=xcalloc(pos+1,sizeof(int));for(int h=0;h<m->qheads;++h){int kvh=h/qpk,qoff=h*m->head_dim,max=INT32_MIN;for(int t=0;t<=pos;++t){int koff=t*kvdim+kvh*m->head_dim;int64_t raw=0;for(int d=0;d<m->head_dim;++d)raw+=(int64_t)q[qoff+d]*r->kcache[layer][koff+d];int64_t scaled=raw*m->attn_scale_q,half2=(int64_t)1<<(2*m->qshift-1);int score=scaled>=0?(int)((scaled+half2)>>(2*m->qshift)):(int)(-(((-scaled)+half2)>>(2*m->qshift)));scores[t]=score;if(score>max)max=score;}int64_t sum=0;for(int t=0;t<=pos;++t){int diff=max-scores[t];int idx=(diff*m->lut_steps+qhalf)>>m->qshift;if(idx<0)idx=0;if(idx>expmax)idx=expmax;exps[t]=m->exp_neg.v[idx];sum+=exps[t];}if(sum<=0)sum=1;for(int d=0;d<m->head_dim;++d){int64_t num=0;for(int t=0;t<=pos;++t){int koff=t*kvdim+kvh*m->head_dim;num+=(int64_t)exps[t]*r->vcache[layer][koff+d];}out[qoff+d]=num>=0?(int)((num+sum/2)/sum):(int)(-(((-num)+sum/2)/sum));}}free(scores);free(exps);}

static int parse_decimal_q(const char *text, int qs, int *out) {
    if (!text || !*text) return -1;
    int sign = 1;
    if (*text == '-') { sign = -1; ++text; }
    if (!*text) return -1;
    const char *dot = strchr(text, '.');
    size_t whole_n = dot ? (size_t)(dot - text) : strlen(text);
    size_t frac_n = dot ? strlen(dot + 1) : 0;
    if (whole_n == 0 || whole_n > 4 || frac_n > 6) return -1;
    if (dot && frac_n == 0) return -1;
    int whole = 0, frac = 0, scale = 1;
    for (size_t i = 0; i < whole_n; ++i) {
        if (text[i] < '0' || text[i] > '9') return -1;
        whole = whole * 10 + (text[i] - '0');
    }
    if (whole > 1000) return -1;
    if (dot) {
        for (size_t i = 0; i < frac_n; ++i) {
            char c = dot[1 + i];
            if (c < '0' || c > '9') return -1;
            frac = frac * 10 + (c - '0');
            scale *= 10;
        }
    }
    int64_t value = (int64_t)whole * qs + ((int64_t)frac * qs + scale / 2) / scale;
    value *= sign;
    if (value < INT32_MIN || value > INT32_MAX) return -1;
    *out = (int)value;
    return 0;
}

static int parse_nonnegative(const char *text, int *out) {
    if (!text || !*text) return -1;
    if (text[0] == '+' || text[0] == '-') return -1;
    if (text[0] == '0' && text[1]) return -1;
    int64_t v = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        v = v * 10 + (*p - '0');
        if (v > INT32_MAX) return -1;
    }
    *out = (int)v;
    return 0;
}

static int velocity_multiplier(const char *mode, int qs, int *out) {
    if (!strcmp(mode, "raw")) *out = qs;
    else if (!strcmp(mode, "nomove")) *out = qs / 2;
    else if (!strcmp(mode, "walk")) *out = (85 * qs + 50) / 100;
    else if (!strcmp(mode, "run")) *out = (120 * qs + 50) / 100;
    else if (!strcmp(mode, "breathe")) *out = (60 * qs + 50) / 100;
    else return -1;
    return 0;
}

static int qmul(int a, int b, int qbits) {
    return qshift((int64_t)a * b, qbits);
}

static void set_velocity(MethodField *f, const char *mode, int qs) {
    int q = 0;
    if (velocity_multiplier(mode, qs, &q)) die("bad velocity");
    if (strlen(mode) >= sizeof(f->velocity)) die("velocity name too long");
    strcpy(f->velocity, mode);
    f->velocity_q = q;
}

static void method_defaults(MethodField *f, const Model *m) {
    const int qs = 1 << m->qshift;
    set_velocity(f, "raw", qs);
    f->debt_q = 0;
    f->last_debt_q = 0;
    f->debt_decay_q = (998 * qs + 500) / 1000;
    f->entropy_floor_q = (10 * qs + 50) / 100;
    f->resonance_ceiling_q = (95 * qs + 50) / 100;
    f->recovery_threshold_q = 5 * qs;
    f->debt_cap_q = 100 * qs;
    f->field_steps = 0;
    f->recoveries = 0;
    f->state_loaded = 0;
    strcpy(f->plasticity, "flat");
    f->plasticity_updates = 0;
    f->plasticity_last_gain_q = qs;
    f->spa_alpha_q = (85 * qs + 50) / 100;
    f->spa_strength_q = (30 * qs + 50) / 100;
    f->spa_history_max = 8;
    f->spa_history_n = 0;
    f->spa_sentences = 0;
    f->spa_last_conn_q = 0;
    f->spa_last_temp_q = qs;
    f->spa_pending_boundary = 0;
    int64_t root = isqrt64((int64_t)m->dim * qs * qs);
    if (root < 1) root = 1;
    f->spa_scale_q = (int)(((int64_t)qs * qs + root / 2) / root);
}

static int effective_temp_q(const MethodField *f, int base_temp_q, int qbits) {
    if (!f || !f->enabled || base_temp_q <= 0) return base_temp_q;
    int t = qmul(base_temp_q, f->velocity_q, qbits);
    return t > 0 ? t : 1;
}

static int greedy_id(const int *logits, int n) {
    int best = 0;
    for (int i = 1; i < n; ++i) if (logits[i] > logits[best]) best = i;
    return best;
}

static int cooc_edges(const MethodField *f, int vocab) {
    if (!f || !f->cooc) return 0;
    int n = 0;
    for (int i = 0; i < vocab * vocab; ++i) if (f->cooc[i] > 0) ++n;
    return n;
}

static int div_round64(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    return num >= 0 ? (int)((num + den / 2) / den)
                    : (int)(-(((-num) + den / 2) / den));
}

static int plasticity_gain_q(const MethodField *f, int qs) {
    if (!strcmp(f->plasticity, "debt")) {
        int64_t den = (int64_t)f->debt_q + 5 * qs;
        if (den > 0) return qs + (int)(((int64_t)f->debt_q * qs + den / 2) / den);
    }
    return qs;
}

static int spa_is_boundary(const Model *m, int token) {
    if (token < 0 || token >= m->vocab) return 0;
    uint8_t b = m->id_to_byte[token];
    return b == '\n' || b == '!' || b == '.' || b == '?';
}

static void spa_observe(MethodField *f, const Model *m, int token) {
    if (!f || !f->enabled || !f->spa) return;
    if (token < 0 || token >= m->vocab) token = m->unk_id;
    if (f->spa_current_n < m->ctx) f->spa_current[f->spa_current_n++] = token;
    else {
        memmove(f->spa_current, f->spa_current + 1, (size_t)(m->ctx - 1) * sizeof(int));
        f->spa_current[m->ctx - 1] = token;
    }
    f->spa_pending_boundary = spa_is_boundary(m, token);
}

static int spa_sentence_embedding(const MethodField *f, const Model *m, int *out) {
    if (!f || f->spa_current_n <= 0) return -1;
    int qs = 1 << m->qshift;
    int64_t *sum = xcalloc((size_t)m->dim, sizeof(int64_t));
    int weight = qs;
    int64_t sumw = 0;
    for (int i = f->spa_current_n - 1; i >= 0; --i) {
        int token = f->spa_current[i];
        int base = token * m->dim;
        for (int d = 0; d < m->dim; ++d)
            sum[d] += (int64_t)m->wte.v[base + d] * weight;
        sumw += weight;
        weight = qmul(weight, f->spa_alpha_q, m->qshift);
    }
    if (sumw <= 0) sumw = 1;
    for (int d = 0; d < m->dim; ++d) out[d] = div_round64(sum[d], sumw);
    free(sum);
    return 0;
}

static int spa_connectedness(MethodField *f, const Model *m) {
    const int qs = 1 << m->qshift;
    f->spa_last_conn_q = 0;
    f->spa_last_temp_q = qs;
    if (!f->enabled || !f->spa || f->spa_history_n <= 0 || f->spa_current_n <= 0) return 0;
    int *query = xcalloc((size_t)m->dim, sizeof(int));
    int *scores = xcalloc((size_t)f->spa_history_n, sizeof(int));
    int *exps = xcalloc((size_t)f->spa_history_n, sizeof(int));
    if (spa_sentence_embedding(f, m, query)) { free(query); free(scores); free(exps); return 0; }
    int max = INT32_MIN;
    int64_t half2 = (int64_t)1 << (2 * m->qshift - 1);
    for (int s = 0; s < f->spa_history_n; ++s) {
        int64_t dot = 0;
        int off = s * m->dim;
        for (int d = 0; d < m->dim; ++d) dot += (int64_t)query[d] * f->spa_history[off + d];
        int64_t scaled = dot * f->spa_scale_q;
        int score = scaled >= 0 ? (int)((scaled + half2) >> (2 * m->qshift))
                                : (int)(-(((-scaled) + half2) >> (2 * m->qshift)));
        scores[s] = score;
        if (score > max) max = score;
    }
    int64_t sum = 0;
    int maxexp = 0;
    int expmax = 16 * m->lut_steps;
    int qhalf = qs >> 1;
    for (int s = 0; s < f->spa_history_n; ++s) {
        int diff = max - scores[s];
        int idx = (diff * m->lut_steps + qhalf) >> m->qshift;
        if (idx < 0) idx = 0;
        if (idx > expmax) idx = expmax;
        exps[s] = m->exp_neg.v[idx];
        sum += exps[s];
        if (exps[s] > maxexp) maxexp = exps[s];
    }
    int conn = sum > 0 ? (int)(((int64_t)maxexp * qs + sum / 2) / sum) : 0;
    if (conn < 0) conn = 0;
    if (conn > qs) conn = qs;
    f->spa_last_conn_q = conn;
    free(query); free(scores); free(exps);
    return conn;
}

static void apply_spa(int *logits, int n, MethodField *f, const Model *m, int trace) {
    if (!f || !f->enabled || !f->spa) return;
    int qs = 1 << m->qshift;
    int conn = spa_connectedness(f, m);
    if (conn > 0 && f->spa_strength_q > 0) {
        int temp_q = qs - qmul(f->spa_strength_q, conn, m->qshift);
        int min_temp = (qs + 500) / 1000;
        if (min_temp < 1) min_temp = 1;
        if (temp_q < min_temp) temp_q = min_temp;
        f->spa_last_temp_q = temp_q;
        for (int i = 0; i < n; ++i)
            logits[i] = div_round64((int64_t)logits[i] * qs, temp_q);
    }
    if (trace) {
        fprintf(stderr, "TRACE spa conn=%d temp=%d current=%d history=%d sentences=%d alpha=%d strength=%d\n",
                f->spa_last_conn_q, f->spa_last_temp_q, f->spa_current_n,
                f->spa_history_n, f->spa_sentences, f->spa_alpha_q, f->spa_strength_q);
    }
}

static void spa_commit_if_boundary(MethodField *f, const Model *m) {
    if (!f || !f->enabled || !f->spa || !f->spa_pending_boundary) return;
    if (f->learn) {
        int *emb = xcalloc((size_t)m->dim, sizeof(int));
        if (!spa_sentence_embedding(f, m, emb)) {
            if (f->spa_history_n >= f->spa_history_max) {
                memmove(f->spa_history, f->spa_history + m->dim,
                        (size_t)(f->spa_history_n - 1) * m->dim * sizeof(int));
                --f->spa_history_n;
            }
            memcpy(f->spa_history + (size_t)f->spa_history_n * m->dim,
                   emb, (size_t)m->dim * sizeof(int));
            ++f->spa_history_n;
            ++f->spa_sentences;
        }
        free(emb);
    }
    f->spa_current_n = 0;
    f->spa_pending_boundary = 0;
}

static void hebbian_ingest(MethodField *f, const Model *m, int token, int trace) {
    if (!f || !f->enabled || !f->hebbian || !f->learn) return;
    const int qs = 1 << m->qshift;
    const int64_t cap = (int64_t)1000000 * qs;
    int gain = plasticity_gain_q(f, qs);
    f->plasticity_last_gain_q = gain;
    if (token < 0 || token >= m->vocab) token = m->unk_id;
    for (int d = 1; d <= 5 && d <= f->ring_n; ++d) {
        int prev = f->ring[f->ring_n - d];
        int base_delta = (qs + d / 2) / d;
        int delta = qmul(base_delta, gain, m->qshift);
        int a = token * m->vocab + prev, b = prev * m->vocab + token;
        if (a == b) {
            int64_t v = (int64_t)f->cooc[a] + 2LL * delta;
            f->cooc[a] = v > cap ? cap : v;
        } else {
            int64_t va = (int64_t)f->cooc[a] + delta, vb = (int64_t)f->cooc[b] + delta;
            f->cooc[a] = va > cap ? cap : va;
            f->cooc[b] = vb > cap ? cap : vb;
        }
    }
    ++f->cooc_total;
    if (!strcmp(f->plasticity, "debt")) ++f->plasticity_updates;
    if (f->ring_n < 8) f->ring[f->ring_n++] = token;
    else {
        memmove(f->ring, f->ring + 1, 7 * sizeof(int));
        f->ring[7] = token;
    }
    if (trace && !strcmp(f->plasticity, "debt")) {
        fprintf(stderr, "TRACE plasticity token=%d debt=%d gain=%d updates=%d\n",
                token, f->debt_q, gain, f->plasticity_updates);
    }
}

static void apply_hebbian(int *logits, int n, int qbits, const MethodField *f) {
    if (!f || !f->enabled || !f->hebbian || !f->cooc || f->ring_n <= 0) return;
    const int qs = 1 << qbits;
    int64_t *h = xcalloc((size_t)n, sizeof(int64_t));
    int64_t hmax = 0;
    for (int c = 0; c < f->ring_n; ++c) {
        int src = f->ring[c];
        int denom = f->ring_n - c;
        int decay = (qs + denom / 2) / denom;
        for (int dst = 0; dst < n; ++dst) {
            int64_t cnt = f->cooc[src * n + dst];
            if (cnt <= 0) continue;
            h[dst] += ((int64_t)cnt * decay + qs / 2) >> qbits;
        }
    }
    for (int i = 0; i < n; ++i) if (h[i] > hmax) hmax = h[i];
    if (hmax > 0) {
        for (int i = 0; i < n; ++i) {
            int64_t delta = ((int64_t)2 * qs * h[i] + hmax / 2) / hmax;
            logits[i] += (int)delta;
        }
    }
    free(h);
}

static long parse_long_token(char **pp, const char *what) {
    char *p = *pp, *end = NULL;
    while (*p == ' ') ++p;
    if (!*p) die(what);
    errno = 0;
    long v = strtol(p, &end, 10);
    if (errno || end == p) die(what);
    *pp = end;
    return v;
}

static void require_line_end(char *p, const char *what) {
    while (*p == ' ') ++p;
    if (*p) die(what);
}

static void soma_load(MethodField *f, const Model *m, const char *path) {
    if (!path || !*path) return;
    FILE *fp = fopen(path, "r");
    if (!fp) {
        if (errno == ENOENT) return;
        die("cannot read soma");
    }
    char line[LINE_CAP];
    int version=0, mv=0, mq=0, mtotal=0, ring_seen=0, end=0;
    int md=0, mdecay=0, mfloor=0, mceiling=0, mvelocity=0, msteps=0, mrecoveries=0;
    int mspa=0, malpha=0, mstrength=0, mhmax=0, mhn=0, msent=0, mconn=0, mtemp=0;
    int mplasticity=0, mpupdates=0, mpgain=0, current_seen=0, sentence_seen=0;
    memset(f->cooc, 0, (size_t)m->vocab * m->vocab * sizeof(int64_t));
    memset(f->spa_history, 0, (size_t)8 * m->dim * sizeof(int));
    memset(f->spa_current, 0, (size_t)m->ctx * sizeof(int));
    f->cooc_total=0; f->ring_n=0; f->spa_history_n=0; f->spa_current_n=0;
    f->spa_pending_boundary=0;
    const int qs = 1 << m->qshift;
    const int64_t cap = (int64_t)1000000 * qs;
    while (fgets(line, sizeof(line), fp)) {
        size_t len=strlen(line);
        while(len && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]=0;
        if (!len || line[0]=='#') continue;
        if (end) die("data after soma end marker");
        if (!strcmp(line,"BFSOMA1") || !strcmp(line,"BFSOMA2") || !strcmp(line,"BFSOMA3")) {
            if(version) die("duplicate soma header");
            version = !strcmp(line,"BFSOMA1") ? 1 : (!strcmp(line,"BFSOMA2") ? 2 : 3);
            continue;
        }
        if (!version) die("soma record before header");
        if (!strcmp(line,"Z")) { end=1; continue; }

        char key[40], value[80], tail;
        if (sscanf(line,"M %39s %79s %c",key,value,&tail)==2) {
            int a=0;
            if(!strcmp(key,"VELOCITY")) {
                if(mvelocity) die("duplicate soma VELOCITY");
                if(version<2 || velocity_multiplier(value,qs,&a)) die("bad soma VELOCITY");
                set_velocity(f,value,qs); mvelocity=1; continue;
            }
            if(!strcmp(key,"PLASTICITY")) {
                if(mplasticity || version!=3) die("bad/duplicate soma PLASTICITY");
                if(strcmp(value,"flat") && strcmp(value,"debt")) die("bad soma PLASTICITY");
                if(strlen(value)>=sizeof(f->plasticity)) die("plasticity name too long");
                strcpy(f->plasticity,value); mplasticity=1; continue;
            }
            if(parse_nonnegative(value,&a)) die("bad soma metadata value");
            if(!strcmp(key,"VOCAB")){ if(mv) die("duplicate soma VOCAB"); if(a!=m->vocab) die("soma VOCAB mismatch"); mv=1; }
            else if(!strcmp(key,"QSHIFT")){ if(mq) die("duplicate soma QSHIFT"); if(a!=m->qshift) die("soma QSHIFT mismatch"); mq=1; }
            else if(!strcmp(key,"COOC_TOTAL")){ if(mtotal) die("duplicate soma COOC_TOTAL"); f->cooc_total=a; mtotal=1; }
            else if(!strcmp(key,"DEBT_Q")){ if(md||version<2||a>f->debt_cap_q) die("bad/duplicate soma DEBT_Q"); f->debt_q=a; md=1; }
            else if(!strcmp(key,"DEBT_DECAY_Q")){ if(mdecay||version<2||a>qs) die("bad/duplicate soma DEBT_DECAY_Q"); f->debt_decay_q=a; mdecay=1; }
            else if(!strcmp(key,"ENTROPY_FLOOR_Q")){ if(mfloor||version<2||a>qs) die("bad/duplicate soma ENTROPY_FLOOR_Q"); f->entropy_floor_q=a; mfloor=1; }
            else if(!strcmp(key,"RESONANCE_CEILING_Q")){ if(mceiling||version<2||a>qs) die("bad/duplicate soma RESONANCE_CEILING_Q"); f->resonance_ceiling_q=a; mceiling=1; }
            else if(!strcmp(key,"FIELD_STEPS")){ if(msteps||version<2) die("bad/duplicate soma FIELD_STEPS"); f->field_steps=a; msteps=1; }
            else if(!strcmp(key,"RECOVERIES")){ if(mrecoveries||version<2) die("bad/duplicate soma RECOVERIES"); f->recoveries=a; mrecoveries=1; }
            else if(!strcmp(key,"SPA_ENABLED")){ if(mspa||version!=3||a>1) die("bad/duplicate soma SPA_ENABLED"); f->spa=a; mspa=1; }
            else if(!strcmp(key,"SPA_ALPHA_Q")){ if(malpha||version!=3||a>qs) die("bad/duplicate soma SPA_ALPHA_Q"); f->spa_alpha_q=a; malpha=1; }
            else if(!strcmp(key,"SPA_STRENGTH_Q")){ if(mstrength||version!=3||a>qs) die("bad/duplicate soma SPA_STRENGTH_Q"); f->spa_strength_q=a; mstrength=1; }
            else if(!strcmp(key,"SPA_HISTORY_MAX")){ if(mhmax||version!=3||a<1||a>8) die("bad/duplicate soma SPA_HISTORY_MAX"); f->spa_history_max=a; mhmax=1; }
            else if(!strcmp(key,"SPA_HISTORY_N")){ if(mhn||version!=3||a>8) die("bad/duplicate soma SPA_HISTORY_N"); f->spa_history_n=a; mhn=1; }
            else if(!strcmp(key,"SPA_SENTENCES")){ if(msent||version!=3) die("bad/duplicate soma SPA_SENTENCES"); f->spa_sentences=a; msent=1; }
            else if(!strcmp(key,"SPA_LAST_CONN_Q")){ if(mconn||version!=3||a>qs) die("bad/duplicate soma SPA_LAST_CONN_Q"); f->spa_last_conn_q=a; mconn=1; }
            else if(!strcmp(key,"SPA_LAST_TEMP_Q")){ if(mtemp||version!=3||a<1||a>qs) die("bad/duplicate soma SPA_LAST_TEMP_Q"); f->spa_last_temp_q=a; mtemp=1; }
            else if(!strcmp(key,"PLASTICITY_UPDATES")){ if(mpupdates||version!=3) die("bad/duplicate soma PLASTICITY_UPDATES"); f->plasticity_updates=a; mpupdates=1; }
            else if(!strcmp(key,"PLASTICITY_LAST_GAIN_Q")){ if(mpgain||version!=3||a<qs||a>2*qs) die("bad/duplicate soma PLASTICITY_LAST_GAIN_Q"); f->plasticity_last_gain_q=a; mpgain=1; }
            else die("unknown soma metadata");
            continue;
        }

        int a,b; long long c;
        if (sscanf(line,"E %d %d %lld %c",&a,&b,&c,&tail)==3) {
            if(!mv||!mq||a<0||a>=m->vocab||b<0||b>=m->vocab||c<=0||c>cap) die("bad soma edge");
            int idx=a*m->vocab+b;
            if(f->cooc[idx]) die("duplicate soma edge");
            f->cooc[idx]=(int64_t)c;
            continue;
        }
        if (line[0]=='R' && line[1]==' ') {
            if(!mv||!mq||ring_seen) die("bad soma ring");
            char *p=line+2; long rn=parse_long_token(&p,"bad soma ring length");
            if (rn < 0 || rn > 8) die("bad soma ring length");
            f->ring_n=(int)rn;
            for(int i=0;i<f->ring_n;++i){
                long id=parse_long_token(&p,"bad soma ring token");
                if(id<0||id>=m->vocab) die("bad soma ring token");
                f->ring[i]=(int)id;
            }
            require_line_end(p,"extra soma ring data");
            ring_seen=1; continue;
        }
        if (line[0]=='S' && line[1]==' ') {
            if(version!=3||!mhn||!mhmax) die("SPA sentence before BFSOMA3 metadata");
            char *p=line+2;
            long idx=parse_long_token(&p,"bad SPA sentence index");
            long dim=parse_long_token(&p,"bad SPA sentence dimension");
            if(idx!=sentence_seen||idx>=f->spa_history_n||dim!=m->dim) die("bad SPA sentence index/dimension");
            for(int d=0;d<m->dim;++d){
                long v=parse_long_token(&p,"bad SPA sentence value");
                if(v<-(1<<26)||v>(1<<26)) die("SPA sentence value outside safe range");
                f->spa_history[(size_t)idx*m->dim+d]=(int)v;
            }
            require_line_end(p,"extra SPA sentence data");
            ++sentence_seen; continue;
        }
        if (line[0]=='C' && line[1]==' ') {
            if(version!=3||current_seen) die("bad/duplicate SPA current sentence");
            char *p=line+2;
            long n=parse_long_token(&p,"bad SPA current length");
            if(n<0||n>m->ctx) die("bad SPA current length");
            f->spa_current_n=(int)n;
            for(int i=0;i<f->spa_current_n;++i){
                long id=parse_long_token(&p,"bad SPA current token");
                if(id<0||id>=m->vocab) die("bad SPA current token");
                f->spa_current[i]=(int)id;
            }
            require_line_end(p,"extra SPA current data");
            current_seen=1; continue;
        }
        die("unknown soma record");
    }
    fclose(fp);
    if(!version||!mv||!mq||!mtotal||!ring_seen||!end) die("incomplete soma");
    if(version>=2 && !(md&&mdecay&&mfloor&&mceiling&&mvelocity&&msteps&&mrecoveries))
        die("incomplete BFSOMA2+ field state");
    if(version==3){
        if(!(mspa&&malpha&&mstrength&&mhmax&&mhn&&msent&&mconn&&mtemp&&mplasticity&&mpupdates&&mpgain&&current_seen))
            die("incomplete BFSOMA3 resonance state");
        if(f->spa_history_n>f->spa_history_max||sentence_seen!=f->spa_history_n||f->spa_sentences<f->spa_history_n)
            die("inconsistent BFSOMA3 SPA history");
    }
    f->spa_pending_boundary = f->spa_current_n > 0 && spa_is_boundary(m,f->spa_current[f->spa_current_n-1]);
    f->state_loaded=version;
}

static void soma_save(const MethodField *f, const Model *m, const char *path) {
    if (!path || !*path || !f->enabled || !f->learn) return;
    FILE *fp=fopen(path,"w"); if(!fp) die("cannot write soma");
    fprintf(fp,"BFSOMA3\nM VOCAB %d\nM QSHIFT %d\nM COOC_TOTAL %d\n",m->vocab,m->qshift,f->cooc_total);
    fprintf(fp,"M DEBT_Q %d\nM DEBT_DECAY_Q %d\n",f->debt_q,f->debt_decay_q);
    fprintf(fp,"M ENTROPY_FLOOR_Q %d\nM RESONANCE_CEILING_Q %d\n",f->entropy_floor_q,f->resonance_ceiling_q);
    fprintf(fp,"M VELOCITY %s\nM FIELD_STEPS %d\nM RECOVERIES %d\n",f->velocity,f->field_steps,f->recoveries);
    fprintf(fp,"M PLASTICITY %s\nM PLASTICITY_UPDATES %d\nM PLASTICITY_LAST_GAIN_Q %d\n",
            f->plasticity,f->plasticity_updates,f->plasticity_last_gain_q);
    fprintf(fp,"M SPA_ENABLED %d\nM SPA_ALPHA_Q %d\nM SPA_STRENGTH_Q %d\n",f->spa,f->spa_alpha_q,f->spa_strength_q);
    fprintf(fp,"M SPA_HISTORY_MAX %d\nM SPA_HISTORY_N %d\n",f->spa_history_max,f->spa_history_n);
    fprintf(fp,"M SPA_SENTENCES %d\nM SPA_LAST_CONN_Q %d\nM SPA_LAST_TEMP_Q %d\n",
            f->spa_sentences,f->spa_last_conn_q,f->spa_last_temp_q);
    for(int src=0;src<m->vocab;++src) for(int dst=0;dst<m->vocab;++dst){
        int64_t v=f->cooc[src*m->vocab+dst];
        if(v>0) fprintf(fp,"E %d %d %" PRId64 "\n",src,dst,v);
    }
    fprintf(fp,"R %d",f->ring_n);
    for(int i=0;i<f->ring_n;++i) fprintf(fp," %d",f->ring[i]);
    fputc('\n',fp);
    for(int s=0;s<f->spa_history_n;++s){
        fprintf(fp,"S %d %d",s,m->dim);
        for(int d=0;d<m->dim;++d) fprintf(fp," %d",f->spa_history[(size_t)s*m->dim+d]);
        fputc('\n',fp);
    }
    fprintf(fp,"C %d",f->spa_current_n);
    for(int i=0;i<f->spa_current_n;++i) fprintf(fp," %d",f->spa_current[i]);
    fprintf(fp,"\nZ\n");
    if(fclose(fp)!=0) die("cannot close soma");
}

static void apply_method(int *logits, int n, const Model *m, MethodField *f, int trace) {
    if (!f || !f->enabled) return;
    const int qbits = m->qshift;
    const int qs = 1 << qbits;

    apply_hebbian(logits, n, qbits, f);
    apply_spa(logits, n, f, m, trace);

    if (f->destiny_q > 0) {
        int prophecy_scale = qs + (f->prophecy - 7) * ((2 * qs + 50) / 100);
        if (prophecy_scale < qs / 2) prophecy_scale = qs / 2;
        if (prophecy_scale > 2 * qs) prophecy_scale = 2 * qs;
        int destiny_bias = qmul(f->destiny_q, prophecy_scale, qbits);
        int max = logits[0];
        for (int i = 1; i < n; ++i) if (logits[i] > max) max = logits[i];
        for (int i = 0; i < n; ++i) {
            int diff = max - logits[i];
            int64_t product = (int64_t)diff * destiny_bias;
            int suppress = (int)((product + qs) / (2 * qs));
            logits[i] -= suppress;
        }
    }

    if (f->pain_q > 0) {
        int64_t sum = 0;
        for (int i = 0; i < n; ++i) sum += logits[i];
        int mean = (int)(sum / n);
        int factor = qs - (f->pain_q + 1) / 2;
        for (int i = 0; i < n; ++i) {
            int deviation = logits[i] - mean;
            logits[i] = mean + qmul(deviation, factor, qbits);
        }
    }

    int scale = qs / 2 + f->focus_q - f->spread_q;
    int min_scale = (qs + 5) / 10;
    if (scale < min_scale) scale = min_scale;
    if (scale > 2 * qs) scale = 2 * qs;
    if (scale != qs) {
        int64_t sum = 0;
        for (int i = 0; i < n; ++i) sum += logits[i];
        int mean = (int)(sum / n);
        for (int i = 0; i < n; ++i) {
            int deviation = logits[i] - mean;
            logits[i] = mean + qmul(deviation, scale, qbits);
        }
    }

    if (n >= 2) {
        int max = logits[0], second = INT32_MIN;
        for (int i = 1; i < n; ++i) {
            if (logits[i] > max) { second = max; max = logits[i]; }
            else if (logits[i] > second) second = logits[i];
        }
        int gap = max - second;
        if (gap > 0 && f->entropy_floor_q > 0) {
            int max_gap = 10 * (qs - f->entropy_floor_q);
            if (gap > max_gap) {
                int reduce = (gap - max_gap + 1) / 2;
                for (int i = 0; i < n; ++i) if (logits[i] == max) logits[i] -= reduce;
            }
        }
        if (f->resonance_ceiling_q < qs) {
            int ceiling_gap = 10 * f->resonance_ceiling_q;
            if (gap > ceiling_gap) {
                int reduce = ((gap - ceiling_gap) * 3 + 5) / 10;
                int threshold = max - (qs + 500) / 1000;
                for (int i = 0; i < n; ++i) if (logits[i] >= threshold) logits[i] -= reduce;
            }
        }
    }
}

static int compute_choice_debt(const int *logits, int n, int chosen, int qbits) {
    if (!logits || n <= 0 || chosen < 0 || chosen >= n) return 0;
    const int qs = 1 << qbits;
    int max = logits[0];
    for (int i = 1; i < n; ++i) if (logits[i] > max) max = logits[i];
    int diff = max - logits[chosen];
    if (diff <= 0) return 0;
    int denom = diff + qs;
    return (int)(((int64_t)diff * qs + denom / 2) / denom);
}

static void register_choice_debt(MethodField *f, const Model *m, const Runtime *r, int chosen) {
    f->last_debt_q = 0;
    if (!f->enabled || !f->learn) return;
    int add = compute_choice_debt(r->logits, m->vocab, chosen, m->qshift);
    f->last_debt_q = add;
    int64_t total = (int64_t)f->debt_q + add;
    f->debt_q = total > f->debt_cap_q ? f->debt_cap_q : (int)total;
}

static void field_step(MethodField *f, const Model *m, int chosen, int base_temp_q, int trace) {
    if (!f->enabled || !f->learn) return;
    f->debt_q = qmul(f->debt_q, f->debt_decay_q, m->qshift);
    ++f->field_steps;
    int recovered = 0;
    if (f->debt_q > f->recovery_threshold_q && strcmp(f->velocity, "nomove")) {
        set_velocity(f, "nomove", 1 << m->qshift);
        ++f->recoveries;
        recovered = 1;
    }
    if (trace) {
        int temp_now = effective_temp_q(f, base_temp_q, m->qshift);
        fprintf(stderr, "TRACE debt chosen=%d add=%d total=%d velocity=%s temp=%d recovered=%d step=%d recoveries=%d\n",
                chosen, f->last_debt_q, f->debt_q, f->velocity, temp_now,
                recovered, f->field_steps, f->recoveries);
    }
}

static uint32_t rng_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x & UINT32_C(0x7fffffff);
}

static int pick_next(const Runtime *r, const Model *m, int temp_q, uint32_t *rng) {
    if (temp_q <= 0) return r->next;
    const int qs = 1 << m->qshift;
    const int qhalf = qs >> 1;
    const int expmax = 16 * m->lut_steps;
    int *scaled = xcalloc((size_t)m->vocab, sizeof(int));
    int *exps = xcalloc((size_t)m->vocab, sizeof(int));
    int max = INT32_MIN;
    for (int i = 0; i < m->vocab; ++i) {
        int64_t a = (int64_t)r->logits[i] * qs;
        scaled[i] = a >= 0 ? (int)((a + temp_q / 2) / temp_q)
                           : (int)(-(((-a) + temp_q / 2) / temp_q));
        if (scaled[i] > max) max = scaled[i];
    }
    int64_t sum = 0;
    for (int i = 0; i < m->vocab; ++i) {
        int diff = max - scaled[i];
        int idx = (diff * m->lut_steps + qhalf) >> m->qshift;
        if (idx < 0) idx = 0;
        if (idx > expmax) idx = expmax;
        exps[i] = m->exp_neg.v[idx];
        sum += exps[i];
    }
    int result = r->next;
    if (sum > 0) {
        int64_t target = (int64_t)rng_next(rng) % sum;
        int64_t cumulative = 0;
        result = m->vocab - 1;
        for (int i = 0; i < m->vocab; ++i) {
            cumulative += exps[i];
            if (target < cumulative) { result = i; break; }
        }
    }
    free(scaled); free(exps);
    return result;
}

static void decode(int token, int pos, const Model *m, Runtime *r,
                   MethodField *field, int trace) {
    int kvdim = m->kvheads * m->head_dim;
    int qs = 1 << m->qshift, qhalf = qs >> 1, sigmax = 16 * m->lut_steps;
    if (token < 0 || token >= m->vocab) token = m->unk_id;
    memcpy(r->h, m->wte.v + token * m->dim, (size_t)m->dim * sizeof(int));
    for (int l = 0; l < m->layers; ++l) {
        Layer *x = &m->layer[l];
        rmsnorm(r->xn, r->h, x->rms1.v, m->dim, m->eps_q2);
        matvec(r->q, x->wq.v, m->dim, m->dim, r->xn, m->qshift);
        matvec(r->k, x->wk.v, kvdim, m->dim, r->xn, m->qshift);
        matvec(r->v, x->wv.v, kvdim, m->dim, r->xn, m->qshift);
        rope(r->q, m->qheads, pos, m);
        rope(r->k, m->kvheads, pos, m);
        memcpy(r->kcache[l] + pos * kvdim, r->k, (size_t)kvdim * sizeof(int));
        memcpy(r->vcache[l] + pos * kvdim, r->v, (size_t)kvdim * sizeof(int));
        attention(r->attn, r->q, l, pos, m, r);
        matvec(r->proj, x->wo.v, m->dim, m->dim, r->attn, m->qshift);
        for (int i = 0; i < m->dim; ++i) r->h[i] += r->proj[i];
        rmsnorm(r->xn, r->h, x->rms2.v, m->dim, m->eps_q2);
        matvec(r->gate, x->wg.v, m->ffn, m->dim, r->xn, m->qshift);
        matvec(r->up, x->wu.v, m->ffn, m->dim, r->xn, m->qshift);
        for (int i = 0; i < m->ffn; ++i) {
            int idx = ((r->gate[i] + 8 * qs) * m->lut_steps + qhalf) >> m->qshift;
            if (idx < 0) idx = 0;
            if (idx > sigmax) idx = sigmax;
            int silu = qshift((int64_t)r->gate[i] * m->sigmoid.v[idx], m->qshift);
            r->act[i] = qshift((int64_t)silu * r->up[i], m->qshift);
        }
        matvec(r->down, x->wd.v, m->dim, m->ffn, r->act, m->qshift);
        for (int i = 0; i < m->dim; ++i) r->h[i] += r->down[i];
        if (trace) {
            fprintf(stderr, "TRACE p=%d l=%d h=%" PRId64 " q=%" PRId64
                            " k=%" PRId64 " a=%" PRId64 "\n",
                    pos, l, checksum(r->h, m->dim), checksum(r->q, m->dim),
                    checksum(r->k, kvdim), checksum(r->attn, m->dim));
        }
    }
    rmsnorm(r->hf, r->h, m->rmsf.v, m->dim, m->eps_q2);
    matvec(r->logits, m->tie_head ? m->wte.v : m->head.v,
           m->vocab, m->dim, r->hf, m->qshift);
    if (trace && field && field->enabled)
        fprintf(stderr, "TRACE p=%d method_raw=%" PRId64 "\n",
                pos, checksum(r->logits, m->vocab));
    apply_method(r->logits, m->vocab, m, field, trace);
    r->next = greedy_id(r->logits, m->vocab);
    if (trace)
        fprintf(stderr, "TRACE p=%d logits=%" PRId64 " greedy=%d\n",
                pos, checksum(r->logits, m->vocab), r->next);
    spa_commit_if_boundary(field, m);
}

int main(int argc, char **argv) {
    const char *weights = "weights/fixture.bfw", *prompt = "AB";
    const char *temp_text = "0", *seed_text = "1", *prophecy_text = "7";
    const char *destiny_text = "0", *pain_text = "0";
    const char *focus_text = "0.5", *spread_text = "0", *velocity = "raw";
    const char *floor_text = NULL, *ceiling_text = NULL, *debt_text = NULL, *decay_text = NULL;
    const char *soma_path = NULL, *learn_text = "on", *plasticity = "flat";
    const char *spa_alpha_text = NULL, *spa_strength_text = NULL, *spa_history_text = NULL;
    int next_only = 0, tokens = 16, trace = 0, generated_only = 0;
    int field_enabled = 0, hebbian = 0, spa = 0;
    int velocity_set = 0, floor_set = 0, ceiling_set = 0, debt_set = 0, decay_set = 0;
    int plasticity_set = 0, spa_set = 0, spa_alpha_set = 0, spa_strength_set = 0, spa_history_set = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--weights") && i + 1 < argc) weights = argv[++i];
        else if (!strcmp(argv[i], "--prompt") && i + 1 < argc) prompt = argv[++i];
        else if (!strcmp(argv[i], "--next-id")) next_only = 1;
        else if (!strcmp(argv[i], "--tokens") && i + 1 < argc) {
            if (parse_nonnegative(argv[++i], &tokens)) die("bad --tokens");
        }
        else if (!strcmp(argv[i], "--temperature") && i + 1 < argc) temp_text = argv[++i];
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed_text = argv[++i];
        else if (!strcmp(argv[i], "--method")) field_enabled = 1;
        else if (!strcmp(argv[i], "--field") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "on")) field_enabled = 1;
            else if (!strcmp(v, "off")) field_enabled = 0;
            else die("bad --field");
        }
        else if (!strcmp(argv[i], "--velocity") && i + 1 < argc) { velocity = argv[++i]; velocity_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--prophecy") && i + 1 < argc) { prophecy_text=argv[++i]; field_enabled=1; }
        else if (!strcmp(argv[i], "--destiny") && i + 1 < argc) { destiny_text=argv[++i]; field_enabled=1; }
        else if (!strcmp(argv[i], "--pain") && i + 1 < argc) { pain_text=argv[++i]; field_enabled=1; }
        else if (!strcmp(argv[i], "--focus") && i + 1 < argc) { focus_text=argv[++i]; field_enabled=1; }
        else if (!strcmp(argv[i], "--spread") && i + 1 < argc) { spread_text=argv[++i]; field_enabled=1; }
        else if (!strcmp(argv[i], "--entropy-floor") && i + 1 < argc) { floor_text=argv[++i]; floor_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--resonance-ceiling") && i + 1 < argc) { ceiling_text=argv[++i]; ceiling_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--debt") && i + 1 < argc) { debt_text=argv[++i]; debt_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--debt-decay") && i + 1 < argc) { decay_text=argv[++i]; decay_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--hebbian")) { hebbian=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--plasticity") && i + 1 < argc) { plasticity=argv[++i]; plasticity_set=1; hebbian=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--spa")) { spa=1; spa_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--spa-alpha") && i + 1 < argc) { spa_alpha_text=argv[++i]; spa=1; spa_set=1; spa_alpha_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--spa-strength") && i + 1 < argc) { spa_strength_text=argv[++i]; spa=1; spa_set=1; spa_strength_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--spa-history") && i + 1 < argc) { spa_history_text=argv[++i]; spa=1; spa_set=1; spa_history_set=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--soma") && i + 1 < argc) { soma_path=argv[++i]; hebbian=1; field_enabled=1; }
        else if (!strcmp(argv[i], "--learn") && i + 1 < argc) learn_text=argv[++i];
        else if (!strcmp(argv[i], "--generated-only")) generated_only=1;
        else if (!strcmp(argv[i], "--trace")) trace=1;
        else die("bad arguments");
    }

    Model m = load_model(weights);
    const int qs = 1 << m.qshift;
    int base_temp_q = 0, seed = 1, prophecy = 7;
    MethodField field; memset(&field,0,sizeof(field));
    method_defaults(&field,&m);
    field.enabled=field_enabled;
    field.prophecy=7;
    field.hebbian=hebbian;
    field.learn=!strcmp(learn_text,"on");
    if (strcmp(learn_text,"on") && strcmp(learn_text,"off")) die("bad --learn");
    field.soma_path=soma_path;
    field.cooc=xcalloc((size_t)m.vocab*m.vocab,sizeof(int64_t));
    field.spa_history=xcalloc((size_t)8*m.dim,sizeof(int));
    field.spa_current=xcalloc((size_t)m.ctx,sizeof(int));
    soma_load(&field,&m,soma_path);
    if (spa_set) field.spa=spa;
    if (plasticity_set) {
        if (strcmp(plasticity,"flat") && strcmp(plasticity,"debt")) die("bad --plasticity");
        if (strlen(plasticity)>=sizeof(field.plasticity)) die("plasticity name too long");
        strcpy(field.plasticity,plasticity);
    }

    if (parse_decimal_q(temp_text,qs,&base_temp_q) || base_temp_q<0) die("bad --temperature");
    if (parse_nonnegative(seed_text,&seed)) die("bad --seed");
    if (parse_nonnegative(prophecy_text,&prophecy) || prophecy<1 || prophecy>64) die("bad --prophecy");
    field.prophecy=prophecy;
    if (parse_decimal_q(destiny_text,qs,&field.destiny_q) || field.destiny_q<0 || field.destiny_q>qs) die("bad --destiny");
    if (parse_decimal_q(pain_text,qs,&field.pain_q) || field.pain_q<0 || field.pain_q>qs) die("bad --pain");
    if (parse_decimal_q(focus_text,qs,&field.focus_q) || field.focus_q<0 || field.focus_q>qs) die("bad --focus");
    if (parse_decimal_q(spread_text,qs,&field.spread_q) || field.spread_q<0 || field.spread_q>qs) die("bad --spread");
    if (velocity_set) set_velocity(&field,velocity,qs);
    if (debt_set) {
        if (parse_decimal_q(debt_text,qs,&field.debt_q) || field.debt_q<0 || field.debt_q>field.debt_cap_q) die("bad --debt");
    }
    if (decay_set) {
        if (parse_decimal_q(decay_text,qs,&field.debt_decay_q) || field.debt_decay_q<0 || field.debt_decay_q>qs) die("bad --debt-decay");
    }
    if (floor_set) {
        if (parse_decimal_q(floor_text,qs,&field.entropy_floor_q) || field.entropy_floor_q<0 || field.entropy_floor_q>qs) die("bad --entropy-floor");
    }
    if (ceiling_set) {
        if (parse_decimal_q(ceiling_text,qs,&field.resonance_ceiling_q) || field.resonance_ceiling_q<0 || field.resonance_ceiling_q>qs) die("bad --resonance-ceiling");
    }
    if (spa_alpha_set) {
        if (parse_decimal_q(spa_alpha_text,qs,&field.spa_alpha_q) || field.spa_alpha_q<0 || field.spa_alpha_q>qs) die("bad --spa-alpha");
    }
    if (spa_strength_set) {
        if (parse_decimal_q(spa_strength_text,qs,&field.spa_strength_q) || field.spa_strength_q<0 || field.spa_strength_q>qs) die("bad --spa-strength");
    }
    if (spa_history_set) {
        int h=0; if(parse_nonnegative(spa_history_text,&h)||h<1||h>8) die("bad --spa-history");
        field.spa_history_max=h;
        while(field.spa_history_n>field.spa_history_max){
            memmove(field.spa_history,field.spa_history+m.dim,(size_t)(field.spa_history_n-1)*m.dim*sizeof(int));
            --field.spa_history_n;
        }
    }

    uint32_t rng=(uint32_t)seed;
    if(!rng) rng=UINT32_C(1831565813);
    if(trace && field.enabled){
        int initial_temp=effective_temp_q(&field,base_temp_q,m.qshift);
        fprintf(stderr,"TRACE method velocity=%s base_temp=%d temp=%d prophecy=%d destiny=%d pain=%d focus=%d spread=%d floor=%d ceiling=%d debt=%d decay=%d hebbian=%d edges=%d plasticity=%s gain=%d updates=%d spa=%d alpha=%d strength=%d conn=%d history=%d current=%d sentences=%d steps=%d recoveries=%d soma=%d\n",
                field.velocity,base_temp_q,initial_temp,field.prophecy,field.destiny_q,field.pain_q,
                field.focus_q,field.spread_q,field.entropy_floor_q,field.resonance_ceiling_q,
                field.debt_q,field.debt_decay_q,field.hebbian,cooc_edges(&field,m.vocab),field.plasticity,
                field.plasticity_last_gain_q,field.plasticity_updates,field.spa,field.spa_alpha_q,
                field.spa_strength_q,field.spa_last_conn_q,field.spa_history_n,field.spa_current_n,
                field.spa_sentences,field.field_steps,field.recoveries,field.state_loaded);
    }

    Runtime r=runtime_new(&m);
    int pos=0;
    for(const unsigned char *p=(const unsigned char*)prompt;*p;++p){
        if(pos>=m.ctx) die("prompt exceeds context");
        int id=m.byte_to_id[*p]; if(id<0) id=m.unk_id;
        spa_commit_if_boundary(&field,&m);
        spa_observe(&field,&m,id);
        hebbian_ingest(&field,&m,id,trace);
        decode(id,pos++,&m,&r,&field,trace);
    }
    if(pos==0){
        spa_commit_if_boundary(&field,&m);
        spa_observe(&field,&m,m.unk_id);
        hebbian_ingest(&field,&m,m.unk_id,trace);
        decode(m.unk_id,pos++,&m,&r,&field,trace);
    }
    int needed=next_only?1:tokens;
    if((int64_t)pos+needed>m.ctx) die("prompt plus generation exceeds context");

    if(next_only){
        printf("%d\n",r.next);
        soma_save(&field,&m,soma_path);
    } else {
        if(!generated_only) fputs(prompt,stdout);
        for(int step=0;step<tokens;++step){
            int temp_q=effective_temp_q(&field,base_temp_q,m.qshift);
            int id=pick_next(&r,&m,temp_q,&rng);
            register_choice_debt(&field,&m,&r,id);
            field_step(&field,&m,id,base_temp_q,trace);
            fputc(m.id_to_byte[id],stdout);
            spa_commit_if_boundary(&field,&m);
            spa_observe(&field,&m,id);
            hebbian_ingest(&field,&m,id,trace);
            if(step+1<tokens) decode(id,pos++,&m,&r,&field,trace);
        }
        spa_commit_if_boundary(&field,&m);
        fputc('\n',stdout);
        soma_save(&field,&m,soma_path);
    }

    spa_commit_if_boundary(&field,&m);
    free(field.spa_current);
    free(field.spa_history);
    free(field.cooc);
    runtime_free(&r,&m);
    model_free(&m);
    return 0;
}
