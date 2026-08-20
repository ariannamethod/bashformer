#ifndef NOTORCH_H
#define NOTORCH_H
#include <stddef.h>

typedef struct { float *data; int ndim; int shape[8]; int stride[8]; int len; int refcount; } nt_tensor;
typedef struct { nt_tensor *output; nt_tensor *grad; int op,parent1,parent2,parent3; float aux,aux2,aux3,aux4; int is_param,no_decay,frozen; } nt_tape_entry;
typedef struct { nt_tape_entry entries[8192]; int count,active; } nt_tape;
typedef struct { float start_lr, min_lr; int warmup_steps,total_steps,current_step; int type,step_size; float gamma; } nt_schedule;
typedef struct { float loss_scale; int consecutive_good,total_nan_count; } nt_nan_guard;

nt_tensor *nt_tensor_new(size_t len);
nt_tensor *nt_tensor_new2d(int rows,int cols);
void nt_tensor_free(nt_tensor *t);
void nt_tensor_fill(nt_tensor *t,float value);
void nt_tensor_xavier(nt_tensor *t,int fan_in,int fan_out);
void nt_tensor_sync_cpu(nt_tensor *t);
void nt_seed(unsigned int seed);
void nt_train_mode(int on);
void nt_tape_start(void);
void nt_tape_clear(void);
nt_tape *nt_tape_get(void);
int nt_tape_param(nt_tensor *t);
void nt_tape_no_decay(int idx);
int nt_tape_record(nt_tensor *output,int op,int p1,int p2,float aux);
void nt_tape_backward(int loss_idx);
float nt_tape_clip_grads(float max_norm);
void nt_tape_chuck_step(float lr,float loss);
int nt_seq_embedding(int wte,int wpe,int tok,int T,int D);
int nt_seq_rmsnorm(int x,int gamma,int T,int D);
int nt_seq_linear(int w,int x,int T);
int nt_rope(int x,int T,int head_dim);
int nt_rope_freq(int x,int T,int head_dim,float freq_base);
int nt_gqa_causal_attention(int q,int k,int v,int T,int head_dim,int n_heads,int n_kv_heads);
int nt_add(int a,int b);
int nt_mul(int a,int b);
int nt_silu(int x);
int nt_seq_cross_entropy(int logits,int targets,int T,int V);
int nt_save(const char *path,nt_tensor **params,int n);
nt_tensor **nt_load(const char *path,int *n);
nt_schedule nt_schedule_cosine(float start_lr,int warmup,int total,float min_lr);
float nt_schedule_get_lr(nt_schedule *s);
nt_nan_guard nt_nan_guard_new(void);
int nt_nan_guard_check(nt_nan_guard *g);
#endif
