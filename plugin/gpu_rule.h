/* SPDX-License-Identifier: MIT
 * 2.3.3: when the GPU path (gpu_probe.h / gpu_gen.h / gpu_view.h) is used, decided once when the host builds the preview
 * pipeline. Everything else stays on the CPU path (d3d11download -> engine -> d3d11upload), which supports all settings.
 *  GPU path: low latency view + full screen, low latency (not stable), exactly 2x, a model with a GPU
 *  (2.3.11: "30 fps smoothing" no longer forces the CPU path. Since 2.3.10 the GPU path spaces a 30 fps source itself
 *  (mid held half a source interval), and the 11-19 GTA6 run showed the CPU path with smoothing at 58-77% even spacing
 *  against 89.5% for VFG, which was on the GPU path. The setting still applies on the CPU path.)
 *  slot (RIFE, FSR 3, NVIDIA VFG, DLSS FG, AMD FSR FG), no earlier GPU-path failure for that model on this PC, and the
 *  host's display GPU is also the high-performance GPU (hybrid laptops keep the CPU path).
 *  NVIDIA VFG keeps the GPU path whenever it is chosen (its CPU path is too slow for 120), unless it failed before.
 * A failure (setup or repeated generate errors) is saved as gpu_path_failed (bit per model id) and the next start uses the
 * CPU path; the settings window says so. CS_TEST_GPU_VIEW=1 forces, =0 turns off the GPU path (tests). */
#ifndef COBJMACROS
#define COBJMACROS
#endif
#include <dxgi1_6.h>
static volatile LONG gpu_path_active=-1;   /* decided for this run: 1 GPU path, 0 CPU path, -1 not yet */
static volatile LONG gpu_path_failed_now;  /* the GPU path failed during this run */
static int gpu_rule_settings_path(wchar_t *path){
 wchar_t dir[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);
 if(!n||n>32000)return 0;swprintf(path,32768,L"%ls\\settings.ini",dir);return 1;
}
static unsigned gpu_rule_failed_mask(void){wchar_t path[32768];if(!gpu_rule_settings_path(path))return 0;return (unsigned)GetPrivateProfileIntW(L"preview",L"gpu_path_failed",0,path);}
static void gpu_rule_mark_failed(int model){
 wchar_t path[32768],v[16];if(model<0||model>31||!gpu_rule_settings_path(path))return;unsigned m=gpu_rule_failed_mask()|(1u<<model);
 swprintf(v,16,L"%u",m);WritePrivateProfileStringW(L"preview",L"gpu_path_failed",v,path);InterlockedExchange(&gpu_path_failed_now,1);
}
/* Display GPU (DXGI adapter 0, where the host's preview device normally lives) == high-performance GPU (engines). */
static int gpu_rule_single_gpu(void){
 static int cached=-1;if(cached>=0)return cached;cached=1;
 HMODULE dx=LoadLibraryW(L"dxgi.dll");HRESULT (WINAPI *mk)(UINT,REFIID,void**)=NULL;if(dx)*(FARPROC*)&mk=GetProcAddress(dx,"CreateDXGIFactory2");if(!mk)return cached;
 static const IID iid6={0xc1b6694f,0xff09,0x44a9,{0xb0,0x3c,0x77,0x90,0x0a,0x0a,0x1d,0x17}};IDXGIFactory6 *f=NULL;if(mk(0,&iid6,(void**)&f)<0||!f)return cached;
 static const IID iida={0x29038f61,0x3839,0x4626,{0x91,0xfd,0x08,0x68,0x79,0x01,0x1a,0x05}};IDXGIAdapter1 *a0=NULL,*hp=NULL;DXGI_ADAPTER_DESC1 d0={0},dh={0};
 if(f->lpVtbl->EnumAdapters1(f,0,&a0)>=0&&a0)a0->lpVtbl->GetDesc1(a0,&d0);
 if(f->lpVtbl->EnumAdapterByGpuPreference(f,0,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,&iida,(void**)&hp)>=0&&hp)hp->lpVtbl->GetDesc1(hp,&dh);
 if(a0&&hp)cached=!memcmp(&d0.AdapterLuid,&dh.AdapterLuid,sizeof(LUID));
 logline("GPU_RULE display_adapter=%ls performance_adapter=%ls same=%d",d0.Description,dh.Description,cached);
 if(a0)a0->lpVtbl->Release(a0);if(hp)hp->lpVtbl->Release(hp);f->lpVtbl->Release(f);return cached;
}
static int gpu_rule_family_ok(int model){if(!cs_model_valid(model))return 0;int f=cs_models[model].family;return f==0||f==3||f==4||f==5||f==6;}
/* Pure rule (also used by the settings window to tell whether a restart is needed). */
static int gpu_rule(int model,int lowview,int display,int stable,int mode,int mult,int target,int src,int cadence,unsigned failed){
 if(!gpu_rule_family_ok(model)||(model<32&&(failed>>model)&1))return 0;
 if(cs_models[model].family==4)return 1;
 /* 1.1.3: RIFE (family 0) also 3x/4x on the GPU path (time position); FSR 3 / DLSS FG / AMD FG stay 2x here */
 int m=mode?(src>0&&target%src==0?target/src:0):mult;
 int twice=cs_models[model].family==0?(m>=2&&m<=4):m==2;
 (void)cadence;
 return lowview&&display&&!stable&&twice;
}
/* Decision for this run from settings.ini (read at pipeline build). */
static int gpu_rule_now(int model){
 wchar_t v[4];DWORD e=GetEnvironmentVariableW(L"CS_TEST_GPU_VIEW",v,4);if(e==1&&v[0]==L'0')return 0;
 if(!gpu_rule_family_ok(model))return 0;
 if(e==1&&v[0]==L'1')return 1;
 wchar_t path[32768];if(!gpu_rule_settings_path(path))return 0;
 int lowview=GetPrivateProfileIntW(L"preview",L"low_latency_view",0,path)==1,display=GetPrivateProfileIntW(L"preview",L"display_mode",0,path)==1;
 lowview=lowview||display; /* 2.3.15 (11-21 user): full screen always uses the low latency view */
 int stable=GetPrivateProfileIntW(L"preview",L"stable",0,path)==1,mode=GetPrivateProfileIntW(L"preview",L"rate_mode",0,path)==1;
 int mult=GetPrivateProfileIntW(L"preview",L"multiplier",2,path),target=GetPrivateProfileIntW(L"preview",L"target_fps",120,path),cadence=GetPrivateProfileIntW(L"preview",L"cadence30",0,path)!=0;
 int r=gpu_rule(model,lowview,display,stable,mode,mult,target,60,cadence,gpu_rule_failed_mask());
 if(r&&cs_models[model].family!=4&&!gpu_rule_single_gpu())r=0;
 logline("GPU_RULE model=%d lowview=%d display=%d stable=%d mode=%d mult=%d target=%d cadence=%d failed=0x%x -> %s",model,lowview,display,stable,mode,mult,target,cadence,gpu_rule_failed_mask(),r?"GPU path":"CPU path");
 return r;
}
