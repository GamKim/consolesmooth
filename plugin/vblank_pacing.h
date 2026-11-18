/* SPDX-License-Identifier: MIT
 * Preview refresh hold. 1.6.4 default is mode 4 (phase lock) while generating (CS_TEST_VBLANK_PACING=0 off). Mode 1 (test): hold each preview buffer at the D3D11 video sink until
 * a fixed point just after a display refresh, so frames reach DWM at the same phase and each is
 * shown for an even number of refreshes. Without it the sink presents on arrival (SyncInterval 0)
 * and capture/display clock jitter gives 1/2/3-refresh patterns (measured: 50% even at 60 on 120 Hz).
 * The slot advances by refresh/content per frame; late frames or growing lead resynchronise.
 */
#include <dwmapi.h>
#include <math.h>
typedef DWM_TIMING_INFO CsDwmTiming;
static HRESULT (WINAPI *dwm_timing)(HWND,CsDwmTiming*);
static int vblank_pacing;
static int no_thumb; /* Test only (CS_TEST_NO_THUMB=1): stop the hidden thumbnail presents, to see if the composed wait shrinks. */
typedef struct {double slot;int valid,long_waits,push,direct,direct_passed;LONG64 frames,waits,resyncs,late,pulls,bins[10];double waited_ms;LONGLONG ref;} PaceState;
static double pace_lead=1.3,pace_latch; /* CS_TEST_PACE_LATCH: latch point in refreshes relative to vblank, e.g. -0.15 */
/* High-resolution waitable timer for the bulk of the wait (Sleep is ~15 ms coarse), then a short spin. */
static void pace_until(LONGLONG target){
 static _Thread_local HANDLE timer;LARGE_INTEGER now,f;QueryPerformanceFrequency(&f);
 if(!timer)timer=CreateWaitableTimerExW(NULL,NULL,0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,TIMER_ALL_ACCESS);
 for(;;){QueryPerformanceCounter(&now);LONGLONG left=target-now.QuadPart;if(left<=0)return;
  double ms=1000.0*left/f.QuadPart;
  if(ms>0.7&&timer){LARGE_INTEGER due;due.QuadPart=-(LONGLONG)((ms-0.5)*10000.0);if(SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)){WaitForSingleObject(timer,INFINITE);continue;}}
  YieldProcessor();}
}
static void pace_free(void *p){HeapFree(GetProcessHeap(),0,p);}
/* 1.9.6: every path that lets the frame through also feeds the full screen window. 11-10 office 60 Hz monitor, 120 fps target:
 * step 0.5 returned before direct_show, so the window stayed on its first frame ("full screen only freezes"). */
static int pace_pass(PaceState *s){return direct_mode&&s->direct==1&&s->direct_passed>=300&&direct_show()?0:1;}
/* Monitor slower than the output (step < 1): a frame within 3/4 refresh of the last one shown cannot reach the screen.
 * Drop it (host sink skipped too) instead of copying 8 MB into the window texture for a present DXGI would refuse anyway. */
static int pace_pass_slow(PaceState *s,double period){
 static LONGLONG lastShown;LARGE_INTEGER now;QueryPerformanceCounter(&now);
 if(!(direct_mode&&s->direct==1&&s->direct_passed>=300&&InterlockedCompareExchange(&direct_state,0,0)==2))return 1;
 if(lastShown&&now.QuadPart-lastShown<(LONGLONG)(period*.75))return 0;
 int r=pace_pass(s);if(!r)lastShown=now.QuadPart;return r;
}
static void tl_mark(int stage,void *buffer);
/* 2.0.0 test only (CS_TEST_TIMELINE): stage 3 = large preview sink, after any pacing wait, right before the host presents. */
static int on_pace_impl(void *pad,void *info,void *data);
static int on_pace(void *pad,void *info,void *data){int r=on_pace_impl(pad,info,data);PaceState *s=data;{static volatile LONG named[3];if(s->direct<3&&!InterlockedExchange(&named[s->direct],1))logline("SINK_THREAD direct=%d tid=%lu",s->direct,GetCurrentThreadId());}if(s->direct==1)tl_mark(r?3:4,probe_buffer(info));return r;} /* 11-18 test: stage 4 = shown by the full screen window (after its Present) */
static int on_pace_impl(void *pad,void *info,void *data){
 (void)info;PaceState *s=data;CsDwmTiming t={.cbSize=sizeof t};
 /* 2.2.0 beta low latency view: no refresh hold; the full screen window presents on arrival, the hidden sinks skip. */
 if(InterlockedCompareExchange(&low_view,0,0)){s->valid=0;s->push=0;if(s->direct&&s->direct_passed<300){++s->direct_passed;return 1;}
  if(s->direct==2&&InterlockedCompareExchange(&direct_state,0,0)==2)return 0;return pace_pass(s);}
 if(!InterlockedCompareExchange(&frame_generating,0,0)){s->valid=0;s->push=0;return 1;}
 /* Test window up: the hidden thumbnail's presents made its swap chain and ours trade the direct flip every few frames. */
 /* Each sink needs its first buffers to preroll: dropping them stopped the pipeline in 5 of 7 runs. */
 if(s->direct&&s->direct_passed<300)++s->direct_passed;
 else if(s->direct==2&&(no_thumb||InterlockedCompareExchange(&direct_state,0,0)==2))return 0;
 /* 2.2.4 (11-14 user): variable refresh or sync off, in the window and in full screen: no refresh hold on the large preview,
  * each frame goes on as it arrives (full screen: our window presents with tearing allowed). */
 if(s->direct==1&&InterlockedCompareExchange(&direct_sync,0,0)){s->valid=0;s->push=0;return pace_pass(s);}
 if(!dwm_timing||dwm_timing(NULL,&t)<0||!t.qpcRefreshPeriod)return pace_pass(s);
 void *caps=current_caps(pad),*st=caps?caps_structure(caps,0):NULL;int fn=0,fd=1;
 if(st)structure_fraction(st,"framerate",&fn,&fd);if(caps)caps_unref(caps);if(fn<=0||fd<=0)return pace_pass(s);
 double period=(double)t.qpcRefreshPeriod,hz=t.rateRefresh.uiDenominator?(double)t.rateRefresh.uiNumerator/t.rateRefresh.uiDenominator:0;if(hz<=0)return pace_pass(s);
 double step=hz*fd/fn; /* refreshes per content frame */
 LARGE_INTEGER now;QueryPerformanceCounter(&now);
 double idx=(double)t.cRefresh+((double)now.QuadPart-(double)t.qpcVBlank)/period;
 if(vblank_pacing==2){
  /* Low latency: present on arrival unless the frame lands in the last part of the refresh before
   * the latch point, where jitter decides between two refreshes; those wait until just past it.
   * Hysteresis keeps a stream that sits near the boundary on one side. Max wait ~ (g+h) refresh. */
  const double g=0.25,h=0.25,h0=0.30;double rel=idx-pace_latch,phase=rel-floor(rel);
  ++s->bins[(int)(phase*10)%10];
  if(!s->push&&phase>=1-g){s->push=1;++s->resyncs;}
  else if(s->push&&phase>=h0&&phase<1-g-h){s->push=0;++s->late;}
  if(s->push&&phase>=1-g-h){
   LONGLONG target=(LONGLONG)((double)t.qpcVBlank+(ceil(rel)+pace_latch-(double)t.cRefresh+0.05)*period);
   if(target>now.QuadPart){LARGE_INTEGER f;QueryPerformanceFrequency(&f);s->waited_ms+=1000.0*(target-now.QuadPart)/f.QuadPart;++s->waits;pace_until(target);}
  }
  if(++s->frames%600==0){char b[64];for(int i=0;i<10;i++)b[i]=(char)('0'+(s->bins[i]*9+299)/600);b[10]=0;
   logline("VBLANK_PACE2 hz=%.3f step=%.3f latch=%.2f waits=%lld avg_wait_ms=%.2f enters=%lld exits=%lld phase_bins=%s",hz,step,pace_latch,(long long)s->waits,s->waits?s->waited_ms/s->waits:0,(long long)s->resyncs,(long long)s->late,b);
   s->waited_ms=0;s->waits=0;memset(s->bins,0,sizeof s->bins);}
  return pace_pass(s);
 }
 if(vblank_pacing==4){
  /* Phase lock: every frame leaves at phase pace_latch (default 0.5, mid-refresh: DWM's own latch
   * after the vblank wanders, and 0.15 lost frames to it) of its own refresh, one slot
   * (step refreshes) after the previous one, so frames never share or skip a refresh. Late frames
   * take the next free slot; a lead above pace_lead (1.3) refreshes for 30 frames gives one slot back. */
  /* 1.8.6: full screen also when the target does not divide the refresh (user decision 11-09). The full screen window is
   * paced with the fractional step (120 fps on 160 Hz: one slot every 1.33 refreshes); presenting on arrival instead skipped
   * about 15% (two frames inside one refresh). The host's own sink keeps the old behaviour (no pacing) when not integer. */
  double st=floor(step+0.5);
  if(st<1||fabs(step-st)>0.02){if(!(step>=1&&direct_mode&&s->direct==1&&s->direct_passed>=300&&InterlockedCompareExchange(&direct_state,0,0)==2))return step<1?pace_pass_slow(s,period):pace_pass(s);st=step;}
  /* Count refreshes from our own reference vblank: cRefresh can disagree with qpcVBlank by one
   * between calls, which looked like a late frame and pushed the slot one refresh further. */
  if(!s->valid)s->ref=t.qpcVBlank;
  double vb=floor(((double)t.qpcVBlank-(double)s->ref)/period+0.5);idx=vb+((double)now.QuadPart-(double)t.qpcVBlank)/period;
  /* The direct window has no DWM latch to avoid: 0.25 was the filmed setting (phase lock 4.3-5.9 ms to display). */
  double ph=pace_latch>0&&pace_latch<1?pace_latch:InterlockedCompareExchange(&direct_state,0,0)==2?0.25:0.5,cand=ceil(idx-ph);
  ++s->bins[(int)((idx-floor(idx))*10)%10];
  if(!s->valid){s->slot=cand;s->valid=1;}
  else{double exp=s->slot+st;
   if(cand>exp){++s->late;s->slot=cand;s->long_waits=0;}
   else if(exp+ph-idx>pace_lead){ /* CS_TEST_PACE_LEAD, default 1.3 */if(++s->long_waits>=30){s->long_waits=0;++s->pulls;exp-=1;}s->slot=exp<cand?cand:exp;}
   else{s->long_waits=0;s->slot=exp;}}
  LONGLONG target=(LONGLONG)((double)t.qpcVBlank+(s->slot+ph-vb)*period);
  s->slot-=vb;s->ref=t.qpcVBlank; /* re-anchor each frame so period rounding never accumulates */
  if(target>now.QuadPart){LARGE_INTEGER f;QueryPerformanceFrequency(&f);s->waited_ms+=1000.0*(target-now.QuadPart)/f.QuadPart;++s->waits;pace_until(target);}
  if(++s->frames%600==0){char b[16];for(int i=0;i<10;i++)b[i]=(char)('0'+(s->bins[i]*9+299)/600);b[10]=0;
   logline("VBLANK_PACE4 hz=%.3f step=%.0f phase=%.2f waits=%lld avg_wait_ms=%.2f late=%lld pulls=%lld arrival_bins=%s",hz,st,ph,(long long)s->waits,s->waits?s->waited_ms/s->waits:0,(long long)s->late,(long long)s->pulls,b);
   s->waited_ms=0;s->waits=0;memset(s->bins,0,sizeof s->bins);}
  return pace_pass(s); /* 0 = GST_PAD_PROBE_DROP: shown in the full screen window */
 }
 double next=s->valid?s->slot+step:floor(idx)+1;
 /* Late by half a refresh: show at the next refresh. More than one frame ahead: drop the backlog. */
 if(idx>floor(next+0.5)+0.5){++s->late;++s->resyncs;next=floor(idx)+1;}
 else if(next-idx>step+1.5){++s->resyncs;next=floor(idx)+1;}
 /* Mode 3: v1 plus a pull-back when the lead stays above ~1.3 refreshes for 30 frames (latency only). */
 else if(vblank_pacing==3&&floor(next+0.5)-idx>pace_lead){if(++s->long_waits>=30){s->long_waits=0;++s->pulls;next-=1;}}
 else s->long_waits=0;
 s->slot=next;s->valid=1;
 LONGLONG target=(LONGLONG)((double)t.qpcVBlank+(floor(next+0.5)-(double)t.cRefresh)*period+0.2*period);
 if(target>now.QuadPart){LARGE_INTEGER f;QueryPerformanceFrequency(&f);s->waited_ms+=1000.0*(target-now.QuadPart)/f.QuadPart;++s->waits;pace_until(target);}
 if(++s->frames%600==0){logline("VBLANK_PACE frames=%lld hz=%.3f step=%.3f waits=%lld avg_wait_ms=%.2f resyncs=%lld late=%lld pulls=%lld",(long long)s->frames,hz,step,(long long)s->waits,s->waits?s->waited_ms/s->waits:0,(long long)s->resyncs,(long long)s->late,(long long)s->pulls);s->waited_ms=0;s->waits=0;}
 return pace_pass(s);
}
static void pace_sink(void *sinkpad){
 if(!vblank_pacing)return;
 if(!dwm_timing){HMODULE m=LoadLibraryW(L"dwmapi.dll");if(m)*(FARPROC*)&dwm_timing=GetProcAddress(m,"DwmGetCompositionTimingInfo");}
 /* SC sets sync=false on its sinks, so each sink presents as soon as this probe returns. */
 logline("VBLANK_PACE_ATTACHED point=preview-sink");
 PaceState *s=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s);if(!s)return;
 /* The root pipeline's sink is the scene thumbnail; the one inside a GstBin is the large preview. */
 {char *path=object_path(sinkpad);s->direct=path&&strstr(path,"/GstBin:")?1:2;
  mem_free(path);} /* Always: the option can be turned on while running. */
 if(!add_probe(sinkpad,1<<4,on_pace,s,pace_free))pace_free(s);
}
