/* SPDX-License-Identifier: MIT
 * 1.9.8 (11-10 user, office RX 6600): "권장 설정" measures this PC's GPU with the installed engine and proposes settings.
 * Timings come from the same calls the live path makes (cs_interpolate_bgra at the processing size):
 *  RIFE 4.6 at the low-latency flow size (floor 0: half from FHD) and the slow-GPU size (floor 2: quarter from FHD),
 *  FSR 3 with acceleration. Generation is paused while measuring so the live engine does not share the GPU.
 * Rule (same 80% rule the engine uses to pick quarter flow, with a margin because a back-to-back benchmark keeps
 * clocks higher than live bursts): generated frames per source x time <= 0.8 x source period (1.9.8: 0.75).
 * Office RX 6600 FHD (11-10): RIFE half 19 ms, quarter 12 ms. Ground truth on real frames: RIFE half 24.0 dB,
 * quarter 23.1 dB, FSR 3 20.3 dB (about a blend), so RIFE is preferred whenever it fits. */
#define UI_RECO_DONE (WM_APP+7)
typedef struct {
 int w,h,keep;                 /* processing size, GPU clock keep for the measurement */
 double rate,content;          /* input fps, game fps seen by the engine (0 unknown) */
 int hz,resume;                /* monitor refresh, turn generation back on afterwards */
 double half,quarter,fsr;      /* measured ms, -1 failed */
 char error[160];
 /* 2.0.0 trial: real generation at 2x for a few seconds per model (ms per frame from the live stage times, share of
  * in-between frames generated). The settings the user had are put back before the result opens. */
 int trial;double rife_ms,rife_gen,fsr_ms,fsr_gen;LONG saved_bits;int saved_mode,saved_mult,saved_target;
} UiReco;
static UiReco ui_reco;static volatile LONG ui_reco_busy;
static double ui_reco_time(void *e,const uint8_t *a,const uint8_t *b,int w,int h,uint8_t *out,
 int (*run)(void*,const uint8_t*,const uint8_t*,int,int,float,uint8_t*,uint64_t,uint64_t),double (*last)(void*),uint64_t *id){
 /* First runs load the network at this size and warm the clocks.
  * 2.0.0 (11-10, RTX 5080 locked at 900 MHz): like the live path, each pair reuses the previous source (ids k, k+1,
  * frames alternate), and the estimate is the lower quartile of 16 runs instead of the mean of 6. Inside Streaming
  * Center the preview shares the GPU, so single synchronous calls spike: the 1.9.9 timing gave RIFE quarter 13.2 ms
  * while live generation at the same clock took 9.4 ms per frame (98.7% generated). */
 double t[16];int n=0;
 for(int i=0;i<20;i++){const uint8_t *x=i&1?b:a,*y=i&1?a:b;if(run(e,x,y,w,h,.5f,out,*id,*id+1))return -1;*id+=1;if(i>=4)t[n++]=last(e);}
 for(int i=1;i<n;i++){double v=t[i];int j=i-1;while(j>=0&&t[j]>v){t[j+1]=t[j];--j;}t[j+1]=v;}
 return n?t[n/4]:-1;
}
static DWORD WINAPI ui_reco_worker(void *unused){
 (void)unused;UiReco *r=&ui_reco;r->half=r->quarter=r->fsr=-1;r->error[0]=0;
 if(r->resume)Sleep(1500); /* generation was just turned off: let the live engine stop */
 wchar_t dir[32768],dll[32768],model[32768];DWORD n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32000);
 HMODULE m=NULL;
 if(n&&n<32000){vulkan_prepare(dir);swprintf(dll,32768,L"%ls\\consolesmooth_engine.dll",dir);m=LoadLibraryExW(dll,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);}
 void *(*create)(const wchar_t*,int)=NULL;void (*destroy)(void*)=NULL;void (*floor_set)(void*,int)=NULL;void (*options)(int)=NULL;double (*last)(void*)=NULL;
 int (*run)(void*,const uint8_t*,const uint8_t*,int,int,float,uint8_t*,uint64_t,uint64_t)=NULL;const char *(*err)(void)=NULL;
 if(m){*(FARPROC*)&create=GetProcAddress(m,"cs_create");*(FARPROC*)&destroy=GetProcAddress(m,"cs_destroy");*(FARPROC*)&floor_set=GetProcAddress(m,"cs_set_flow_floor");
  *(FARPROC*)&options=GetProcAddress(m,"cs_set_gpu_options");*(FARPROC*)&last=GetProcAddress(m,"cs_last_ms");*(FARPROC*)&run=GetProcAddress(m,"cs_interpolate_bgra");*(FARPROC*)&err=GetProcAddress(m,"cs_last_error");}
 size_t bytes=(size_t)r->w*r->h*4;uint8_t *a=malloc(bytes),*b=malloc(bytes),*out=malloc(bytes);
 if(!create||!destroy||!floor_set||!options||!last||!run||!a||!b||!out)snprintf(r->error,sizeof r->error,"engine not available");
 else{
  /* A moving gradient: timing does not depend on the picture, but keep both frames different. */
  for(int y=0;y<r->h;y++)for(int x=0;x<r->w;x++){size_t p=((size_t)y*r->w+x)*4;a[p]=(uint8_t)x;a[p+1]=(uint8_t)y;a[p+2]=(uint8_t)(x+y);a[p+3]=255;b[p]=(uint8_t)(x+8);b[p+1]=(uint8_t)(y+4);b[p+2]=(uint8_t)(x+y+12);b[p+3]=255;}
  uint64_t id=1000;options((r->keep?1:0)|4); /* bit 0 clock keep, bit 2 FSR 3 acceleration (same bits as the live path) */
  swprintf(model,32768,L"%ls\\%ls",dir,cs_models[0].directory);void *e=create(model,-1);
  if(e){floor_set(e,0);r->half=ui_reco_time(e,a,b,r->w,r->h,out,run,last,&id);floor_set(e,2);r->quarter=ui_reco_time(e,a,b,r->w,r->h,out,run,last,&id);destroy(e);}
  else snprintf(r->error,sizeof r->error,"RIFE: %s",err?err():"?");
  for(int i=0;i<CS_MODEL_COUNT;i++)if(cs_models[i].family==3){swprintf(model,32768,L"%ls\\%ls",dir,cs_models[i].directory);e=create(model,-1);
   if(e){r->fsr=ui_reco_time(e,a,b,r->w,r->h,out,run,last,&id);destroy(e);}break;}
  options((int)(InterlockedCompareExchange(&engine_options,0,0)>>2)&7);
 }
 free(a);free(b);free(out);
 logline("UI_RECO_MEASURED size=%dx%d keep=%d rife_half_ms=%.2f rife_quarter_ms=%.2f fsr3_ms=%.2f error=%s",r->w,r->h,r->keep,r->half,r->quarter,r->fsr,r->error[0]?r->error:"none");
 PostMessageW(ui_popup,UI_RECO_DONE,0,0);return 0;
}
/* 2.0.0 (11-10, RTX 5080 locked at 900 MHz): timing single calls inside Streaming Center read RIFE quarter flow at
 * 13.2-15.3 ms while live generation at the same clock took 9.4 ms per frame and made 98.7%, so the button chose FSR 3
 * where RIFE works. With a capture signal the button now runs the live path: RIFE 4.6 then FSR 3 at 2x, low latency,
 * 25 ms buffer, about 11 s each (warm-up 7 s, then 3 s counted). The live engine picks half or quarter flow itself. */
static void reco_bits(LONG bits){
 LONG prior;do{prior=ui_requested();}while(InterlockedCompareExchange(&control_bits,bits,prior)!=prior);
 if((prior^bits)&1){LARGE_INTEGER now;QueryPerformanceCounter(&now);InterlockedExchange64(&toggle_qpc,now.QuadPart);LONG serial=InterlockedIncrement(&toggle_serial);
  logline("UI_TOGGLE enabled=%d serial=%ld qpc=%lld reason=reco",(int)(bits&1),serial,(long long)now.QuadPart);}
}
static CsStats reco_stats(void){CsStats s;AcquireSRWLockShared(&reco_lock);s=reco_cs;ReleaseSRWLockShared(&reco_lock);return s;}
static void reco_try(int model,double *ms,double *gen){
 *ms=-1;*gen=0;
 reco_bits((ui_requested()&~(4|(255<<8)|(255<<16)))|1|(ui_buffer_bits(0,ui_saved_buffer,25)<<8)|(model<<16));
 /* Wait for the engine of this model (new stream: counters restart), up to 10 s. */
 Sleep(500); /* let the streaming thread see the new model before reading readiness */
 AcquireSRWLockExclusive(&reco_lock);reco_group=NULL;memset(&reco_cs,0,sizeof reco_cs);ReleaseSRWLockExclusive(&reco_lock);
 AcquireSRWLockExclusive(&stage_lock);ui_stage_valid=0;ui_gpu_info[0]=0;ReleaseSRWLockExclusive(&stage_lock);
 ULONGLONG start=GetTickCount64();CsStats s=reco_stats();
 while(GetTickCount64()-start<10000&&!engine_load_reason()[0]){Sleep(100);s=reco_stats();if(s.ready==1&&s.generated+s.repeated>0)break;}
 if(s.ready!=1){
  /* 1.1.4 (11-21): say why in the log and on the failure page (9070 XT report had no reason). */
  char g[512];AcquireSRWLockShared(&stage_lock);memcpy(g,ui_gpu_info,sizeof g);ReleaseSRWLockShared(&stage_lock);
  const char* load=engine_load_reason();const char* why=load[0]?load:!strncmp(g,"engine_error=",13)?g+13:"engine did not start in 10 s";
  logline("UI_RECO_TRIAL model=%s failed ready=%d reason=%s",cs_models[model].name,s.ready,why);
  size_t n=strlen(ui_reco.error);snprintf(ui_reco.error+n,sizeof ui_reco.error-n,"%s%s: %s",n?" / ":"",cs_models[model].name,why);return;}
 /* Live RIFE on the 5080 ran 6.2 ms per frame for the first ~5 s and 4.4 ms from ~10 s (11-10): warm up 7 s. */
 Sleep(7000);CsStats a=reco_stats();Sleep(3000);CsStats b=reco_stats();
 StageStats st;int valid;AcquireSRWLockShared(&stage_lock);st=ui_stage;valid=ui_stage_valid;ReleaseSRWLockShared(&stage_lock);
 uint64_t g=b.generated>=a.generated?b.generated-a.generated:0,rp=b.repeated>=a.repeated?b.repeated-a.repeated:0;
 if(g+rp)*gen=(double)g/(g+rp);if(valid&&st.total_ms>0)*ms=st.total_ms;
 logline("UI_RECO_TRIAL model=%s generated=%llu repeated=%llu share=%.3f total_ms=%.2f gpu_ms=%.2f samples=%llu",cs_models[model].name,(unsigned long long)g,(unsigned long long)rp,*gen,*ms,valid?st.gpu_ms:-1,(unsigned long long)(valid?st.samples:0));
}
static DWORD WINAPI ui_reco_trial_worker(void *unused){
 (void)unused;UiReco *r=&ui_reco;
 AcquireSRWLockExclusive(&auto_lock);auto_mode=0;auto_mult=2;ReleaseSRWLockExclusive(&auto_lock);
 InterlockedExchange(&reco_trial,1);AcquireSRWLockExclusive(&stage_lock);ui_stage_valid=0;ReleaseSRWLockExclusive(&stage_lock);
 reco_try(0,&r->rife_ms,&r->rife_gen);
 AcquireSRWLockExclusive(&stage_lock);ui_stage_valid=0;ReleaseSRWLockExclusive(&stage_lock);
 for(int i=0;i<CS_MODEL_COUNT;i++)if(cs_models[i].family==3&&cs_model_valid(i)){reco_try(i,&r->fsr_ms,&r->fsr_gen);break;}
 InterlockedExchange(&reco_trial,0);
 /* The trial generated, so the engine has just measured the game fps (30fps smoothing choice). */
 if(GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_content_at,0,0)<3000)r->content=InterlockedCompareExchange(&auto_content_x100,0,0)/100.0;
 /* Put the user's choice back exactly (on/off, model, buffer, latency mode, rate). */
 AcquireSRWLockExclusive(&auto_lock);auto_mode=r->saved_mode;auto_mult=r->saved_mult;auto_target=r->saved_target;ReleaseSRWLockExclusive(&auto_lock);
 reco_bits(r->saved_bits);
 r->half=r->rife_ms;r->quarter=-1;r->fsr=r->fsr_ms; /* for the failure check in ui_reco_finish */
 PostMessageW(ui_popup,UI_RECO_DONE,0,0);return 0;
}
static int ui_primary_vendor(int vendor){DISPLAY_DEVICEW d;d.cb=sizeof d;wchar_t key[16];swprintf(key,16,L"VEN_%04X",vendor);
 for(DWORD i=0;EnumDisplayDevicesW(NULL,i,&d,0);i++,d.cb=sizeof d)if(d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE)return wcsstr(d.DeviceID,key)!=NULL;return 0;}
/* Button: gather the situation on the UI thread, pause generation, measure on a worker. */
static void ui_reco_start(void){
 if(InterlockedCompareExchange(&ui_reco_busy,1,0))return;
 AutoRate d=auto_snapshot();int w,h;AcquireSRWLockShared(&status_lock);w=ui_width;h=ui_height;ReleaseSRWLockShared(&status_lock);
 UiReco *r=&ui_reco;memset(r,0,sizeof *r);
 /* Processing size follows the recording resolution when the engine has run; otherwise the capture, else FHD. */
 r->w=w>0&&h>0?w:d.live&&d.width>0?d.width:1920;r->h=w>0&&h>0?h:d.live&&d.height>0?d.height:1080;
 r->rate=d.live&&d.sn>0&&d.sd>0?(double)d.sn/d.sd:60;r->hz=ui_monitor_hz();
 r->content=GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_content_at,0,0)<3000?InterlockedCompareExchange(&auto_content_x100,0,0)/100.0:0;
 r->keep=ui_primary_vendor(0x1002)||ui_primary_vendor(0x8086); /* AMD, Intel: clocks drop between bursts (11-08/11-10) */
 /* 2.0.0: with a live capture signal, try real generation (see ui_reco_trial_worker); the timing worker stays for no signal. */
 r->trial=d.live&&d.sn>0&&d.sd>0;
 if(r->trial){
  r->saved_bits=ui_requested();AcquireSRWLockShared(&auto_lock);r->saved_mode=auto_mode;r->saved_mult=auto_mult;r->saved_target=auto_target;ReleaseSRWLockShared(&auto_lock);
  EnableWindow(ui_items[174],FALSE);SetWindowTextW(ui_items[174],ui_tr(L"시험 생성 중…",L"Testing…"));
  logline("UI_RECO_START size=%dx%d rate=%.2f content=%.2f hz=%d keep=%d trial=1 enabled=%d",r->w,r->h,r->rate,r->content,r->hz,r->keep,(int)(r->saved_bits&1));
  HANDLE t=CreateThread(NULL,0,ui_reco_trial_worker,NULL,0,NULL);if(t)CloseHandle(t);else{InterlockedExchange(&ui_reco_busy,0);EnableWindow(ui_items[174],TRUE);SetWindowTextW(ui_items[174],ui_tr(L"권장 설정",L"Recommended"));}
  return;
 }
 r->resume=!!(ui_requested()&1);
 if(r->resume){LARGE_INTEGER now;QueryPerformanceCounter(&now);InterlockedExchange64(&toggle_qpc,now.QuadPart);InterlockedXor(&control_bits,1);LONG serial=InterlockedIncrement(&toggle_serial);logline("UI_TOGGLE enabled=0 serial=%ld qpc=%lld reason=reco",serial,(long long)now.QuadPart);}
 EnableWindow(ui_items[174],FALSE);SetWindowTextW(ui_items[174],ui_tr(L"측정 중…",L"Measuring…"));
 logline("UI_RECO_START size=%dx%d rate=%.2f content=%.2f hz=%d keep=%d resume=%d",r->w,r->h,r->rate,r->content,r->hz,r->keep,r->resume);
 HANDLE t=CreateThread(NULL,0,ui_reco_worker,NULL,0,NULL);if(t)CloseHandle(t);else{InterlockedExchange(&ui_reco_busy,0);EnableWindow(ui_items[174],TRUE);SetWindowTextW(ui_items[174],ui_tr(L"권장 설정",L"Recommended"));}
}
/* 1.9.9 (11-10 user): the result opens in the settings window's own style instead of a Windows message box.
 * Two cards (measured times, recommended settings), muted notes, Apply/Cancel drawn by ui_draw_button. Modal to
 * the settings window; Esc cancels; drag by the header. Sizes in 96-dpi units through ui_px. */
#define UI_RV_W 600
typedef struct {wchar_t title[64],head1[96],head2[64],l1[4][32],v1[4][96],l2[8][32],v2[8][96],notes[1024];int n1,n2,apply;} UiRecoView;
static UiRecoView ui_rv;static int ui_rv_result,ui_rv_open,ui_rv_notes_h;
static int ui_rv_card1(void){return 72;}
static int ui_rv_card2(void){return ui_rv_card1()+44+ui_rv.n1*30+12;}
static int ui_rv_notes(void){return ui_rv_card2()+44+ui_rv.n2*30+14;}
static int ui_rv_buttons(void){return ui_rv_notes()+(ui_rv.notes[0]?MulDiv(ui_rv_notes_h,96,ui_dpi?ui_dpi:96)+22:2);}
static void ui_rv_card(HDC dc,int top,const wchar_t *head,int n,wchar_t (*l)[32],wchar_t (*v)[96]){
 int bottom=top+44+n*30;HBRUSH b=CreateSolidBrush(UI_BG),ob=SelectObject(dc,b);HGDIOBJ op=SelectObject(dc,GetStockObject(NULL_PEN));
 RoundRect(dc,ui_px(20),ui_px(top),ui_px(UI_RV_W-20),ui_px(bottom),ui_px(22),ui_px(22));SelectObject(dc,ob);SelectObject(dc,op);DeleteObject(b);
 HGDIOBJ of=SelectObject(dc,ui_heading);SetTextColor(dc,UI_TEXT);RECT r={ui_px(38),ui_px(top+8),ui_px(UI_RV_W-38),ui_px(top+40)};DrawTextW(dc,head,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
 SelectObject(dc,ui_font);
 for(int i=0;i<n;i++){RECT a={ui_px(38),ui_px(top+42+i*30),ui_px(220),ui_px(top+42+(i+1)*30)},c={ui_px(220),a.top,ui_px(UI_RV_W-38),a.bottom};
  SetTextColor(dc,UI_MUTED);DrawTextW(dc,l[i],-1,&a,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
  SetTextColor(dc,UI_TEXT);DrawTextW(dc,v[i],-1,&c,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX|DT_END_ELLIPSIS);}
 SelectObject(dc,of);
}
static LRESULT CALLBACK ui_rv_proc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 switch(m){
 case WM_DRAWITEM:ui_draw_button((DRAWITEMSTRUCT*)lp);return TRUE;
 case WM_COMMAND:if(HIWORD(wp)==BN_CLICKED&&(LOWORD(wp)==801||LOWORD(wp)==802)){ui_rv_result=LOWORD(wp)==801&&ui_rv.apply;DestroyWindow(w);}return 0;
 case WM_CLOSE:ui_rv_result=0;DestroyWindow(w);return 0;
 case WM_DESTROY:ui_rv_open=0;return 0;
 case WM_NCHITTEST:{POINT p={(short)LOWORD(lp),(short)HIWORD(lp)};ScreenToClient(w,&p);if(p.y<ui_px(56))return HTCAPTION;break;}
 case WM_ERASEBKGND:return 1;
 case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;GetClientRect(w,&r);ui_fill(dc,r,RGB(38,38,38));RECT head={0,0,r.right,ui_px(56)};ui_fill(dc,head,UI_TOOLBAR);
  SetBkMode(dc,TRANSPARENT);HGDIOBJ of=SelectObject(dc,ui_heading);SetTextColor(dc,UI_TEXT);RECT t={ui_px(24),0,r.right,ui_px(56)};DrawTextW(dc,ui_rv.title,-1,&t,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);SelectObject(dc,of);
  if(ui_rv.n1)ui_rv_card(dc,ui_rv_card1(),ui_rv.head1,ui_rv.n1,ui_rv.l1,ui_rv.v1);
  if(ui_rv.n2)ui_rv_card(dc,ui_rv_card2(),ui_rv.head2,ui_rv.n2,ui_rv.l2,ui_rv.v2);
  if(ui_rv.notes[0]){of=SelectObject(dc,ui_small);SetTextColor(dc,UI_MUTED);RECT n={ui_px(28),ui_px(ui_rv_notes()),ui_px(UI_RV_W-28),ui_px(ui_rv_notes())+ui_rv_notes_h};DrawTextW(dc,ui_rv.notes,-1,&n,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);SelectObject(dc,of);}
  HBRUSH b=CreateSolidBrush(UI_LINE);FrameRect(dc,&r,b);DeleteObject(b);EndPaint(w,&ps);return 0;}
 }return DefWindowProcW(w,m,wp,lp);
}
/* Returns 1 when the user chose Apply. */
static int ui_rv_show(void){
 static int registered;HINSTANCE module=GetModuleHandleW(NULL);
 if(!registered){WNDCLASSW k={0};k.lpfnWndProc=ui_rv_proc;k.hInstance=module;k.lpszClassName=L"ConsoleSmooth.Recommend";k.hCursor=LoadCursorW(NULL,(LPCWSTR)IDC_ARROW);RegisterClassW(&k);registered=1;}
 /* Notes height: word-wrapped at the dialog width in the small font. */
 ui_rv_notes_h=0;if(ui_rv.notes[0]){HDC dc=GetDC(ui_popup);HGDIOBJ of=SelectObject(dc,ui_small);RECT n={0,0,ui_px(UI_RV_W-56),0};DrawTextW(dc,ui_rv.notes,-1,&n,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX|DT_CALCRECT);SelectObject(dc,of);ReleaseDC(ui_popup,dc);ui_rv_notes_h=n.bottom+ui_px(4);}
 int by=ui_rv_buttons(),h=ui_px(by+42+20),w=ui_px(UI_RV_W);RECT o;GetWindowRect(ui_popup,&o);int x=(o.left+o.right-w)/2,y=(o.top+o.bottom-h)/2;
 HWND d=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_CONTROLPARENT,L"ConsoleSmooth.Recommend",ui_rv.title,WS_POPUP|WS_CLIPCHILDREN,x,y,w,h,ui_popup,NULL,module,NULL);
 if(!d)return 0;
 HWND ok=NULL,cancel=CreateWindowExW(0,L"BUTTON",ui_rv.apply?ui_tr(L"취소",L"Cancel"):ui_tr(L"닫기",L"Close"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,ui_px(UI_RV_W-20-142),ui_px(by),ui_px(142),ui_px(42),d,(HMENU)(INT_PTR)802,module,NULL);
 if(ui_rv.apply)ok=CreateWindowExW(0,L"BUTTON",ui_tr(L"적용",L"Apply"),WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,ui_px(UI_RV_W-20-142-12-142),ui_px(by),ui_px(142),ui_px(42),d,(HMENU)(INT_PTR)801,module,NULL);
 if(ok)SendMessageW(ok,WM_SETFONT,(WPARAM)ui_font,FALSE);SendMessageW(cancel,WM_SETFONT,(WPARAM)ui_font,FALSE);
 ui_rv_result=0;ui_rv_open=1;EnableWindow(ui_popup,FALSE);ShowWindow(d,SW_SHOW);SetForegroundWindow(d);SetFocus(ok?ok:cancel);
 MSG msg;int quit=0;
 while(ui_rv_open){int got=GetMessageW(&msg,NULL,0,0);if(got<=0){quit=1;break;}
  if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){ui_rv_result=0;DestroyWindow(d);continue;}
  if(!IsDialogMessageW(d,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
 EnableWindow(ui_popup,TRUE);if(IsWindow(d))DestroyWindow(d);SetForegroundWindow(ui_popup);if(quit)PostQuitMessage(0);
 return ui_rv_result;
}
/* 2.2.4 (11-14 user): the recommendation names the GPU and checks the monitor. */
static void ui_gpu_name(wchar_t *out,int cap){DISPLAY_DEVICEW d;d.cb=sizeof d;out[0]=0;
 for(DWORD i=0;EnumDisplayDevicesW(NULL,i,&d,0);i++,d.cb=sizeof d)if(d.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE){wcsncpy(out,d.DeviceString,cap-1);out[cap-1]=0;return;}}
/* Variable refresh from the primary monitor's EDID (registry copy): AMD FreeSync vendor block in a CTA extension, a DisplayID
 * adaptive-sync block, a "range limits only" descriptor spanning 30 Hz or more, or a wide range reaching 100 Hz+ (estimate).
 * 1 yes, 0 no, -1 unknown. lo/hi: the vertical range from the range descriptor (0 when absent). */
static int ui_monitor_vrr(int *lo,int *hi){
 *lo=*hi=0;DISPLAY_DEVICEW a,m;a.cb=sizeof a;m.cb=sizeof m;int found=0;
 for(DWORD i=0;EnumDisplayDevicesW(NULL,i,&a,0);i++,a.cb=sizeof a)if(a.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE){found=EnumDisplayDevicesW(a.DeviceName,0,&m,EDD_GET_DEVICE_INTERFACE_NAME);break;}
 if(!found)return -1;
 wchar_t *p=wcsstr(m.DeviceID,L"DISPLAY#");if(!p)return -1;p+=8;wchar_t *h1=wcschr(p,L'#'),*h2=h1?wcschr(h1+1,L'#'):NULL;if(!h2||h1-p>60||h2-h1>120)return -1;
 wchar_t model[64],inst[128],key[400];wcsncpy(model,p,h1-p);model[h1-p]=0;wcsncpy(inst,h1+1,h2-h1-1);inst[h2-h1-1]=0;
 swprintf(key,400,L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\%ls\\%ls\\Device Parameters",model,inst);
 BYTE e[2048];DWORD n=sizeof e;if(RegGetValueW(HKEY_LOCAL_MACHINE,key,L"EDID",RRF_RT_REG_BINARY,NULL,e,&n)!=ERROR_SUCCESS||n<128)return -1;
 int vrr=0;
 for(int d=54;d<=108;d+=18)if(!e[d]&&!e[d+1]&&!e[d+2]&&e[d+3]==0xFD){
  int mn=e[d+5]+((e[d+4]&3)==3?255:0),mx=e[d+6]+((e[d+4]&2)?255:0);*lo=mn;*hi=mx;
  /* 11-14: the user's MSI 4K 160 Hz (EDID 1.3) reports 48-160 with no VRR flag; fixed monitors report narrow ranges (LG 56-75). */
  if((e[d+10]==1&&mx-mn>=30)||(mx>=100&&mx-mn>=60))vrr=1;}
 for(DWORD b=1;b<=e[126]&&(b+1)*128<=n;b++){BYTE *x=e+b*128;
  if(x[0]==0x02){int end=x[2]>4&&x[2]<128?x[2]:4;for(int i=4;i<end;){int tag=x[i]>>5,len=x[i]&31;if(tag==3&&len>=3&&x[i+1]==0x1A&&x[i+2]==0&&x[i+3]==0)vrr=1;i+=len+1;}}
  else if(x[0]==0x70){for(int i=5;i+2<127;){int tag=x[i],len=x[i+2];if(!tag&&!len)break;if(tag==0x2B)vrr=1;i+=len+3;}}}
 logline("UI_MONITOR_VRR model=%ls range=%d-%d vrr=%d",model,*lo,*hi,vrr);return vrr;
}
/* Result: pick the settings, show them with the measured times, apply on Yes. */
static void ui_reco_finish(void){
 /* 1.9.9: 0.8 like the engine's own quarter-flow rule. 1.9.8 used 0.75 and on the office RX 6600 (60 fps in, quarter
  * 11.8-13.4 ms over runs while SC was generating) flipped between RIFE and FSR 3; live RIFE 2x there is 99.8% (1.9.5). */
 UiReco *r=&ui_reco;double period=1000.0/r->rate,budget=.8*period;int src=(int)(r->rate+.5),fhd=(int64_t)r->w*r->h>=1920LL*1080;
 double rife=r->quarter>0?r->quarter:r->half; /* the engine drops to quarter flow on its own when half does not fit */
 double fsr=r->fsr;
 /* 2.0.0 trial: a model counts when it generated 95% or more at 2x; its time is the live time per frame. */
 if(r->trial){rife=r->rife_gen>=.95&&r->rife_ms>0?r->rife_ms:-1;fsr=r->fsr_gen>=.95&&r->fsr_ms>0?r->fsr_ms:-1;}
 /* 2.2.4 (11-14 class test, RTX 5080 clock-locked to Time Spy classes, Streaming Center 1.9.19.1, live 2x): RIFE per frame
  * 10.0 ms (4080 class) ... 13.9 (5060), 14.6 (2080/4060, made it but at the edge), 19.0 -> 68% (2070/3060), 23.4 -> 6% (2060);
  * FSR 3 100% at every class (4.8-6.8 ms). The trial scene can be light, so RIFE also needs headroom: per frame at most
  * 0.85 x the source frame gap (14.2 ms at 60 fps). Without FSR 3 the button keeps RIFE. */
 int rifeTight=0;if(r->trial&&rife>0&&rife>.85*period&&fsr>0){rifeTight=1;rife=-1;}
 int vlo=0,vhi=0,vrr=ui_monitor_vrr(&vlo,&vhi),tear=direct_tearing_supported();wchar_t gpu[128];ui_gpu_name(gpu,128);
 int model=0,mode=0,mult=2,target=0,ok=1,quarterUsed=0;double cost=-1;
 if(rife>0&&(r->trial||rife<=budget)){
  /* Highest output the GPU sustains that the monitor shows evenly. 60-multiple monitors: a multiplier dividing the
   * refresh; other monitors (144/165 Hz): a target equal to the refresh halved into range (see ui_monitor_hint). */
  int best=0;
  /* 2.2.4: on a variable-refresh monitor any output up to the refresh is even, so the largest multiplier that fits wins. */
  if(vrr==1&&r->hz>0){for(int k=4;k>=2&&!best;k--)if(k*src<=240&&k*src<=r->hz&&((k==2&&r->trial)||(k-1)*rife<=budget))best=k;mult=best?best:2;}
  else{
  if(r->hz>0&&r->hz%60){int t=r->hz;while(t>4*src)t/=2;if(t>src&&((double)t/src-1)*rife<=budget){mode=1;target=t;}}
  if(!mode){for(int k=4;k>=2&&!best;k--)if(k*src<=240&&((k==2&&r->trial)||(k-1)*rife<=budget)&&(r->hz<=0||(k*src<=r->hz&&r->hz%(k*src)==0)))best=k;mult=best?best:2;}
  }
  double gens=mode?(double)target/src-1:mult-1;if(r->trial)cost=rife;else{quarterUsed=!(r->half>0&&gens*r->half<=.8*period);cost=quarterUsed?r->quarter:r->half;}
 }else if(fsr>0&&(r->trial||fsr<=budget)){model=5;cost=fsr;}
 else ok=0;
 if(!cs_model_valid(model))model=0;
 /* Low-latency buffer (FHD and below, 50+ fps in, 2x): half an output tick + 1.3 x time + 4 ms, as the engine sizes it. */
 int out=mode?target:src*mult,eligible=(int64_t)r->w*r->h<=1920LL*1080&&src>=50&&out==2*src,low=25;
 /* 2.2.4 (friend test 11-14, 4070 Ti: 15 ms not recommended): at least 17 ms. */
 /* 2.3.1 (11-18 buffer sweep, RTX 5080, low latency view, CPU path, 15/17/20/25 ms): 99%+ generated needed AMD FG (3.2 ms) 17,
  * DLSS FG (3.8 ms) 20, RIFE 4.6 (7.2 ms) and FSR 3 (7.8 ms) 25 (96% at 20). Originals left the engine 17.5 ms after arrival at
  * 15-20 ms vs 28 ms at 25. Rule: half an output tick + 3 x time + 4 ms, 15-25 ms (the 1.3 x rule picked 19 for RIFE: 96%).
  * With the low latency view in full screen the 2.3.x GPU path does not use this buffer at all. */
 if(eligible&&cost>0){low=(int)ceil(500.0/out+3.0*cost+4);if(low<15)low=15;if(low>25)low=25;}
 /* 2.2.4: RIFE made it but not cleanly (95-99%): stable keeps a longer buffer instead of risking "generation short". */
 int stable=r->trial&&model==0&&r->rife_gen>0&&r->rife_gen<.99,sync=vrr==1?1:0;
 /* 2.2.2 (user 11-11: 30fps smoothing off plays much better; it stays an option): the button no longer turns it on for a 30 fps
  * game; it only says so in the notes. */
 int game30=src>=50&&r->content>=20&&r->content<=40,cadence=0;
 UiRecoView *v=&ui_rv;memset(v,0,sizeof *v);const wchar_t *on=ui_tr(L"켬",L"On"),*off=ui_tr(L"끔",L"Off"),*fail=ui_tr(L"측정 실패",L"Failed");
 wcscpy(v->title,ui_tr(L"권장 설정",L"Recommended settings"));
 swprintf(v->head1,96,ui_tr(L"이 PC 측정 결과 · %d × %d",L"Measured on this PC · %d × %d"),r->w,r->h);
 if(r->trial){
  swprintf(v->head1,96,ui_tr(L"이 PC에서 2배 시험 생성 · %d × %d",L"Test run at 2x on this PC · %d × %d"),r->w,r->h);
  wcscpy(v->l1[0],L"RIFE 4.6");if(r->rife_ms>0)swprintf(v->v1[0],96,ui_tr(L"생성 %.0f%% · 1장 %.1f ms",L"generated %.0f%% · %.1f ms per frame"),100*r->rife_gen,r->rife_ms);else wcscpy(v->v1[0],fail);
  wcscpy(v->l1[1],L"FSR 3");if(r->fsr_ms>0)swprintf(v->v1[1],96,ui_tr(L"생성 %.0f%% · 1장 %.1f ms",L"generated %.0f%% · %.1f ms per frame"),100*r->fsr_gen,r->fsr_ms);else wcscpy(v->v1[1],fail);
 }else{
 wcscpy(v->l1[0],L"RIFE 4.6");if(r->half>0)swprintf(v->v1[0],96,ui_tr(L"%.1f ms · 빠른 계산(1/4) %.1f ms",L"%.1f ms · fast (1/4) %.1f ms"),r->half,r->quarter);else wcscpy(v->v1[0],fail);
 wcscpy(v->l1[1],L"FSR 3");if(r->fsr>0)swprintf(v->v1[1],96,L"%.1f ms",r->fsr);else wcscpy(v->v1[1],fail);
 }
 wcscpy(v->l1[2],ui_tr(L"입력",L"Input"));swprintf(v->v1[2],96,ui_tr(L"%.0f fps · 원본 간격 %.1f ms",L"%.0f fps · %.1f ms per frame"),r->rate,period);
 wcscpy(v->l1[3],ui_tr(L"그래픽카드",L"GPU"));wcsncpy(v->v1[3],gpu[0]?gpu:L"?",95);v->n1=4;
 wcscpy(v->head2,ui_tr(L"권장 설정",L"Recommended"));
 wcscpy(v->l2[0],ui_tr(L"모델",L"Model"));
 if(mode)swprintf(v->v2[0],96,ui_tr(L"%hs · 목표 %d fps",L"%hs · target %d fps"),cs_models[model].name,target);
 else swprintf(v->v2[0],96,ui_tr(L"%hs · %d배 (%d fps)",L"%hs · %dx (%d fps)"),cs_models[model].name,mult,src*mult);
 wcscpy(v->l2[1],ui_tr(L"지연",L"Latency"));if(stable)wcscpy(v->v2[1],ui_tr(L"안정 우선",L"Stable"));else if(eligible)swprintf(v->v2[1],96,ui_tr(L"낮은 지연 · 생성 버퍼 %dms",L"Low latency · buffer %d ms"),low);else wcscpy(v->v2[1],ui_tr(L"낮은 지연",L"Low latency"));
 wcscpy(v->l2[2],ui_tr(L"30fps 보정",L"30fps smoothing"));wcscpy(v->v2[2],cadence?on:off);
 wcscpy(v->l2[3],ui_tr(L"HUD 보호",L"HUD protection"));wcscpy(v->v2[3],on);
 wcscpy(v->l2[4],ui_tr(L"GPU 클럭 유지",L"Keep GPU clock up"));wcscpy(v->v2[4],r->keep?on:off);
 wcscpy(v->l2[5],ui_tr(L"FSR 3 가속",L"FSR 3 acceleration"));wcscpy(v->v2[5],on);
 wcscpy(v->l2[6],ui_tr(L"화면 동기화",L"Display sync"));if(sync&&vhi>vlo)swprintf(v->v2[6],96,ui_tr(L"가변 주사율 (모니터 %d~%d Hz)",L"Variable refresh (monitor %d-%d Hz)"),vlo,vhi);else wcscpy(v->v2[6],sync?ui_tr(L"가변 주사율",L"Variable refresh"):ui_tr(L"수직 동기화",L"V-Sync"));v->n2=7;v->apply=1;
 #define UI_RV_NOTE(k,e) do{if(v->notes[0])wcscat(v->notes,L"\n");wcscat(v->notes,ui_tr(k,e));}while(0)
 if(model==0&&quarterUsed)UI_RV_NOTE(L"· GPU 여유가 적어 움직임을 1/4 크기로 계산합니다(화질 조금 낮음).",L"· Little GPU headroom: motion is estimated at 1/4 size (slightly lower quality).");
 if(model==5&&rifeTight){wchar_t note[200];swprintf(note,200,ui_tr(L"· RIFE는 1장 %.1f ms로 여유가 적어, 움직임이 많은 장면에서 모자랄 수 있어 FSR 3을 고릅니다(RIFE를 쓰려면 모델만 바꾸세요).",L"· RIFE takes %.1f ms per frame, too little headroom for busy scenes, so FSR 3 (switch the model to use RIFE anyway)."),r->rife_ms);if(v->notes[0])wcscat(v->notes,L"\n");wcscat(v->notes,note);}
 else if(model==5)UI_RV_NOTE(L"· RIFE가 제때 못 만들어 FSR 3을 고릅니다. 움직임 주변 잔상이 더 많습니다.",L"· RIFE is too slow here, so FSR 3: more ghosting around motion.");
 if(!ok){if(fhd)UI_RV_NOTE(L"· 이 PC는 생성이 모자랄 수 있습니다. 녹화 해상도를 1920×1080 이하로 낮추세요.",L"· Generation may fall short. Lower the recording resolution to 1920×1080 or less.");else UI_RV_NOTE(L"· 이 PC는 생성이 모자랄 수 있습니다.",L"· Generation may fall short on this PC.");}
 if(game30)UI_RV_NOTE(L"· 30fps 게임이 감지됐습니다. 30fps 보정은 끈 채로 둡니다(필요하면 고급 탭에서 켤 수 있습니다).",L"· A 30 fps game was detected. 30fps smoothing stays off (it can be turned on in the Advanced tab).");
 if(src<50)UI_RV_NOTE(L"· 30fps 입력은 장면 사이 움직임이 커서 캐릭터 주변 잔상이 남을 수 있습니다. 게임·소스를 60fps로 받으면 가장 깨끗합니다.",L"· 30fps input moves twice as far between frames, so edges can ghost. A 60fps source is cleanest.");
 if(stable)UI_RV_NOTE(L"· RIFE 생성이 99%에 조금 못 미쳐 안정 우선을 권합니다(지연은 조금 늘고 생성 부족이 덜 뜹니다).",L"· RIFE generated just under 99%, so Stable is recommended (a little more delay, fewer shortfalls).");
 if(sync)UI_RV_NOTE(L"· 모니터가 가변 주사율(G-Sync·FreeSync)을 지원하는 것으로 보입니다. 그래픽카드 설정에서 켜져 있어야 고르게 보입니다(창 보기는 NVIDIA 창 모드 G-Sync 필요).",L"· The monitor appears to support variable refresh (G-Sync/FreeSync). Turn it on in the GPU settings (window view needs NVIDIA windowed G-Sync).");
 if(sync&&!tear)UI_RV_NOTE(L"· 이 PC는 전체 화면에서 찢어짐 허용을 지원하지 않아 전체 화면은 수직 동기화처럼 동작합니다.",L"· This PC does not allow tearing, so full screen behaves like V-Sync.");
 if(r->hz>0&&out>r->hz)UI_RV_NOTE(L"· 모니터 주사율이 출력보다 낮아 미리보기에서는 생성 프레임이 다 보이지 않습니다(녹화·방송에는 들어갑니다).",L"· The monitor refresh is below the output, so the preview cannot show every generated frame (recording and streaming still get them).");
 if(r->half<0&&r->fsr<0){v->n2=0;v->apply=0;wcscpy(v->title,ui_tr(L"권장 설정 · 측정 실패",L"Recommended settings · failed"));swprintf(v->notes,1024,ui_tr(L"GPU 측정에 실패했습니다. 진단 정보를 내보내 주세요.\n%hs",L"GPU measurement failed. Please export diagnostics.\n%hs"),r->error);ui_rv_show();}
 else{
  if(ui_rv_show()){
   ui_model=model;ui_mode=mode;if(mode)ui_target=target;else ui_mult=mult;ui_stable=stable;ui_low_buffer=low;ui_sync=sync;InterlockedExchange(&direct_sync,ui_sync);ui_save_sync();
   if(mode){wchar_t value[20];swprintf(value,20,L"%d",ui_target);SetWindowTextW(ui_items[138],value);}
   ui_cadence=cadence;ui_hudkeep=1;ui_gpukeep=r->keep;ui_fsrfast=1;ui_save_options();
   logline("UI_RECO_APPLY model=%s mode=%d mult=%d target=%d low_buffer=%d stable=%d sync=%d vrr=%d tearing=%d cadence30=%d gpu_keep=%d quarter=%d ok=%d",cs_models[model].name,mode,mult,target,low,stable,sync,vrr,tear,cadence,r->keep,quarterUsed,ok);
   ui_refresh();SendMessageW(ui_popup,WM_COMMAND,MAKEWPARAM(143,BN_CLICKED),(LPARAM)ui_items[143]);
  }else logline("UI_RECO_DECLINED");
 }
 if(r->resume&&!(ui_requested()&1)){LARGE_INTEGER now;QueryPerformanceCounter(&now);InterlockedExchange64(&toggle_qpc,now.QuadPart);InterlockedXor(&control_bits,1);LONG serial=InterlockedIncrement(&toggle_serial);logline("UI_TOGGLE enabled=1 serial=%ld qpc=%lld reason=reco",serial,(long long)now.QuadPart);}
 if(r->trial)memset(&ui_slow_key,0xFF,sizeof ui_slow_key); /* a slow-GPU stop during the trial must not stick */
 EnableWindow(ui_items[174],TRUE);SetWindowTextW(ui_items[174],ui_tr(L"권장 설정",L"Recommended"));InterlockedExchange(&ui_reco_busy,0);ui_invalidate();
}
