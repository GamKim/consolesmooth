/* SPDX-License-Identifier: MIT
 * In-process, host-owned settings. Rate changes are persisted and applied live;
 * source timing is read from the capture device; output preferences persist.
 */
#include "ui_lang.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <math.h>
#include "../engine/rate_math.h"
#include "../engine/frame_limits.h"
#include "../engine/model_catalog.h"
#include "gpu_rule.h" /* 2.3.3 */
static volatile LONG control_bits;
static volatile LONG toggle_serial;
static volatile LONG64 toggle_qpc;
static int interactive_ui;
static SRWLOCK status_lock=SRWLOCK_INIT;
static CsStats ui_stats;
static int ui_width,ui_height,ui_error;
static double ui_live_buffer_ms;
static void ui_publish_buffer(double ms){AcquireSRWLockExclusive(&status_lock);ui_live_buffer_ms=ms;ReleaseSRWLockExclusive(&status_lock);}
static ULONGLONG ui_stats_updated;
static CsCountWindow ui_counts;
static LONG ui_meter_mode=-1;
#define UI_ESC_OFF (WM_APP+8)
static HWND ui_panel,ui_popup,ui_parent,ui_toggle,ui_settings,ui_tip,ui_hover;
static HWND ui_meter,ui_full_meter;
static wchar_t ui_full_text[100];
static HFONT ui_meter_font;static int ui_meter_px;
static int ui_full_dirty;static RECT ui_full_rect;
static int ui_show_fps=1,ui_reconcile_target,ui_fps_game; /* 1.0.1 (11-13 user): readout = input / shown by default, game / shown when ui_fps_game */
/* 2.0.1: full screen sharpen (bicubic upscale + adaptive sharpen in direct_present.h). Default off. */
static int ui_sharpen;static volatile LONG direct_sharpen;
/* 2.3.12 internal test (11-19 GTA6 home): gen_share = % of a source interval the mid frame stays on screen in the
 * low-latency view (50 = even, 25 = default); gen_sharpen = sharpen mid frames only (they are ~half as sharp). */
static volatile LONG gen_share_pct=25,gen_sharpen;static int ui_genshort=1;
static volatile LONG gen_hdr;static int ui_hdr; /* 2.3.13: RTX Video HDR in the full screen window (rtx_hdr.h) */
/* 2.2.4 internal (11-14 user): display sync, full screen and window. 0 vertical sync (refresh hold, as before), 1 variable refresh
 * (present on arrival; full screen with tearing allowed so G-Sync/FreeSync can follow), 2 off (present on arrival, full screen may tear). */
static int ui_sync;static volatile LONG direct_sync;static int direct_tearing_supported(void);
/* 2.2.4 (11-14, friend's RX 9070 XT test: turning it on and off in full screen is awkward): a global shortcut toggles frame
 * generation like the toolbar button. Default Ctrl+Alt+F (no Windows default, not an NVIDIA/AMD overlay key); the Advanced tab
 * records another combination (Ctrl or Alt required; Backspace = no shortcut). Saved as hotkey_mods (MOD_* bits) and hotkey_vk. */
static int ui_hot_mods=MOD_CONTROL|MOD_ALT,ui_hot_vk='F',ui_hot_capture,ui_hot_ok=-1;
/* 2.2.0 beta (11-13 user): "저지연 보기". Our full screen window shows the picture as it arrives, also with generation off,
 * without the preview refresh hold or output spacing; the record/stream route gets no frames (recording and streaming off). */
static int ui_lowview;static volatile LONG low_view;static LONG test_lowview; /* 11-18 test only: CS_TEST_LOWVIEW=1 keeps the low latency view on (filming/timeline runs) */
static int ui_record,ui_saved_record,ui_buffer,ui_saved_buffer,ui_model,ui_saved_model;
static int ui_low_buffer=15,ui_saved_low_buffer=15,ui_low_eligible=1;
/* Display: 0 the host's window (composed, stays usable), 1 our direct full-screen window (direct_present.h). */
static int ui_display,ui_saved_display,ui_direct_reopen;
static int ui_apply_pending,ui_act_display,ui_act_lowview; /* 2.2.4: see ui_toggle_generation */
static void direct_ui_sync(int allowed,int reopen);
static LONG direct_status(void);
static void direct_set_label(const wchar_t *text,int on);
static double direct_rate(void);
/* Bits 8-15 hold the manual stable buffer or, in low latency, a shorter buffer choice; 0 = plan buffer. */
static int ui_buffer_bits(int stable,int manual,int low){return stable?manual:low==25?0:low;}
static HWND ui_items[200];
/* 1.9.8 (11-10 user): the settings are split into tabs. 0 frame generation (what is changed most: on/off, model, rate,
 * latency, display, recommended settings), 1 FPS display and recording, 2 advanced (engine switches, diagnostics). */
static int ui_tab;
static int ui_tab_of(int id){
 static const short t0[]={123,111,122,128,106,130,107,132,133,129,134,135,136,138,139,142,125,126,124,140,152,158,159,127,180,181,182,153,154,155,156},
  t1[]={145,165,144,146,183,188,189,148,149,150,151,178,179,185,186,187,190,191,192,193,194,198,199},t2[]={160,161,162,163,164,166,167,168,169,170,171,172,157,184,195,196,197,113,114,115};
 for(unsigned i=0;i<sizeof t0/sizeof t0[0];i++)if(t0[i]==id)return 0;
 for(unsigned i=0;i<sizeof t1/sizeof t1[0];i++)if(t1[i]==id)return 1;
 for(unsigned i=0;i<sizeof t2/sizeof t2[0];i++)if(t2[i]==id)return 2;
 return -1;
}
/* 1.6.9 engine switches: bit 0 30 fps cadence, bit 1 HUD keep. Saved as cadence30 / hud_keep, default on.
 * 1.7.8: bit 2 GPU clock keep (gpu_keep, default off); a change recreates the engine. Bit 3 (RIFE fp16 arithmetic) has no
 * switch: 11-08 it changed neither speed nor quality on the RTX 5080 (9.96 vs 9.87 ms) or the RDNA2 iGPU (70.4 vs 71.8 ms).
 * 1.8.1: bit 4 FSR 3 acceleration (fsr_fast, default on; recreates the engine): depth/game-vector passes at half size and
 * Prepare only after a reset. Same output (hash) on the RTX 5080 and the RDNA2 iGPU; iGPU 30.1 -> 23.2 ms. */
/* 1.8.7 (user 11-09): all four engine switches default off. */
static volatile LONG engine_options=0;static int ui_cadence=0,ui_hudkeep=0,ui_gpukeep=0,ui_fsrfast=0,ui_latency=0; /* 2.0.2 ui_latency: 지연 우선 (quarter-size flow) */
#define UI_ENGINE_BITS() ((ui_cadence?1:0)|(ui_hudkeep?2:0)|(ui_gpukeep?4:0)|(ui_fsrfast?16:0)|(ui_latency?32:0))
/* 1.7.1: corner of the fps readout over the fullscreen preview and the direct window: 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right. */
static volatile LONG ui_fps_corner=1;
/* 1.7.2: own tooltip for the two toolbar buttons. The common-control tooltip measured its box too small for the
 * font and opened over the preview with the text cut off; this one is measured and placed under the button. */
static HWND ui_tipwin,ui_tip_for;static ULONGLONG ui_tip_since;static int ui_tip_bar_h;static HFONT ui_tip_font;static int ui_tip_px;
static const wchar_t *ui_corner_name(LONG c){return c==1?ui_tr(L"오른쪽 위",L"Top right"):c==2?ui_tr(L"왼쪽 아래",L"Bottom left"):c==3?ui_tr(L"오른쪽 아래",L"Bottom right"):ui_tr(L"왼쪽 위",L"Top left");}
static HFONT ui_font,ui_small,ui_heading,ui_latin;
static HBRUSH ui_brush,ui_head_brush,ui_field_brush,ui_main_brush,ui_nav_brush;
static int ui_expanded,ui_dpi,ui_src=60,ui_mode,ui_mult=2,ui_target=120,ui_stable;
static int ui_saved_src=60,ui_saved_out=120,ui_saved_stable,ui_saved_mode,ui_saved_mult=2,ui_saved_target=120;
static wchar_t ui_font_path[32768];
#define UI_BG RGB(45,45,45)
#define UI_TOOLBAR RGB(31,31,31)
#define UI_TOOL_HOVER RGB(64,64,64)
#define UI_BUTTON RGB(61,61,61)
#define UI_HOVER RGB(76,76,76)
#define UI_TEXT RGB(232,232,232)
#define UI_MUTED RGB(174,174,174)
#define UI_LINE RGB(126,126,126)
#define UI_RED RGB(230,0,45)
static LONG ui_requested(void){return InterlockedCompareExchange(&control_bits,0,0);}
static int ui_output(void){return ui_mode?ui_target:ui_src*ui_mult;}
#include <shellapi.h> /* 2.3.3: NVIDIA VFG download offer in every build */
/* 2.2.8 INTERNAL: NVIDIA VFG runtime = %LOCALAPPDATA%\ConsoleSmooth\nvidia-vfg (one-click download) or a pip install. */
static int ui_vfg_runtime_ok(void){wchar_t p[MAX_PATH];DWORD n=GetEnvironmentVariableW(L"CS_VFG_DIR",p,MAX_PATH);
 if(n&&n<MAX_PATH-40){wcscat(p,L"\\NVVideoEffects.dll");if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)return 1;}
 n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,MAX_PATH);if(n&&n<MAX_PATH-60){wcscat(p,L"\\ConsoleSmooth\\nvidia-vfg\\NVVideoEffects.dll");if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)return 1;}
 n=GetEnvironmentVariableW(L"APPDATA",p,MAX_PATH);if(n&&n<MAX_PATH-80){wchar_t q[MAX_PATH];WIN32_FIND_DATAW fd;swprintf(q,MAX_PATH,L"%ls\\Python\\Python3*",p);HANDLE h=FindFirstFileW(q,&fd);int ok=0;
  if(h!=INVALID_HANDLE_VALUE){do{swprintf(q,MAX_PATH,L"%ls\\Python\\%ls\\site-packages\\nvvfx\\libs\\NVVideoEffects.dll",p,fd.cFileName);if(GetFileAttributesW(q)!=INVALID_FILE_ATTRIBUTES)ok=1;}while(!ok&&FindNextFileW(h,&fd));FindClose(h);}
  if(ok)return 1;}
 return 0;}
static void ui_vfg_offer_download(HWND owner){
 if(ui_vfg_runtime_ok())return;
 int r=MessageBoxW(owner,ui_tr(L"NVIDIA VFG 런타임이 이 PC에 없어요.\r\n\r\nNVIDIA 공식 서버(pypi.nvidia.com)에서 nvidia-vfx 0.2.0.0을 받아 필요한 파일 5개(약 210 MB)만 설치할까요?\r\n받는 양은 약 416 MB이고, NVIDIA 서명을 확인한 뒤 %LOCALAPPDATA%\\ConsoleSmooth\\nvidia-vfg 에 넣어요.\r\n\r\nNVIDIA 라이선스가 적용돼요. '아니요'를 누르면 라이선스 페이지만 열어요.\r\n(내부 시험판 전용 기능)",
  L"The NVIDIA VFG runtime is not on this PC.\r\n\r\nDownload nvidia-vfx 0.2.0.0 from NVIDIA (pypi.nvidia.com) and install only the 5 runtime files (~210 MB)?\r\nThe download is ~416 MB; files are checked for NVIDIA signatures and go to %LOCALAPPDATA%\\ConsoleSmooth\\nvidia-vfg.\r\n\r\nNVIDIA license terms apply. 'No' opens the license page only.\r\n(internal test build only)"),
  L"ConsoleSmooth · NVIDIA VFG",MB_YESNOCANCEL|MB_ICONINFORMATION);
 if(r==IDNO){ShellExecuteW(owner,L"open",L"https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-software-license-agreement/",NULL,NULL,SW_SHOWNORMAL);return;}
 if(r!=IDYES)return;
 wchar_t args[33000];swprintf(args,33000,L"-NoProfile -ExecutionPolicy Bypass -File \"%ls\\engines\\nvvfg\\Get-NvidiaVfg.ps1\"",engine_dir);
 HINSTANCE h=ShellExecuteW(owner,L"open",L"powershell.exe",args,NULL,SW_SHOWNORMAL);
 logline("UI_VFG_DOWNLOAD started=%d",(INT_PTR)h>32);}
/* 2.2.8 INTERNAL: DLSS FG runtime = copy of the NVIDIA app's own DLSS cache file (nothing downloaded or shipped). */
#ifdef CS_INTERNAL_DLSSG /* 2.3.4: every build */
static void ui_dlssg_offer(HWND owner){
 {wchar_t b[32768];swprintf(b,32768,L"%ls\\engines\\dlssg\\nvngx_dlssg.dll",engine_dir);if(GetFileAttributesW(b)!=INVALID_FILE_ATTRIBUTES)return;} /* 2.3.4: bundled */
 wchar_t p[MAX_PATH];DWORD n=GetEnvironmentVariableW(L"CS_DLSSG_DIR",p,MAX_PATH);
 if(n&&n<MAX_PATH-40){wcscat(p,L"\\nvngx_dlssg.dll");if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)return;}
 n=GetEnvironmentVariableW(L"LOCALAPPDATA",p,MAX_PATH);if(n&&n<MAX_PATH-60){wcscat(p,L"\\ConsoleSmooth\\nvidia-dlssg\\nvngx_dlssg.dll");if(GetFileAttributesW(p)!=INVALID_FILE_ATTRIBUTES)return;}
 int r=MessageBoxW(owner,ui_tr(L"DLSS 프레임 생성 파일(nvngx_dlssg.dll)이 준비되지 않았어요.\r\n\r\n이 PC의 NVIDIA 앱이 받아 둔 DLSS 파일(C:\\ProgramData\\NVIDIA\\NGX)에서 가장 새 버전을 찾아, NVIDIA 서명을 확인한 뒤 %LOCALAPPDATA%\\ConsoleSmooth\\nvidia-dlssg 에 복사할까요?\r\n인터넷에서 받지 않아요. RTX 40/50에서만 동작해요.\r\n(내부 시험판 전용 기능)",
  L"The DLSS Frame Generation file (nvngx_dlssg.dll) is not set up.\r\n\r\nCopy the newest DLSS file the NVIDIA app keeps on this PC (C:\\ProgramData\\NVIDIA\\NGX), after an NVIDIA signature check, to %LOCALAPPDATA%\\ConsoleSmooth\\nvidia-dlssg?\r\nNothing is downloaded. RTX 40/50 only.\r\n(internal test build only)"),L"ConsoleSmooth · DLSS FG",MB_YESNO|MB_ICONINFORMATION);
 if(r!=IDYES)return;
 wchar_t args[33000];swprintf(args,33000,L"-NoProfile -ExecutionPolicy Bypass -File \"%ls\\engines\\dlssg\\Get-NvidiaDlssg.ps1\"",engine_dir);
 HINSTANCE h=ShellExecuteW(owner,L"open",L"powershell.exe",args,NULL,SW_SHOWNORMAL);
 logline("UI_DLSSG_COPY started=%d",(INT_PTR)h>32);}
#endif
static int ui_model_choices(void){int n=0;for(int i=0;i<CS_MODEL_COUNT;i++)n+=cs_model_valid(i);return n;}
/* 1.9.5: default of "GPU clock keep" when nothing is saved yet: on when the primary display adapter is AMD. Office RX 6600,
 * Mario Kart 60->120 FSR 3, 15 ms: keep off 89% generated (360 repeats/min), keep on 99.3% (6). NVIDIA stays off (5080: +7% GPU time). */
static int ui_primary_is_amd(void){DISPLAY_DEVICEW d;d.cb=sizeof d;
 for(DWORD i=0;EnumDisplayDevicesW(NULL,i,&d,0);i++,d.cb=sizeof d)if(d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)return wcsstr(d.DeviceID,L"VEN_1002")!=NULL;
 return 0;}
static int ui_preferences(int save){
 if(!interactive_ui)return 0;
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return 0;
 swprintf(path,32768,L"%ls\\settings.ini",dir);
 if(save){
  if(!cs_model_valid(ui_model)||!cs_model_rate_supported(ui_model,ui_src,1,ui_output(),1))return 0;
  const wchar_t* keys[]={L"rate_mode",L"multiplier",L"target_fps",L"stable",L"record_generated",L"buffer_ms",L"model",L"low_buffer_ms",L"display_mode"};
  int values[]={ui_mode,ui_mult,ui_target,ui_stable,ui_record,ui_buffer,ui_model,ui_low_buffer,ui_display};
  for(int i=0;i<9;i++){wchar_t value[20];swprintf(value,20,L"%d",values[i]);if(!WritePrivateProfileStringW(L"preview",keys[i],value,path)){ui_error=-2;return 0;}}
 }else{
  ui_reconcile_target=1;ui_src=60; /* Source rate is observed from capture; old manual values are ignored. */
  ui_mode=GetPrivateProfileIntW(L"preview",L"rate_mode",0,path)==1;
  ui_mult=GetPrivateProfileIntW(L"preview",L"multiplier",2,path);if(ui_mult<2)ui_mult=2;if(ui_mult>4)ui_mult=4;
  ui_target=GetPrivateProfileIntW(L"preview",L"target_fps",120,path);if(ui_target<30)ui_target=120;if(ui_target>240)ui_target=240;
  ui_buffer=GetPrivateProfileIntW(L"preview",L"buffer_ms",0,path);if(ui_buffer!=50&&ui_buffer!=100&&ui_buffer!=150)ui_buffer=0;
  ui_low_buffer=GetPrivateProfileIntW(L"preview",L"low_buffer_ms",15,path);if(ui_low_buffer<15||ui_low_buffer>25)ui_low_buffer=15;
  ui_model=GetPrivateProfileIntW(L"preview",L"model",0,path);if(!cs_model_valid(ui_model))ui_model=0;
  ui_stable=GetPrivateProfileIntW(L"preview",L"stable",0,path)==1;
  ui_display=GetPrivateProfileIntW(L"preview",L"display_mode",0,path)==1;
  ui_show_fps=GetPrivateProfileIntW(L"preview",L"show_fps",1,path)==1;ui_fps_game=GetPrivateProfileIntW(L"preview",L"fps_game",0,path)==1;
  ui_sharpen=GetPrivateProfileIntW(L"preview",L"sharpen",0,path)==1;InterlockedExchange(&direct_sharpen,ui_sharpen);
  ui_hdr=GetPrivateProfileIntW(L"preview",L"rtx_hdr",0,path)==1;InterlockedExchange(&gen_hdr,ui_hdr);
  {ui_genshort=GetPrivateProfileIntW(L"preview",L"gen_short",1,path)!=0;int gs=GetPrivateProfileIntW(L"preview",L"gen_share",ui_genshort?25:50,path);if(gs<20)gs=20;if(gs>50)gs=50;InterlockedExchange(&gen_share_pct,gs);InterlockedExchange(&gen_sharpen,GetPrivateProfileIntW(L"preview",L"gen_sharpen",0,path)==1);}
  ui_hot_mods=GetPrivateProfileIntW(L"preview",L"hotkey_mods",MOD_CONTROL|MOD_ALT,path)&(MOD_CONTROL|MOD_ALT|MOD_SHIFT);ui_hot_vk=GetPrivateProfileIntW(L"preview",L"hotkey_vk",'F',path);if(ui_hot_vk<0||ui_hot_vk>255)ui_hot_vk='F';if(ui_hot_vk&&!(ui_hot_mods&(MOD_CONTROL|MOD_ALT)))ui_hot_mods|=MOD_CONTROL|MOD_ALT;
  ui_sync=GetPrivateProfileIntW(L"preview",L"sync_mode",0,path);if(ui_sync<0||ui_sync>2)ui_sync=0;InterlockedExchange(&direct_sync,ui_sync);
  ui_lowview=GetPrivateProfileIntW(L"preview",L"low_latency_view",0,path)==1;InterlockedExchange(&low_view,test_lowview); /* 2.2.4: active only after the on button */
  {int c=GetPrivateProfileIntW(L"preview",L"fps_corner",1,path);InterlockedExchange(&ui_fps_corner,c>=0&&c<=3?c:1);}
  ui_cadence=GetPrivateProfileIntW(L"preview",L"cadence30",0,path)!=0;ui_hudkeep=GetPrivateProfileIntW(L"preview",L"hud_keep",1,path)!=0; /* 1.9.6: default on (user 11-10) */
  ui_gpukeep=GetPrivateProfileIntW(L"preview",L"gpu_keep",ui_primary_is_amd()?1:0,path)==1;ui_fsrfast=GetPrivateProfileIntW(L"preview",L"fsr_fast",0,path)!=0;ui_latency=GetPrivateProfileIntW(L"preview",L"latency_priority",0,path)==1;InterlockedExchange(&engine_options,UI_ENGINE_BITS());
  ui_record=GetPrivateProfileIntW(L"preview",L"record_generated",0,path)==1;
  framegen_fps=ui_src;framegen_output_fps=ui_output();auto_mode=ui_mode;auto_mult=ui_mult;auto_target=ui_target;
  InterlockedExchange(&control_bits,(ui_src==60?2:0)|(ui_stable?4:0)|(ui_buffer_bits(ui_stable,ui_buffer,ui_low_buffer)<<8)|(ui_model<<16));
 }
 ui_saved_display=ui_display;ui_saved_model=ui_model;ui_saved_low_buffer=ui_low_buffer;ui_saved_buffer=ui_buffer;ui_saved_record=ui_record;InterlockedExchange(&record_generated,ui_record);
 ui_saved_src=ui_src;ui_saved_out=ui_output();ui_saved_stable=ui_stable;ui_saved_mode=ui_mode;ui_saved_mult=ui_mult;ui_saved_target=ui_target;return 1;
}
static void ui_publish(const CsStats *s,int w,int h,int error){AcquireSRWLockExclusive(&status_lock);if(s){ui_stats=*s;ui_stats_updated=GetTickCount64();}ui_width=w;ui_height=h;ui_error=error;ReleaseSRWLockExclusive(&status_lock);}
static int ui_px(int v){return MulDiv(v,ui_dpi?ui_dpi:96,96);}
static void ui_fill(HDC dc,RECT r,COLORREF color){HBRUSH b=CreateSolidBrush(color);FillRect(dc,&r,b);DeleteObject(b);}
static void ui_text(HWND window,const wchar_t *text){wchar_t old[512];GetWindowTextW(window,old,512);if(wcscmp(old,text))SetWindowTextW(window,text);}
static void ui_save_sharpen(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);if(!WritePrivateProfileStringW(L"preview",L"sharpen",ui_sharpen?L"1":L"0",path))ui_error=-2;
 logline("UI_SHARPEN enabled=%d",ui_sharpen);
}
static void ui_save_hdr(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);if(!WritePrivateProfileStringW(L"preview",L"rtx_hdr",ui_hdr?L"1":L"0",path))ui_error=-2;
 logline("UI_RTX_HDR enabled=%d",ui_hdr);
}
static void ui_save_genshort(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);if(!WritePrivateProfileStringW(L"preview",L"gen_short",ui_genshort?L"1":L"0",path)||!WritePrivateProfileStringW(L"preview",L"gen_share",ui_genshort?L"25":L"50",path))ui_error=-2;
 logline("UI_GEN_SHORT enabled=%d",ui_genshort);
}
static void ui_save_sync(void){
 wchar_t dir[32768],path[32768],value[8];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);swprintf(value,8,L"%d",ui_sync);if(!WritePrivateProfileStringW(L"preview",L"sync_mode",value,path))ui_error=-2;
 logline("UI_SYNC mode=%d",ui_sync);
}
static void ui_save_hotkey(void){
 wchar_t dir[32768],path[32768],m[16],k[16];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);swprintf(m,16,L"%d",ui_hot_mods);swprintf(k,16,L"%d",ui_hot_vk);
 if(!WritePrivateProfileStringW(L"preview",L"hotkey_mods",m,path)||!WritePrivateProfileStringW(L"preview",L"hotkey_vk",k,path))ui_error=-2;
}
static void ui_hot_name(wchar_t *out,int cap,int mods,int vk){
 if(!vk){wcsncpy(out,ui_tr(L"없음",L"None"),cap-1);out[cap-1]=0;return;}
 wchar_t key[32];
 if((vk>='A'&&vk<='Z')||(vk>='0'&&vk<='9'))swprintf(key,32,L"%c",vk);
 else if(vk>=VK_F1&&vk<=VK_F24)swprintf(key,32,L"F%d",vk-VK_F1+1);
 else{UINT sc=MapVirtualKeyW((UINT)vk,MAPVK_VK_TO_VSC);LONG lp=(LONG)(sc<<16);
  if(vk==VK_INSERT||vk==VK_DELETE||vk==VK_HOME||vk==VK_END||vk==VK_PRIOR||vk==VK_NEXT||(vk>=VK_LEFT&&vk<=VK_DOWN))lp|=1<<24;
  if(!GetKeyNameTextW(lp,key,32))swprintf(key,32,L"0x%02X",vk);}
 swprintf(out,cap,L"%ls%ls%ls%ls",mods&MOD_CONTROL?L"Ctrl + ":L"",mods&MOD_ALT?L"Alt + ":L"",mods&MOD_SHIFT?L"Shift + ":L"",key);
}
static void ui_hot_texts(void){
 if(!ui_items[196]||!ui_items[197])return;wchar_t name[64];
 if(ui_hot_capture)wcscpy(name,ui_tr(L"새 조합을 누르세요…",L"Press a new combination…"));else ui_hot_name(name,64,ui_hot_mods,ui_hot_vk);
 SetWindowTextW(ui_items[196],name);InvalidateRect(ui_items[196],NULL,FALSE);
 SetWindowTextW(ui_items[197],ui_hot_capture?ui_tr(L"Ctrl 또는 Alt와 함께 누르세요 · Backspace: 단축키 없음 · Esc: 취소",L"Hold Ctrl or Alt with a key · Backspace: no shortcut · Esc: cancel")
  :ui_hot_ok==0?ui_tr(L"다른 프로그램이 이미 쓰는 조합이라 등록하지 못했어요. 다른 조합을 고르세요.",L"Another program already uses this combination. Pick another one.")
  :ui_tr(L"전체 화면에서도 프레임 생성을 켜고 끕니다 · 버튼을 눌러 바꾸기",L"Turns frame generation on and off, also in full screen · click to change"));
}
/* Registered on the settings thread (ui_panel); the toolbar click path does the toggle. */
static void ui_hot_register(void){
 if(!ui_panel)return;UnregisterHotKey(ui_panel,1);ui_hot_ok=-1;
 if(ui_hot_vk&&!ui_hot_capture){ui_hot_ok=RegisterHotKey(ui_panel,1,(UINT)ui_hot_mods|MOD_NOREPEAT,(UINT)ui_hot_vk)?1:0;}
 wchar_t name[64];ui_hot_name(name,64,ui_hot_mods,ui_hot_vk);logline("UI_HOTKEY %ls registered=%d",name,ui_hot_ok);ui_hot_texts();
}
/* Settings window recording a new combination: called from the message loop for key presses. 1 = consumed. */
static int ui_hot_key(const MSG *m){
 if(!ui_hot_capture||(m->message!=WM_KEYDOWN&&m->message!=WM_SYSKEYDOWN))return 0;
 int vk=(int)m->wParam;
 if(vk==VK_CONTROL||vk==VK_SHIFT||vk==VK_MENU||vk==VK_LWIN||vk==VK_RWIN||vk==VK_LCONTROL||vk==VK_RCONTROL||vk==VK_LSHIFT||vk==VK_RSHIFT||vk==VK_LMENU||vk==VK_RMENU||vk==VK_PROCESSKEY||vk==VK_HANGUL||vk==VK_HANJA)return 1;
 if(vk==VK_ESCAPE){ui_hot_capture=0;ui_hot_register();return 1;}
 if(vk==VK_BACK){ui_hot_vk=0;ui_hot_capture=0;ui_save_hotkey();ui_hot_register();return 1;}
 int mods=(GetKeyState(VK_CONTROL)<0?MOD_CONTROL:0)|(GetKeyState(VK_MENU)<0?MOD_ALT:0)|(GetKeyState(VK_SHIFT)<0?MOD_SHIFT:0);
 if(!(mods&(MOD_CONTROL|MOD_ALT)))return 1; /* plain keys and Shift-only would steal typing in other programs */
 ui_hot_mods=mods;ui_hot_vk=vk;ui_hot_capture=0;ui_save_hotkey();ui_hot_register();return 1;
}
static void ui_save_lowview(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);if(!WritePrivateProfileStringW(L"preview",L"low_latency_view",ui_lowview?L"1":L"0",path))ui_error=-2;
 logline("UI_LOW_VIEW enabled=%d",ui_lowview);
}
static void ui_save_fps(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);
 wchar_t corner[8];swprintf(corner,8,L"%ld",ui_fps_corner);
 if(!WritePrivateProfileStringW(L"preview",L"show_fps",ui_show_fps?L"1":L"0",path)||!WritePrivateProfileStringW(L"preview",L"fps_corner",corner,path)||!WritePrivateProfileStringW(L"preview",L"fps_game",ui_fps_game?L"1":L"0",path))ui_error=-2;
 logline("UI_FPS_DISPLAY enabled=%d corner=%ld game=%d",ui_show_fps,ui_fps_corner,ui_fps_game);
}
static void ui_save_options(void){
 wchar_t dir[32768],path[32768];DWORD n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32768);
 if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32768);if(!n||n>32000)return;
 swprintf(path,32768,L"%ls\\settings.ini",dir);
 if(!WritePrivateProfileStringW(L"preview",L"cadence30",ui_cadence?L"1":L"0",path)||!WritePrivateProfileStringW(L"preview",L"hud_keep",ui_hudkeep?L"1":L"0",path)||
    !WritePrivateProfileStringW(L"preview",L"gpu_keep",ui_gpukeep?L"1":L"0",path)||!WritePrivateProfileStringW(L"preview",L"fsr_fast",ui_fsrfast?L"1":L"0",path)||
    !WritePrivateProfileStringW(L"preview",L"latency_priority",ui_latency?L"1":L"0",path))ui_error=-2;
 InterlockedExchange(&engine_options,UI_ENGINE_BITS());logline("UI_ENGINE_OPTIONS cadence30=%d hud_keep=%d gpu_keep=%d fsr_fast=%d latency_priority=%d",ui_cadence,ui_hudkeep,ui_gpukeep,ui_fsrfast,ui_latency);
}
static void ui_line(HDC dc,int x,int y,int xx,int yy){MoveToEx(dc,x,y,NULL);LineTo(dc,xx,yy);}
#include "controls_icons.h"
static int ui_selected(int id){if(id==128||id==140||id==165||id==801)return 1; /* 801: Apply in the recommended-settings window */return id>=191&&id<=193?ui_sync==id-191:id==154?!ui_display:id==155?ui_display:id==149?!ui_record:id==150?ui_record:id==130?ui_src==30:id==131?ui_src==60:id==132?!ui_mode:id==133?ui_mode:id>=134&&id<=136?ui_mult==id-132:0;}
static void ui_draw_button(DRAWITEMSTRUCT *d){
 HDC dc=d->hDC;RECT r=d->rcItem;int id=(int)d->CtlID,chosen=ui_selected(id),hot=d->hwndItem==ui_hover;
 if(id>=175&&id<=177){ /* 1.9.8 tab list on the left, same look as the old single "프레임 생성" entry */
  int on=ui_tab==id-175;ui_fill(dc,r,on?UI_BUTTON:hot?UI_TOOL_HOVER:UI_TOOLBAR);if(on){RECT bar=r;bar.right=bar.left+ui_px(3);ui_fill(dc,bar,UI_RED);}
  wchar_t text[64];GetWindowTextW(d->hwndItem,text,64);RECT t=r;t.left+=ui_px(23);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UI_TEXT);HGDIOBJ f=SelectObject(dc,ui_font);
  DrawTextW(dc,text,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,f);return;}
 /* Toolbar buttons have no box until hovered, like the host buttons beside them. */
 COLORREF bg=(d->itemState&ODS_SELECTED)?RGB(82,82,82):hot?UI_HOVER:id==101||id==102?UI_TOOLBAR:UI_BUTTON;
 ui_fill(dc,r,id==101||id==102?UI_TOOLBAR:UI_BG);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,(d->itemState&ODS_DISABLED)?UI_MUTED:UI_TEXT);SelectObject(dc,ui_font);
 HPEN pen=CreatePen(PS_SOLID,ui_px(chosen?2:1),chosen?UI_RED:bg),oldpen=SelectObject(dc,pen);HBRUSH b=CreateSolidBrush(bg),oldb=SelectObject(dc,b);
 if(id==101||id==102){
  /* 1.7.2: host hover is a square box over the full toolbar height in RGB(64,64,64) (measured from the user's screenshots). */
  COLORREF box=(d->itemState&ODS_SELECTED)?RGB(72,72,72):hot?UI_TOOL_HOVER:UI_TOOLBAR;ui_fill(dc,r,box);
  RECT icon=r;int inset=(r.bottom-(r.bottom*3+2)/4)/2;InflateRect(&icon,-inset,-inset);
  ui_icon(dc,icon,id==101?0:1,id==101&&(ui_requested()&1)?UI_RED:RGB(193,193,193),box);}
 else if(id==111||id==144||id==161||id==163||id==168||id==171||id==179||id==199||id==114||id==181||id==186||id==189)ui_draw_switch(dc,r,id==189?ui_fps_game:id==186?ui_lowview:id==181?ui_latency:id==179?ui_sharpen:id==199?ui_genshort:id==114?ui_hdr:id==144?ui_show_fps:id==161?ui_cadence:id==163?ui_hudkeep:id==168?ui_gpukeep:id==171?ui_fsrfast:!!(ui_requested()&1));
 else {RoundRect(dc,1,1,r.right-1,r.bottom-1,ui_px(7),ui_px(7));wchar_t text[96];GetWindowTextW(d->hwndItem,text,96);DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);}
 /* 1.7.4: no focus rectangle - it drew a white box on the last clicked button and switch (user request; host buttons show none). */
 SelectObject(dc,oldb);SelectObject(dc,oldpen);DeleteObject(b);DeleteObject(pen);
}
static void ui_move(HWND window,int x,int y,int w,int h){MoveWindow(window,ui_px(x),ui_px(y),ui_px(w),ui_px(h),TRUE);}
static void ui_place(HWND window,HWND after,int x,int y,int w,int h,UINT flags){
 RECT old;if(!window)return;
 if(GetWindowRect(window,&old)){
  if(GetWindowLongPtrW(window,GWL_STYLE)&WS_CHILD)MapWindowPoints(NULL,GetParent(window),(POINT*)&old,2);
  if(old.left==x&&old.top==y&&old.right-old.left==w&&old.bottom-old.top==h){
   HWND previous=GetWindow(window,GW_HWNDPREV);
   while(previous&&(previous==ui_panel||previous==ui_meter))previous=GetWindow(previous,GW_HWNDPREV);
   if(!(flags&SWP_NOZORDER)&&after==HWND_TOP&&previous)SetWindowPos(window,after,0,0,0,0,flags|SWP_NOMOVE|SWP_NOSIZE);
   return;
  }
 }
 SetWindowPos(window,after,x,y,w,h,flags);
}
static void ui_layout(void){
 HFONT old=ui_font,small=ui_small,heading=ui_heading,latin=ui_latin;
 ui_font=CreateFontW(-ui_px(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");
 ui_small=CreateFontW(-ui_px(14),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");
 ui_heading=CreateFontW(-ui_px(22),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");
 ui_latin=CreateFontW(-ui_px(18),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,ui_font_path[0]?L"Noto Sans":L"Segoe UI");
 for(int i=0;i<200;i++)if(ui_items[i]&&i!=147)SendMessageW(ui_items[i],WM_SETFONT,(WPARAM)((i==120||i==123||i==124||i==145)?ui_heading:(i==105||i==108||i==125||i==126||i==127||i==146||i==151||i==156||i==164||i==166||i==169||i==172||i==182||i==183||i==184||i==194||i==197)?ui_small:ui_font),TRUE);
 if(ui_tip)SendMessageW(ui_tip,WM_SETFONT,(WPARAM)ui_font,TRUE);
 if(old)DeleteObject(old);if(small)DeleteObject(small);if(heading)DeleteObject(heading);if(latin)DeleteObject(latin);
 ui_move(ui_items[120],24,18,500,34);for(int i=0;i<3;i++)ui_move(ui_items[175+i],1,74+52*i,208,52);
 ui_move(ui_items[123],254,94,310,30);ui_move(ui_items[111],792,90,56,36);
 ui_move(ui_items[122],254,147,180,30);ui_move(ui_items[128],504,141,340,42);
 ui_move(ui_items[106],254,197,190,32);ui_move(ui_items[130],504,197,340,32);
 ui_move(ui_items[107],254,251,190,32);ui_move(ui_items[132],504,245,164,42);ui_move(ui_items[133],680,245,164,42);
 ui_move(ui_items[129],254,305,190,32);for(int i=134;i<=136;i++)ui_move(ui_items[i],504+(i-134)*116,299,108,42);
 ui_move(ui_items[138],504,299,188,42);ui_move(ui_items[139],708,299,62,42);ui_move(ui_items[142],782,299,62,42);
 ui_move(ui_items[125],254,355,590,24);ui_move(ui_items[126],254,385,590,28);
 ui_move(ui_items[124],254,452,250,30);ui_move(ui_items[140],504,448,340,42);
 ui_move(ui_items[152],254,500,190,32);ui_move(ui_items[158],504,496,262,36);ui_move(ui_items[159],776,498,68,32);
 ui_move(ui_items[127],254,540,590,20);
 /* 1.9.8 tabs: tab 0 continues with the display choice; tabs 1 and 2 start at the top again (see ui_tab_of). */
 ui_move(ui_items[180],254,572,300,30);ui_move(ui_items[181],792,568,56,36);ui_move(ui_items[182],254,608,594,24); /* 2.0.3 (11-11 user): latency priority under the latency card */
 ui_move(ui_items[153],254,664,240,30);ui_move(ui_items[154],504,658,164,42);ui_move(ui_items[155],680,658,164,42);ui_move(ui_items[156],254,708,590,24);
 ui_move(ui_items[145],254,90,200,30);ui_move(ui_items[165],560,86,200,36);ui_move(ui_items[144],792,86,56,36);
 ui_move(ui_items[146],254,124,590,40);ui_move(ui_items[183],254,168,590,24); /* 2.1.1 (11-11 user): what the readout numbers mean */
 ui_move(ui_items[188],254,202,500,30);ui_move(ui_items[189],792,198,56,36); /* 1.0.1 (11-13 user): game frame rate switch; rows below +40 */
 ui_move(ui_items[178],254,358,500,30);ui_move(ui_items[179],792,354,56,36); /* 2.0.1 sharpen */
 ui_move(ui_items[185],254,430,500,30);ui_move(ui_items[186],792,426,56,36);ui_move(ui_items[187],254,466,594,66); /* 2.2.0 beta: low latency view (+40 in 2.2.1) */
 ui_move(ui_items[190],254,562,500,30);for(int i=191;i<=193;i++)ui_move(ui_items[i],254+(i-191)*202,600,190,42);ui_move(ui_items[194],254,652,594,44); /* 2.2.4: display sync */
 ui_move(ui_items[198],254,708,500,30);ui_move(ui_items[199],792,704,56,36); /* 2.3.12 (11-19 user): short mid frame switch */
 ui_move(ui_items[148],254,258,240,30);ui_move(ui_items[149],504,252,164,42);ui_move(ui_items[150],680,252,164,42);ui_move(ui_items[151],254,302,590,24); /* 2.1.1: +26 */
 ui_move(ui_items[160],254,92,160,30);ui_move(ui_items[161],420,88,56,36);ui_move(ui_items[162],540,92,180,30);ui_move(ui_items[163],792,88,56,36);ui_move(ui_items[164],254,132,262,44); /* 2.1.8: two lines for the plain-language note */ui_move(ui_items[166],540,132,308,44); /* 11-13: two lines, when to turn it off */ /* 1.7.3: one note under each switch */
 /* 1.7.8 GPU switches row (AMD tests). */
 ui_move(ui_items[167],254,208,160,30);ui_move(ui_items[168],792,204,56,36);ui_move(ui_items[169],254,244,594,24); /* 2.1.8: +18 */
 ui_move(ui_items[170],254,300,200,30);ui_move(ui_items[171],792,296,56,36);ui_move(ui_items[172],254,336,594,24); /* 1.8.1, 2.1.8: +18 */
 ui_move(ui_items[157],254,394,300,42);ui_move(ui_items[184],254,442,594,24);
 ui_move(ui_items[195],254,498,300,30);ui_move(ui_items[196],560,494,288,42);ui_move(ui_items[197],254,544,594,40); /* 2.2.4 shortcut */
 ui_move(ui_items[113],254,604,500,30);ui_move(ui_items[114],792,600,56,36);ui_move(ui_items[115],254,640,594,64); /* 2.3.13 RTX Video HDR */ /* 2.1.8 (11-11 user): what the diagnostics button does */
 ui_move(ui_items[105],232,758,634,22);ui_move(ui_items[108],232,784,634,24);
 ui_move(ui_items[174],232,824,180,42);ui_move(ui_items[143],558,824,142,42);ui_move(ui_items[112],714,824,152,42);
}
static void ui_tab_apply(void){
 for(int id=103;id<200;id++){int t=ui_tab_of(id);if(t<0||!ui_items[id])continue;
  int show=t==ui_tab&&id!=111; /* 2.2.4 (11-14 user): no on/off switch in the settings window; toolbar button and shortcut only */if(id>=134&&id<=136)show=show&&!ui_mode;if(id==138||id==139||id==142)show=show&&ui_mode;
  ShowWindow(ui_items[id],show?SW_SHOW:SW_HIDE);}
 if(ui_popup)InvalidateRect(ui_popup,NULL,TRUE);
}
/* Streaming Center's fullscreen preview is its own top-level popup covering a whole monitor. */
static BOOL CALLBACK ui_find_fullscreen(HWND window,LPARAM out){
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId()||window==ui_parent||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;
 wchar_t klass[64];GetClassNameW(window,klass,64);if(!wcscmp(klass,L"ConsoleSmooth.Settings")||!wcscmp(klass,L"ConsoleSmoothDirectTest"))return TRUE;
 RECT r;MONITORINFO m={.cbSize=sizeof(m)};if(!GetWindowRect(window,&r)||!GetMonitorInfoW(MonitorFromWindow(window,MONITOR_DEFAULTTONULL),&m))return TRUE;
 if(r.left<=m.rcMonitor.left&&r.top<=m.rcMonitor.top&&r.right>=m.rcMonitor.right&&r.bottom>=m.rcMonitor.bottom){*(HWND*)out=window;return FALSE;}
 return TRUE;
}
/* Top-left fps readout over the fullscreen preview. Topmost and click-through rather than
 * owned by the host window (no cross-thread input attachment); shown only while Streaming
 * Center is in front, so it never floats over other apps. */
/* White text only, no box or outline. The text is rendered into a per-pixel-alpha bitmap and swapped
 * in with one UpdateLayeredWindow call, so an update never shows a half-drawn frame. */
static void ui_full_render(RECT at){
 int w=at.right-at.left,h=at.bottom-at.top;HDC screen=GetDC(NULL),mem=CreateCompatibleDC(screen);
 BITMAPINFO info={0};info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
 uint32_t *px=NULL;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,(void**)&px,NULL,0);
 if(bitmap&&px){HGDIOBJ oldbmp=SelectObject(mem,bitmap);memset(px,0,(size_t)w*h*4);
  /* Grayscale antialiasing: each channel is the coverage, which becomes the alpha of premultiplied white. */
  HFONT font=CreateFontW(-ui_px(22),0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,0,0,ANTIALIASED_QUALITY,0,L"Malgun Gothic");HGDIOBJ oldfont=SelectObject(mem,font);
  SetBkMode(mem,TRANSPARENT);SetTextColor(mem,RGB(255,255,255));RECT r={0,0,w,h};DrawTextW(mem,ui_full_text,-1,&r,((ui_fps_corner&1)?DT_RIGHT:DT_LEFT)|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);GdiFlush();
  for(size_t i=0;i<(size_t)w*h;i++){uint32_t c=px[i],a=c&255;if(((c>>8)&255)>a)a=(c>>8)&255;if(((c>>16)&255)>a)a=(c>>16)&255;px[i]=a<<24|a<<16|a<<8|a;}
  POINT source={0,0},target={at.left,at.top};SIZE size={w,h};BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
  if(UpdateLayeredWindow(ui_full_meter,screen,&target,&size,mem,&source,0,&blend,ULW_ALPHA)){ui_full_rect=at;ui_full_dirty=0;}
  SelectObject(mem,oldfont);DeleteObject(font);SelectObject(mem,oldbmp);}
 if(bitmap)DeleteObject(bitmap);DeleteDC(mem);ReleaseDC(NULL,screen);
}
static void ui_position_fullscreen(void){
 if(!ui_full_meter)return;
 HWND full=NULL;if(ui_show_fps&&(ui_requested()&1))EnumWindows(ui_find_fullscreen,(LPARAM)&full);
 DWORD pid=0;HWND front=GetForegroundWindow();if(front)GetWindowThreadProcessId(front,&pid);
 /* A window above ours would make Windows compose the direct window again. */
 int show=full&&pid==GetCurrentProcessId()&&direct_status()<1;
 int visible=!!(GetWindowLongPtrW(ui_full_meter,GWL_STYLE)&WS_VISIBLE);
 if(show){MONITORINFO m={.cbSize=sizeof(m)};GetMonitorInfoW(MonitorFromWindow(full,MONITOR_DEFAULTTONEAREST),&m);
  LONG corner=InterlockedCompareExchange(&ui_fps_corner,0,0);int bw=ui_px(620),bh=ui_px(40);
  RECT at;at.left=(corner&1)?m.rcMonitor.right-ui_px(16)-bw:m.rcMonitor.left+ui_px(16);at.top=(corner&2)?m.rcMonitor.bottom-ui_px(12)-bh:m.rcMonitor.top+ui_px(12);at.right=at.left+bw;at.bottom=at.top+bh;
  static LONG drawnCorner=-1;if(drawnCorner!=corner){drawnCorner=corner;ui_full_dirty=1;}
  if(ui_full_dirty||!EqualRect(&at,&ui_full_rect))ui_full_render(at);
  if(!visible)SetWindowPos(ui_full_meter,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
 }else if(visible)ShowWindow(ui_full_meter,SW_HIDE);
}
/* Streaming Center rounds the Windows scale (150% draws at 2x, 100% and 125% at 1x), so its toolbar
 * is measured rather than derived from the DPI. The toolbar is a native child window: the tallest
 * full-width strip in the top quarter (the title strip above it is shorter). */
typedef struct {HWND parent;int width,height,preview_left;RECT bar;} UiHost;
static int ui_ours(HWND w){return w==ui_panel||w==ui_meter||IsChild(ui_panel,w)||IsChild(ui_meter,w);}
static int ui_child_rect(UiHost *h,HWND w,RECT *r){if(ui_ours(w)||!IsWindowVisible(w)||!GetWindowRect(w,r))return 0;MapWindowPoints(NULL,h->parent,(POINT*)r,2);return 1;}
static BOOL CALLBACK ui_scan_bar(HWND w,LPARAM lp){
 UiHost *h=(UiHost*)lp;RECT r;if(!ui_child_rect(h,w,&r))return TRUE;int wd=r.right-r.left,ht=r.bottom-r.top;
 if(wd*10>=h->width*9&&ht>=20&&ht<=200&&r.top<h->height/4&&ht>h->bar.bottom-h->bar.top)h->bar=r;
 return TRUE;
}
/* The preview column starts at the leftmost tall pane directly under the toolbar, right of the source list. */
static BOOL CALLBACK ui_scan_content(HWND w,LPARAM lp){
 UiHost *h=(UiHost*)lp;RECT r;if(!ui_child_rect(h,w,&r))return TRUE;
 if(r.top>=h->bar.bottom&&r.top<=h->bar.bottom+8&&r.bottom-r.top>h->height/3&&r.left>h->bar.left+20&&r.left<(h->bar.left+h->bar.right)/2&&(!h->preview_left||r.left<h->preview_left))h->preview_left=r.left;
 return TRUE;
}
/* 1.0.1: where the host's centre toolbar group ends, in toolbar units after the preview's left edge, by Streaming
 * Center version (ApInfo.ini "Version=v1.9.19.1" next to the exe). 1.9.19 added pen and pointer buttons after full
 * screen: two more buttons at the 45-unit pitch (measured 11-12 on 1.9.19.1). Unknown or older versions keep 276. */
static int ui_centre_units;
static int ui_host_centre_units(void){
 if(ui_centre_units)return ui_centre_units;
 int units=276;wchar_t path[32768],text[64]={0};DWORD n=GetModuleFileNameW(NULL,path,32000);wchar_t *slash=n&&n<32000?wcsrchr(path,L'\\'):NULL;
 if(slash){wcscpy(slash+1,L"ApInfo.ini");GetPrivateProfileStringW(L"Ver",L"Version",L"",text,64,path);}
 const wchar_t *v=text;while(*v==L' '||*v==L'v'||*v==L'V')v++;int a=0,b=0,c=0;
 if(swscanf(v,L"%d.%d.%d",&a,&b,&c)==3&&(a>1||(a==1&&(b>9||(b==9&&c>=19)))))units=366;
 logline("HOST_VERSION version=%ls toolbar_units=%d",text[0]?text:L"unknown",units);
 return ui_centre_units=units;
}
static void ui_position(void){
 RECT r;if(!ui_panel||!GetClientRect(ui_parent,&r))return;
 ui_position_fullscreen();
 int dpi=(int)GetDpiForWindow(ui_parent);if(!dpi)dpi=96;
 MONITORINFO monitor={.cbSize=sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(ui_parent,MONITOR_DEFAULTTONEAREST),&monitor);
 int fitX=MulDiv(monitor.rcWork.right-monitor.rcWork.left-24,96,900),fitY=MulDiv(monitor.rcWork.bottom-monitor.rcWork.top-24,96,1292);if(dpi>fitX)dpi=fitX;if(dpi>fitY)dpi=fitY;if(dpi<48)dpi=48;
 if(ui_dpi!=dpi){ui_dpi=dpi;ui_layout();}
 /* Toolbar geometry comes from the host's own toolbar window, so it holds at any monitor size,
  * Windows scale and window width. Units below are host toolbar units (44 per toolbar height). */
 UiHost host={.parent=ui_parent,.width=r.right,.height=r.bottom};EnumChildWindows(ui_parent,ui_scan_bar,(LPARAM)&host);
 int barH=host.bar.bottom-host.bar.top;
 if(barH<=0){barH=ui_px(44);host.bar=(RECT){ui_px(5),ui_px(34),r.right-ui_px(5),ui_px(34)+barH};}
 else EnumChildWindows(ui_parent,ui_scan_content,(LPARAM)&host);
 double unit=barH/44.0;
 if(!host.preview_left)host.preview_left=host.bar.left+(int)(325*unit);
 /* Same 45-unit pitch as the host's centre group, so the two buttons read as part of it. */
 /* 1.7.2: like the host buttons, each button is a full-height square (44 units) at a 45-unit pitch, so the hover box
  * matches theirs. The icon keeps its old size (drawn in the middle 3/4); centres stay where they were. */
 int b=(barH*3+2)/4;
 int fb=barH,fgap=(int)(45*unit)-fb;if(fgap<0)fgap=0;int fpw=2*fb+fgap;
 /* Right after the host's centre group, which ends about 277 units after the preview's left edge. The right-hand
  * group starts 232 units from the toolbar's right edge and about 373 while recording (the timer pushes it left). */
 int x=host.preview_left+(int)(ui_host_centre_units()*unit),limit=host.bar.right-(int)((InterlockedCompareExchange(&record_branches,0,0)?380:242)*unit);
 int y=host.bar.top+(barH-b)/2,fx=x-(fb-b)/2;
 ui_place(ui_panel,HWND_TOP,fx,host.bar.top,fpw,fb,SWP_NOACTIVATE);
 ui_place(ui_toggle,NULL,0,0,fb,fb,SWP_NOACTIVATE|SWP_NOZORDER);ui_place(ui_settings,NULL,fb+fgap,0,fb,fb,SWP_NOACTIVATE|SWP_NOZORDER);
 ui_tip_bar_h=barH;
 if(ui_meter){
  int fontPx=b*15/33;if(fontPx<11)fontPx=11;
  if(fontPx!=ui_meter_px){HFONT next=CreateFontW(-fontPx,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");
   if(next){SendMessageW(ui_items[147],WM_SETFONT,(WPARAM)next,TRUE);if(ui_meter_font)DeleteObject(ui_meter_font);ui_meter_font=next;ui_meter_px=fontPx;}}
  /* Beside the buttons when the toolbar has room, else the top-left corner of the preview area. */
  int pad=(int)(16*unit),mx=fx+fpw,mw=limit-mx,my=y;if(mw>(int)(420*unit))mw=(int)(420*unit); /* 2.0.7: 260 -> 420 for the game / new pictures part */
  if(mw<(int)(125*unit)){mw=(int)(420*unit);mx=host.preview_left+(int)(8*unit);my=host.bar.bottom+(int)(8*unit);}
  ui_place(ui_meter,HWND_TOP,mx,my,mw,b,SWP_NOACTIVATE);ui_place(ui_items[147],NULL,pad,0,mw-pad,b,SWP_NOACTIVATE|SWP_NOZORDER);
  int showMeter=ui_show_fps&&!!(ui_requested()&1);if(!!(GetWindowLongPtrW(ui_meter,GWL_STYLE)&WS_VISIBLE)!=showMeter)ShowWindow(ui_meter,showMeter?SW_SHOWNOACTIVATE:SW_HIDE);}
 if(ui_expanded){RECT host;GetWindowRect(ui_parent,&host);int w=ui_px(900),h=ui_px(888); /* 1.9.8 tabs: was 1292; 2.0.3 +72 for latency priority */int left=(host.left+host.right-w)/2,top=(host.top+host.bottom-h)/2;
  if(left+w>monitor.rcWork.right)left=monitor.rcWork.right-w;if(left<monitor.rcWork.left)left=monitor.rcWork.left;
  if(top+h>monitor.rcWork.bottom)top=monitor.rcWork.bottom-h;if(top<monitor.rcWork.top)top=monitor.rcWork.top;
  ui_place(ui_popup,NULL,left,top,w,h,SWP_NOACTIVATE|SWP_NOZORDER);
 }
}
static void ui_tip_update(void){
 if(!ui_tipwin)return;
 HWND target=ui_hover==ui_toggle||ui_hover==ui_settings?ui_hover:NULL;ULONGLONG now=GetTickCount64();
 if(target!=ui_tip_for){ui_tip_for=target;ui_tip_since=now;if(IsWindowVisible(ui_tipwin))ShowWindow(ui_tipwin,SW_HIDE);return;}
 if(!target||now<ui_tip_since+400||IsWindowVisible(ui_tipwin))return;
 const wchar_t *text=target==ui_toggle?ui_tr(L"프레임 생성 켜기/끄기",L"Frame generation on/off"):ui_tr(L"프레임 생성 설정",L"Frame generation settings");SetWindowTextW(ui_tipwin,text);
 int px=(ui_tip_bar_h>0?ui_tip_bar_h:ui_px(44))*9/44;if(px<12)px=12;
 if(px!=ui_tip_px||!ui_tip_font){HFONT f=CreateFontW(-px,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Malgun Gothic");if(f){if(ui_tip_font)DeleteObject(ui_tip_font);ui_tip_font=f;ui_tip_px=px;}}
 HDC dc=GetDC(ui_tipwin);HGDIOBJ old=SelectObject(dc,ui_tip_font);RECT t={0};DrawTextW(dc,text,-1,&t,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);ReleaseDC(ui_tipwin,dc);
 int w=t.right+px*4/3+2,h=t.bottom+px*2/3+2;RECT b;GetWindowRect(target,&b);
 int x=(b.left+b.right-w)/2,y=b.bottom+px/3;
 MONITORINFO m={.cbSize=sizeof(m)};GetMonitorInfoW(MonitorFromWindow(target,MONITOR_DEFAULTTONEAREST),&m);
 if(x+w>m.rcWork.right)x=m.rcWork.right-w;if(x<m.rcWork.left)x=m.rcWork.left;if(y+h>m.rcWork.bottom)y=b.top-h-px/3;
 /* Topmost: the host's red preview frame is its own window and covered a plain top-level tip. Hidden as soon as the pointer leaves. */
 SetWindowPos(ui_tipwin,HWND_TOPMOST,x,y,w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
static LRESULT CALLBACK ui_tip_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_NCHITTEST:return HTTRANSPARENT;
 case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);ui_fill(dc,r,UI_BG);HBRUSH line=CreateSolidBrush(UI_LINE);FrameRect(dc,&r,line);DeleteObject(line);
  wchar_t text[64];GetWindowTextW(window,text,64);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,UI_TEXT);HGDIOBJ old=SelectObject(dc,ui_tip_font?ui_tip_font:ui_font);DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,old);EndPaint(window,&p);return 0;}
 }
 return DefWindowProcW(window,message,wp,lp);
}
static void ui_menu(int show){ui_expanded=show;ui_position();ShowWindow(ui_popup,show?SW_SHOW:SW_HIDE);if(show){SetForegroundWindow(ui_popup);SetFocus(ui_items[111]);}InvalidateRect(ui_settings,NULL,FALSE);}
/* GPU too slow: generation that delivers fewer preview frames than the input is worse than off.
 * After a warm-up, 4 s below the input rate falls back to the host rate until a setting, the toggle or the input changes. */
typedef struct {LONG bits;int sn,sd,tn,td,width,height;} UiSlowKey;
static UiSlowKey ui_slow_key;static ULONGLONG ui_slow_start,ui_slow_low;
static void ui_check_slow(LONG bits,const AutoRate *a,int measuring,double delivered){
 UiSlowKey key={0};if((bits&1)&&a->live&&a->sd>0)key=(UiSlowKey){bits,a->sn,a->sd,a->tn,a->td,a->width,a->height};
 ULONGLONG now=GetTickCount64();
 if(memcmp(&key,&ui_slow_key,sizeof key)){ui_slow_key=key;ui_slow_start=now;ui_slow_low=0;if(InterlockedExchange(&auto_gpu_slow,0))logline("GPU_SLOW_CLEARED");return;}
 if(!key.bits||InterlockedCompareExchange(&auto_gpu_slow,0,0))return;
 double source=(double)a->sn/a->sd;
 if(now-ui_slow_start<5000||!measuring||delivered>=source*0.95){ui_slow_low=0;return;}
 if(!ui_slow_low)ui_slow_low=now;
 if(now-ui_slow_low>=4000){InterlockedExchange(&auto_gpu_slow,1);logline("GPU_SLOW fallback=1 source=%.2f delivered=%.2f",source,delivered);}
}
/* EnumDisplaySettings truncates fractional modes (143.85 Hz reads 143). Use the DWM rate when it
 * belongs to the same mode, else count 59/119/239 Hz as 60/120/240. */
static int ui_round_hz(int mode,double exact){
 if(exact>0&&fabs(exact-mode)<1)return (int)floor(exact+0.5);
 return (mode+1)%60==0?mode+1:mode;
}
static int ui_monitor_hz(void){
 HMONITOR m=MonitorFromWindow(ui_parent?ui_parent:ui_panel,MONITOR_DEFAULTTOPRIMARY);MONITORINFOEXW mi={.cbSize=sizeof mi};DEVMODEW dm={.dmSize=sizeof dm};
 if(!m||!GetMonitorInfoW(m,(MONITORINFO*)&mi)||!EnumDisplaySettingsW(mi.szDevice,ENUM_CURRENT_SETTINGS,&dm)||dm.dmDisplayFrequency<30)return 0;
 static HRESULT (WINAPI *timing)(HWND,DWM_TIMING_INFO*);if(!timing){HMODULE d=LoadLibraryW(L"dwmapi.dll");if(d)*(FARPROC*)&timing=GetProcAddress(d,"DwmGetCompositionTimingInfo");}
 DWM_TIMING_INFO t={.cbSize=sizeof t};double exact=timing&&timing(NULL,&t)>=0&&t.rateRefresh.uiDenominator?(double)t.rateRefresh.uiNumerator/t.rateRefresh.uiDenominator:0;
 int hz=ui_round_hz((int)dm.dmDisplayFrequency,exact),mhz=exact>0&&fabs(exact-hz)<1?(int)floor(exact*1000+0.5):hz*1000;
 if(abs(mhz-hz*1000)<=20)mhz=hz*1000; /* 159.997 Hz: a refresh every ~6 min, keep 160/1 */
 /* DWM's rate wobbles by a few mHz (143.848/143.850); each change would restart generation. */
 AcquireSRWLockExclusive(&auto_lock);if(abs(auto_display_mhz-mhz)>20)auto_display_mhz=mhz;ReleaseSRWLockExclusive(&auto_lock);return hz;
}
/* Even output needs a monitor refresh that is a whole multiple of the output rate (preview pacing
 * holds each frame for the same number of refreshes). 60-multiple monitors (120/180/240 Hz) keep
 * the multiplier; others (144/160/165 Hz) get a target-frame suggestion equal to the refresh. */
static int ui_monitor_hint(wchar_t *text,int cap,int family,int src,int tn,int td,int hz){
 if(hz<=0||src<=0||tn<=0||td<=0)return 0;
 if((long long)hz*td%tn==0)return 0;
 if(hz%60==0){int k=0;for(int i=4;i>=2&&!k;i--)if(i*src<=hz&&hz%(i*src)==0)k=i;
  if(k)swprintf(text,cap,ui_tr(L"모니터 %dHz · %d배(%dfps) 권장 · 지금 출력은 고르지 않을 수 있습니다.",L"Monitor %dHz · %dx (%dfps) recommended · output may be uneven now."),hz,k,k*src);
  else swprintf(text,cap,ui_tr(L"모니터 %dHz에서는 생성 프레임이 보이지 않습니다 · 120Hz 이상 권장.",L"Generated frames are not visible at %dHz · 120Hz or more recommended."),hz);return 1;}
 int target=hz;while(target>4*src)target/=2;
 if(target<=src)return 0;
 if(family==3)swprintf(text,cap,ui_tr(L"모니터 %dHz · RIFE 4.6 목표 %dfps 권장 · FSR 3 배수는 고르지 않을 수 있습니다.",L"Monitor %dHz · RIFE 4.6 target %dfps recommended · FSR 3 multipliers may be uneven."),hz,target);
 else swprintf(text,cap,ui_tr(L"모니터 %dHz · 목표 프레임 %dfps 권장 · 배수는 고르지 않을 수 있습니다.",L"Monitor %dHz · target %dfps recommended · multiplier may be uneven."),hz,target);
 return 1;
}
static void ui_refresh(void){
 wchar_t modelText[100];swprintf(modelText,100,ui_model_choices()>1?L"%ls  ▾":L"%ls",cs_models[ui_model].label);ui_text(ui_items[128],modelText);
 AutoRate detected=auto_snapshot();
 if(ui_reconcile_target&&detected.live){
  ui_reconcile_target=0;int maximum=4*detected.sn/detected.sd;
  if(ui_target>maximum){ui_target=maximum;AcquireSRWLockExclusive(&auto_lock);auto_target=maximum;ReleaseSRWLockExclusive(&auto_lock);ui_preferences(1);wchar_t value[20];swprintf(value,20,L"%d",ui_target);SetWindowTextW(ui_items[138],value);}
 }
 auto_apply_preview();detected=auto_snapshot();if(detected.live&&detected.sn>0)ui_src=(detected.sn*2>=90*detected.sd)?60:30;
 LONG bits=ui_requested();CsStats s;int error;ULONGLONG statsUpdated;AcquireSRWLockShared(&status_lock);s=ui_stats;error=ui_error;statsUpdated=ui_stats_updated;double actualBuffer=ui_live_buffer_ms;int actualW=ui_width,actualH=ui_height;ReleaseSRWLockShared(&status_lock);
 ui_text(ui_toggle,bits&1?ui_tr(L"프레임 생성 끄기",L"Turn frame generation off"):ui_tr(L"프레임 생성 켜기",L"Turn frame generation on"));ui_text(ui_items[111],bits&1?ui_tr(L"프레임 생성 켜짐",L"Frame generation on"):ui_tr(L"프레임 생성 꺼짐",L"Frame generation off"));
 ui_text(ui_items[144],ui_show_fps?ui_tr(L"프레임 표시 켜짐",L"FPS display on"):ui_tr(L"프레임 표시 꺼짐",L"FPS display off"));
 {wchar_t cornerText[32];swprintf(cornerText,32,L"%ls  ▾",ui_corner_name(ui_fps_corner));ui_text(ui_items[165],cornerText);}
 double delivered=0;int measuring=fps_snapshot(&delivered);wchar_t fpsText[256],inputText[64];
 /* 1.9.6: the slow checks count from the moment the engine is ready, not from the setting change. A model change rebuilds the
  * engine (RIFE load + calibration on the office RX 6600 takes seconds) and its first seconds always repeat. */
 {static int readyWas;int readyNow=s.ready==1;if(readyNow&&!readyWas&&!InterlockedCompareExchange(&auto_gpu_slow,0,0)){ui_slow_start=GetTickCount64();ui_slow_low=0;logline("UI_SLOW_TIMER_RESTART engine_ready=1");}readyWas=readyNow;}
 ui_check_slow(bits,&detected,measuring,delivered);int slow=InterlockedCompareExchange(&auto_gpu_slow,0,0)&&detected.live;
 /* 1.9.6: game frame rate measured from changed images (30 fps game in a 60 Hz signal), shown only when clearly below the input. */
 double contentFps=GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_content_at,0,0)<3000?InterlockedCompareExchange(&auto_content_x100,0,0)/100.0:0;
 int contentLow=detected.live&&(bits&1)&&contentFps>=15&&contentFps<(double)detected.sn/detected.sd*.75;
 if(detected.live&&contentLow)swprintf(inputText,64,ui_tr(L"입력 %.2f fps (게임 약 %.0f fps)",L"Input %.2f fps (game about %.0f fps)"),(double)detected.sn/detected.sd,contentFps);
 else if(detected.live)swprintf(inputText,64,ui_tr(L"입력 %.2f fps",L"Input %.2f fps"),(double)detected.sn/detected.sd);else wcscpy(inputText,ui_tr(L"입력 신호 대기",L"Waiting for input"));
 int active=(bits&1)&&detected.live&&detected.supported&&!error&&s.ready==1&&GetTickCount64()-statsUpdated<1500;
 if(ui_meter_mode!=bits||!active){ui_counts=(CsCountWindow){0};ui_meter_mode=bits;}
 if(active){LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);fps_counts(&ui_counts,(uint64_t)now.QuadPart,(uint64_t)freq.QuadPart,s.generated,s.repeated);}
 /* The first seconds after a start always repeat (measured: about 15 s), and the slow timer restarts on any setting change. */
 ULONGLONG shortfallNow=GetTickCount64();static CsShortfall ui_shortfall;if(!active||!measuring||shortfallNow-ui_slow_start<20000)ui_shortfall=(CsShortfall){0};
 else{int was=ui_shortfall.warn,is=fps_shortfall(&ui_shortfall,shortfallNow,delivered,ui_counts.repeat_fps);if(was!=is)logline("GEN_SHORTFALL warn=%d delivered=%.1f repeat_fps=%.1f",is,delivered,ui_counts.repeat_fps);}
 /* Same fallback as a delivered rate below the input: repeats alone only add latency. Same start-up exclusion as the warning. */
 /* 1.7.5: hopeless case first, after 8 s instead of 20 s (see fps_too_slow). */
 static CsTooSlow ui_too_slow;if(!active||!measuring||slow||shortfallNow-ui_slow_start<8000||detected.sn<=0)ui_too_slow=(CsTooSlow){0};
 else if(fps_too_slow(&ui_too_slow,shortfallNow,ui_counts.gen_fps,ui_counts.repeat_fps,s.last_inference_ms,1000.0*detected.sd/detected.sn,cs_models[ui_model].family==3)){InterlockedExchange(&auto_gpu_slow,1);slow=1;logline("GPU_SLOW fallback=1 reason=too_slow gen_fps=%.1f repeat_fps=%.1f frame_ms=%.1f family=%d",ui_counts.gen_fps,ui_counts.repeat_fps,s.last_inference_ms,cs_models[ui_model].family);}
 static CsNoGeneration ui_no_generation;if(!active||!measuring||slow||shortfallNow-ui_slow_start<20000)ui_no_generation=(CsNoGeneration){0};
 else if(fps_no_generation(&ui_no_generation,shortfallNow,ui_counts.gen_fps,ui_counts.repeat_fps)){InterlockedExchange(&auto_gpu_slow,1);logline("GPU_SLOW fallback=1 reason=no_generation gen_fps=%.1f repeat_fps=%.1f",ui_counts.gen_fps,ui_counts.repeat_fps);}
 wchar_t outputText[100],sourceRateText[32],previewRateText[32],pictureText[64]=L"";
 double shownFps=GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_shown_at,0,0)<3000?InterlockedCompareExchange(&auto_shown_x100,0,0)/100.0:0;
 if(ui_fps_game&&detected.live&&active&&!slow&&GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_content_at,0,0)<3000&&GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_shown_at,0,0)<3000)swprintf(pictureText,64,L"%.0f / %.0f fps",contentFps,shownFps); /* 2.0.8 (11-11 user): the two real rates; 2.0.9: numbers only */
 if(detected.live&&detected.sd>0)swprintf(sourceRateText,32,L"%.0f",(double)detected.sn/detected.sd);else wcscpy(sourceRateText,L"—");
 double directRate=direct_status()==2?direct_rate():0;
 if(directRate>0)swprintf(previewRateText,32,L"%.0f",directRate);else if(measuring)swprintf(previewRateText,32,L"%.0f",delivered);else wcscpy(previewRateText,L"—");
 if(slow)wcscpy(outputText,ui_tr(L"GPU 성능 부족 · 생성 안 함",L"GPU too slow · not generating"));
 /* 2.2.4 (11-14 user): no "· 생성 부족" in the readout any more; it shows the numbers only (the settings window keeps its note). */
 /* 2.0.7 (11-11 user: the readout "59.9 / 119.9" differs from the pictures really shown): add the game frame rate (changed
  * images in) and the new pictures shown per second (CsShownMeter) while generating; GTA5 quality mode without 30 fps
  * smoothing reads "게임 30 → 새 화면 60", with it 120. Hidden while either is stale (still picture, under 3 s). */
 else if(detected.live&&pictureText[0])wcscpy(outputText,pictureText);
 else if(detected.live)swprintf(outputText,100,L"%ls / %ls fps",sourceRateText,previewRateText);
 else wcscpy(outputText,detected.provider?ui_tr(L"신호 없음 · 생성 안 함",L"No signal · not generating"):ui_tr(L"장치 연결 대기 · 생성 안 함",L"Waiting for device · not generating"));
 /* 1.8.6 (11-09 user): the readout drawn over the picture (our full screen, SC full screen) shows the numbers only;
  * "· 생성 부족" stays on the toolbar meter. */
 wchar_t overlayText[100];wcscpy(overlayText,outputText);if(detected.live&&!slow){if(pictureText[0])wcscpy(overlayText,pictureText);else swprintf(overlayText,100,L"%ls / %ls fps",sourceRateText,previewRateText);}
 direct_set_label(overlayText,ui_show_fps&&(bits&1));
 if(ui_meter)ui_text(ui_items[147],outputText);if(ui_full_meter&&wcscmp(ui_full_text,overlayText)){wcscpy(ui_full_text,overlayText);ui_full_dirty=1;}
 swprintf(fpsText,256,ui_tr(L"%ls · 생성 %.1f fps · 반복 %.1f fps · 지금 %.0f fps\r\n생성: 새 프레임 · 반복: 이전 화면 유지",L"%ls · generated %.1f fps · repeated %.1f fps · now %.0f fps\r\ngenerated: new frame · repeated: previous picture kept"),inputText,active?ui_counts.gen_fps:0.0,active?ui_counts.repeat_fps:0.0,active?shownFps:0.0);
 ui_text(ui_items[146],fpsText);
 ui_text(ui_items[183],ui_fps_game?ui_tr(L"화면 숫자: 원본 / 지금 fps · 원본 = 게임이 1초에 바꾼 화면 · 지금 = 원본 + 생성한 장",L"Readout: original / now fps · original = game pictures/s · now = original + generated"):ui_tr(L"화면 숫자: 입력 / 지금 fps · 입력 = 캡처 신호 · 지금 = 1초에 화면에 낸 장",L"Readout: input / now fps · input = capture signal · now = frames shown per second")); /* 1.0.1 (11-13 user) */
 ui_text(ui_items[129],ui_mode?ui_tr(L"목표 프레임",L"Target fps"):ui_tr(L"배수",L"Multiplier"));for(int i=134;i<=136;i++)ShowWindow(ui_items[i],ui_mode||ui_tab?SW_HIDE:SW_SHOW);
 ShowWindow(ui_items[138],ui_mode&&!ui_tab?SW_SHOW:SW_HIDE);ShowWindow(ui_items[139],ui_mode&&!ui_tab?SW_SHOW:SW_HIDE);ShowWindow(ui_items[142],ui_mode&&!ui_tab?SW_SHOW:SW_HIDE);
 wchar_t text[512];int tn=ui_mode?ui_target:detected.sn*ui_mult,td=ui_mode?1:detected.sd;CsRatePlan draft;
 int valid=detected.live?cs_rate_fraction(detected.sn,detected.sd,tn,td,ui_stable,&draft):(!ui_mode||(ui_target>=30&&ui_target<=240));
 // FSR 3: 2x and 4x (2.2.2: midpoint twice). Lock the unsupported choices.
 int family=cs_models[ui_model].family,engineOk=cs_model_rate_supported(ui_model,detected.live?detected.sn:ui_src,detected.live?detected.sd:1,detected.live?tn:ui_output(),detected.live?td:1);
 if(!engineOk)valid=0;
 EnableWindow(ui_items[133],family!=3&&family!=5&&family!=6);EnableWindow(ui_items[135],family!=3&&family!=6);EnableWindow(ui_items[136],family!=6); /* 2.2.2: FSR 3 4x; 2.2.8 internal DLSS FG 2x only */
 // Processing size follows the Streaming Center recording resolution (Settings > Recording), not the capture.
 int scaled=actualW&&actualH&&(actualW!=detected.width||actualH!=detected.height);
 int pixels=scaled?actualW*actualH:detected.width*detected.height;
 ui_low_eligible=detected.live?cs_low_buffer_eligible(scaled?actualW:detected.width,scaled?actualH:detected.height,detected.sn,detected.sd,tn,td):(ui_mode?ui_target==120:ui_mult==2);
 int lowShort=!ui_stable&&ui_low_eligible&&ui_low_buffer<25;
 wchar_t scaledHint[160];swprintf(scaledHint,160,ui_tr(L"캡처 %d × %d · 처리 해상도는 녹화 설정 해상도를 따릅니다.",L"Capture %d × %d · processing follows the recording resolution."),detected.width,detected.height);
 if(detected.live&&scaled)swprintf(text,512,ui_tr(L"처리 %d × %d · %.2f fps",L"Processing %d × %d · %.2f fps"),actualW,actualH,(double)detected.sn/detected.sd);
 else if(detected.live&&detected.capped)swprintf(text,512,ui_tr(L"%d × %d · %.2f fps · Streaming Center 설정",L"%d × %d · %.2f fps · Streaming Center setting"),detected.width,detected.height,(double)detected.sn/detected.sd); /* 1.9.7: the signal rate made the line wrap and clip; it stays in diagnostics (input_detail). */
 else if(detected.live)swprintf(text,512,detected.fallback?ui_tr(L"%d × %d · %.2f fps · 캡처 설정 기준",L"%d × %d · %.2f fps · capture settings"):L"%d × %d · %.2f fps",detected.width,detected.height,(double)detected.sn/detected.sd);else wcscpy(text,detected.provider?ui_tr(L"입력 신호 없음",L"No input signal"):ui_tr(L"장치 연결 대기",L"Waiting for device"));ui_text(ui_items[130],text);
 {static int smallWas=-1;static HWND smallHwnd;int smallNow=detected.live&&!scaled&&detected.capped;if(smallNow!=smallWas||ui_items[130]!=smallHwnd){smallWas=smallNow;smallHwnd=ui_items[130];SendMessageW(ui_items[130],WM_SETFONT,(WPARAM)(smallNow?ui_small:ui_font),TRUE);}} /* 2.1.8: the full name "Streaming Center" needs the small font to fit */
 if(detected.live)swprintf(text,512,ui_tr(L"%ls: %.2f → %.2f fps · 해상도 유지",L"%ls: %.2f → %.2f fps · same resolution"),bits&1?ui_tr(L"선택 목표",L"Selected target"):ui_tr(L"켜면 사용할 목표",L"Target when on"),(double)detected.sn/detected.sd,(double)tn/td);else if(ui_mode)swprintf(text,512,ui_tr(L"입력 감지 후 %d fps 목표 · 해상도 유지",L"%d fps target once input is detected · same resolution"),ui_target);else swprintf(text,512,ui_tr(L"입력 감지 후 %d배 출력 · 해상도 유지",L"%dx output once input is detected · same resolution"),ui_mult);ui_text(ui_items[125],text);
 /* Choose the final line first: writing each candidate repaints the label and makes it flicker. */
 const wchar_t *note=slow?(ui_stable?ui_tr(L"GPU 성능 부족으로 생성을 멈췄습니다. 끄고 켜면 다시 시도합니다.",L"Generation stopped: GPU too slow. Turn it off and on to retry."):lowShort?ui_tr(L"GPU 성능 부족으로 생성을 멈췄습니다. 버퍼를 늘리면 다시 시도합니다.",L"Generation stopped: GPU too slow. Raise the buffer to retry."):ui_tr(L"GPU 성능 부족으로 생성을 멈췄습니다. 안정 우선으로 바꾸면 다시 시도합니다.",L"Generation stopped: GPU too slow. Switch to Stable to retry.")):(int64_t)pixels>1920LL*1080?ui_tr(L"FHD보다 크면 생성이 모자랄 수 있습니다 · 녹화 해상도 1920×1080 권장",L"Above FHD, generation may fall short · 1920×1080 recording recommended"):scaled?scaledHint:ui_tr(L"FHD · QHD · UHD · GPU 성능에 따라 생성량이 달라집니다.",L"FHD · QHD · UHD · generated frames depend on the GPU.");
 if(family==3&&!slow)note=ui_tr(L"FSR 3 · 2배·4배 · 빠르지만 움직임에 잔상이 생길 수 있습니다.",L"FSR 3 · 2x and 4x · fast, but motion can leave ghosting.");
 /* 1.9.7 (11-10 user, RX 6600, 30 fps source): FSR 3 shimmers around moving characters. On video it is nearly a blend
  * (ground truth 20.25 dB vs blend 19.79, RIFE 4.6 24.18), and at 30 fps input RIFE 2x has a 33 ms budget, enough on the RX 6600. */
 if(family==3&&!slow&&detected.live&&detected.sn<=35*detected.sd)note=ui_tr(L"FSR 3 · 30fps 입력에서는 RIFE 4.6이 캐릭터 주변 잔상이 더 적습니다.",L"FSR 3 · at 30fps input, RIFE 4.6 leaves less ghosting.");
 wchar_t monitorHint[160];if(detected.live&&!slow&&ui_monitor_hint(monitorHint,160,family,(detected.sn+detected.sd/2)/detected.sd,tn,td,ui_monitor_hz()))note=monitorHint;
 /* 1.8.5 (11-09 user, 4K input): processing ran at 3840x2160 because the recording resolution was 4K; RIFE took 26 ms a frame. */
 if(ui_shortfall.warn&&!slow&&(int64_t)pixels>1920LL*1080)note=ui_tr(L"생성이 목표를 못 따라갑니다 · 녹화 해상도를 1920×1080으로 낮추세요.",L"Generation can't keep up · lower the recording resolution to 1920×1080.");
 else if(ui_shortfall.warn&&!slow)note=ui_stable?ui_tr(L"생성이 목표를 못 따라갑니다 · 목표 프레임이나 녹화 해상도를 낮추세요.",L"Generation can't keep up · lower the target fps or recording resolution."):lowShort?ui_tr(L"생성이 목표를 못 따라갑니다 · 생성 버퍼를 늘리거나 안정 우선으로 바꾸세요.",L"Generation can't keep up · raise the buffer or switch to Stable."):ui_tr(L"생성이 목표를 못 따라갑니다 · 안정 우선으로 바꾸거나 목표 프레임을 낮추세요.",L"Generation can't keep up · switch to Stable or lower the target fps.");
 /* USB cards report the frame rate chosen in the source settings, not the console's signal. */
 if(detected.fallback&&detected.live&&!detected.supported)note=ui_tr(L"소스 편집에서 주사율을 60 FPS(또는 30 FPS)로 설정하세요.",L"Set the refresh rate to 60 FPS (or 30 FPS) in source settings.");
 ui_text(ui_items[126],note);
 ui_text(ui_items[140],ui_stable?ui_tr(L"안정 우선  ▾",L"Stable  ▾"):ui_tr(L"낮은 지연  ▾",L"Low latency  ▾"));EnableWindow(ui_items[158],ui_stable||ui_low_eligible);
 /* Outside FHD 60->120 low latency runs the plan buffer, which is not always 25 ms (60->160: 31.25 ms). */
 {wchar_t label[16];if(ui_stable&&!ui_buffer)wcscpy(label,ui_tr(L"자동",L"Auto"));else if(!ui_stable&&!ui_low_eligible){if(detected.live&&valid)swprintf(label,16,L"%.0fms",1000.0*draft.delay*td/tn);else wcscpy(label,ui_tr(L"기본",L"Default"));}else swprintf(label,16,L"%dms",ui_stable?ui_buffer:ui_low_buffer);ui_text(ui_items[159],label);}
 if(!(bits&1))wcscpy(text,ui_tr(L"꺼짐 · 생성 버퍼 없음",L"Off · no generation buffer"));else if(!detected.live)wcscpy(text,ui_tr(L"입력 신호 대기",L"Waiting for input"));else if(valid){
 double buffer=1000.0*draft.delay*td/tn;int adaptive=ui_stable&&!ui_buffer&&(int64_t)pixels>1920LL*1080;
 if(ui_stable&&ui_buffer)buffer=ui_buffer;
 if(lowShort)buffer=1000.0*td/tn+cs_low_reserve_ms(ui_low_buffer);
 /* 1.6.5: low latency and FHD stable size the buffer from the measured inference time; show the applied value. */
 if(!(ui_stable&&ui_buffer)&&ui_stable==ui_saved_stable&&ui_buffer==ui_saved_buffer&&ui_low_buffer==ui_saved_low_buffer&&actualBuffer>0&&s.ready==1)buffer=actualBuffer;
 if(adaptive&&(!actualBuffer||s.ready!=1||ui_stable!=ui_saved_stable||ui_buffer!=ui_saved_buffer))wcscpy(text,ui_tr(L"버퍼 자동 · 최대 200ms",L"Buffer auto · up to 200ms"));
 else swprintf(text,512,ui_tr(L"생성 버퍼 %.1f ms%ls%ls",L"Buffer %.1f ms%ls%ls"),buffer,adaptive?ui_tr(L" · 자동",L" · auto"):L"",!ui_stable&&!ui_low_eligible?(detected.live&&detected.sn<50*detected.sd?ui_tr(L" · 30fps 입력은 다음 원본을 기다려야 해서 고정",L" · fixed at 30fps input (waits for the next frame)"):ui_tr(L" · 25ms 미만은 FHD 60→120 전용",L" · below 25ms: FHD 60→120 only")):ui_tr(L" · 클수록 지연 증가",L" · larger means more latency"));
 }else wcscpy(text,ui_tr(L"목표는 입력보다 높고 입력의 4배 이하여야 합니다.",L"Target must be above the input and at most 4× the input."));ui_text(ui_items[127],text);
 const wchar_t *state=!(bits&1)?ui_tr(L"꺼짐 · 원본 표시",L"Off · showing original"):error==-3?ui_tr(L"출력 변경 중 · 원본 표시",L"Changing output · showing original"):error||s.ready<0?ui_tr(L"입력 또는 엔진 확인 필요",L"Check the input or engine"):s.ready==0?ui_tr(L"엔진 준비 중",L"Engine starting"):ui_tr(L"켜짐",L"On");
 if(!(bits&1))wcscpy(text,ui_tr(L"꺼짐 · 원본 표시",L"Off · showing original"));else if(!detected.provider)wcscpy(text,ui_tr(L"장치 연결 대기 · 원본 표시",L"Waiting for device · showing original"));else if(!detected.live)wcscpy(text,ui_tr(L"입력 신호 없음 · 원본 표시",L"No input signal · showing original"));else if(!detected.supported)wcscpy(text,ui_tr(L"지원 입력 확인 필요 · 원본 표시",L"Check supported input · showing original"));else swprintf(text,512,L"%.2f → %.2f fps · %ls%ls",(double)detected.sn/detected.sd,(double)detected.tn/detected.td,state,synthetic?ui_tr(L" · 합성 영상 시험",L" · synthetic video test"):L"");ui_text(ui_items[105],text);
 int rateDirty=ui_saved_mode!=ui_mode||ui_saved_mult!=ui_mult||ui_saved_target!=ui_target;
 int recording=InterlockedCompareExchange(&record_branches,0,0)>0;
 const wchar_t *status=!valid?(engineOk?ui_tr(L"목표 프레임을 확인하세요.",L"Check the target fps."):ui_tr(L"FSR 3은 배수 모드의 2배만 지원합니다.",L"FSR 3 supports only 2x in multiplier mode.")):error==-2?ui_tr(L"설정을 저장하지 못했습니다.",L"Could not save settings."):error==-3?ui_tr(L"출력 변경 중",L"Changing output"):L"";
 if(recording&&rateDirty)status=ui_tr(L"녹화 종료 후 목표 프레임을 변경하세요.",L"Change the target fps after recording stops.");
 if(InterlockedCompareExchange(&record_fault,0,0))status=ui_tr(L"녹화 보호 연결 실패 · 녹화를 다시 시작하세요.",L"Recording protection failed · restart the recording.");
 ui_text(ui_items[108],status);
 {/* Even only when each frame holds a whole number of the primary monitor's refreshes; the phase lock does nothing otherwise. */
  DEVMODEW dm={.dmSize=sizeof dm};int hz=EnumDisplaySettingsW(NULL,ENUM_CURRENT_SETTINGS,&dm)?ui_round_hz((int)dm.dmDisplayFrequency,0):0;
  int fits=detected.live&&hz>0&&detected.tn>0&&detected.td>0&&(long long)hz*detected.td%detected.tn==0;
  /* 1.8.6 (11-09 user decision): full screen opens even when the target does not divide the refresh rate (e.g. 120 fps on
   * 160 Hz); frames then hold one or two refreshes in turn, so motion is a little uneven, but latency is still the shorter one. */
  direct_ui_sync((ui_act_display||ui_act_lowview)&&detected.live&&(detected.tn>0||ui_act_lowview),ui_direct_reopen);ui_direct_reopen=0;
  LONG shown=direct_status();wchar_t displayText[160];
  if(!ui_display)wcscpy(displayText,ui_stable?ui_tr(L"안정 우선은 지연이 길어 전체 화면을 권장합니다 · 창은 Streaming Center 화면 유지",L"Stable adds latency · full screen recommended · a window keeps the Streaming Center view"):ui_tr(L"Streaming Center 화면을 보며 창 이동 가능 · 전체 화면보다 지연이 깁니다",L"Movable window in Streaming Center · more latency than full screen"));
  else if(shown==-1&&ui_saved_display)wcscpy(displayText,ui_tr(L"전체 화면 표시 실패 · 창으로 표시 중 · 적용을 누르면 다시 시도합니다.",L"Full screen failed · showing a window · press Apply to retry."));
  else if(detected.live&&!fits)swprintf(displayText,160,ui_tr(L"주 모니터 %dHz는 목표의 배수가 아님 · 조금 고르지 않을 수 있음 · Esc로 복귀",L"Main monitor %dHz is not a multiple of the target · may be slightly uneven · Esc to return"),hz);
  else wcscpy(displayText,ui_tr(L"창보다 지연이 짧음 · 주 모니터 전체 · Esc로 복귀 · 다른 모니터 클릭 가능",L"Less latency than a window · main monitor · Esc to return · other monitors stay usable"));
  /* 2.3.3: the faster GPU path is chosen when Streaming Center builds its preview; say when a restart changes it. */
  {LONG act=InterlockedCompareExchange(&gpu_path_active,0,0);
   if(InterlockedCompareExchange(&gpu_path_failed_now,0,0))wcscpy(displayText,ui_tr(L"빠른 저지연 방식을 이 PC에서 쓸 수 없어 다음 시작부터 기존 방식을 씁니다 · Streaming Center를 다시 시작하세요",L"The faster low latency path failed on this PC · the next start uses the standard path · restart Streaming Center"));
   else if(act>=0){int want=gpu_rule(ui_saved_model,ui_lowview,ui_saved_display,ui_stable,ui_mode,ui_mult,ui_target,ui_src,ui_cadence,gpu_rule_failed_mask());
    if(want&&cs_models[ui_saved_model].family!=4&&!gpu_rule_single_gpu())want=0;
    if(want!=act)wcscpy(displayText,want?ui_tr(L"Streaming Center를 다시 시작하면 더 빠른 저지연 방식이 적용됩니다",L"Restart Streaming Center to use the faster low latency path"):ui_tr(L"Streaming Center를 다시 시작해야 바뀐 설정이 모두 적용됩니다",L"Restart Streaming Center to apply all changed settings"));}}
  ui_text(ui_items[156],displayText);}
 ui_text(ui_items[151],ui_record?ui_tr(L"선택한 배수로 녹화·방송 · 생성이 꺼진 동안은 장면 반복 · 다음 녹화부터",L"Recorded and streamed at the chosen rate · repeats while generation is off · from the next recording"):ui_tr(L"원본 · 다음 녹화부터",L"Original · from the next recording"));
 EnableWindow(ui_items[143],valid&&!(recording&&rateDirty));
 POINT point;GetCursorPos(&point);HWND hover=WindowFromPoint(point);if(hover!=ui_hover){if(ui_hover&&IsWindow(ui_hover)&&(GetParent(ui_hover)==ui_panel||GetParent(ui_hover)==ui_popup))InvalidateRect(ui_hover,NULL,FALSE);ui_hover=hover;if(GetParent(hover)==ui_panel||GetParent(hover)==ui_popup)InvalidateRect(hover,NULL,FALSE);}ui_position();ui_tip_update();
}
static void ui_invalidate(void){for(int i=0;i<200;i++)if(ui_items[i])InvalidateRect(ui_items[i],NULL,FALSE);ui_refresh();}
#include "diagnostics.h"
#include "recommend.h"
/* Generation buffer slider: low latency 15-25 ms in 1 ms steps, stable auto/50/100/150. Applied with the Apply button like other choices. */
static int ui_slider_stops(void){return ui_stable?4:11;}
static int ui_slider_pos(void){return ui_stable?ui_buffer/50:(ui_low_eligible?ui_low_buffer:25)-15;}
static void ui_slider_set(int pos){
 int stops=ui_slider_stops();if(pos<0)pos=0;if(pos>=stops)pos=stops-1;if(pos==ui_slider_pos())return;
 if(ui_stable)ui_buffer=pos*50;else ui_low_buffer=pos+15;InvalidateRect(ui_items[158],NULL,FALSE);ui_refresh();
}
static void ui_slider_mouse(HWND window,int x){
 RECT r;GetClientRect(window,&r);int pad=ui_px(11),span=r.right-2*pad,stops=ui_slider_stops();if(span<1)return;
 ui_slider_set(((x-pad)*(stops-1)+span/2+(x<pad?-span:0))/span);
}
static void ui_slider_paint(HWND window,HDC dc){
 RECT r;GetClientRect(window,&r);int w=r.right,h=r.bottom,S=4,pad=ui_px(11)*S,cy=h*S/2,span=w*S-2*pad,stops=ui_slider_stops(),on=IsWindowEnabled(window);
 int x=pad+span*ui_slider_pos()/(stops-1),rail=ui_px(3)*S,knob=ui_px(9)*S;
 HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,w*S,h*S);HGDIOBJ oldbmp=SelectObject(mem,bitmap),oldpen=SelectObject(mem,GetStockObject(NULL_PEN));
 RECT full={0,0,w*S,h*S};ui_fill(mem,full,UI_BG);
 HBRUSH right=CreateSolidBrush(RGB(92,92,92)),left=CreateSolidBrush(on?UI_RED:UI_LINE),thumb=CreateSolidBrush(on?RGB(239,239,239):UI_MUTED),halo=CreateSolidBrush(UI_HOVER);
 HGDIOBJ oldbrush=SelectObject(mem,right);RoundRect(mem,pad-rail,cy-rail,w*S-pad+rail,cy+rail,2*rail,2*rail);
 if(stops<=4)for(int i=0;i<stops;i++){int t=pad+span*i/(stops-1),d=rail*3/2;Ellipse(mem,t-d,cy-d,t+d,cy+d);}
 SelectObject(mem,left);RoundRect(mem,pad-rail,cy-rail,x+rail,cy+rail,2*rail,2*rail);
 if(on&&(GetFocus()==window||GetCapture()==window||ui_hover==window)){SelectObject(mem,halo);int d=knob*3/2;Ellipse(mem,x-d,cy-d,x+d,cy+d);}
 SelectObject(mem,thumb);Ellipse(mem,x-knob,cy-knob,x+knob,cy+knob);
 SelectObject(mem,oldbrush);SelectObject(mem,oldpen);DeleteObject(right);DeleteObject(left);DeleteObject(thumb);DeleteObject(halo);
 int mode=SetStretchBltMode(dc,HALFTONE);POINT origin;SetBrushOrgEx(dc,0,0,&origin);StretchBlt(dc,0,0,w,h,mem,0,0,w*S,h*S,SRCCOPY);SetBrushOrgEx(dc,origin.x,origin.y,NULL);SetStretchBltMode(dc,mode);
 SelectObject(mem,oldbmp);DeleteObject(bitmap);DeleteDC(mem);
}
/* Labels whose text changes while the window is open. A STATIC erases and redraws on every change, which flickers; these draw into memory first. */
static void ui_label_paint(HWND window,HDC dc){
 int id=GetDlgCtrlID(window);RECT r;GetClientRect(window,&r);HFONT font=(HFONT)GetWindowLongPtrW(window,GWLP_USERDATA);
 HDC mem=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,r.right,r.bottom);HGDIOBJ oldbmp=SelectObject(mem,bitmap),oldfont=SelectObject(mem,font?font:ui_font);
 ui_fill(mem,r,id==105||id==108?RGB(38,38,38):UI_BG);SetBkMode(mem,TRANSPARENT);SetTextColor(mem,id==130||id==159?UI_TEXT:UI_MUTED);
 wchar_t text[512];GetWindowTextW(window,text,512);DrawTextW(mem,text,-1,&r,id==159?DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX:DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);
 BitBlt(dc,0,0,r.right,r.bottom,mem,0,0,SRCCOPY);SelectObject(mem,oldfont);SelectObject(mem,oldbmp);DeleteObject(bitmap);DeleteDC(mem);
}
static LRESULT CALLBACK ui_label_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_ERASEBKGND:return 1;
 case WM_NCHITTEST:return HTTRANSPARENT;
 case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);ui_label_paint(window,dc);EndPaint(window,&p);return 0;}
 case WM_SETFONT:SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)wp);if(lp)InvalidateRect(window,NULL,FALSE);return 0;
 case WM_GETFONT:return GetWindowLongPtrW(window,GWLP_USERDATA);
 case WM_SETTEXT:{LRESULT done=DefWindowProcW(window,message,wp,lp);InvalidateRect(window,NULL,FALSE);return done;}
 }
 return DefWindowProcW(window,message,wp,lp);
}
static const wchar_t *ui_label_class(int id){return id==105||id==108||id==125||id==126||id==127||id==130||id==146||id==151||id==156||id==159?L"ConsoleSmooth.Label":L"STATIC";}
static LRESULT CALLBACK ui_slider_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);ui_slider_paint(window,dc);EndPaint(window,&p);return 0;}
 case WM_GETDLGCODE:return DLGC_WANTARROWS;
 case WM_ENABLE:case WM_SETFOCUS:case WM_KILLFOCUS:InvalidateRect(window,NULL,FALSE);return 0;
 case WM_LBUTTONDOWN:SetFocus(window);SetCapture(window);ui_slider_mouse(window,(short)LOWORD(lp));InvalidateRect(window,NULL,FALSE);return 0;
 case WM_MOUSEMOVE:if(GetCapture()==window)ui_slider_mouse(window,(short)LOWORD(lp));return 0;
 case WM_LBUTTONUP:if(GetCapture()==window)ReleaseCapture();InvalidateRect(window,NULL,FALSE);return 0;
 case WM_KEYDOWN:if(wp==VK_LEFT||wp==VK_DOWN){ui_slider_set(ui_slider_pos()-1);return 0;}if(wp==VK_RIGHT||wp==VK_UP){ui_slider_set(ui_slider_pos()+1);return 0;}break;
 }
 return DefWindowProcW(window,message,wp,lp);
}
/* 2.2.4 (11-14 user): the settings window only chooses and saves. Choices reach the running preview when frame generation is
 * turned on with the toolbar button or the shortcut: model, rate and latency, the full screen window and the low latency view.
 * Apply while generation is on keeps them pending (no window opens over the game). ui_act_*: what the running session uses. */
static void ui_publish_saved(void){
 AcquireSRWLockExclusive(&auto_lock);auto_mode=ui_saved_mode;auto_mult=ui_saved_mult;auto_target=ui_saved_target;ReleaseSRWLockExclusive(&auto_lock);
 LONG prior,next;do{prior=ui_requested();next=(prior&~(4|(255<<8)|(255<<16)))|(ui_saved_stable?4:0)|(ui_buffer_bits(ui_saved_stable,ui_saved_buffer,ui_saved_low_buffer)<<8)|(ui_saved_model<<16);}while(InterlockedCompareExchange(&control_bits,next,prior)!=prior);
 /* 1.9.6 (11-10 office): a new start always retries after a "GPU too slow" stop. */
 memset(&ui_slow_key,0xFF,sizeof ui_slow_key);ui_counts=(CsCountWindow){0};ui_apply_pending=0;
 logline("UI_SETTINGS_PUBLISHED model=%s target=%d mode=%d multiplier=%d stable=%d display=%d low_view=%d",cs_models[ui_saved_model].name,ui_saved_out,ui_saved_mode,ui_saved_mult,ui_saved_stable,ui_saved_display,ui_lowview);
}
/* Toolbar button / shortcut. Turning on publishes pending choices and opens full screen or low latency view if chosen. */
static void ui_toggle_generation(const char *how){
 LARGE_INTEGER now;QueryPerformanceCounter(&now);int on=!(ui_requested()&1);
 if(on){if(ui_apply_pending)ui_publish_saved();ui_act_display=ui_saved_display;ui_act_lowview=ui_lowview;InterlockedExchange(&low_view,ui_act_lowview);ui_direct_reopen=1;}
 InterlockedExchange64(&toggle_qpc,now.QuadPart);InterlockedXor(&control_bits,1);LONG serial=InterlockedIncrement(&toggle_serial);
 if(!on){ui_act_lowview=0;InterlockedExchange(&low_view,test_lowview);}
 logline("UI_TOGGLE enabled=%d serial=%ld qpc=%lld by=%s display=%d low_view=%d",!!(ui_requested()&1),serial,(long long)now.QuadPart,how,ui_act_display,ui_act_lowview);
}
static LRESULT CALLBACK ui_proc(HWND window,UINT message,WPARAM wp,LPARAM lp){
 switch(message){
 case WM_MEASUREITEM:{MEASUREITEMSTRUCT *m=(MEASUREITEMSTRUCT*)lp;if(m->CtlType==ODT_MENU){m->itemWidth=ui_px(m->itemID>=400?200:304);m->itemHeight=ui_px(42);return TRUE;}break;}
 case WM_DRAWITEM:{DRAWITEMSTRUCT* d=(DRAWITEMSTRUCT*)lp;if(d->CtlType==ODT_MENU){int item=(int)d->itemID,corner=item>=400,model=item-200,latency=item>=300&&!corner;if(!latency&&!corner&&!cs_model_valid(model))return FALSE;
  RECT r=d->rcItem;ui_fill(d->hDC,r,(d->itemState&ODS_SELECTED)?UI_HOVER:UI_BUTTON);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,UI_TEXT);HGDIOBJ font=SelectObject(d->hDC,ui_font);
  /* 1.7.2: the red mark of the current choice sits at the right end (user request). */
  if(corner?ui_fps_corner==item-400:latency?ui_stable==item-300:model==ui_model){RECT bar=r;bar.left=bar.right-ui_px(3);ui_fill(d->hDC,bar,UI_RED);}r.left+=ui_px(18);
  DrawTextW(d->hDC,corner?ui_corner_name(item-400):latency?(item==301?ui_tr(L"안정 우선",L"Stable"):ui_tr(L"낮은 지연",L"Low latency")):cs_models[model].label,-1,&r,DT_SINGLELINE|DT_VCENTER);SelectObject(d->hDC,font);return TRUE;
 }ui_draw_button(d);return TRUE;}
 case WM_COMMAND:{int id=LOWORD(wp);
  if(id==138&&HIWORD(wp)==EN_CHANGE){wchar_t text[20];GetWindowTextW(ui_items[138],text,20);ui_target=_wtoi(text);ui_refresh();return 0;}
  if(HIWORD(wp)!=BN_CLICKED)break;
  if((id==101||id==102)&&ui_tipwin){ShowWindow(ui_tipwin,SW_HIDE);ui_tip_since=GetTickCount64()+86400000ULL;} /* no tip again until the pointer leaves */
  switch(id){
   case 101:ui_toggle_generation(lp==0?"shortcut":"button");break;
   case 102:ui_menu(!ui_expanded);return 0;
   case 157:diag_export(ui_popup,&ui_counts);return 0;
   case 174:ui_reco_start();return 0;
   case 175:case 176:case 177:ui_tab=id-175;ui_tab_apply();break;
   case 144:ui_show_fps=!ui_show_fps;ui_save_fps();break;
   case 165:{HMENU menu=CreatePopupMenu();for(int c=0;c<4;c++)AppendMenuW(menu,MF_OWNERDRAW,400+c,ui_corner_name(c));RECT r;GetWindowRect(ui_items[165],&r);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,0,window,NULL);DestroyMenu(menu);
    if(choice>=400&&choice<=403){InterlockedExchange(&ui_fps_corner,choice-400);ui_save_fps();}break;}
   case 189:ui_fps_game=!ui_fps_game;ui_save_fps();break;
   case 114:ui_hdr=!ui_hdr;InterlockedExchange(&gen_hdr,ui_hdr);ui_save_hdr();break;
   case 199:ui_genshort=!ui_genshort;InterlockedExchange(&gen_share_pct,ui_genshort?25:50);ui_save_genshort();break;
   case 179:ui_sharpen=!ui_sharpen;InterlockedExchange(&direct_sharpen,ui_sharpen);ui_save_sharpen();break;
   case 196:ui_hot_capture=1;ui_hot_register();SetFocus(ui_items[196]);break;
   case 191:case 192:case 193:ui_sync=id-191;InterlockedExchange(&direct_sync,ui_sync);ui_save_sync();break;
   case 186:ui_lowview=!ui_lowview;ui_save_lowview();break; /* 2.2.4: takes effect at the next turn-on */
   case 161:ui_cadence=!ui_cadence;ui_save_options();break;
   case 163:ui_hudkeep=!ui_hudkeep;ui_save_options();break;
   case 168:ui_gpukeep=!ui_gpukeep;ui_save_options();break;
   case 171:ui_fsrfast=!ui_fsrfast;ui_save_options();break;
   case 181:ui_latency=!ui_latency;ui_save_options();break;
   case 149:case 150:ui_record=id==150;break;
   case 154:case 155:ui_display=id==155;break;
   case 112:ui_menu(0);SetFocus(ui_settings);return 0;
   
   case 132:case 133:ui_mode=id==133;break;
   case 128:{
    if(ui_model_choices()<2)break; /* Nothing to choose with a single engine. */
    HMENU menu=CreatePopupMenu();for(int k=0;k<CS_MODEL_ORDER_COUNT;k++){int i=cs_model_order[k];if(cs_model_valid(i))AppendMenuW(menu,MF_OWNERDRAW|(i==ui_model?MF_CHECKED:0),200+i,cs_models[i].label);}
    RECT r;GetWindowRect(ui_items[128],&r);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,0,window,NULL);DestroyMenu(menu);
    if(choice>=200&&choice<200+CS_MODEL_COUNT&&cs_model_valid(choice-200)){ui_model=choice-200;
     {wchar_t gp[32768],gv[16];unsigned fm=gpu_rule_failed_mask();if(ui_model<32&&(fm>>ui_model&1)&&gpu_rule_settings_path(gp)){swprintf(gv,16,L"%u",fm&~(1u<<ui_model));WritePrivateProfileStringW(L"preview",L"gpu_path_failed",gv,gp);}} /* 2.3.3: choosing it again retries the GPU path */
     // Move to the nearest supported rate: FSR 3 needs 2x.
     if(cs_models[ui_model].family==3){ui_mode=0;if(ui_mult!=4)ui_mult=2;} /* 2.2.2: FSR 3 keeps 4x */
     if(cs_models[ui_model].family==4)ui_vfg_offer_download(window);
#ifdef CS_INTERNAL_DLSSG
     if(cs_models[ui_model].family==5){ui_mode=0;ui_dlssg_offer(window);} /* 2x/3x/4x */
#endif
     if(cs_models[ui_model].family==6){ui_mode=0;ui_mult=2;} /* AMD FSR FG DLL: 2x */
    }
    break;}
   case 134:case 135:case 136:ui_mult=id-132;break;
   case 139:case 142:{int next=ui_target+(id==139?-1:1);AutoRate a=auto_snapshot();int min=a.live?a.sn/a.sd+1:30,max=a.live?4*a.sn/a.sd:240;if(next<min)next=min;if(next>max)next=max;wchar_t text[20];swprintf(text,20,L"%d",next);SetWindowTextW(ui_items[138],text);break;}
   case 140:{HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_OWNERDRAW,300,ui_tr(L"낮은 지연",L"Low latency"));AppendMenuW(menu,MF_OWNERDRAW,301,ui_tr(L"안정 우선",L"Stable"));RECT r;GetWindowRect(ui_items[140],&r);int choice=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,0,window,NULL);DestroyMenu(menu);if(choice==300||choice==301)ui_stable=choice==301;break;}
   case 143:if(IsWindowEnabled(ui_items[143])&&ui_preferences(1)){
    /* 2.2.4 (11-14 user): Apply saves. While generation is off the choices are published at once (nothing on screen changes);
     * while it is on they wait for the next turn-on with the toolbar button or the shortcut. */
    if(ui_requested()&1){ui_apply_pending=1;logline("UI_APPLY_SAVED pending=1 (takes effect at the next turn-on)");}
    else ui_publish_saved();
   }break;
  }ui_invalidate();return 0;}
 case UI_RECO_DONE:ui_reco_finish();return 0;
 case WM_CLOSE:if(window==ui_popup){
#ifdef CS_UI_PREVIEW
  DestroyWindow(ui_panel);
#else
  ui_menu(0);
#endif
  return 0;}break;
 /* 2.2.5 (11-14 user): Esc in the full screen window turns generation off, so one press of the button or shortcut opens it again. */
 case UI_ESC_OFF:if(window==ui_panel&&(ui_requested()&1)){logline("UI_ESCAPE_OFF");SendMessageW(ui_panel,WM_COMMAND,MAKEWPARAM(101,BN_CLICKED),(LPARAM)ui_toggle);}return 0;
 case WM_HOTKEY:if(window==ui_panel&&wp==1){logline("UI_HOTKEY_PRESSED");SendMessageW(ui_panel,WM_COMMAND,MAKEWPARAM(101,BN_CLICKED),0);}return 0;
 case WM_TIMER:if(!IsWindow(ui_parent)){DestroyWindow(ui_panel);return 0;}ui_refresh();return 0;
 case WM_CTLCOLOREDIT:SetTextColor((HDC)wp,UI_TEXT);SetBkColor((HDC)wp,UI_HOVER);return (LRESULT)ui_field_brush;
 case WM_CTLCOLORSTATIC:{int id=GetDlgCtrlID((HWND)lp);COLORREF bg=id==120||id==147?UI_TOOLBAR:id==121?UI_BUTTON:id==105||id==108?RGB(38,38,38):UI_BG;SetTextColor((HDC)wp,id==105||id==108||id==125||id==126||id==127||id==146||id==151||id==156||id==164||id==166||id==183||id==184||id==194||id==197?UI_MUTED:UI_TEXT);SetBkColor((HDC)wp,bg);return (LRESULT)(id==120||id==147?ui_head_brush:id==121?ui_nav_brush:id==105||id==108?ui_main_brush:ui_brush);}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(window,&p);RECT r;GetClientRect(window,&r);ui_fill(dc,r,window==ui_panel?UI_TOOLBAR:RGB(38,38,38));
  if(window==ui_meter)ui_fill(dc,r,UI_TOOLBAR);
  if(window==ui_popup){RECT header={0,0,r.right,ui_px(64)},side={0,ui_px(64),ui_px(210),r.bottom};ui_fill(dc,header,UI_TOOLBAR);ui_fill(dc,side,UI_TOOLBAR);
   /* 1.9.8: cards of the selected tab (top, bottom pairs in 96-dpi units). */
   static const short cards[3][10]={{78,428,440,640,652,740,0,0,0,0},{78,236,244,336,344,408,416,540,548,708},{78,184,192,276,284,368,382,476,484,592}};
   HBRUSH b=CreateSolidBrush(UI_BG),old=SelectObject(dc,b);HGDIOBJ pen=SelectObject(dc,GetStockObject(NULL_PEN));
   for(int i=0;i<10&&cards[ui_tab][i];i+=2)RoundRect(dc,ui_px(232),ui_px(cards[ui_tab][i]),ui_px(868),ui_px(cards[ui_tab][i+1]),ui_px(22),ui_px(22));
   SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(b);
   b=CreateSolidBrush(UI_LINE);FrameRect(dc,&r,b);DeleteObject(b);
   if(ui_tab==2){RECT split={ui_px(520),ui_px(92),ui_px(520)+ui_px(1),ui_px(156)};ui_fill(dc,split,RGB(70,70,70));} /* 1.7.3: divider between the 30fps and HUD columns */
  }EndPaint(window,&p);return 0;}
 case WM_DESTROY:if(window==ui_panel){UnregisterHotKey(ui_panel,1);InterlockedExchange(&auto_stopping,1);KillTimer(window,1);if(ui_tip)DestroyWindow(ui_tip);if(ui_tipwin)DestroyWindow(ui_tipwin);if(ui_tip_font)DeleteObject(ui_tip_font);if(ui_meter)DestroyWindow(ui_meter);if(ui_full_meter)DestroyWindow(ui_full_meter);if(ui_popup)DestroyWindow(ui_popup);PostQuitMessage(0);}return 0;
 }return DefWindowProcW(window,message,wp,lp);
}
static HWND ui_child(HWND parent,const wchar_t *klass,const wchar_t *text,DWORD style,int id){HWND w=CreateWindowExW(0,klass,text,WS_CHILD|WS_VISIBLE|style,0,0,1,1,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);ui_items[id]=w;return w;}
static BOOL CALLBACK ui_find(HWND window,LPARAM unused){(void)unused;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid!=GetCurrentProcessId()||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;wchar_t title[128];GetWindowTextW(window,title,128);if(!wcscmp(title,L"Streaming Center")){ui_parent=window;return FALSE;}return TRUE;}
static DWORD WINAPI ui_main(void *unused){
 (void)unused;/* 1.0.1: GST_TRACERS starts us at gst_init, before the main window (and after an update its dialog comes first): wait up to 30 min */
 for(int i=0;i<9000&&!ui_parent;i++){EnumWindows(ui_find,0);if(!ui_parent)Sleep(200);}if(!ui_parent){logline("UI_NO_HOST_WINDOW");return 0;}
 HINSTANCE module=GetModuleHandleW(NULL);ui_brush=CreateSolidBrush(UI_BG);ui_head_brush=CreateSolidBrush(UI_TOOLBAR);ui_field_brush=CreateSolidBrush(UI_HOVER);ui_main_brush=CreateSolidBrush(RGB(38,38,38));ui_nav_brush=CreateSolidBrush(UI_BUTTON);
 GetModuleFileNameW(NULL,ui_font_path,32000);wchar_t *slash=wcsrchr(ui_font_path,L'\\');if(slash){wcscpy(slash+1,L"fonts\\NotoSansRegular.ttf");if(!AddFontResourceExW(ui_font_path,FR_PRIVATE,NULL))ui_font_path[0]=0;}else ui_font_path[0]=0;
 WNDCLASSW klass={0};klass.lpfnWndProc=ui_proc;klass.hInstance=module;klass.lpszClassName=L"ConsoleSmooth.Settings";klass.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);RegisterClassW(&klass);
 ui_panel=CreateWindowExW(WS_EX_NOPARENTNOTIFY|WS_EX_CONTROLPARENT,klass.lpszClassName,ui_tr(L"프레임 생성 도구",L"Frame generation tool"),WS_CHILD|WS_VISIBLE|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,96,44,ui_parent,NULL,module,NULL);
#ifdef CS_UI_PREVIEW
 ui_popup=CreateWindowExW(WS_EX_APPWINDOW|WS_EX_CONTROLPARENT,klass.lpszClassName,ui_tr(L"프레임 생성 설정 · 설정 미리보기",L"Frame generation settings · preview"),WS_POPUP|WS_CLIPCHILDREN,0,0,900,1200,NULL,NULL,module,NULL);
#else
 ui_popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,klass.lpszClassName,ui_tr(L"프레임 생성 설정",L"Frame generation settings"),WS_POPUP|WS_CLIPCHILDREN,0,0,900,1200,ui_parent,NULL,module,NULL);
#endif
 if(!ui_panel||!ui_popup){logline("UI_CREATE_FAILED win32=%lu",GetLastError());return 0;}
 ui_meter=CreateWindowExW(WS_EX_NOPARENTNOTIFY,klass.lpszClassName,ui_tr(L"프레임 표시",L"FPS display"),WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,ui_parent,NULL,module,NULL);
 ui_child(ui_meter,L"STATIC",L"— / — fps",SS_LEFT|SS_CENTERIMAGE,147);
 ui_full_meter=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_TRANSPARENT,klass.lpszClassName,ui_tr(L"프레임 표시 · 전체 화면",L"FPS display · full screen"),WS_POPUP|WS_CLIPCHILDREN,0,0,1,1,NULL,NULL,module,NULL);
 WNDCLASSW label={0};label.lpfnWndProc=ui_label_proc;label.hInstance=module;label.lpszClassName=L"ConsoleSmooth.Label";label.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);RegisterClassW(&label);
 ui_child(ui_popup,L"STATIC",ui_tr(L"프레임 표시",L"FPS display"),0,145);ui_child(ui_popup,L"BUTTON",ui_tr(L"왼쪽 위  ▾",L"Top left  ▾"),WS_TABSTOP|BS_OWNERDRAW,165);
 ui_child(ui_popup,ui_label_class(146),ui_tr(L"입력 신호 대기 · 생성 0.0 fps · 반복 0.0 fps\r\n생성: 새 프레임 · 반복: 이전 화면 유지",L"Waiting for input · generated 0.0 fps · repeated 0.0 fps\r\ngenerated: new frame · repeated: previous picture kept"),0,146);ui_child(ui_popup,ui_label_class(183),ui_tr(L"화면 숫자: 원본 / 지금 fps · 원본 = 게임이 1초에 바꾼 화면 · 지금 = 원본 + 생성한 장",L"Readout: original / now fps · original = game pictures/s · now = original + generated"),0,183);
 DWORD button=WS_TABSTOP|BS_OWNERDRAW;
 ui_toggle=ui_child(ui_panel,L"BUTTON",ui_tr(L"프레임 생성 켜기",L"Turn frame generation on"),button,101);ui_settings=ui_child(ui_panel,L"BUTTON",ui_tr(L"프레임 생성 설정",L"Frame generation settings"),button,102);
 struct {int id;const wchar_t *text;} labels[]={{120,ui_tr(L"프레임 생성",L"Frame generation")},{123,ui_tr(L"프레임 생성",L"Frame generation")},{122,ui_tr(L"모델",L"Model")},{106,ui_tr(L"입력 프레임 · 자동",L"Input fps · auto")},{130,ui_tr(L"입력 신호 대기",L"Waiting for input")},{107,ui_tr(L"출력 방식",L"Output mode")},{129,ui_tr(L"배수",L"Multiplier")},{125,L""},{126,L""},{124,ui_tr(L"지연 설정",L"Latency")},{152,ui_tr(L"생성 버퍼",L"Generation buffer")},{127,L""},{105,L""},{108,L""}};
 for(unsigned i=0;i<sizeof(labels)/sizeof(labels[0]);i++)ui_child(ui_popup,ui_label_class(labels[i].id),labels[i].text,0,labels[i].id);
 struct {int id;const wchar_t *text;} buttons[]={{128,L"RIFE 4.6  ▾"},{111,ui_tr(L"프레임 생성 꺼짐",L"Frame generation off")},{132,ui_tr(L"배수",L"Multiplier")},{133,ui_tr(L"목표 프레임",L"Target fps")},{134,L"2×"},{135,L"3×"},{136,L"4×"},{139,L"−"},{142,L"+"},{140,ui_tr(L"낮은 지연  ▾",L"Low latency  ▾")},{157,ui_tr(L"진단 정보 내보내기",L"Export diagnostics")},{143,ui_tr(L"적용",L"Apply")},{112,ui_tr(L"닫기",L"Close")},
  {174,ui_tr(L"권장 설정",L"Recommended")},{175,ui_tr(L"프레임 생성",L"Frame generation")},{176,ui_tr(L"표시 · 녹화",L"Display · recording")},{177,ui_tr(L"고급",L"Advanced")}};
 for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++)ui_child(ui_popup,L"BUTTON",buttons[i].text,button,buttons[i].id);
 ui_child(ui_popup,L"BUTTON",ui_tr(L"프레임 표시 꺼짐",L"FPS display off"),button,144);
 ui_child(ui_popup,L"STATIC",ui_tr(L"게임 실제 프레임으로 표시",L"Show the game's real frame rate"),0,188);ui_child(ui_popup,L"BUTTON",ui_tr(L"게임 실제 프레임",L"Game frame rate"),button,189);
 ui_child(ui_popup,L"STATIC",ui_tr(L"전체 화면 선명하게 (확대·선명화)",L"Sharpen full screen (upscale)"),0,178);ui_child(ui_popup,L"BUTTON",ui_tr(L"전체 화면 선명하게",L"Sharpen full screen"),button,179);ui_child(ui_popup,L"STATIC",ui_tr(L"RTX Video HDR (NVIDIA RTX, 전체 화면)",L"RTX Video HDR (NVIDIA RTX, full screen)"),0,113);ui_child(ui_popup,L"BUTTON",ui_tr(L"RTX Video HDR",L"RTX Video HDR"),button,114);ui_child(ui_popup,L"STATIC",ui_tr(L"전체 화면을 HDR로 보여 줍니다. NVIDIA 드라이버의 RTX Video HDR을 쓰고, 꺼져 있으면 Windows HDR을 잠깐 켰다가 닫을 때 끕니다. HDR 모니터 필요. 녹화·방송은 그대로입니다.",L"Shows the full screen in HDR with the NVIDIA driver's RTX Video HDR. Turns Windows HDR on while open if it is off. Needs an HDR monitor. Recording and streaming are unchanged."),0,115);ui_child(ui_popup,L"STATIC",ui_tr(L"만든 장 짧게 보이기 (번짐 적게, 끄면 고른 간격)",L"Show generated frames briefly (less smear; off = even spacing)"),0,198);ui_child(ui_popup,L"BUTTON",ui_tr(L"만든 장 짧게 보이기",L"Show generated frames briefly"),button,199);
 ui_child(ui_popup,L"STATIC",ui_tr(L"켜기/끄기 단축키",L"On/off shortcut"),0,195);ui_child(ui_popup,L"BUTTON",L"",button,196);ui_child(ui_popup,L"STATIC",L"",0,197);
 ui_child(ui_popup,L"STATIC",ui_tr(L"화면 동기화 (창·전체 화면)",L"Display sync (window and full screen)"),0,190);ui_child(ui_popup,L"BUTTON",ui_tr(L"수직 동기화",L"V-Sync"),button,191);ui_child(ui_popup,L"BUTTON",ui_tr(L"가변 주사율",L"Variable refresh"),button,192);ui_child(ui_popup,L"BUTTON",ui_tr(L"끔",L"Off"),button,193);
 ui_child(ui_popup,L"STATIC",ui_tr(L"가변 주사율: G-Sync·FreeSync 모니터에서 고르게 (창은 NVIDIA 창 모드 G-Sync)\r\n끔: 바로 표시해 지연이 가장 짧음 · 전체 화면은 찢어질 수 있음",L"Variable refresh: even on G-Sync/FreeSync monitors (window: NVIDIA windowed G-Sync)\r\nOff: shown at once, lowest delay · full screen may tear"),0,194);
 ui_child(ui_popup,L"STATIC",ui_tr(L"저지연 보기",L"Low latency view"),0,185);ui_child(ui_popup,L"BUTTON",ui_tr(L"저지연 보기",L"Low latency view"),button,186);ui_child(ui_popup,L"STATIC",ui_tr(L"켜기 버튼·단축키를 누르면 전체 화면으로 들어온 화면을 바로 보여 줘요.\r\n보는 동안에도 녹화·방송은 그대로 이어져요.",L"With the on button or shortcut, shows the picture in full screen as soon as it arrives.\r\nRecording and streaming go on while it is shown."),0,187);
 WNDCLASSW slider={0};slider.lpfnWndProc=ui_slider_proc;slider.hInstance=module;slider.lpszClassName=L"ConsoleSmooth.Slider";slider.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);RegisterClassW(&slider);
 ui_child(ui_popup,slider.lpszClassName,ui_tr(L"생성 버퍼",L"Generation buffer"),WS_TABSTOP,158);ui_child(ui_popup,ui_label_class(159),L"",0,159);
 ui_child(ui_popup,L"STATIC",ui_tr(L"녹화 소스",L"Recording source"),0,148);ui_child(ui_popup,L"BUTTON",ui_tr(L"원본",L"Original"),button,149);ui_child(ui_popup,L"BUTTON",ui_tr(L"생성 프레임 포함",L"With generated"),button,150);ui_child(ui_popup,ui_label_class(151),L"",0,151);
 ui_child(ui_popup,L"STATIC",ui_tr(L"30fps 보정",L"30fps smoothing"),0,160);ui_child(ui_popup,L"BUTTON",ui_tr(L"30fps 보정",L"30fps smoothing"),button,161);ui_child(ui_popup,L"STATIC",ui_tr(L"HUD 보호",L"HUD protection"),0,162);ui_child(ui_popup,L"BUTTON",ui_tr(L"HUD 보호",L"HUD protection"),button,163);
 ui_child(ui_popup,L"STATIC",ui_tr(L"30fps 게임 전용\r\n더 부드럽게, 지연 약간 증가",L"For 30fps games\r\nSmoother, a little more delay"),0,164);ui_child(ui_popup,L"STATIC",ui_tr(L"체력바 등 고정 화면 떨림 감소\r\n글자 창·웹페이지가 깨지면 끄세요",L"Less shimmer on fixed HUD like health bars\r\nTurn off if text boxes or web pages break"),0,166);
 ui_child(ui_popup,L"STATIC",ui_tr(L"GPU 클럭 유지",L"Keep GPU clock up"),0,167);ui_child(ui_popup,L"BUTTON",ui_tr(L"GPU 클럭 유지",L"Keep GPU clock up"),button,168);
 ui_child(ui_popup,L"STATIC",ui_tr(L"AMD는 기본 켬 · 생성이 모자랄 때 켭니다 · GPU 전력이 조금 늘어납니다",L"On by default for AMD · turn on when generation falls short · more GPU power"),0,169);
 ui_child(ui_popup,L"STATIC",ui_tr(L"지연 우선",L"Latency priority"),0,180);ui_child(ui_popup,L"BUTTON",ui_tr(L"지연 우선",L"Latency priority"),button,181);ui_child(ui_popup,L"STATIC",ui_tr(L"RIFE 전용 · 움직임을 1/4 크기로 계산: 버퍼 조금 짧게, 화질 조금 낮음",L"RIFE only · motion at 1/4 size: slightly less buffer, slightly lower quality"),0,182);
 ui_child(ui_popup,L"STATIC",ui_tr(L"FSR 3 가속",L"FSR 3 acceleration"),0,170);ui_child(ui_popup,L"BUTTON",ui_tr(L"FSR 3 가속",L"FSR 3 acceleration"),button,171);
 ui_child(ui_popup,L"STATIC",ui_tr(L"문제가 있을 때 눌러 저장한 파일을 보내 주세요 · 자동 전송 없음",L"Save a file to send when something goes wrong · nothing is sent automatically"),0,184);
 ui_child(ui_popup,L"STATIC",ui_tr(L"FSR 3 화면은 그대로 · 처리 시간만 줄어듭니다 · 기본 끔",L"Same FSR 3 picture · only shorter processing time · off by default"),0,172);
 ui_child(ui_popup,L"STATIC",ui_tr(L"표시 방식",L"Display"),0,153);ui_child(ui_popup,L"BUTTON",ui_tr(L"창",L"Window"),button,154);ui_child(ui_popup,L"BUTTON",ui_tr(L"전체 화면",L"Full screen"),button,155);ui_child(ui_popup,ui_label_class(156),L"",0,156);
 ui_child(ui_popup,L"EDIT",L"",WS_TABSTOP|ES_NUMBER|ES_CENTER|WS_BORDER,138);SendMessageW(ui_items[138],EM_SETLIMITTEXT,3,0);wchar_t value[20];swprintf(value,20,L"%d",ui_target);SetWindowTextW(ui_items[138],value);
 WNDCLASSW tip={0};tip.lpfnWndProc=ui_tip_proc;tip.hInstance=module;tip.lpszClassName=L"ConsoleSmooth.Tip";tip.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);RegisterClassW(&tip);
 ui_tipwin=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,tip.lpszClassName,L"",WS_POPUP,0,0,1,1,ui_parent,NULL,module,NULL);
 {wchar_t title[64];swprintf(title,64,ui_tr(L"프레임 생성 · %hs",L"Frame generation · %hs"),CS_PLUGIN_VERSION);SetWindowTextW(ui_items[120],title);} /* version in the settings title */
 ui_hot_register();ui_tab_apply();ui_dpi=0;ui_position();SetTimer(ui_panel,1,200,NULL);ui_refresh();logline("UI_ATTACHED pid=%lu style=native-large source=%d target=%d",GetCurrentProcessId(),framegen_fps,framegen_output_fps);
#ifdef CS_UI_PREVIEW
 ui_text(ui_items[120],ui_tr(L"프레임 생성 · 설정 미리보기",L"Frame generation · preview"));ui_menu(1);
#endif
 MSG msg;while(GetMessageW(&msg,NULL,0,0)>0){if(ui_hot_key(&msg))continue;if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE&&ui_expanded){ui_menu(0);SetFocus(ui_settings);continue;}if(!IsDialogMessageW(ui_expanded?ui_popup:ui_panel,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 if(ui_meter_font)DeleteObject(ui_meter_font);DeleteObject(ui_font);DeleteObject(ui_small);DeleteObject(ui_heading);DeleteObject(ui_latin);DeleteObject(ui_brush);DeleteObject(ui_head_brush);DeleteObject(ui_field_brush);DeleteObject(ui_main_brush);DeleteObject(ui_nav_brush);if(ui_font_path[0])RemoveFontResourceExW(ui_font_path,FR_PRIVATE,NULL);logline("UI_STOP");return 0;
}
static void start_ui(void){static LONG started;if(!interactive_ui||InterlockedExchange(&started,1))return;HANDLE thread=CreateThread(NULL,0,ui_main,NULL,0,NULL);if(thread)CloseHandle(thread);}


