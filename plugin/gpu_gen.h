/* SPDX-License-Identifier: MIT
 * 11-16 GPU-path step 2c, TEST ONLY (CS_TEST_GPU_OUT=1 + CS_TEST_GPU_GEN=1, test tree). 2x RIFE on the GPU path:
 *  - frame filter is "identity ! identity" (no d3d11download/upload), frames stay D3D11 textures on the host's device;
 *  - each new source is copied on the GPU into one of 3 shared input textures (NT handles imported by the engine as
 *    Vulkan images); a worker thread asks the engine for mid(previous, new) into one of 2 shared output textures;
 *  - output is one source late: on the new-source tick the previous source, on the repeated tick the generated frame
 *    (or the previous source again when it is not ready). Buffers come from a D3D11 buffer pool on the host's device.
 * 11-16 night: follows the on/off switch (off = untouched passthrough), static catch-up (2 ticks without a new
 * source show the newest), cut skip (64-point fingerprint jump -> no mid frame), basic GPU HUD keep (gpu_hud.h,
 * CS_TEST_GPU_HUD=0 turns it off). Logs GPU_GEN lines.
 */
#include "gpu_hud.h"
#include "gpu_view.h"
static int gpu_view_wanted(void); /* gpu_probe.h */
/* 2.3.7 (11-18 user: recording on the GPU path saved nothing, 0-byte file). The record route found no originals: it relies on
 * qdata the GPU path's buffers do not carry to the encoder. The GPU path now says when it generates (gpu_gen_running, for the
 * record route's generated/original choice) and marks generated output buffers with a buffer flag (flags survive copies). */
static volatile LONG gpu_gen_running;
#define CS_BUF_GENERATED (1u<<28) /* above GST_BUFFER_FLAG_LAST (1<<20) */
static void cs_buf_mark(void *buffer,int generated){unsigned *flags=(unsigned*)((char*)buffer+16); /* GstMiniObject.flags */if(generated)*flags|=CS_BUF_GENERATED;else *flags&=~CS_BUF_GENERATED;}
static int cs_buf_generated(void *buffer){return buffer&&(*(unsigned*)((char*)buffer+16)&CS_BUF_GENERATED)!=0;}
/* 2.3.6 (11-18 user, dual monitor): after the low latency window closed, the Streaming Center preview flickered between a
 * normal and a darker picture every frame (measured 45 vs 28 grey). The engines leave alpha < 1 in their output; the
 * full screen window ignores alpha but the host's preview blends it over black. Each generated frame's alpha is set to 1
 * on the host's device right after the engine wrote it (write mask alpha only, one draw). */
typedef struct {int ok;ID3D11DeviceContext *dc;ID3D11VertexShader *vs;ID3D11PixelShader *ps;ID3D11BlendState *bs;ID3D11RenderTargetView *rtv[2];unsigned w,h;unsigned long long n;} GpuAlpha;
static const char ga_src[]="float4 vs(uint i:SV_VertexID):SV_Position{float2 t=float2((i<<1)&2,i&2);return float4(t*float2(2,-2)+float2(-1,1),0,1);}\n"
 "float4 ps(float4 p:SV_Position):SV_Target{return float4(0,0,0,1);}\n";
static int ga_setup(GpuAlpha *a,ID3D11Device *dev,ID3D11Texture2D **out,unsigned w,unsigned h){
 HMODULE m=LoadLibraryW(L"d3dcompiler_47.dll");PFN_D3DCompile_ cf=m?(PFN_D3DCompile_)(void*)GetProcAddress(m,"D3DCompile"):NULL;if(!cf)return 0;
 ID3DBlob *b=NULL,*e=NULL;
 if(FAILED(cf(ga_src,sizeof ga_src-1,"cs_alpha",NULL,NULL,"vs","vs_5_0",0,0,&b,&e))){if(e)e->lpVtbl->Release(e);return 0;}
 dev->lpVtbl->CreateVertexShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&a->vs);b->lpVtbl->Release(b);b=NULL;
 if(FAILED(cf(ga_src,sizeof ga_src-1,"cs_alpha",NULL,NULL,"ps","ps_5_0",0,0,&b,&e))){if(e)e->lpVtbl->Release(e);return 0;}
 dev->lpVtbl->CreatePixelShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&a->ps);b->lpVtbl->Release(b);
 D3D11_BLEND_DESC bd={0};bd.RenderTarget[0].BlendEnable=FALSE;bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALPHA;
 if(!a->vs||!a->ps||FAILED(dev->lpVtbl->CreateBlendState(dev,&bd,&a->bs)))return 0;
 for(int i=0;i<2;i++){D3D11_RENDER_TARGET_VIEW_DESC rd={0};rd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
  if(FAILED(dev->lpVtbl->CreateRenderTargetView(dev,(ID3D11Resource*)out[i],&rd,&a->rtv[i])))return 0;}
 if(FAILED(dev->lpVtbl->CreateDeferredContext(dev,0,&a->dc)))return 0;
 a->w=w;a->h=h;a->ok=1;return 1;
}
/* worker thread, after the engine wrote output i (0/1). Same immediate context as the host's copy, so it runs first. */
static void ga_fix(GpuAlpha *a,ID3D11Device *dev,void *gdev,int i){
 if(!a->ok)return;ID3D11DeviceContext *dc=a->dc,*imm=NULL;dev->lpVtbl->GetImmediateContext(dev,&imm);D3D11_VIEWPORT vp={0,0,(float)a->w,(float)a->h,0,1};float bf[4]={0};
 dc->lpVtbl->IASetPrimitiveTopology(dc,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->lpVtbl->IASetInputLayout(dc,NULL);
 dc->lpVtbl->VSSetShader(dc,a->vs,NULL,0);dc->lpVtbl->PSSetShader(dc,a->ps,NULL,0);dc->lpVtbl->OMSetBlendState(dc,a->bs,bf,0xffffffff);
 dc->lpVtbl->RSSetViewports(dc,1,&vp);dc->lpVtbl->OMSetRenderTargets(dc,1,&a->rtv[i],NULL);dc->lpVtbl->Draw(dc,3,0);
 ID3D11CommandList *cl=NULL;if(SUCCEEDED(dc->lpVtbl->FinishCommandList(dc,FALSE,&cl))){gp_lock(gdev);imm->lpVtbl->ExecuteCommandList(imm,cl,TRUE);imm->lpVtbl->Flush(imm);gp_unlock(gdev);cl->lpVtbl->Release(cl);a->n++;}
 imm->lpVtbl->Release(imm);
}
static void ga_free(GpuAlpha *a){
 if(a->dc)a->dc->lpVtbl->Release(a->dc);if(a->vs)a->vs->lpVtbl->Release(a->vs);if(a->ps)a->ps->lpVtbl->Release(a->ps);if(a->bs)a->bs->lpVtbl->Release(a->bs);
 for(int i=0;i<2;i++)if(a->rtv[i])a->rtv[i]->lpVtbl->Release(a->rtv[i]);memset(a,0,sizeof *a);
}
static void *(*gg_create)(const wchar_t*,int),(*gg_destroy)(void*),*(*gg_ctx_create)(void*,int,int,uint8_t*);
static int (*gg_ctx_import)(void*,HANDLE),(*gg_ctx_generate)(void*,int,uint64_t,int,uint64_t,int,float,double*);
static void (*gg_ctx_destroy)(void*);static const char *(*gg_error)(void);
static int gpu_gen_on,gpu_gen_vfg,gpu_gen_force_vfg,gpu_gen_family,gpu_gen_model;static void *(*gg_seq_create)(int,const wchar_t*,int,int,uint8_t*);static void *(*gg_vfg_create)(int,int,uint8_t*);
typedef struct {
 GpuOut o; void *engine,*ctx; ID3D11Texture2D *tex[GV_TEX]; HANDLE nt[GV_TEX]; ID3D11Query *q[3]; /* 0-2 inputs, 3-4 engine out, 5-6 view HUD out (gpu_view.h) */
 int gv,gv_reg,hk; volatile LONG hud_k; volatile LONG64 hud_pair; /* 2.3.8: last HUD-composed view frame (tex 5/6) and its pair */ LONGLONG jarr,last_due; unsigned long long gv_skips; LONGLONG last_arr; double src_ms,lead_ms; ID3D11Query *hq;
 uint64_t id[3]; int cur,prev; uint64_t next_id; void *last_res; unsigned last_sub;
 HANDLE thread,wake; volatile LONG stop,busy; /* job */ int ja,jb,jout; uint64_t jaid,jbid;
 /* result */ volatile LONG ready_out; volatile LONG64 ready_pair; int out_next;
 unsigned long long ticks,gen,repeat,skipped,blocked,fail,news; double ms[3]; unsigned msn;
 void *bin; char pipe[64]; int pipe_state; ID3D11Texture2D *fp; uint32_t fpv[512];
 GpuAlpha alpha; GpuHud hud; int hud_on; int since_new, caught, was_on; int hold_src,hold_org; LONGLONG hold_until; /* 2.3.10: mid frame held for half a source interval */ uint32_t fps[3][512]; unsigned long long cuts,catchups,composed,offticks;
} GpuGen;
static int gpu_gen_init(void){
 HMODULE m=GetModuleHandleW(L"consolesmooth_engine.dll");if(!m){logline("GPU_GEN engine module missing");return 0;}
 *(FARPROC*)&gg_create=GetProcAddress(m,"cs_create");*(FARPROC*)&gg_destroy=GetProcAddress(m,"cs_destroy");
 *(FARPROC*)&gg_ctx_create=GetProcAddress(m,"cs_gpu_ctx_create");*(FARPROC*)&gg_ctx_import=GetProcAddress(m,"cs_gpu_ctx_import");
 *(FARPROC*)&gg_ctx_generate=GetProcAddress(m,"cs_gpu_ctx_generate");*(FARPROC*)&gg_ctx_destroy=GetProcAddress(m,"cs_gpu_ctx_destroy");
 *(FARPROC*)&gg_error=GetProcAddress(m,"cs_gpu_bench_error");
 /* 11-16 night: CS_TEST_GPU_VFG=1 -> NVIDIA VFG through CUDA instead of RIFE through Vulkan (same slot API) */
 {wchar_t v[4];if(gpu_gen_force_vfg||(GetEnvironmentVariableW(L"CS_TEST_GPU_VFG",v,4)==1&&v[0]==L'1')){*(FARPROC*)&gg_vfg_create=GetProcAddress(m,"cs_vfg_gpu_ctx_create");
  if(gg_vfg_create){gpu_gen_vfg=1;*(FARPROC*)&gg_ctx_import=GetProcAddress(m,"cs_vfg_gpu_ctx_import");*(FARPROC*)&gg_ctx_generate=GetProcAddress(m,"cs_vfg_gpu_ctx_generate");
   *(FARPROC*)&gg_ctx_destroy=GetProcAddress(m,"cs_vfg_gpu_ctx_destroy");*(FARPROC*)&gg_error=GetProcAddress(m,"cs_vfg_gpu_error");}
  logline("GPU_GEN vfg=%d",gpu_gen_vfg);}}
 /* 11-18: FSR 3 (family 3): the engine's own FSR 3 context, same calls as RIFE under cs_fsr3_gpu_* names */
 if(!gpu_gen_vfg&&gpu_gen_family==3){*(FARPROC*)&gg_ctx_create=GetProcAddress(m,"cs_fsr3_gpu_ctx_create");*(FARPROC*)&gg_ctx_import=GetProcAddress(m,"cs_fsr3_gpu_ctx_import");
  *(FARPROC*)&gg_ctx_generate=GetProcAddress(m,"cs_fsr3_gpu_ctx_generate");*(FARPROC*)&gg_ctx_destroy=GetProcAddress(m,"cs_fsr3_gpu_ctx_destroy");*(FARPROC*)&gg_error=GetProcAddress(m,"cs_fsr3_gpu_error");}
 /* 11-18: DLSS FG (family 5) and AMD FG (family 6) through the D3D12 DLLs (seq_gpu.cpp) */
 if(!gpu_gen_vfg&&(gpu_gen_family==5||gpu_gen_family==6)){*(FARPROC*)&gg_seq_create=GetProcAddress(m,"cs_seq_gpu_ctx_create");*(FARPROC*)&gg_ctx_import=GetProcAddress(m,"cs_seq_gpu_ctx_import");
  *(FARPROC*)&gg_ctx_generate=GetProcAddress(m,"cs_seq_gpu_ctx_generate");*(FARPROC*)&gg_ctx_destroy=GetProcAddress(m,"cs_seq_gpu_ctx_destroy");*(FARPROC*)&gg_error=GetProcAddress(m,"cs_seq_gpu_error");
  int ok=gg_seq_create&&gg_ctx_import&&gg_ctx_generate&&gg_ctx_destroy&&gg_error;logline("GPU_GEN seq family=%d init ok=%d",gpu_gen_family,ok);return ok;}
 int ok=gpu_gen_vfg?(gg_vfg_create&&gg_ctx_import&&gg_ctx_generate&&gg_ctx_destroy&&gg_error):(gg_create&&gg_destroy&&gg_ctx_create&&gg_ctx_import&&gg_ctx_generate&&gg_ctx_destroy&&gg_error);logline("GPU_GEN init ok=%d",ok);return ok;
}
static void gg_wait_query(GpuGen *g,ID3D11Query *q){
 ID3D11DeviceContext *ctx=NULL;g->o.g.dev->lpVtbl->GetImmediateContext(g->o.g.dev,&ctx);BOOL done=FALSE;
 for(;;){gp_lock(g->o.g.gdev);HRESULT r=ctx->lpVtbl->GetData(ctx,(ID3D11Asynchronous*)q,&done,sizeof done,0);gp_unlock(g->o.g.gdev);if(r==S_OK&&done)break;SwitchToThread();}
 ctx->lpVtbl->Release(ctx);
}
static DWORD WINAPI gg_worker(void *p){
 GpuGen *g=p;
 while(WaitForSingleObject(g->wake,INFINITE)==WAIT_OBJECT_0&&!g->stop){
  if(!g->busy)continue;
  gg_wait_query(g,g->q[g->ja]);gg_wait_query(g,g->q[g->jb]);
  double ms[3]={0};int rc=gg_ctx_generate(g->ctx,g->ja,g->jaid,g->jb,g->jbid,3+g->jout,0.5f,ms);
  if(rc){g->fail++;if(g->fail<5)logline("GPU_GEN generate rc=%d %s",rc,gg_error());if(g->fail==120&&g->gen<g->fail){logline("GPU_GEN failing, CPU path next start");gpu_rule_mark_failed(gpu_gen_model);}}
  else{ga_fix(&g->alpha,g->o.g.dev,g->o.g.gdev,g->jout);for(int i=0;i<3;i++)g->ms[i]+=ms[i];g->msn++;InterlockedExchange(&g->ready_out,g->jout);InterlockedExchange64(&g->ready_pair,(LONG64)g->jbid);}
  if(g->gv&&InterlockedCompareExchange(&direct_gpu_feed,0,0)){
   int out=3+g->jout;
   if(!rc&&g->hud_on){ID3D11DeviceContext *ctx=NULL;g->o.g.dev->lpVtbl->GetImmediateContext(g->o.g.dev,&ctx);int k=5+g->hk;g->hk^=1;
    gp_lock(g->o.g.gdev);int composed=gh_compose(&g->hud,ctx,g->o.g.dev,(ID3D11Resource*)g->tex[k],0,3+g->jout,g->jb);if(composed){ctx->lpVtbl->End(ctx,(ID3D11Asynchronous*)g->hq);ctx->lpVtbl->Flush(ctx);}gp_unlock(g->o.g.gdev);ctx->lpVtbl->Release(ctx);
    if(composed){gg_wait_query(g,g->hq);out=k;g->composed++;InterlockedExchange(&g->hud_k,k);InterlockedExchange64(&g->hud_pair,(LONG64)g->jbid);}}
   LARGE_INTEGER now;QueryPerformanceCounter(&now);double gen=1000.0*(now.QuadPart-g->jarr)/gv_freq;
   g->lead_ms=0.9*g->lead_ms+0.1*(gen+0.5);if(g->lead_ms<1)g->lead_ms=1;if(g->lead_ms>10)g->lead_ms=10;
   /* 2.3.5: the host sometimes hands sources over unevenly (gaps of ~8 and ~25 ms instead of 16.7). Then the new mid frame
    * would be due before the previous original and one of them was dropped (DLSS FG run 11-18: 90 of 600 per 10 s).
    * Keep at least 40% of half a source interval after the last original; skip the mid frame only when that pushes it
    * more than half an interval late. */
   LONGLONG half=(LONGLONG)(g->src_ms*0.5*gv_freq/1000),gap=(LONGLONG)(g->src_ms*0.2*gv_freq/1000);
   LONGLONG mid=g->jarr+(LONGLONG)(g->lead_ms*gv_freq/1000),want=mid;if(mid<now.QuadPart)mid=now.QuadPart;
   if(g->last_due&&mid<g->last_due+gap)mid=g->last_due+gap;
   LONGLONG share=(LONGLONG)(g->src_ms*0.01*InterlockedCompareExchange(&gen_share_pct,0,0)*gv_freq/1000);
   int skipMid=mid-want>half;LONGLONG orig=skipMid?(mid>want+half?mid:want+half):mid+share;
   if(!rc&&!skipMid)gv_post(out,mid,g->jarr,1);else if(skipMid)g->gv_skips++;
   gv_post(g->jb,orig,g->jarr,0);g->last_due=orig;
  }
  InterlockedExchange(&g->busy,0);
 }
 return 0;
}
static int gg_setup(GpuGen *g,void *pad,ID3D11Resource *res,void *gdev){
 {wchar_t v[4];if(GetEnvironmentVariableW(L"CS_TEST_GPU_FAIL",v,4)==1&&v[0]==L'1'){logline("GPU_GEN test failure (CS_TEST_GPU_FAIL)");return 0;}} /* 2.3.3 test: fallback check */
 /* reuse 2b setup for device, pool and the first shared texture */
 (void)pad;GpuProbe *p=&g->o.g;ID3D11Texture2D *t=NULL;res->lpVtbl->QueryInterface(res,&IID_ID3D11Texture2D,(void**)&t);
 D3D11_TEXTURE2D_DESC d={0};if(t){t->lpVtbl->GetDesc(t,&d);t->lpVtbl->Release(t);}
 D3D11_TEXTURE2D_DESC s=d;s.MipLevels=1;s.ArraySize=1;s.Usage=D3D11_USAGE_DEFAULT;s.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;s.CPUAccessFlags=0;s.MiscFlags=0x800|0x2;
 wchar_t model[32768];LONG settings=ui_requested();int mi=frame_force_model>=0?frame_force_model:(settings>>16)&255;if(!cs_model_valid(mi)||(cs_models[mi].family!=0&&cs_models[mi].family!=gpu_gen_family))mi=0;
 swprintf(model,32768,L"%ls\\%ls",engine_dir,cs_models[mi].directory);
 uint8_t luid[8]={0};
 if(gpu_gen_vfg)g->ctx=gg_vfg_create(d.Width,d.Height,luid);
 else if(gg_seq_create)g->ctx=gg_seq_create(gpu_gen_family,model,d.Width,d.Height,luid);
 else{g->engine=gg_create(model,-1);if(!g->engine){logline("GPU_GEN cs_create failed");return 0;}
  {HMODULE m=GetModuleHandleW(L"consolesmooth_engine.dll");void (*floor)(void*,int)=NULL;if(m)*(FARPROC*)&floor=GetProcAddress(m,"cs_set_flow_floor");if(floor)floor(g->engine,0);} /* low latency: half flow from FHD */g->ctx=gg_ctx_create(g->engine,d.Width,d.Height,luid);}if(!g->ctx){logline("GPU_GEN ctx failed %s",gg_error());return 0;}
 IDXGIDevice *xd=NULL;IDXGIAdapter *ad=NULL;DXGI_ADAPTER_DESC ades={0};
 if(SUCCEEDED(p->dev->lpVtbl->QueryInterface(p->dev,&IID_IDXGIDevice,(void**)&xd))&&SUCCEEDED(xd->lpVtbl->GetAdapter(xd,&ad)))ad->lpVtbl->GetDesc(ad,&ades);
 if(ad)ad->lpVtbl->Release(ad);if(xd)xd->lpVtbl->Release(xd);
 if(memcmp(&ades.AdapterLuid,luid,8)){logline("GPU_GEN LUID mismatch (host GPU != Vulkan GPU)");return 0;}
 for(int i=0;i<GV_TEX;i++){
  if(FAILED(p->dev->lpVtbl->CreateTexture2D(p->dev,&s,NULL,&g->tex[i]))){logline("GPU_GEN texture %d failed",i);return 0;}
  IDXGIResource1 *r1=NULL;g->tex[i]->lpVtbl->QueryInterface(g->tex[i],&IID_IDXGIResource1,(void**)&r1);
  if(!r1||FAILED(r1->lpVtbl->CreateSharedHandle(r1,NULL,DXGI_SHARED_RESOURCE_READ|DXGI_SHARED_RESOURCE_WRITE,NULL,&g->nt[i]))){if(r1)r1->lpVtbl->Release(r1);logline("GPU_GEN shared handle %d failed",i);return 0;}
  r1->lpVtbl->Release(r1);
  if(i>=5)continue;
  int slot=gg_ctx_import(g->ctx,g->nt[i]);if(slot!=i){logline("GPU_GEN import %d -> %d %s",i,slot,gg_error());return 0;}
 }
 D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0};for(int i=0;i<3;i++)p->dev->lpVtbl->CreateQuery(p->dev,&qd,&g->q[i]);
 {D3D11_TEXTURE2D_DESC f=s;f.Width=512;f.Height=1;f.Usage=D3D11_USAGE_STAGING;f.BindFlags=0;f.CPUAccessFlags=D3D11_CPU_ACCESS_READ;f.MiscFlags=0;
  if(FAILED(p->dev->lpVtbl->CreateTexture2D(p->dev,&f,NULL,&g->fp))){logline("GPU_GEN fingerprint texture failed");return 0;}}
 if(!ga_setup(&g->alpha,p->dev,g->tex+3,d.Width,d.Height)){ga_free(&g->alpha);logline("GPU_GEN alpha fix setup failed");}
 {wchar_t v[4];g->hud_on=!(GetEnvironmentVariableW(L"CS_TEST_GPU_HUD",v,4)==1&&v[0]==L'0');if(g->hud_on&&!gh_setup(&g->hud,p->dev,g->tex,d.Width,d.Height))g->hud_on=0;}
 {g->gv=gpu_view_wanted();gv_switch=g->gv;g->src_ms=1000.0/60;g->lead_ms=4;p->dev->lpVtbl->CreateQuery(p->dev,&qd,&g->hq);if(g->gv)logline("GPU_VIEW test on");}
 g->wake=CreateEventW(NULL,FALSE,FALSE,NULL);g->thread=CreateThread(NULL,0,gg_worker,g,0,NULL);
 g->cur=g->prev=-1;g->ready_out=-1;g->hold_src=-1;
 logline("GPU_GEN setup %ux%u model=%ls hud=%d",d.Width,d.Height,gpu_gen_vfg?L"NVIDIA VFG (internal GPU path)":cs_models[mi].label,g->hud_on);(void)gdev;return 1;
}
static int gpu_gen_buffer(void *pad,void *info,void *data){
 GpuGen *g=data;GpuOut *o=&g->o;GpuProbe *p=&o->g;void *in=probe_buffer(info);if(!in||gp_n_memory(in)<1)return 1;
 void *mem=gp_peek_memory(in,0);if(!gp_is_d3d11(mem))return 1;
 ID3D11Resource *res=gp_resource(mem);unsigned sub=gp_subresource(mem);void *gdev=*(void**)((char*)mem+112);if(!res)return 1;
 if(!p->dev){ /* 2b setup (device, pool), then 2c setup */
  void *saved=((CsProbeInfo*)info)->data;gpu_out_buffer(pad,info,o); /* sets p->dev and the pool, outputs this first buffer */
  if(!gpu_out_on||!p->dev||!gg_setup(g,pad,res,gdev)){gpu_gen_on=0;logline("GPU_GEN disabled");gpu_rule_mark_failed(gpu_gen_model);} /* 2.3.3: next start uses the CPU path */
  (void)saved;return 1;
 }
 if(!gpu_gen_on)return gpu_out_buffer(pad,info,o);
 /* only the large preview's pipeline generates (as the CPU path, see main_pipe); others pass through untouched */
 if(g->pipe_state!=1){
  if(!g->pipe[0]&&g->bin){char *path=object_path(g->bin);if(path){char *e=strstr(path+1,"/");size_t n=e?(size_t)(e-path)+1:strlen(path);if(n>=sizeof g->pipe)n=sizeof g->pipe-1;memcpy(g->pipe,path,n);g->pipe[n]=0;mem_free(path);}}
  char known[64];AcquireSRWLockShared(&main_pipe_lock);memcpy(known,main_pipe,sizeof known);ReleaseSRWLockShared(&main_pipe_lock);
  if(!known[0]||!g->pipe[0])return 1;
  int other=strcmp(known,g->pipe)!=0;if(g->pipe_state!=(other?2:1)){g->pipe_state=other?2:1;logline("GPU_GEN role=%s pipeline=%s",other?"other":"main",g->pipe);}
  if(other)return 1;
 }
 {int on=!!((ui_requested()|frame_force_on)&1);
  if(!on){InterlockedExchange(&gpu_gen_running,0);if(g->gv)gv_end();if(g->was_on){while(g->busy)SwitchToThread();g->cur=g->prev=-1;g->since_new=0;g->caught=0;g->hold_src=-1;InterlockedExchange(&g->ready_out,-1);InterlockedExchange64(&g->ready_pair,0);logline("GPU_GEN off (passthrough)");}
   g->was_on=0;g->offticks++;return 1;}
  if(!g->was_on){g->was_on=1;logline("GPU_GEN on");}InterlockedExchange(&gpu_gen_running,1);}
 LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
 ID3D11DeviceContext *ctx=NULL;p->dev->lpVtbl->GetImmediateContext(p->dev,&ctx);
 /* new source = changed pixels at 64 points (8x1 each), like the CPU path's sampled fingerprint */
 int isnew=1;{gp_lock(gdev);for(int k=0;k<64;k++){int x=(int)((k%8*2+1)*(unsigned)p->w/16),y=(int)((k/8*2+1)*(unsigned)p->h/16);D3D11_BOX b={(UINT)x,(UINT)y,0,(UINT)x+8,(UINT)y+1,1};
   ctx->lpVtbl->CopySubresourceRegion(ctx,(ID3D11Resource*)g->fp,0,(UINT)k*8,0,0,res,sub,&b);}
  D3D11_MAPPED_SUBRESOURCE mp;if(SUCCEEDED(ctx->lpVtbl->Map(ctx,(ID3D11Resource*)g->fp,0,D3D11_MAP_READ,0,&mp))){isnew=memcmp(g->fpv,mp.pData,sizeof g->fpv)!=0;memcpy(g->fpv,mp.pData,sizeof g->fpv);ctx->lpVtbl->Unmap(ctx,(ID3D11Resource*)g->fp,0);}
  gp_unlock(gdev);}
 g->last_res=res;g->last_sub=sub;g->ticks++;if(isnew)g->news++;
 int src=-1,org=-1; /* shared texture to show this tick; org = newest original of a generated pair (HUD keep) */
 int gvact=0;
 if(g->gv&&InterlockedCompareExchange(&low_view,0,0)){
  if(!g->gv_reg){g->gv_reg=1;gv_register(g->nt,p->w,p->h);}
  gv_begin(p->w,p->h);gvact=InterlockedCompareExchange(&direct_gpu_feed,0,0)!=0;}
 if(isnew){
  if(g->last_arr){double d=1000.0*(t0.QuadPart-g->last_arr)/p->freq.QuadPart;if(d>8&&d<45)g->src_ms=0.9*g->src_ms+0.1*d;
   {static unsigned long long hb[6];int b=d<6?0:d<12?1:d<15.5?2:d<18?3:d<22?4:5;hb[b]++;static unsigned long long hn;if(++hn%600==0){logline("GPU_GEN_ARRIVAL gaps <6=%llu 6-12=%llu 12-15.5=%llu 15.5-18=%llu 18-22=%llu >22=%llu",hb[0],hb[1],hb[2],hb[3],hb[4],hb[5]);memset(hb,0,sizeof hb);}}}
  g->last_arr=t0.QuadPart;
  int slot=-1;for(int i=0;i<3;i++)if(i!=g->cur&&i!=g->prev){slot=i;break;}
  /* the worker may still read the slot that is about to become free next time; wait only if it reads this one */
  if(g->busy&&(g->ja==slot||g->jb==slot)){g->blocked++;while(g->busy)SwitchToThread();}
  gp_lock(gdev);ctx->lpVtbl->CopySubresourceRegion(ctx,(ID3D11Resource*)g->tex[slot],0,0,0,0,res,sub,NULL);
  if(g->hud_on)gh_source(&g->hud,ctx,slot);
  ctx->lpVtbl->End(ctx,(ID3D11Asynchronous*)g->q[slot]);ctx->lpVtbl->Flush(ctx);gp_unlock(gdev);
  /* cut: mean channel difference of the 64-point fingerprint against the current source */
  int cut=0;if(g->cur>=0){unsigned long long sum=0;const uint8_t *a=(const uint8_t*)g->fpv,*b=(const uint8_t*)g->fps[g->cur];for(int k=0;k<512*4;k++)if((k&3)!=3)sum+=(unsigned)abs((int)a[k]-(int)b[k]);cut=sum>48ull*512*3;}
  memcpy(g->fps[slot],g->fpv,sizeof g->fpv);if(cut)g->cuts++;
  /* Show order: mid(P,C) on the new-source tick, C on the repeated tick (1.5 sources late), so the worker has a whole
   * source interval for each pair. P/C = previous/current before this source. */
  int P=g->prev,C=g->cur;
  if(g->caught){src=C>=0?C:slot;g->caught=0;g->repeat++;} /* already showed C during the still scene: do not step back to mid(P,C) */
  else if(P>=0&&C>=0&&InterlockedCompareExchange64(&g->ready_pair,0,0)==(LONG64)g->id[C]){src=3+InterlockedCompareExchange(&g->ready_out,0,0);org=C;g->gen++;}
  else if(P>=0){src=P;g->repeat++;}else src=C>=0?C:slot;
  g->since_new=0;
  /* 2.3.10 (11-18 GTA5 30 fps, 120 ticks): keep the mid frame on screen for half a source interval, then the original, so a
   * 30 fps source gives mid,mid,orig,orig. 60 fps at 120 ticks stays mid,orig (the half interval minus 2 ms ends before tick 2). */
  if(src>=3){g->hold_src=src;g->hold_org=org;g->hold_until=t0.QuadPart+(LONGLONG)((g->src_ms*0.5-2.0)*p->freq.QuadPart/1000);}else g->hold_src=-1;
  g->prev=C;g->cur=slot;g->id[slot]=++g->next_id;
  if(g->prev>=0){
   if(cut){InterlockedExchange64(&g->ready_pair,0);if(gvact){gv_post(g->cur,t0.QuadPart,t0.QuadPart,0);g->last_due=t0.QuadPart;}} /* no mid frame across a cut */
   else if(!g->busy){g->jarr=t0.QuadPart;g->ja=g->prev;g->jaid=g->id[g->prev];g->jb=g->cur;g->jbid=g->id[g->cur];g->jout=g->out_next;g->out_next^=1;
    InterlockedExchange(&g->busy,1);SetEvent(g->wake);}
   else{g->skipped++;if(gvact){LONGLONG d=t0.QuadPart+(LONGLONG)((g->lead_ms+g->src_ms*0.5)*p->freq.QuadPart/1000);gv_post(g->cur,d,t0.QuadPart,0);g->last_due=d;}}
  }
  else if(gvact)gv_post(g->cur,t0.QuadPart,t0.QuadPart,0); /* first source */
 }else{
  g->since_new++;
  /* 2.3.10: still scene by time (no new source for 1.5 source intervals), not by tick count: at 120 ticks a 30 fps source
   * repeats 3 ticks and was taken for a still scene every time, so the host output (recording, preview) had no mid frames. */
  double since=g->last_arr?1000.0*(t0.QuadPart-g->last_arr)/p->freq.QuadPart:1e9;
  if(g->hold_src>=0&&t0.QuadPart<g->hold_until){src=g->hold_src;org=g->hold_org;}
  else if(since>g->src_ms*1.5&&g->cur>=0){src=g->cur;if(!g->caught){g->caught=1;g->catchups++;}} /* still scene: catch up to the newest source */
  else src=g->prev>=0?g->prev:g->cur;
  if(!(g->hold_src>=0&&t0.QuadPart<g->hold_until))g->hold_src=-1;
 }
 void *out=NULL;
 if(src<0||pool_acquire(o->pool,&out,NULL)!=0||!out){ctx->lpVtbl->Release(ctx);return 1;}
 buffer_copy_into(out,in,7,0,(size_t)-1);cs_buf_mark(out,src>=3);
 void *omem=gp_peek_memory(out,0);ID3D11Resource *ores=gp_is_d3d11(omem)?gp_resource(omem):NULL;unsigned osub=ores?gp_subresource(omem):0;
 if(!ores){buffer_unref(out);ctx->lpVtbl->Release(ctx);return 1;}
 gp_lock(gdev);
 if(src>=3&&org>=0&&g->hud_on&&!gvact&&gh_compose(&g->hud,ctx,p->dev,ores,osub,src,org))g->composed++;
 /* 2.3.8: low latency view on (gvact): recording keeps going, so use the worker's HUD-composed copy of this generated frame */
 else if(src>=3&&org>=0&&gvact&&g->hud_on&&InterlockedCompareExchange64(&g->hud_pair,0,0)==(LONG64)g->id[org])ctx->lpVtbl->CopySubresourceRegion(ctx,ores,osub,0,0,0,(ID3D11Resource*)g->tex[InterlockedCompareExchange(&g->hud_k,0,0)],0,NULL);
 else ctx->lpVtbl->CopySubresourceRegion(ctx,ores,osub,0,0,0,(ID3D11Resource*)g->tex[src],0,NULL);
 ctx->lpVtbl->Flush(ctx);gp_unlock(gdev);
 ctx->lpVtbl->Release(ctx);
 ((CsProbeInfo*)info)->data=out;buffer_unref(in);
 QueryPerformanceCounter(&t1);o->cpu_sum+=1000.0*(t1.QuadPart-t0.QuadPart)/p->freq.QuadPart;o->n++;
 if(g->ticks%1200==0){double n=g->msn?g->msn:1;
  logline("GPU_GEN view_mid_skips=%llu alpha_fixed=%llu",g->gv_skips,g->alpha.n);logline("GPU_GEN ticks=%llu new=%llu generated=%llu repeat=%llu skipped=%llu blocked=%llu fail=%llu cuts=%llu catchups=%llu hud_composed=%llu off=%llu tick_cpu_ms=%.3f gen_copyin_ms=%.2f gen_rife_ms=%.2f gen_copyout_ms=%.2f",
   g->ticks,g->news,g->gen,g->repeat,g->skipped,g->blocked,g->fail,g->cuts,g->catchups,g->composed,g->offticks,o->n?o->cpu_sum/o->n:0,g->ms[0]/n,g->ms[1]/n,g->ms[2]/n);
  o->cpu_sum=0;o->n=0;g->ms[0]=g->ms[1]=g->ms[2]=0;g->msn=0;}
 return 1;
}
static void gpu_gen_free(void *v){
 GpuGen *g=v;InterlockedExchange(&gpu_gen_running,0);if(g->thread){g->stop=1;SetEvent(g->wake);WaitForSingleObject(g->thread,5000);CloseHandle(g->thread);}if(g->wake)CloseHandle(g->wake);
 if(g->ctx)gg_ctx_destroy(g->ctx);if(g->engine)gg_destroy(g->engine);
 if(g->gv)gv_end();if(g->hq)g->hq->lpVtbl->Release(g->hq);
 for(int i=0;i<GV_TEX;i++){if(g->nt[i])CloseHandle(g->nt[i]);if(g->tex[i])g->tex[i]->lpVtbl->Release(g->tex[i]);}
 gh_free(&g->hud);ga_free(&g->alpha);
 for(int i=0;i<3;i++)if(g->q[i])g->q[i]->lpVtbl->Release(g->q[i]);if(g->fp)g->fp->lpVtbl->Release(g->fp);
 GpuOut *o=&g->o;if(o->pool){pool_set_active(o->pool,0);object_unref(o->pool);}if(o->g.query)o->g.query->lpVtbl->Release(o->g.query);if(o->g.shared)o->g.shared->lpVtbl->Release(o->g.shared);if(o->g.dev)o->g.dev->lpVtbl->Release(o->g.dev);
 HeapFree(GetProcessHeap(),0,v);
}
