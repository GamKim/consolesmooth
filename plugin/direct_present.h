/* SPDX-License-Identifier: MIT
 * "표시 방식 · 전체 화면" in the settings window (off by default), or CS_TEST_DIRECT=1 for filming runs.
 * Shows the generated frames in our own borderless window that covers the primary monitor, on our own
 * D3D11 device, so Windows can flip it directly instead of composing it (the host's preview stays
 * Composed: Flip, about 2 refreshes behind). Pacing stays with the large preview sink's phase lock.
 * Esc, or another program becoming active on the same monitor, closes the window and the host's preview carries on
 * (1.8.4, 11-09 user: clicking a window on another monitor closed it; that now keeps it open); turning generation
 * off and on, or Apply, opens it again. 2.2.5 (11-14 user): Esc also turns generation off, so one press of the button or
 * shortcut turns it back on in full screen; losing focus still only closes the window. No signal or a changed frame size closes it until frames return.
 * CS_TEST_DIRECT=1 keeps the test behaviour: closes only on Esc or after 300 s.
 * The fps readout is drawn into the frame: a window above this one would make Windows compose it again.
 * direct_state: 0 idle, 1 starting, 2 ready, -1 failed, -2 closed by the user, -3 waiting for frames.
 */
#define COBJMACROS
#include "ui_lang.h"
#include <d3d11.h>
#include <dxgi1_5.h> /* 2.2.4: IDXGIFactory5 for tearing (variable refresh / sync off) */
#define DIRECT_CLOSE_MESSAGE (WM_APP+1)
static volatile LONG direct_mode;static int direct_forced;
static volatile LONG direct_state;
static SRWLOCK direct_lock=SRWLOCK_INIT,direct_frame_lock=SRWLOCK_INIT,direct_life=SRWLOCK_INIT;
static uint8_t *direct_frame;static int direct_w,direct_h;static LONG64 direct_seq,direct_shown_seq;
static ID3D11Device *direct_dev;static ID3D11DeviceContext *direct_ctx;static IDXGISwapChain1 *direct_chain;
static ID3D11Texture2D *direct_tex;static ID3D11ShaderResourceView *direct_srv;static ID3D11RenderTargetView *direct_rtv;
static ID3D11VertexShader *direct_vs;static ID3D11PixelShader *direct_ps,*direct_ps_sharp;static ID3D11SamplerState *direct_sampler;
static HWND direct_hwnd;static D3D11_VIEWPORT direct_view;static HMONITOR direct_monitor;
#define DIRECT_FOCUS_TIMER 2 /* while inactive: close once the active window is on our monitor */
static LONG64 direct_presents,direct_repeats,direct_skips;static double direct_copy_ms,direct_present_ms;
typedef HRESULT (WINAPI *CsCompile)(const void*,SIZE_T,const char*,const void*,void*,const char*,const char*,UINT,UINT,ID3DBlob**,ID3DBlob**);
static const IID cs_iid_factory1={0x770aae78,0xf26f,0x4dba,{0xa8,0x29,0x25,0x3c,0x83,0xd1,0xb3,0x87}};
static const IID cs_iid_factory2={0x50c83a1c,0xe072,0x4c48,{0x87,0xb0,0x36,0x30,0xfa,0x36,0xa6,0xd0}};
static const IID cs_iid_texture={0x6f15aaf2,0xd208,0x4e89,{0x9a,0xb4,0x48,0x95,0x35,0xd3,0x4f,0x9c}};
static const IID cs_iid_factory5={0x7632e1f5,0xee65,0x4dca,{0x87,0xfd,0x84,0xcd,0x75,0xf8,0x83,0x8d}};
static int direct_tearing; /* this swap chain was made with ALLOW_TEARING */
static const IID cs_iid_device1={0x77db970f,0x6276,0x48ba,{0xba,0x28,0x07,0x01,0x43,0xb4,0x39,0x2c}};
static const char direct_vs_src[]="struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};V main(uint i:SV_VertexID){V o;o.uv=float2((i<<1)&2,i&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}";
/* 2.0.1 (11-11 night): full screen sharpen switch. The 1080p frame is stretched to the monitor (4K here) with a plain
 * bilinear sample. This pass: 5-tap Catmull-Rom upscale (13 reads cost 4.6% skipped presents on a 4K monitor vs
 * 2.3% off) plus a contrast-adaptive sharpen in the spirit of AMD CAS (neighbours one source texel away, weight -0.15 x amplitude). */
static const char direct_ps_sharp_src[]=
 "Texture2D t:register(t0);SamplerState s:register(s0);"
 "float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{uint W,H;t.GetDimensions(W,H);float2 sz=float2(W,H),d=1.0/sz;"
 "float2 q=uv*sz-0.5,f=frac(q),i=floor(q);"
 "float2 w0=f*(-0.5+f*(1.0-0.5*f)),w1=1.0+f*f*(-2.5+1.5*f),w2=f*(0.5+f*(2.0-1.5*f)),w3=f*f*(-0.5+0.5*f),w12=w1+w2;"
 "float2 a=(i-0.5)*d,c=(i+2.5)*d,b=(i+0.5+w2/w12)*d;"
 /* 5-tap Catmull-Rom (corner taps dropped, weights renormalised): centre cross only. */
 "float wc=w12.x*w12.y,wl=w0.x*w12.y,wr=w3.x*w12.y,wt=w12.x*w0.y,wb=w12.x*w3.y;"
 "float3 cc=t.SampleLevel(s,b,0).rgb,l=t.SampleLevel(s,float2(a.x,b.y),0).rgb,r=t.SampleLevel(s,float2(c.x,b.y),0).rgb,u=t.SampleLevel(s,float2(b.x,a.y),0).rgb,dn=t.SampleLevel(s,float2(b.x,c.y),0).rgb;"
 "float3 col=(cc*wc+l*wl+r*wr+u*wt+dn*wb)/(wc+wl+wr+wt+wb);"
 /* adaptive sharpen from the same five reads (neighbours ~1 source texel away). */
 "float3 mn=min(col,min(min(l,r),min(u,dn))),mx=max(col,max(max(l,r),max(u,dn)));"
 "float3 amp=sqrt(saturate(min(mn,1.0-mx)/max(mx,1e-4))),k=-0.15*amp;"
 "return float4(saturate((col+k*(l+r+u+dn))/(1.0+4.0*k)),1);}";
static const char direct_ps_src[]="Texture2D t:register(t0);SamplerState s:register(s0);float4 main(float4 p:SV_Position,float2 uv:TEXCOORD0):SV_Target{return float4(t.Sample(s,uv).rgb,1);}";
/* Readout, white text like the fullscreen readout over the host preview. Alpha only; built on the settings window's thread. */
static SRWLOCK direct_label_lock=SRWLOCK_INIT;static uint8_t *direct_label;static int direct_label_w,direct_label_h;static volatile LONG direct_label_on;
static void direct_set_label(const wchar_t *text,int on){
 static wchar_t last[100];static int last_px;static LONG last_corner=-1;InterlockedExchange(&direct_label_on,on);if(!on)return;
 LONG corner=InterlockedCompareExchange(&ui_fps_corner,0,0);
 int px=direct_h>0?direct_h*22/1080:22;if(px<12)px=12;if(px==last_px&&corner==last_corner&&!wcscmp(last,text))return;
 int w=px*28,h=px*2; /* 2.0.7: 16 -> 28 for the game / new pictures part */uint8_t *next=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,(size_t)w*h);if(!next)return;
 HDC screen=GetDC(NULL),mem=CreateCompatibleDC(screen);BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 uint32_t *bits=NULL;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,(void**)&bits,NULL,0);
 if(bitmap&&bits){HGDIOBJ oldbmp=SelectObject(mem,bitmap);memset(bits,0,(size_t)w*h*4);HFONT font=CreateFontW(-px,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Malgun Gothic");HGDIOBJ oldfont=SelectObject(mem,font);
  SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(255,255,255));RECT r={0,0,w,h};DrawTextW(mem,text,-1,&r,((corner&1)?DT_RIGHT:DT_LEFT)|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
  for(size_t i=0;i<(size_t)w*h;i++){uint32_t c=bits[i],a=c&255;if(((c>>8)&255)>a)a=(c>>8)&255;if(((c>>16)&255)>a)a=(c>>16)&255;next[i]=(uint8_t)a;}
  SelectObject(mem,oldfont);DeleteObject(font);SelectObject(mem,oldbmp);}
 if(bitmap)DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(NULL,screen);
 AcquireSRWLockExclusive(&direct_label_lock);uint8_t *old=direct_label;direct_label=next;direct_label_w=w;direct_label_h=h;ReleaseSRWLockExclusive(&direct_label_lock);if(old)HeapFree(GetProcessHeap(),0,old);
 wcsncpy(last,text,99);last[99]=0;last_px=px;last_corner=corner;
}
static void direct_draw_label(uint8_t *pixels,size_t pitch){
 if(!InterlockedCompareExchange(&direct_label_on,0,0))return;
 AcquireSRWLockShared(&direct_label_lock);
 if(direct_label){LONG corner=InterlockedCompareExchange(&ui_fps_corner,0,0);int mx=direct_h*16/1080,my=direct_h*12/1080,w=direct_label_w,h=direct_label_h;
  int x0=(corner&1)?direct_w-w-mx:mx,y0=(corner&2)?direct_h-h-my:my;if(x0<0)x0=0;if(y0<0)y0=0;if(x0+w>direct_w)w=direct_w-x0;if(y0+h>direct_h)h=direct_h-y0;
  for(int y=0;y<h;y++){const uint8_t *a=direct_label+(size_t)y*direct_label_w;uint8_t *d=pixels+(size_t)(y0+y)*pitch+(size_t)x0*4;
   /* Blend from the cached frame copy: reading back the mapped texture is slow write-combined memory. */
   const uint8_t *from=direct_frame+((size_t)(y0+y)*direct_w+x0)*4;
   for(int x=0;x<w;x++,d+=4,from+=4)if(a[x]){int k=a[x];d[0]=(uint8_t)((from[0]*(255-k)+255*k)/255);d[1]=(uint8_t)((from[1]*(255-k)+255*k)/255);d[2]=(uint8_t)((from[2]*(255-k)+255*k)/255);}}}
 ReleaseSRWLockShared(&direct_label_lock);
}
#define DIRECT_RELEASE(p,release) do{if(p){release(p);p=NULL;}}while(0)
/* Runs on the window's thread when it ends. Holding direct_lock keeps direct_show out; the frame lock keeps direct_tap out. */
static void direct_release(void){
 /* Releasing the device can take a moment; do not leave an empty window over the screen meanwhile. */
 if(direct_hwnd)ShowWindow(direct_hwnd,SW_HIDE);
 AcquireSRWLockExclusive(&direct_lock);
 DIRECT_RELEASE(direct_sampler,ID3D11SamplerState_Release);DIRECT_RELEASE(direct_ps,ID3D11PixelShader_Release);DIRECT_RELEASE(direct_ps_sharp,ID3D11PixelShader_Release);DIRECT_RELEASE(direct_vs,ID3D11VertexShader_Release);
 DIRECT_RELEASE(direct_rtv,ID3D11RenderTargetView_Release);DIRECT_RELEASE(direct_srv,ID3D11ShaderResourceView_Release);DIRECT_RELEASE(direct_tex,ID3D11Texture2D_Release);
 DIRECT_RELEASE(direct_chain,IDXGISwapChain1_Release);DIRECT_RELEASE(direct_ctx,ID3D11DeviceContext_Release);DIRECT_RELEASE(direct_dev,ID3D11Device_Release);
 if(direct_hwnd){DestroyWindow(direct_hwnd);direct_hwnd=NULL;}
 AcquireSRWLockExclusive(&direct_frame_lock);if(direct_frame){HeapFree(GetProcessHeap(),0,direct_frame);direct_frame=NULL;}direct_w=direct_h=0;direct_seq=direct_shown_seq=0;ReleaseSRWLockExclusive(&direct_frame_lock);
 ReleaseSRWLockExclusive(&direct_lock);
}
/* Any thread. The window's thread does the cleanup. */
static void direct_close(LONG reason){
 LONG was=InterlockedExchange(&direct_state,reason);if(was<1)return;
 AcquireSRWLockShared(&direct_lock);if(direct_hwnd)PostMessageW(direct_hwnd,DIRECT_CLOSE_MESSAGE,0,0);ReleaseSRWLockShared(&direct_lock);
}
static LRESULT CALLBACK direct_proc(HWND w,UINT m,WPARAM a,LPARAM b){
 /* Focus moved away: keep the window while the active window is on another monitor (checked now and every 250 ms),
  * close as soon as one on this monitor is active, so a single-monitor user is never left under a topmost window. */
 if(m==WM_ACTIVATEAPP&&!direct_forced){if(a)KillTimer(w,DIRECT_FOCUS_TIMER);else{SetTimer(w,DIRECT_FOCUS_TIMER,250,NULL);PostMessageW(w,WM_TIMER,DIRECT_FOCUS_TIMER,0);}return 0;}
 if(m==WM_TIMER&&a==DIRECT_FOCUS_TIMER){HWND fg=GetForegroundWindow();
  if(fg==w){KillTimer(w,DIRECT_FOCUS_TIMER);return 0;}
  /* The desktop (Progman/WorkerW) spans every monitor and counts as the largest one; use the pointer's monitor for it. */
  HMONITOR at=NULL;wchar_t cls[32]=L"";if(fg)GetClassNameW(fg,cls,32);
  if(fg&&(!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW"))){POINT pt;if(GetCursorPos(&pt))at=MonitorFromPoint(pt,MONITOR_DEFAULTTONULL);}else if(fg)at=MonitorFromWindow(fg,MONITOR_DEFAULTTONULL);
  if(!at||at!=direct_monitor)return 0; /* other monitor (or a transition): stay */
  logline("DIRECT_FOCUS_SAME_MONITOR");}
 int leaving=m==WM_TIMER||m==WM_CLOSE||(m==WM_KEYDOWN&&a==VK_ESCAPE);
 if(leaving&&m==WM_KEYDOWN&&!direct_forced&&ui_panel)PostMessageW(ui_panel,UI_ESC_OFF,0,0); /* 2.2.5: Esc = turn generation off */
 if(leaving){logline("DIRECT_CLOSE message=%u",m);if(InterlockedCompareExchange(&direct_state,0,0)>=0)InterlockedExchange(&direct_state,-2);PostQuitMessage(0);return 0;}
 if(m==DIRECT_CLOSE_MESSAGE){logline("DIRECT_CLOSE state=%ld",direct_state);PostQuitMessage(0);return 0;}
 if(m==WM_SETCURSOR){SetCursor(NULL);return 1;}
 return DefWindowProcW(w,m,a,b);
}
/* The adapter that owns the monitor, so the swap chain is not routed through another GPU. */
static IDXGIAdapter1 *direct_adapter(HMONITOR mon,IDXGIFactory2 *factory){
 IDXGIAdapter1 *a=NULL;for(UINT i=0;IDXGIFactory2_EnumAdapters1(factory,i,&a)==S_OK;i++){
  IDXGIOutput *o=NULL;for(UINT j=0;IDXGIAdapter1_EnumOutputs(a,j,&o)==S_OK;j++){DXGI_OUTPUT_DESC d;int hit=IDXGIOutput_GetDesc(o,&d)==S_OK&&d.Monitor==mon;IDXGIOutput_Release(o);if(hit)return a;}
  IDXGIAdapter1_Release(a);}
 return NULL;
}
static int direct_shader(CsCompile compile,const char *src,const char *target,ID3DBlob **out){
 ID3DBlob *err=NULL;HRESULT hr=compile(src,strlen(src),"cs",NULL,NULL,"main",target,0,0,out,&err);
 if(hr<0)logline("DIRECT_SHADER_FAILED target=%s hr=%08lx %s",target,(unsigned long)hr,err?(char*)ID3D10Blob_GetBufferPointer(err):"");
 if(err)ID3D10Blob_Release(err);return hr>=0;
}
/* 2.2.4: DXGI_FEATURE_PRESENT_ALLOW_TEARING on this PC (needed for variable refresh and sync off in full screen). Cached. */
static int direct_tearing_check(IDXGIFactory2 *factory){
 IDXGIFactory5 *f5=NULL;BOOL allow=FALSE;
 if(IDXGIFactory2_QueryInterface(factory,&cs_iid_factory5,(void**)&f5)>=0){if(IDXGIFactory5_CheckFeatureSupport(f5,DXGI_FEATURE_PRESENT_ALLOW_TEARING,&allow,sizeof allow)<0)allow=FALSE;IDXGIFactory5_Release(f5);}
 return allow?1:0;
}
static int direct_tearing_supported(void){
 static volatile LONG cached=-1;LONG c=InterlockedCompareExchange(&cached,0,0);if(c>=0)return (int)c;
 HMODULE dxgi=LoadLibraryW(L"dxgi.dll");HRESULT (WINAPI *make_factory)(REFIID,void**)=NULL;if(dxgi)*(FARPROC*)&make_factory=GetProcAddress(dxgi,"CreateDXGIFactory1");
 IDXGIFactory2 *factory=NULL;int ok=0;if(make_factory&&make_factory(&cs_iid_factory2,(void**)&factory)>=0){ok=direct_tearing_check(factory);IDXGIFactory2_Release(factory);}
 InterlockedExchange(&cached,ok);return ok;
}
static int direct_win_w;
static int direct_create(void){
 HMODULE dxgi=LoadLibraryW(L"dxgi.dll"),d3d=LoadLibraryW(L"d3d11.dll"),fxc=LoadLibraryW(L"d3dcompiler_47.dll");if(!dxgi||!d3d||!fxc){logline("DIRECT_LOAD_FAILED");return 0;}
 HRESULT (WINAPI *make_factory)(REFIID,void**);HRESULT (WINAPI *make_device)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);CsCompile compile;
 *(FARPROC*)&make_factory=GetProcAddress(dxgi,"CreateDXGIFactory1");*(FARPROC*)&make_device=GetProcAddress(d3d,"D3D11CreateDevice");*(FARPROC*)&compile=GetProcAddress(fxc,"D3DCompile");if(!make_factory||!make_device||!compile)return 0;
 /* The host's awareness must not scale our window, or Windows would compose it. */
 HMODULE user=GetModuleHandleW(L"user32.dll");void *(WINAPI *set_dpi)(void*);*(FARPROC*)&set_dpi=user?GetProcAddress(user,"SetThreadDpiAwarenessContext"):NULL;if(set_dpi)set_dpi((void*)(intptr_t)-4);
 POINT origin={0,0};HMONITOR mon=MonitorFromPoint(origin,MONITOR_DEFAULTTOPRIMARY);direct_monitor=mon;MONITORINFO info={.cbSize=sizeof info};if(!GetMonitorInfoW(mon,&info))return 0;
 int sw=info.rcMonitor.right-info.rcMonitor.left,sh=info.rcMonitor.bottom-info.rcMonitor.top,wx=info.rcMonitor.left,wy=info.rcMonitor.top;
 /* CS_TEST_DIRECT_WIN=<width>: a movable-size 16:9 window instead of the whole monitor, to see whether it can still avoid composition. */
 if(direct_win_w>0){sw=direct_win_w;sh=sw*9/16;wx+=200;wy+=200;}
 IDXGIFactory2 *factory=NULL;(void)cs_iid_factory1;if(make_factory(&cs_iid_factory2,(void**)&factory)<0)return 0;
 IDXGIAdapter1 *adapter=direct_adapter(mon,factory);if(!adapter){logline("DIRECT_NO_ADAPTER");return 0;}
 HRESULT hr=make_device((IDXGIAdapter*)adapter,D3D_DRIVER_TYPE_UNKNOWN,NULL,0,NULL,0,D3D11_SDK_VERSION,&direct_dev,NULL,&direct_ctx);if(hr<0){logline("DIRECT_DEVICE_FAILED hr=%08lx",(unsigned long)hr);return 0;}
 IDXGIDevice1 *dd=NULL;if(ID3D11Device_QueryInterface(direct_dev,&cs_iid_device1,(void**)&dd)>=0){IDXGIDevice1_SetMaximumFrameLatency(dd,1);IDXGIDevice1_Release(dd);}
 WNDCLASSW wc={.lpfnWndProc=direct_proc,.hInstance=GetModuleHandleW(NULL),.lpszClassName=L"ConsoleSmoothDirectTest"};RegisterClassW(&wc);
 direct_hwnd=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"ConsoleSmooth full screen",WS_POPUP,wx,wy,sw,sh,NULL,NULL,wc.hInstance,NULL);if(!direct_hwnd){logline("DIRECT_WINDOW_FAILED %lu",GetLastError());return 0;}
 direct_tearing=direct_tearing_check(factory);
 DXGI_SWAP_CHAIN_DESC1 desc={.Width=(UINT)sw,.Height=(UINT)sh,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT,.BufferCount=3,.Scaling=DXGI_SCALING_NONE,.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD,.AlphaMode=DXGI_ALPHA_MODE_IGNORE,.Flags=direct_tearing?DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING:0};
 hr=IDXGIFactory2_CreateSwapChainForHwnd(factory,(IUnknown*)direct_dev,direct_hwnd,&desc,NULL,NULL,&direct_chain);if(hr<0){logline("DIRECT_CHAIN_FAILED hr=%08lx",(unsigned long)hr);return 0;}
 ID3D11Texture2D *back=NULL;if(IDXGISwapChain1_GetBuffer(direct_chain,0,&cs_iid_texture,(void**)&back)<0)return 0;
 hr=ID3D11Device_CreateRenderTargetView(direct_dev,(ID3D11Resource*)back,NULL,&direct_rtv);ID3D11Texture2D_Release(back);if(hr<0)return 0;
 D3D11_TEXTURE2D_DESC td={.Width=(UINT)direct_w,.Height=(UINT)direct_h,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_DYNAMIC,.BindFlags=D3D11_BIND_SHADER_RESOURCE,.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE};
 if(ID3D11Device_CreateTexture2D(direct_dev,&td,NULL,&direct_tex)<0||ID3D11Device_CreateShaderResourceView(direct_dev,(ID3D11Resource*)direct_tex,NULL,&direct_srv)<0)return 0;
 ID3DBlob *vb=NULL,*pb=NULL;if(!direct_shader(compile,direct_vs_src,"vs_5_0",&vb)||!direct_shader(compile,direct_ps_src,"ps_5_0",&pb))return 0;
 hr=ID3D11Device_CreateVertexShader(direct_dev,ID3D10Blob_GetBufferPointer(vb),ID3D10Blob_GetBufferSize(vb),NULL,&direct_vs);if(hr>=0)hr=ID3D11Device_CreatePixelShader(direct_dev,ID3D10Blob_GetBufferPointer(pb),ID3D10Blob_GetBufferSize(pb),NULL,&direct_ps);if(hr<0)return 0;
 {ID3DBlob *sb=NULL;if(direct_shader(compile,direct_ps_sharp_src,"ps_5_0",&sb)){if(ID3D11Device_CreatePixelShader(direct_dev,ID3D10Blob_GetBufferPointer(sb),ID3D10Blob_GetBufferSize(sb),NULL,&direct_ps_sharp)<0)direct_ps_sharp=NULL;}logline("DIRECT_SHARPEN_SHADER ok=%d",direct_ps_sharp!=NULL);}
 D3D11_SAMPLER_DESC sd={.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR,.AddressU=D3D11_TEXTURE_ADDRESS_CLAMP,.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP,.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP,.MaxLOD=D3D11_FLOAT32_MAX};if(ID3D11Device_CreateSamplerState(direct_dev,&sd,&direct_sampler)<0)return 0;
 direct_view=(D3D11_VIEWPORT){0,0,(float)sw,(float)sh,0,1};
 ShowWindow(direct_hwnd,SW_SHOW);SetForegroundWindow(direct_hwnd);if(direct_forced)SetTimer(direct_hwnd,1,300000,NULL);
 logline("DIRECT_READY monitor=%dx%d frame=%dx%d tearing=%d sync=%ld",sw,sh,direct_w,direct_h,direct_tearing,InterlockedCompareExchange(&direct_sync,0,0));return 1;
}
static DWORD WINAPI direct_thread(void *unused){
 (void)unused;AcquireSRWLockExclusive(&direct_life); /* A new window waits for the previous one's cleanup. */
 if(!direct_create()){logline("DIRECT_CREATE_FAILED");InterlockedExchange(&direct_state,-1);}
 else if(InterlockedCompareExchange(&direct_state,2,1)==1){MSG m;while(GetMessageW(&m,NULL,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}}
 direct_release();ReleaseSRWLockExclusive(&direct_life);return 0;
}
/* Generated frame, leaving the engine in system memory. */
static void direct_tap(void *buffer,int w,int h){
 if(!direct_mode||!buffer||w<=0||h<=0||InterlockedCompareExchange(&direct_state,0,0)<0)return;
 size_t bytes=(size_t)w*h*4;CsMap map={0};if(!buffer_map(buffer,&map,1))return;int resized=0;
 if(map.size==bytes){AcquireSRWLockExclusive(&direct_frame_lock);
  if(!direct_frame&&(direct_frame=HeapAlloc(GetProcessHeap(),0,bytes))){direct_w=w;direct_h=h;}
  if(direct_frame&&direct_w==w&&direct_h==h){memcpy(direct_frame,map.data,bytes);++direct_seq;}else if(direct_frame)resized=1;
  ReleaseSRWLockExclusive(&direct_frame_lock);}
 buffer_unmap(buffer,&map);if(resized){logline("DIRECT_FRAME_SIZE_CHANGED width=%d height=%d",w,h);direct_close(-3);return;}
 if(direct_frame&&InterlockedCompareExchange(&direct_state,1,0)==0){HANDLE t=CreateThread(NULL,0,direct_thread,NULL,0,NULL);if(t)CloseHandle(t);else InterlockedExchange(&direct_state,-1);}
}
/* Called by the large preview sink's probe when its frame is due. 1: shown here, the host sink skips it. */
static int direct_show(void){
 if(!direct_mode||InterlockedCompareExchange(&direct_state,0,0)!=2)return 0;
 AcquireSRWLockShared(&direct_lock);if(InterlockedCompareExchange(&direct_state,0,0)!=2){ReleaseSRWLockShared(&direct_lock);return 0;}
 LARGE_INTEGER a,b,c,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&a);D3D11_MAPPED_SUBRESOURCE mapped;HRESULT hr;
 AcquireSRWLockShared(&direct_frame_lock);
 if(direct_seq==direct_shown_seq)++direct_repeats;direct_shown_seq=direct_seq;
 hr=ID3D11DeviceContext_Map(direct_ctx,(ID3D11Resource*)direct_tex,0,D3D11_MAP_WRITE_DISCARD,0,&mapped);
 if(hr>=0){for(int y=0;y<direct_h;y++)memcpy((uint8_t*)mapped.pData+y*(size_t)mapped.RowPitch,direct_frame+y*(size_t)direct_w*4,(size_t)direct_w*4);direct_draw_label(mapped.pData,mapped.RowPitch);ID3D11DeviceContext_Unmap(direct_ctx,(ID3D11Resource*)direct_tex,0);}
 ReleaseSRWLockShared(&direct_frame_lock);
 if(hr>=0){
  ID3D11DeviceContext_OMSetRenderTargets(direct_ctx,1,&direct_rtv,NULL);ID3D11DeviceContext_RSSetViewports(direct_ctx,1,&direct_view);
  ID3D11DeviceContext_IASetPrimitiveTopology(direct_ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);ID3D11DeviceContext_VSSetShader(direct_ctx,direct_vs,NULL,0);ID3D11DeviceContext_PSSetShader(direct_ctx,InterlockedCompareExchange(&direct_sharpen,0,0)&&direct_ps_sharp?direct_ps_sharp:direct_ps,NULL,0);
  ID3D11DeviceContext_PSSetShaderResources(direct_ctx,0,1,&direct_srv);ID3D11DeviceContext_PSSetSamplers(direct_ctx,0,1,&direct_sampler);ID3D11DeviceContext_Draw(direct_ctx,3,0);
  /* A blocking Present keeps one frame queued for good (measured: 1.45 refreshes instead of 0.5).
   * If the previous flip is still pending, skip this frame once so the queue empties. */
  /* 2.2.4: variable refresh / sync off present at once with tearing allowed: a G-Sync/FreeSync monitor then refreshes when the frame arrives. */
  LONG sync=InterlockedCompareExchange(&direct_sync,0,0);
  QueryPerformanceCounter(&b);hr=sync&&direct_tearing?IDXGISwapChain1_Present(direct_chain,0,DXGI_PRESENT_ALLOW_TEARING):IDXGISwapChain1_Present(direct_chain,1,DXGI_PRESENT_DO_NOT_WAIT);QueryPerformanceCounter(&c);
  if(hr==DXGI_ERROR_WAS_STILL_DRAWING){++direct_skips;hr=S_OK;}
  direct_copy_ms+=1000.0*(b.QuadPart-a.QuadPart)/f.QuadPart;direct_present_ms+=1000.0*(c.QuadPart-b.QuadPart)/f.QuadPart;
  if(++direct_presents%600==0){logline("DIRECT_STATS presents=%lld repeats=%lld skips=%lld draw_avg_ms=%.2f present_avg_ms=%.2f",(long long)direct_presents,(long long)direct_repeats,(long long)direct_skips,direct_copy_ms/600,direct_present_ms/600);direct_copy_ms=direct_present_ms=0;}
 }
 ReleaseSRWLockShared(&direct_lock);
 if(hr<0){logline("DIRECT_PRESENT_FAILED hr=%08lx",(unsigned long)hr);direct_close(-1);return 0;}
 return 1;
}
static LONG direct_status(void){return InterlockedCompareExchange(&direct_state,0,0);}
/* Frames that actually reached the swap chain per second, for the readout. The host's meter does not see them. */
static double direct_rate(void){
 static ULONGLONG since;static LONG64 count;static double rate;ULONGLONG now=GetTickCount64();LONG64 shown=direct_presents-direct_skips;
 if(!since||now-since>3000){since=now;count=shown;rate=0;}else if(now-since>=1000){rate=(double)(shown-count)*1000.0/(double)(now-since);since=now;count=shown;}
 return rate;
}
/* Settings window, every refresh. allowed: the option is on and the monitor can show it evenly. reopen: Apply was pressed. */
static void direct_ui_sync(int allowed,int reopen){
 if(direct_forced)return;
 InterlockedExchange(&direct_mode,allowed);
 LONG state=direct_status();if(reopen&&state<0){InterlockedExchange(&direct_state,-3);state=-3;}
 if(!allowed||(!InterlockedCompareExchange(&frame_generating,0,0)&&!InterlockedCompareExchange(&low_view,0,0))){
  if(state>=1)direct_close(-3);else if(state<0)InterlockedExchange(&direct_state,-3); /* Off and on again gives another try. */
  return;
 }
 if(state==-3)InterlockedCompareExchange(&direct_state,0,-3);
}
