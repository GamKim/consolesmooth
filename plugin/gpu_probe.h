/* SPDX-License-Identifier: MIT
 * 11-16 GPU-path step 2a, TEST ONLY (CS_TEST_GPU_PROBE=1, test tree): on the frame filter's entry pad (before
 * d3d11download) take the host's D3D11 texture straight from the buffer, open the host's device, and time a GPU copy
 * of each new frame into our own shared (NT handle) texture. Nothing is changed in the picture; logs GPU_PROBE lines.
 */
#include <d3d11_1.h>
#include <dxgi1_2.h>
static int gpu_probe_on;
static unsigned (*gp_n_memory)(void*);static void *(*gp_peek_memory)(void*,unsigned);
static int (*gp_is_d3d11)(void*);static void *(*gp_resource)(void*);static unsigned (*gp_subresource)(void*);
static void *(*gp_device_handle)(void*);static void (*gp_lock)(void*),(*gp_unlock)(void*);
typedef struct {void *last;ID3D11Device *dev;ID3D11Texture2D *shared;HANDLE handle;ID3D11Query *query;void *gdev;
 double sum,max;unsigned n,total,fail;LARGE_INTEGER freq;int w,h;} GpuProbe;
static int gpu_probe_init(void){
 HMODULE gst=GetModuleHandleW(L"gstreamer-1.0-0.dll"),d=GetModuleHandleW(L"gstd3d11-1.0-0.dll");
 if(!gst||!d){logline("GPU_PROBE missing module gst=%p d3d11=%p",(void*)gst,(void*)d);return 0;}
 *(FARPROC*)&gp_n_memory=GetProcAddress(gst,"gst_buffer_n_memory");*(FARPROC*)&gp_peek_memory=GetProcAddress(gst,"gst_buffer_peek_memory");
 *(FARPROC*)&gp_is_d3d11=GetProcAddress(d,"gst_is_d3d11_memory");*(FARPROC*)&gp_resource=GetProcAddress(d,"gst_d3d11_memory_get_resource_handle");
 *(FARPROC*)&gp_subresource=GetProcAddress(d,"gst_d3d11_memory_get_subresource_index");*(FARPROC*)&gp_device_handle=GetProcAddress(d,"gst_d3d11_device_get_device_handle");
 *(FARPROC*)&gp_lock=GetProcAddress(d,"gst_d3d11_device_lock");*(FARPROC*)&gp_unlock=GetProcAddress(d,"gst_d3d11_device_unlock");
 int ok=gp_n_memory&&gp_peek_memory&&gp_is_d3d11&&gp_resource&&gp_subresource&&gp_device_handle&&gp_lock&&gp_unlock;
 logline("GPU_PROBE init ok=%d",ok);return ok;
}
static int gpu_probe_buffer(void *pad,void *info,void *data){
 (void)pad;GpuProbe *g=data;void *buf=probe_buffer(info);if(!buf||buf==g->last)return 1;g->last=buf;
 if(gp_n_memory(buf)<1){g->fail++;return 1;}
 void *mem=gp_peek_memory(buf,0);
 if(!gp_is_d3d11(mem)){if(!g->fail++)logline("GPU_PROBE not_d3d11_memory");return 1;}
 ID3D11Resource *res=gp_resource(mem);unsigned sub=gp_subresource(mem);if(!res){g->fail++;return 1;}
 void *gdev=*(void**)((char*)mem+112); /* GstD3D11Memory.device after GstMemory (112 bytes on Win64) */
 ID3D11Device *dev=NULL;res->lpVtbl->GetDevice(res,&dev);
 if(!g->dev){
  QueryPerformanceFrequency(&g->freq);
  void *handle=gp_device_handle(gdev);
  ID3D11Texture2D *tex=NULL;res->lpVtbl->QueryInterface(res,&IID_ID3D11Texture2D,(void**)&tex);
  D3D11_TEXTURE2D_DESC d={0};if(tex){tex->lpVtbl->GetDesc(tex,&d);tex->lpVtbl->Release(tex);}
  IDXGIDevice *xd=NULL;IDXGIAdapter *ad=NULL;DXGI_ADAPTER_DESC adesc={0};
  if(SUCCEEDED(dev->lpVtbl->QueryInterface(dev,&IID_IDXGIDevice,(void**)&xd))&&SUCCEEDED(xd->lpVtbl->GetAdapter(xd,&ad)))ad->lpVtbl->GetDesc(ad,&adesc);
  if(ad)ad->lpVtbl->Release(ad);if(xd)xd->lpVtbl->Release(xd);
  logline("GPU_PROBE first tex=%ux%u fmt=%d array=%u sub=%u bind=0x%x misc=0x%x device_match=%d luid=%08lx:%08lx adapter=%ls",
   d.Width,d.Height,(int)d.Format,d.ArraySize,sub,d.BindFlags,d.MiscFlags,handle==(void*)dev,adesc.AdapterLuid.HighPart,adesc.AdapterLuid.LowPart,adesc.Description);
  if(handle!=(void*)dev){logline("GPU_PROBE device pointer mismatch, stop");dev->lpVtbl->Release(dev);gpu_probe_on=0;return 1;}
  D3D11_TEXTURE2D_DESC s=d;s.MipLevels=1;s.ArraySize=1;s.Usage=D3D11_USAGE_DEFAULT;s.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;s.CPUAccessFlags=0;
  s.MiscFlags=0x800|0x2; /* SHARED_NTHANDLE | SHARED */
  HRESULT hr=dev->lpVtbl->CreateTexture2D(dev,&s,NULL,&g->shared);
  D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0};dev->lpVtbl->CreateQuery(dev,&qd,&g->query);
  logline("GPU_PROBE shared texture hr=0x%08lx",(unsigned long)hr);if(FAILED(hr)){dev->lpVtbl->Release(dev);gpu_probe_on=0;return 1;}
  g->dev=dev;g->gdev=gdev;g->w=d.Width;g->h=d.Height;dev=NULL;
 }
 if(dev)dev->lpVtbl->Release(dev);
 LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
 ID3D11DeviceContext *ctx=NULL;g->dev->lpVtbl->GetImmediateContext(g->dev,&ctx);
 gp_lock(g->gdev);
 ctx->lpVtbl->CopySubresourceRegion(ctx,(ID3D11Resource*)g->shared,0,0,0,0,res,sub,NULL);
 ctx->lpVtbl->End(ctx,(ID3D11Asynchronous*)g->query);ctx->lpVtbl->Flush(ctx);
 gp_unlock(g->gdev);
 BOOL done=FALSE;for(;;){gp_lock(g->gdev);HRESULT q=ctx->lpVtbl->GetData(ctx,(ID3D11Asynchronous*)g->query,&done,sizeof done,0);gp_unlock(g->gdev);if(q==S_OK&&done)break;YieldProcessor();}
 ctx->lpVtbl->Release(ctx);
 QueryPerformanceCounter(&t1);double ms=1000.0*(t1.QuadPart-t0.QuadPart)/g->freq.QuadPart;
 g->sum+=ms;if(ms>g->max)g->max=ms;g->n++;g->total++;
 if(g->n==300){logline("GPU_PROBE copy %ux%u frames=%u avg_ms=%.3f max_ms=%.3f fail=%u",g->w,g->h,g->total,g->sum/g->n,g->max,g->fail);g->sum=g->max=0;g->n=0;}
 return 1;
}
static void gpu_probe_free(void *p){GpuProbe *g=p;if(g->query)g->query->lpVtbl->Release(g->query);if(g->shared)g->shared->lpVtbl->Release(g->shared);if(g->dev)g->dev->lpVtbl->Release(g->dev);HeapFree(GetProcessHeap(),0,p);}
static __attribute__((unused)) void gpu_probe_attach(void *bin){ /* test probe (CS_TEST_GPU_PROBE), not hooked in 2.2.8 */
 static int checked;if(!checked){checked=1;wchar_t v[4];gpu_probe_on=GetEnvironmentVariableW(L"CS_TEST_GPU_PROBE",v,4)==1&&v[0]==L'1'&&gpu_probe_init();}
 if(!gpu_probe_on)return;
 void *entry=bin_find(bin,"cs_entry"),*sink=entry?static_pad(entry,"sink"):NULL;
 GpuProbe *g=sink?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*g)):NULL;
 if(g&&!add_probe(sink,1<<4,gpu_probe_buffer,g,gpu_probe_free))gpu_probe_free(g);
 if(sink)object_unref(sink);if(entry)object_unref(entry);
}
/* 11-16 step 2b, TEST ONLY (CS_TEST_GPU_OUT=1): the frame filter becomes "identity ! identity" (no d3d11download /
 * d3d11upload). Each buffer is copied on the GPU: host texture -> our shared texture (engine input stand-in) -> a new
 * buffer from a D3D11 buffer pool on the host's device (engine output stand-in), with a red 96x96 square in the top
 * left so it is visible that our buffer reaches the screen. Logs GPU_OUT lines. No frame generation in this mode. */
static int gpu_out_on;
static void *(*go_pool_new)(void*),*(*go_params_new)(void*,void*,int,unsigned,unsigned),*(*go_info_new)(void*);
static void (*go_set_params)(void*,void*),(*go_params_free)(void*),(*go_info_free)(void*);
typedef struct {GpuProbe g;void *pool;double cpu_sum,gpu_sum,cpu_max;unsigned n,gn;unsigned long long total,fallback;} GpuOut;
static int gpu_out_init(void){
 if(!gpu_probe_init())return 0;
 HMODULE d=GetModuleHandleW(L"gstd3d11-1.0-0.dll"),v=GetModuleHandleW(L"gstvideo-1.0-0.dll");if(!d||!v)return 0;
 *(FARPROC*)&go_pool_new=GetProcAddress(d,"gst_d3d11_buffer_pool_new");*(FARPROC*)&go_params_new=GetProcAddress(d,"gst_d3d11_allocation_params_new");
 *(FARPROC*)&go_set_params=GetProcAddress(d,"gst_buffer_pool_config_set_d3d11_allocation_params");*(FARPROC*)&go_params_free=GetProcAddress(d,"gst_d3d11_allocation_params_free");
 *(FARPROC*)&go_info_new=GetProcAddress(v,"gst_video_info_new_from_caps");*(FARPROC*)&go_info_free=GetProcAddress(v,"gst_video_info_free");
 int ok=go_pool_new&&go_params_new&&go_set_params&&go_params_free&&go_info_new&&go_info_free;logline("GPU_OUT init ok=%d",ok);return ok;
}
static int gpu_out_buffer(void *pad,void *info,void *data){
 GpuOut *o=data;GpuProbe *g=&o->g;void *in=probe_buffer(info);if(!in||gp_n_memory(in)<1)return 1;
 void *mem=gp_peek_memory(in,0);if(!gp_is_d3d11(mem)){if(!o->fallback++)logline("GPU_OUT not_d3d11_memory");return 1;}
 ID3D11Resource *res=gp_resource(mem);unsigned sub=gp_subresource(mem);void *gdev=*(void**)((char*)mem+112);if(!res)return 1;
 LARGE_INTEGER t0,t1,t2;QueryPerformanceCounter(&t0);
 if(!g->dev){
  QueryPerformanceFrequency(&g->freq);ID3D11Device *dev=NULL;res->lpVtbl->GetDevice(res,&dev);
  if(gp_device_handle(gdev)!=(void*)dev){logline("GPU_OUT device mismatch");dev->lpVtbl->Release(dev);gpu_out_on=0;return 1;}
  ID3D11Texture2D *tex=NULL;res->lpVtbl->QueryInterface(res,&IID_ID3D11Texture2D,(void**)&tex);D3D11_TEXTURE2D_DESC d={0};if(tex){tex->lpVtbl->GetDesc(tex,&d);tex->lpVtbl->Release(tex);}
  D3D11_TEXTURE2D_DESC s=d;s.MipLevels=1;s.ArraySize=1;s.Usage=D3D11_USAGE_DEFAULT;s.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;s.CPUAccessFlags=0;s.MiscFlags=0x800|0x2;
  if(FAILED(dev->lpVtbl->CreateTexture2D(dev,&s,NULL,&g->shared))){logline("GPU_OUT shared texture failed");dev->lpVtbl->Release(dev);gpu_out_on=0;return 1;}
  D3D11_QUERY_DESC qd={D3D11_QUERY_EVENT,0};dev->lpVtbl->CreateQuery(dev,&qd,&g->query);
  void *caps=current_caps(pad),*vinfo=caps?go_info_new(caps):NULL;
  o->pool=go_pool_new(gdev);void *params=vinfo?go_params_new(gdev,vinfo,0,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET,0):NULL;
  size_t size=vinfo?*(size_t*)((char*)vinfo+24):0; /* GstVideoInfo.size */
  int ok=0;if(o->pool&&params){void *config=pool_get_config(o->pool);pool_config_params(config,caps,(unsigned)size,4,0);go_set_params(config,params);ok=pool_set_config(o->pool,config)&&pool_set_active(o->pool,1);}
  logline("GPU_OUT setup %ux%u pool=%d size=%llu",d.Width,d.Height,ok,(unsigned long long)size);
  if(params)go_params_free(params);if(vinfo)go_info_free(vinfo);if(caps)caps_unref(caps);
  g->dev=dev;g->gdev=gdev;g->w=d.Width;g->h=d.Height;if(!ok){gpu_out_on=0;return 1;}
 }
 void *out=NULL;if(pool_acquire(o->pool,&out,NULL)!=0||!out){if(!o->fallback++)logline("GPU_OUT acquire failed");return 1;}
 if(!buffer_copy_into(out,in,7,0,(size_t)-1)){buffer_unref(out);return 1;}
 void *omem=gp_peek_memory(out,0);ID3D11Resource *ores=gp_is_d3d11(omem)?gp_resource(omem):NULL;unsigned osub=ores?gp_subresource(omem):0;
 if(!ores){buffer_unref(out);if(!o->fallback++)logline("GPU_OUT output not d3d11");return 1;}
 ID3D11DeviceContext *ctx=NULL;g->dev->lpVtbl->GetImmediateContext(g->dev,&ctx);
 ID3D11DeviceContext1 *c1=NULL;ctx->lpVtbl->QueryInterface(ctx,&IID_ID3D11DeviceContext1,(void**)&c1);
 ID3D11RenderTargetView *rtv=NULL;D3D11_RENDER_TARGET_VIEW_DESC rd={0};rd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
 int arr=osub>0;if(arr){rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rd.Texture2DArray.FirstArraySlice=osub;rd.Texture2DArray.ArraySize=1;}else rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 gp_lock(g->gdev);
 ctx->lpVtbl->CopySubresourceRegion(ctx,(ID3D11Resource*)g->shared,0,0,0,0,res,sub,NULL);
 ctx->lpVtbl->CopySubresourceRegion(ctx,ores,osub,0,0,0,(ID3D11Resource*)g->shared,0,NULL);
 /* 11-16 night: red 2b marker removed */
 int sample=(o->total%60)==0;if(sample)ctx->lpVtbl->End(ctx,(ID3D11Asynchronous*)g->query);
 ctx->lpVtbl->Flush(ctx);gp_unlock(g->gdev);
 QueryPerformanceCounter(&t1);
 if(sample){BOOL done=FALSE;for(;;){gp_lock(g->gdev);HRESULT q=ctx->lpVtbl->GetData(ctx,(ID3D11Asynchronous*)g->query,&done,sizeof done,0);gp_unlock(g->gdev);if(q==S_OK&&done)break;YieldProcessor();}
  QueryPerformanceCounter(&t2);o->gpu_sum+=1000.0*(t2.QuadPart-t0.QuadPart)/g->freq.QuadPart;o->gn++;}
 if(rtv)rtv->lpVtbl->Release(rtv);if(c1)c1->lpVtbl->Release(c1);ctx->lpVtbl->Release(ctx);
 ((CsProbeInfo*)info)->data=out;buffer_unref(in);
 double ms=1000.0*(t1.QuadPart-t0.QuadPart)/g->freq.QuadPart;o->cpu_sum+=ms;if(ms>o->cpu_max)o->cpu_max=ms;o->n++;o->total++;
 if(o->n==600){logline("GPU_OUT %ux%u frames=%llu cpu_avg_ms=%.3f cpu_max_ms=%.3f gpu_done_avg_ms=%.3f (sampled %u) arr=%d fallback=%llu",g->w,g->h,o->total,o->cpu_sum/o->n,o->cpu_max,o->gn?o->gpu_sum/o->gn:-1,o->gn,arr,o->fallback);o->cpu_sum=o->cpu_max=o->gpu_sum=0;o->n=o->gn=0;}
 return 1;
}
static void gpu_out_free(void *p){GpuOut *o=p;if(o->pool){pool_set_active(o->pool,0);object_unref(o->pool);}if(o->g.query)o->g.query->lpVtbl->Release(o->g.query);if(o->g.shared)o->g.shared->lpVtbl->Release(o->g.shared);if(o->g.dev)o->g.dev->lpVtbl->Release(o->g.dev);HeapFree(GetProcessHeap(),0,p);}
#include "gpu_gen.h"
static int rife_gpu;
/* 11-18 internal: GPU low latency view (gpu_view.h) when settings.ini has the low latency view and full screen on.
 * CS_TEST_GPU_VIEW=1 forces it, =0 turns it off. Read once, at pipeline build. */
static int gpu_view_wanted(void){
 static int cached=-1;if(cached>=0)return cached;wchar_t v[4];DWORD e=GetEnvironmentVariableW(L"CS_TEST_GPU_VIEW",v,4);
 if(e==1){cached=v[0]==L'1';return cached;}
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);
 cached=0;if(n&&n<32000){swprintf(path,32768,L"%ls\\settings.ini",dir);cached=GetPrivateProfileIntW(L"preview",L"display_mode",0,path)==1;} /* 2.3.15: full screen always means the low latency view */
 logline("GPU_VIEW wanted=%d",cached);return cached;
}
static void *gpu_out_bin(void){
 /* 2.2.8 internal: NVIDIA VFG (family 4) always runs on this GPU path (the CPU frame path is 12-17 ms per frame, too slow for 120). */
 int model=frame_force_model>=0?frame_force_model:(ui_requested()>>16)&255,vfg=cs_model_valid(model)&&cs_models[model].family==4;
 /* 11-18 test (CS_TEST_GPU_VIEW=1): RIFE models (family 0) on the GPU path too */
 /* 2.3.3: one rule decides (gpu_rule.h); VFG failed before -> CPU path too */
 gpu_gen_model=model;
 {int use=gpu_rule_now(model);if(vfg&&!use){vfg=0;logline("GPU_RULE VFG on the CPU path (earlier GPU-path failure)");}
  if(!vfg&&use){rife_gpu=1;gpu_gen_family=cs_models[model].family;}InterlockedExchange(&gpu_path_active,(vfg||rife_gpu)?1:0);}
 static int checked;if(!checked){checked=1;wchar_t v[4];gpu_out_on=(vfg||rife_gpu||(GetEnvironmentVariableW(L"CS_TEST_GPU_OUT",v,4)==1&&v[0]==L'1'))&&gpu_out_init();}
 if(!gpu_out_on)return NULL;
 void *error=NULL,*bin=parse_bin("identity name=cs_entry ! identity name=cs_framegen",1,&error);
 if(error){error_free(error);if(bin)object_unref(bin);return NULL;}if(!bin)return NULL;
 void *el=bin_find(bin,"cs_framegen"),*src=el?static_pad(el,"src"):NULL;
 static int genChecked;if(!genChecked){genChecked=1;wchar_t v[4];gpu_gen_force_vfg=vfg;gpu_gen_on=(vfg||rife_gpu||(GetEnvironmentVariableW(L"CS_TEST_GPU_GEN",v,4)==1&&v[0]==L'1'))&&gpu_gen_init();}
 GpuOut *o=src?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,gpu_gen_on?sizeof(GpuGen):sizeof(GpuOut)):NULL;
 int added=o&&(gpu_gen_on?add_probe(src,1<<4,gpu_gen_buffer,o,gpu_gen_free):add_probe(src,1<<4,gpu_out_buffer,o,gpu_out_free));
 if(added&&gpu_gen_on)((GpuGen*)o)->bin=bin;
 if(!added){if(o)HeapFree(GetProcessHeap(),0,o);if(src)object_unref(src);if(el)object_unref(el);object_unref(bin);return NULL;}
 if(src)object_unref(src);if(el)object_unref(el);logline("GPU_OUT bin");return bin;
}
