/* SPDX-License-Identifier: MIT
 * Authoritative device input state, separate from capture/output caps.
 * The verified SDK adapter (GC573) is authoritative. Other devices (USB cards such as GC551G2/GC553G2)
 * have no verified input-state API, so their negotiated capture caps stand in, but only while buffers
 * keep arriving from exactly one capture source ("fallback").
 */
/* capped (1.9.6): the device signal is faster than the rate Streaming Center captures at (source settings 30 FPS on a
 * 60 Hz signal); sn/sd then follow the capture, because only those frames reach the plugin. 11-10 office GC573: 30 FPS
 * selected, the readout said 60.00 and the engine planned 60 -> 120 on 30 real frames. */
typedef struct {int sn,sd,tn,td,live,supported,provider,width,height,progressive,fallback,capped,signal_sn,signal_sd;} AutoRate;
/* Changed images per second the engine accepted while generating (x100), and when (GetTickCount64). A 30 fps game in a
 * 60 Hz signal reads about 30 here while the input reads 60. Published by framegen.h. */
static volatile LONG auto_content_x100;static volatile LONG64 auto_content_at;
/* 2.0.7: new pictures shown per second (x100) and when. 2.1.0: changed originals + generated frames used (framegen.h). */
static volatile LONG auto_shown_x100;static volatile LONG64 auto_shown_at;
static SRWLOCK auto_lock=SRWLOCK_INIT;
static int auto_sn,auto_sd=1,auto_mode,auto_mult=2,auto_target=120;
static int auto_force_target,auto_force_td=1; /* CS_TEST_TARGET=<fps> or <n>/<d> (e.g. 143850/1000): target frame rate without the toolbar (test) */
static ULONGLONG auto_last;
/* Exact monitor refresh in mHz (143850 for a 143.85 Hz mode). A whole-number target equal to the rounded
 * refresh runs at this rate: 144 fps on 143.85 Hz measured 94.1% even refreshes, 143.85 fps 97.7%. */
static int auto_display_mhz;
static int auto_provider,auto_width,auto_height,auto_locked,auto_progressive=1;
static volatile LONG auto_stopping;
static int auto_capture_sn,auto_capture_sd,auto_capture_w,auto_capture_h;
static ULONGLONG auto_capture_last;
static volatile LONG auto_capture_count;
/* Set by the UI when the preview delivers fewer frames than the input (GPU too slow):
 * generation stops and the preview returns to the host rate, as with no signal. */
static volatile LONG auto_gpu_slow;
/* Test only (CS_TEST_KEEP_RATE=1): keep the generation rate without signal, to measure 120 fps pacing on the No Signal picture. */
static int keep_rate_without_signal;
static void auto_seen(int n,int d,int w,int h){
 AcquireSRWLockExclusive(&auto_lock);auto_capture_sn=n;auto_capture_sd=d;auto_capture_w=w;auto_capture_h=h;auto_capture_last=GetTickCount64();ReleaseSRWLockExclusive(&auto_lock);
}
/* Only a verified SDK device-state adapter may call this. The adapter must
 * identify the selected physical source and poll/update at least once a second.
 * Unknown SDK builds and ambiguous devices fail closed, never guess. */
static inline void auto_device_report(int locked,int width,int height,int n,int d,int progressive){
 AcquireSRWLockExclusive(&auto_lock);auto_provider=1;auto_locked=locked&&width>0&&height>0&&n>0&&d>0;auto_width=width;auto_height=height;
 auto_sn=n;auto_sd=d>0?d:1;auto_progressive=progressive;auto_last=GetTickCount64();ReleaseSRWLockExclusive(&auto_lock);
}
static inline void auto_device_state(int locked,int width,int height,int n,int d){auto_device_report(locked,width,height,n,d,1);}
static inline void auto_device_lost(void){AcquireSRWLockExclusive(&auto_lock);auto_last=0;auto_provider=0;auto_locked=0;auto_sn=0;ReleaseSRWLockExclusive(&auto_lock);}
static AutoRate auto_snapshot(void){
 AutoRate a={0};AcquireSRWLockShared(&auto_lock);ULONGLONG now=GetTickCount64();a.sn=auto_sn;a.sd=auto_sd;a.provider=auto_provider&&auto_last&&now-auto_last<1500;a.live=a.provider&&auto_locked;a.width=auto_width;a.height=auto_height;
 a.progressive=auto_progressive;
 if(!a.provider&&InterlockedCompareExchange(&auto_capture_count,0,0)==1){
  /* No verified device state: the capture stream's own caps, while buffers are flowing. */
  a.provider=1;a.fallback=1;a.sn=0;a.sd=1;
  if(auto_capture_sn>0&&auto_capture_sd>0&&auto_capture_last&&now-auto_capture_last<1000){a.sn=auto_capture_sn;a.sd=auto_capture_sd;a.width=auto_capture_w;a.height=auto_capture_h;a.progressive=1;a.live=1;}
 }else if(a.live&&a.sn>0&&InterlockedCompareExchange(&auto_capture_count,0,0)==1&&auto_capture_sn>0&&auto_capture_sd>0&&auto_capture_last&&now-auto_capture_last<1000
  &&(int64_t)auto_capture_sn*a.sd<(int64_t)a.sn*auto_capture_sd){a.signal_sn=a.sn;a.signal_sd=a.sd;a.sn=auto_capture_sn;a.sd=auto_capture_sd;a.capped=1;}
 if(synthetic){a.sn=framegen_fps;a.sd=1;a.live=1;a.provider=1;a.progressive=1;}
 a.tn=auto_mode?auto_target:a.sn*auto_mult;a.td=auto_mode?1:a.sd;
 /* 2.0.5: an NTSC input (59.94 = 60000/1001, 29.97) scales a whole-number target by the same 1000/1001 (160 -> 159.84,
  * 120 -> 119.88), as video does. The ratio to the source is then the same as for 60 fps: at 60000/1001 -> 160/1 no
  * original landed on an output tick, every tick needed a generated frame (+14% work) and RIFE made 74.6-75.1% on the
  * 5080 (11-11 PS5), and FSR 3 (exact 2x only) refused 120/1. */
 if(auto_mode&&a.sd==1001&&a.td==1&&a.tn>0&&a.tn<=240){a.tn*=1000;a.td=1001;}
 if(auto_force_target>0){a.tn=auto_force_target;a.td=auto_force_td;}
 if(a.td==1&&auto_display_mhz%1000&&(auto_display_mhz+500)/1000==a.tn){a.tn=auto_display_mhz;a.td=1000;}
 ReleaseSRWLockShared(&auto_lock);CsRatePlan p;a.supported=a.live&&a.progressive&&cs_rate_fraction(a.sn,a.sd,a.tn,a.td,0,&p);return a;
}
static void auto_apply_preview(void);
