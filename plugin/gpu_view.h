/* SPDX-License-Identifier: MIT
 * 11-18 TEST (CS_TEST_GPU_VIEW=1, GPU path + low latency view + full screen): the full screen window is fed straight
 * from the GPU path's worker instead of from the host's preview sink.
 *  - order: when a new original N arrives, mid(C,N) is made and shown at arrival + lead, N at arrival + lead + half the
 *    source interval (lead = recent generation time + 0.5 ms, so mid and N are evenly spaced);
 *  - the window's own device opens the GPU path's shared NT textures (no CPU copy, no host queue or sink);
 *  - presents go out as soon as they are due; an overdue present is replaced by a newer due one (no queue).
 * Logs GPU_VIEW lines (arrival -> present for mid and original, late, dropped).
 */
#define GV_TEX 7
static int gv_switch;                         /* CS_TEST_GPU_VIEW=1 */
static SRWLOCK gv_lock=SRWLOCK_INIT;
typedef struct {int tex;LONGLONG due,arr;int kind;} GvItem; /* kind 0 original, 1 generated */
static GvItem gv_q[8];static int gv_n;
static HANDLE gv_nt[GV_TEX];static LONG gv_gen,gv_srv_gen=-1;static unsigned gv_w,gv_h;
static ID3D11ShaderResourceView *gv_srv[GV_TEX];
static HANDLE gv_wake,gv_thread;static volatile LONG gv_stop;
static LONG64 gv_shown[2],gv_late,gv_dropped,gv_fail,gv_drop_kind[2],gv_post_late[2];static double gv_wake_late_max,gv_wake_late_sum,gv_pres_max,gv_pres_sum,gv_post_late_max[2];static LONG64 gv_pres_n;static double gv_lat_sum[2],gv_lat_max[2];static LONGLONG gv_freq;
static void gv_release_srv(void){for(int i=0;i<GV_TEX;i++)if(gv_srv[i]){ID3D11ShaderResourceView_Release(gv_srv[i]);gv_srv[i]=NULL;}gv_srv_gen=-1;}
static void gv_lab_release(void);
static void gv_hdr_src_release(void);static ID3D11Resource *gv_hdr_src_label(ID3D11Resource *src);
static void gv_direct_release_hook(void){gv_release_srv();gv_lab_release();gv_hdr_src_release();} /* window thread, direct_lock held exclusively */
static void gv_register(HANDLE *nt,unsigned w,unsigned h){
 AcquireSRWLockExclusive(&gv_lock);for(int i=0;i<GV_TEX;i++)gv_nt[i]=nt[i];gv_w=w;gv_h=h;gv_n=0;++gv_gen;ReleaseSRWLockExclusive(&gv_lock);
 logline("GPU_VIEW register %ux%u gen=%ld",w,h,gv_gen);
}
static void gv_post(int tex,LONGLONG due,LONGLONG arr,int kind){
 {LARGE_INTEGER n;QueryPerformanceCounter(&n);if(gv_freq&&due<n.QuadPart){double l=1000.0*(n.QuadPart-due)/gv_freq;gv_post_late[kind]++;if(l>gv_post_late_max[kind])gv_post_late_max[kind]=l;}}
 AcquireSRWLockExclusive(&gv_lock);
 if(gv_n==8){memmove(gv_q,gv_q+1,sizeof(GvItem)*7);gv_n=7;++gv_dropped;}
 int i=gv_n;while(i>0&&gv_q[i-1].due>due){gv_q[i]=gv_q[i-1];--i;}gv_q[i]=(GvItem){tex,due,arr,kind};++gv_n;
 ReleaseSRWLockExclusive(&gv_lock);SetEvent(gv_wake);
}
static void gv_wait_until(LONGLONG target){
 static _Thread_local HANDLE timer;LARGE_INTEGER now;
 if(!timer)timer=CreateWaitableTimerExW(NULL,NULL,0x00000002,TIMER_ALL_ACCESS);
 for(;;){QueryPerformanceCounter(&now);LONGLONG left=target-now.QuadPart;if(left<=0)return;double ms=1000.0*left/gv_freq;
  if(ms>0.7&&timer){LARGE_INTEGER due;due.QuadPart=-(LONGLONG)((ms-0.5)*10000.0);if(SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)){WaitForSingleObject(timer,INFINITE);continue;}}
  YieldProcessor();}
}
static const IID gv_iid_device1={0xa04bfb29,0x08ef,0x43d6,{0xa4,0x9c,0xa9,0xbd,0xbd,0xcb,0xe6,0x86}}; /* ID3D11Device1 */
static void gv_draw_label(void);
static int gv_present(int tex,int kind){
 int ok=0;AcquireSRWLockShared(&direct_lock);
 if(InterlockedCompareExchange(&direct_state,0,0)==2&&direct_dev){
  AcquireSRWLockShared(&gv_lock);LONG gen=gv_gen;HANDLE h=gv_nt[tex];ReleaseSRWLockShared(&gv_lock);
  if(gen!=gv_srv_gen){gv_release_srv();gv_srv_gen=gen;}
  if(!gv_srv[tex]&&h){ID3D11Device1 *d1=NULL;ID3D11Texture2D *t=NULL;
   if(ID3D11Device_QueryInterface(direct_dev,&gv_iid_device1,(void**)&d1)>=0){if(ID3D11Device1_OpenSharedResource1(d1,h,&cs_iid_texture,(void**)&t)<0)t=NULL;ID3D11Device1_Release(d1);}
   if(t){if(ID3D11Device_CreateShaderResourceView(direct_dev,(ID3D11Resource*)t,NULL,&gv_srv[tex])<0)gv_srv[tex]=NULL;ID3D11Texture2D_Release(t);}
   if(!gv_srv[tex]&&gv_fail++<5)logline("GPU_VIEW open shared texture %d failed",tex);}
  if(gv_srv[tex]&&hdr_on){ /* 2.3.13: RTX Video HDR straight into the HDR10 back buffer */
   ID3D11Resource *r=NULL;ID3D11ShaderResourceView_GetResource(gv_srv[tex],&r);int b=r?hdr_blt(gv_hdr_src_label(r)):0;if(r)ID3D11Resource_Release(r);
   /* readout in HDR too (dimmed shader), drawn into the HDR texture, then copied to the back buffer */
   if(b)b=hdr_finish(); /* no 3D draws in the HDR window: a label draw next to the RTX Video HDR Blt removed the device (887a0005, 11-19) */
   LONG sync=InterlockedCompareExchange(&direct_sync,0,0);
   HRESULT hr=!b?E_FAIL:sync&&direct_tearing?IDXGISwapChain1_Present(direct_chain,0,DXGI_PRESENT_ALLOW_TEARING):IDXGISwapChain1_Present(direct_chain,1,DXGI_PRESENT_DO_NOT_WAIT);
   if(hr==DXGI_ERROR_WAS_STILL_DRAWING){++direct_skips;hr=S_OK;}else if(hr>=0)++direct_presents;
   ok=hr>=0;{static int nfail,inrow;if(!ok&&nfail++<10)logline("GPU_VIEW hdr present failed hr=%08lx blt=%d",(unsigned long)hr,b);
    /* safety: HDR keeps failing -> off for this session; the window closes and reopens in SDR */
    inrow=ok?0:inrow+1;if(inrow>=30){inrow=0;InterlockedExchange(&gen_hdr,0);logline("HDR_FAILED_OFF");InterlockedExchange(&direct_state,-3);if(direct_hwnd)PostMessageW(direct_hwnd,DIRECT_CLOSE_MESSAGE,0,0);}}}
  else if(gv_srv[tex]){
   ID3D11DeviceContext_OMSetRenderTargets(direct_ctx,1,&direct_rtv,NULL);ID3D11DeviceContext_RSSetViewports(direct_ctx,1,&direct_view);
   ID3D11DeviceContext_IASetPrimitiveTopology(direct_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11DeviceContext_VSSetShader(direct_ctx,direct_vs,NULL,0);
   ID3D11DeviceContext_PSSetShader(direct_ctx,(InterlockedCompareExchange(&direct_sharpen,0,0)||(kind==1&&InterlockedCompareExchange(&gen_sharpen,0,0)))&&direct_ps_sharp?direct_ps_sharp:direct_ps,NULL,0);
   ID3D11DeviceContext_PSSetShaderResources(direct_ctx,0,1,&gv_srv[tex]);ID3D11DeviceContext_PSSetSamplers(direct_ctx,0,1,&direct_sampler);ID3D11DeviceContext_Draw(direct_ctx,3,0);
   gv_draw_label();
   ID3D11ShaderResourceView *none=NULL;ID3D11DeviceContext_PSSetShaderResources(direct_ctx,0,1,&none);
   LONG sync=InterlockedCompareExchange(&direct_sync,0,0);
   HRESULT hr=sync&&direct_tearing?IDXGISwapChain1_Present(direct_chain,0,DXGI_PRESENT_ALLOW_TEARING):IDXGISwapChain1_Present(direct_chain,1,DXGI_PRESENT_DO_NOT_WAIT);
   if(hr==DXGI_ERROR_WAS_STILL_DRAWING){++direct_skips;hr=S_OK;}else ++direct_presents;
   ok=hr>=0;if(!ok)logline("GPU_VIEW present failed hr=%08lx",(unsigned long)hr);}
 }
 ReleaseSRWLockShared(&direct_lock);return ok;
}
static DWORD WINAPI gv_thread_main(void *u){
 (void)u;
 {HMODULE av=LoadLibraryW(L"avrt.dll");HANDLE (WINAPI *set)(const wchar_t*,DWORD*)=NULL;if(av)*(FARPROC*)&set=GetProcAddress(av,"AvSetMmThreadCharacteristicsW");DWORD k=0;if(set)set(L"Games",&k);}
 SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_TIME_CRITICAL);
 LONGLONG lastLog=0;
 while(!gv_stop){
  AcquireSRWLockExclusive(&gv_lock);int have=gv_n>0;GvItem it={0};if(have)it=gv_q[0];ReleaseSRWLockExclusive(&gv_lock);
  if(!have){WaitForSingleObject(gv_wake,50);continue;}
  LARGE_INTEGER now;QueryPerformanceCounter(&now);
  if(it.due>now.QuadPart){gv_wait_until(it.due);continue;} /* re-read: a newer item may have arrived */
  /* due: take the newest item that is due now, drop older due ones */
  AcquireSRWLockExclusive(&gv_lock);QueryPerformanceCounter(&now);int take=0;while(take+1<gv_n&&gv_q[take+1].due<=now.QuadPart)++take;
  for(int k=0;k<take;k++)gv_drop_kind[gv_q[k].kind]++;
  it=gv_q[take];gv_dropped+=take;memmove(gv_q,gv_q+take+1,sizeof(GvItem)*(gv_n-take-1));gv_n-=take+1;ReleaseSRWLockExclusive(&gv_lock);
  if(now.QuadPart-it.due>gv_freq/1000)++gv_late; /* more than 1 ms after due */
  {double wl=1000.0*(now.QuadPart-it.due)/gv_freq;gv_wake_late_sum+=wl;if(wl>gv_wake_late_max)gv_wake_late_max=wl;}
  LARGE_INTEGER p0;QueryPerformanceCounter(&p0);int shown=gv_present(it.tex,it.kind);{LARGE_INTEGER p1;QueryPerformanceCounter(&p1);double pm=1000.0*(p1.QuadPart-p0.QuadPart)/gv_freq;gv_pres_sum+=pm;gv_pres_n++;if(pm>gv_pres_max)gv_pres_max=pm;}
  if(shown){LARGE_INTEGER t;QueryPerformanceCounter(&t);double ms=1000.0*(t.QuadPart-it.arr)/gv_freq;gv_shown[it.kind]++;gv_lat_sum[it.kind]+=ms;if(ms>gv_lat_max[it.kind])gv_lat_max[it.kind]=ms;}
  if(now.QuadPart-lastLog>gv_freq*10){lastLog=now.QuadPart;
   logline("GPU_VIEW shown_orig=%lld shown_gen=%lld arrival_to_present_orig_ms=%.2f (max %.2f) gen_ms=%.2f (max %.2f) late=%lld dropped=%lld presents=%lld skips=%lld",
    gv_shown[0],gv_shown[1],gv_shown[0]?gv_lat_sum[0]/gv_shown[0]:0,gv_lat_max[0],gv_shown[1]?gv_lat_sum[1]/gv_shown[1]:0,gv_lat_max[1],gv_late,gv_dropped,(long long)direct_presents,(long long)direct_skips);
   logline("GPU_VIEW_DIAG dropped_orig=%lld dropped_gen=%lld posted_late_orig=%lld (max %.2f) posted_late_gen=%lld (max %.2f) wake_late_avg=%.3f max=%.2f present_avg=%.3f max=%.2f",gv_drop_kind[0],gv_drop_kind[1],gv_post_late[0],gv_post_late_max[0],gv_post_late[1],gv_post_late_max[1],gv_pres_n?gv_wake_late_sum/gv_pres_n:0,gv_wake_late_max,gv_pres_n?gv_pres_sum/gv_pres_n:0,gv_pres_max);
   gv_wake_late_sum=gv_pres_sum=0;gv_pres_n=0;gv_wake_late_max=gv_pres_max=0;gv_post_late_max[0]=gv_post_late_max[1]=0;
   gv_lat_sum[0]=gv_lat_sum[1]=0;gv_shown[0]=gv_shown[1]=0;gv_lat_max[0]=gv_lat_max[1]=0;}
 }
 return 0;
}
/* Called from the GPU path (host thread) while generating with the low latency view on: open the full screen window. */
static void gv_begin(unsigned w,unsigned h){
 if(!direct_mode||InterlockedCompareExchange(&direct_state,0,0)<0)return;
 if(!gv_thread){LARGE_INTEGER f;QueryPerformanceFrequency(&f);gv_freq=f.QuadPart;gv_wake=CreateEventW(NULL,FALSE,FALSE,NULL);gv_thread=CreateThread(NULL,0,gv_thread_main,NULL,0,NULL);}
 direct_release_hook=gv_direct_release_hook;InterlockedExchange(&direct_gpu_feed,1);
 if(InterlockedCompareExchange(&direct_state,0,0)!=0)return;
 AcquireSRWLockExclusive(&direct_frame_lock);
 if(!direct_frame&&(direct_frame=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(size_t)w*h*4))){direct_w=(int)w;direct_h=(int)h;}
 ReleaseSRWLockExclusive(&direct_frame_lock);
 if(direct_frame&&InterlockedCompareExchange(&direct_state,1,0)==0){HANDLE t=CreateThread(NULL,0,direct_thread,NULL,0,NULL);if(t)CloseHandle(t);else InterlockedExchange(&direct_state,-1);}
}
static void gv_end(void){InterlockedExchange(&direct_gpu_feed,0);AcquireSRWLockExclusive(&gv_lock);gv_n=0;ReleaseSRWLockExclusive(&gv_lock);}
/* fps readout (direct_set_label's alpha bitmap) drawn over the GPU view with alpha blending */
static ID3D11Texture2D *gv_lab_tex;static ID3D11ShaderResourceView *gv_lab_srv;static ID3D11PixelShader *gv_lab_ps;static ID3D11BlendState *gv_lab_blend;
static const uint8_t *gv_lab_ptr;static int gv_lab_tried;
static const char gv_lab_src[]="Texture2D t:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{return float4(1,1,1,t.Sample(s,uv).r);}";
/* 2.3.13: same label in the HDR10 (PQ) back buffer: code 0.58 is about 200 nits instead of 10000 */
static const char gv_lab_src_hdr[]="Texture2D t:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{return float4(0.58,0.58,0.58,t.Sample(s,uv).r);}";
static ID3D11PixelShader *gv_lab_ps_hdr;
static void gv_lab_release(void){if(gv_lab_srv){ID3D11ShaderResourceView_Release(gv_lab_srv);gv_lab_srv=NULL;}if(gv_lab_tex){ID3D11Texture2D_Release(gv_lab_tex);gv_lab_tex=NULL;}
 if(gv_lab_ps){ID3D11PixelShader_Release(gv_lab_ps);gv_lab_ps=NULL;}if(gv_lab_ps_hdr){ID3D11PixelShader_Release(gv_lab_ps_hdr);gv_lab_ps_hdr=NULL;}if(gv_lab_blend){ID3D11BlendState_Release(gv_lab_blend);gv_lab_blend=NULL;}gv_lab_ptr=NULL;gv_lab_tried=0;}
/* 2.3.13 (11-19 user: transparent readout): the label region of the SDR source is read back (small copy), the text is
 * mixed in on the CPU and written into a copy of the source, which then goes through RTX Video HDR. Only copies, no draws
 * (draws and a second video processor next to the TrueHDR Blt removed or hung the device). Recording keeps its originals. */
static ID3D11Texture2D *gv_hs_copy,*gv_hs_stage,*gv_hs_patch;static int gv_hs_w,gv_hs_h;static uint32_t *gv_hs_px;
static void gv_hdr_src_release(void){if(gv_hs_copy){ID3D11Texture2D_Release(gv_hs_copy);gv_hs_copy=NULL;}if(gv_hs_stage){ID3D11Texture2D_Release(gv_hs_stage);gv_hs_stage=NULL;}if(gv_hs_patch){ID3D11Texture2D_Release(gv_hs_patch);gv_hs_patch=NULL;}if(gv_hs_px){HeapFree(GetProcessHeap(),0,gv_hs_px);gv_hs_px=NULL;}gv_hs_w=gv_hs_h=0;}
/* returns the resource to feed the video processor: src, or the labelled copy */
static ID3D11Resource *gv_hdr_src_label(ID3D11Resource *src){
 if(!InterlockedCompareExchange(&direct_label_on,0,0)||direct_w<=0||direct_h<=0)return src;
 AcquireSRWLockShared(&direct_label_lock);int lw=direct_label_w,lh=direct_label_h;const uint8_t *lab=direct_label;
 if(!lab||lw<=0||lh<=0||lw>direct_w||lh>direct_h){ReleaseSRWLockShared(&direct_label_lock);return src;}
 int used=0;for(int y=0;y<lh;y++)for(int x=lw-1;x>=used;x--)if(lab[y*lw+x]){used=x+1;break;}if(used<1){ReleaseSRWLockShared(&direct_label_lock);return src;}
 if(!gv_hs_copy){D3D11_TEXTURE2D_DESC td={.Width=(UINT)direct_w,.Height=(UINT)direct_h,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET};
  if(ID3D11Device_CreateTexture2D(direct_dev,&td,NULL,&gv_hs_copy)<0){gv_hs_copy=NULL;ReleaseSRWLockShared(&direct_label_lock);return src;}}
 if(gv_hs_w!=used||gv_hs_h!=lh){if(gv_hs_stage){ID3D11Texture2D_Release(gv_hs_stage);gv_hs_stage=NULL;}if(gv_hs_patch){ID3D11Texture2D_Release(gv_hs_patch);gv_hs_patch=NULL;}if(gv_hs_px){HeapFree(GetProcessHeap(),0,gv_hs_px);gv_hs_px=NULL;}
  D3D11_TEXTURE2D_DESC sd={.Width=(UINT)used,.Height=(UINT)lh,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_STAGING,.CPUAccessFlags=D3D11_CPU_ACCESS_READ};
  D3D11_TEXTURE2D_DESC pd=sd;pd.Usage=D3D11_USAGE_DEFAULT;pd.CPUAccessFlags=0;pd.BindFlags=D3D11_BIND_SHADER_RESOURCE;
  gv_hs_px=(uint32_t*)HeapAlloc(GetProcessHeap(),0,(size_t)used*lh*4);
  if(!gv_hs_px||ID3D11Device_CreateTexture2D(direct_dev,&sd,NULL,&gv_hs_stage)<0||ID3D11Device_CreateTexture2D(direct_dev,&pd,NULL,&gv_hs_patch)<0){ReleaseSRWLockShared(&direct_label_lock);gv_hdr_src_release();return src;}
  gv_hs_w=used;gv_hs_h=lh;}
 LONG corner=InterlockedCompareExchange(&ui_fps_corner,0,0);int mx=direct_h*16/1080,my=direct_h*12/1080;
 int x0=(corner&1)?direct_w-lw-mx:mx,y0=(corner&2)?direct_h-lh-my:my;if(x0<0)x0=0;if(y0<0)y0=0;if(x0+used>direct_w)x0=direct_w-used;
 ID3D11DeviceContext_CopyResource(direct_ctx,(ID3D11Resource*)gv_hs_copy,src);
 D3D11_BOX box={(UINT)x0,(UINT)y0,0,(UINT)(x0+used),(UINT)(y0+lh),1};ID3D11DeviceContext_CopySubresourceRegion(direct_ctx,(ID3D11Resource*)gv_hs_stage,0,0,0,0,src,0,&box);
 D3D11_MAPPED_SUBRESOURCE m;
 if(ID3D11DeviceContext_Map(direct_ctx,(ID3D11Resource*)gv_hs_stage,0,D3D11_MAP_READ,0,&m)>=0){
  for(int y=0;y<lh;y++){const uint32_t *in=(const uint32_t*)((const uint8_t*)m.pData+(size_t)y*m.RowPitch);for(int x=0;x<used;x++){unsigned a=lab[y*lw+x],p=in[x];
   unsigned b=(p&255)*(255-a)/255+235*a/255,g=((p>>8)&255)*(255-a)/255+235*a/255,r=((p>>16)&255)*(255-a)/255+235*a/255;gv_hs_px[y*used+x]=b|(g<<8)|(r<<16)|0xFF000000u;}}
  ID3D11DeviceContext_Unmap(direct_ctx,(ID3D11Resource*)gv_hs_stage,0);
  ID3D11DeviceContext_UpdateSubresource(direct_ctx,(ID3D11Resource*)gv_hs_patch,0,NULL,gv_hs_px,(UINT)used*4,0);
  ID3D11DeviceContext_CopySubresourceRegion(direct_ctx,(ID3D11Resource*)gv_hs_copy,0,(UINT)x0,(UINT)y0,0,(ID3D11Resource*)gv_hs_patch,0,NULL);}
 ReleaseSRWLockShared(&direct_label_lock);
 return (ID3D11Resource*)gv_hs_copy;
}
static void gv_draw_label(void){ /* window device, direct_lock held shared */
 if(!InterlockedCompareExchange(&direct_label_on,0,0)||direct_w<=0)return;
 if(!gv_lab_ps&&!gv_lab_tried){gv_lab_tried=1;HMODULE fxc=LoadLibraryW(L"d3dcompiler_47.dll");CsCompile compile=NULL;if(fxc)*(FARPROC*)&compile=GetProcAddress(fxc,"D3DCompile");
  ID3DBlob *b=NULL;if(compile&&direct_shader(compile,gv_lab_src,"ps_5_0",&b)){ID3D11Device_CreatePixelShader(direct_dev,ID3D10Blob_GetBufferPointer(b),ID3D10Blob_GetBufferSize(b),NULL,&gv_lab_ps);ID3D10Blob_Release(b);}
  b=NULL;if(compile&&direct_shader(compile,gv_lab_src_hdr,"ps_5_0",&b)){ID3D11Device_CreatePixelShader(direct_dev,ID3D10Blob_GetBufferPointer(b),ID3D10Blob_GetBufferSize(b),NULL,&gv_lab_ps_hdr);ID3D10Blob_Release(b);}
  D3D11_BLEND_DESC bd={0};bd.RenderTarget[0].BlendEnable=TRUE;bd.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bd.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bd.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
  bd.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bd.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bd.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bd.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
  ID3D11Device_CreateBlendState(direct_dev,&bd,&gv_lab_blend);}
 if(!gv_lab_ps||!gv_lab_blend)return;
 AcquireSRWLockShared(&direct_label_lock);int lw=direct_label_w,lh=direct_label_h;
 if(direct_label&&direct_label!=gv_lab_ptr){if(gv_lab_srv){ID3D11ShaderResourceView_Release(gv_lab_srv);gv_lab_srv=NULL;}if(gv_lab_tex){ID3D11Texture2D_Release(gv_lab_tex);gv_lab_tex=NULL;}
  D3D11_TEXTURE2D_DESC td={.Width=(UINT)lw,.Height=(UINT)lh,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_R8_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_IMMUTABLE,.BindFlags=D3D11_BIND_SHADER_RESOURCE};
  D3D11_SUBRESOURCE_DATA sd={direct_label,(UINT)lw,0};if(ID3D11Device_CreateTexture2D(direct_dev,&td,&sd,&gv_lab_tex)>=0)ID3D11Device_CreateShaderResourceView(direct_dev,(ID3D11Resource*)gv_lab_tex,NULL,&gv_lab_srv);
  gv_lab_ptr=direct_label;}
 ReleaseSRWLockShared(&direct_label_lock);
 if(!gv_lab_srv)return;
 LONG corner=InterlockedCompareExchange(&ui_fps_corner,0,0);int mx=direct_h*16/1080,my=direct_h*12/1080;
 int x0=(corner&1)?direct_w-lw-mx:mx,y0=(corner&2)?direct_h-lh-my:my;if(x0<0)x0=0;if(y0<0)y0=0;
 float sx=direct_view.Width/(float)direct_w,sy=direct_view.Height/(float)direct_h;
 D3D11_VIEWPORT v={x0*sx,y0*sy,lw*sx,lh*sy,0,1};float bf[4]={0};
 ID3D11DeviceContext_RSSetViewports(direct_ctx,1,&v);ID3D11DeviceContext_OMSetBlendState(direct_ctx,gv_lab_blend,bf,0xffffffff);
 ID3D11DeviceContext_PSSetShader(direct_ctx,hdr_on&&gv_lab_ps_hdr?gv_lab_ps_hdr:gv_lab_ps,NULL,0);ID3D11DeviceContext_PSSetShaderResources(direct_ctx,0,1,&gv_lab_srv);ID3D11DeviceContext_Draw(direct_ctx,3,0);
 ID3D11DeviceContext_OMSetBlendState(direct_ctx,NULL,bf,0xffffffff);ID3D11DeviceContext_RSSetViewports(direct_ctx,1,&direct_view);
}
