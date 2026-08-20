/* Link/run probe for the exact Notorch features Bashformer's forge consumes. */
#include "notorch.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define V 8
#define D 8
#define T 2
#define H 2
#define KVH 1
#define HD 4
#define KVD (KVH * HD)
#define F 16

static void init(nt_tensor *t, int fi, int fo) { nt_tensor_xavier(t, fi, fo); }

int main(void) {
    nt_seed(7);
    nt_tensor *wte=nt_tensor_new2d(V,D), *r1=nt_tensor_new(D), *wq=nt_tensor_new2d(D,D);
    nt_tensor *wk=nt_tensor_new2d(KVD,D), *wv=nt_tensor_new2d(KVD,D), *wo=nt_tensor_new2d(D,D);
    nt_tensor *r2=nt_tensor_new(D), *wg=nt_tensor_new2d(F,D), *wu=nt_tensor_new2d(F,D);
    nt_tensor *wd=nt_tensor_new2d(D,F), *rf=nt_tensor_new(D);
    nt_tensor *tok=nt_tensor_new(T), *tgt=nt_tensor_new(T);
    if(!wte||!r1||!wq||!wk||!wv||!wo||!r2||!wg||!wu||!wd||!rf||!tok||!tgt) return 2;
    init(wte,V,D); init(wq,D,D); init(wk,D,KVD); init(wv,D,KVD); init(wo,D,D);
    init(wg,D,F); init(wu,D,F); init(wd,F,D);
    nt_tensor_fill(r1,1.0f); nt_tensor_fill(r2,1.0f); nt_tensor_fill(rf,1.0f);
    tok->data[0]=1; tok->data[1]=2; tgt->data[0]=2; tgt->data[1]=3;

    nt_tape_start();
    int iwte=nt_tape_param(wte); nt_tape_no_decay(iwte);
    int ir1=nt_tape_param(r1), iq=nt_tape_param(wq), ik=nt_tape_param(wk), iv=nt_tape_param(wv);
    int io=nt_tape_param(wo), ir2=nt_tape_param(r2), ig=nt_tape_param(wg), iu=nt_tape_param(wu);
    int id=nt_tape_param(wd), irf=nt_tape_param(rf);
    int it=nt_tape_record(tok,0,-1,-1,0), iy=nt_tape_record(tgt,0,-1,-1,0);
    int x=nt_seq_embedding(iwte,-1,it,T,D);
    int xn=nt_seq_rmsnorm(x,ir1,T,D);
    int q=nt_rope_freq(nt_seq_linear(iq,xn,T),T,HD,500000.0f);
    int k=nt_rope_freq(nt_seq_linear(ik,xn,T),T,HD,500000.0f);
    int v=nt_seq_linear(iv,xn,T);
    int a=nt_gqa_causal_attention(q,k,v,T,HD,H,KVH);
    x=nt_add(x,nt_seq_linear(io,a,T));
    xn=nt_seq_rmsnorm(x,ir2,T,D);
    int gate=nt_silu(nt_seq_linear(ig,xn,T));
    int up=nt_seq_linear(iu,xn,T);
    x=nt_add(x,nt_seq_linear(id,nt_mul(gate,up),T));
    int hf=nt_seq_rmsnorm(x,irf,T,D);
    int logits=nt_seq_linear(iwte,hf,T);
    int loss=nt_seq_cross_entropy(logits,iy,T,V);
    float before=nt_tape_get()->entries[loss].output->data[0];
    if(!isfinite(before)) return 3;
    nt_tape_backward(loss);
    nt_tape_clip_grads(1.0f);
    nt_tape_chuck_step(1e-3f,before);
    nt_tape_clear();

    printf("notorch-doctor: OK loss=%.6f gqa=%d:%d rope=500000 chuck=OK\n", before, H, KVH);
    nt_tensor_free(wte); nt_tensor_free(r1); nt_tensor_free(wq); nt_tensor_free(wk);
    nt_tensor_free(wv); nt_tensor_free(wo); nt_tensor_free(r2); nt_tensor_free(wg);
    nt_tensor_free(wu); nt_tensor_free(wd); nt_tensor_free(rf); nt_tensor_free(tok); nt_tensor_free(tgt);
    return 0;
}
