/* SPDX-License-Identifier: MIT
 * Authoritative device input state, separate from capture/output caps.
 * The verified SDK adapter (GC573) is authoritative. Other devices (USB cards such as GC551G2/GC553G2)
 * have no verified input-state API, so their negotiated capture caps stand in, but only while buffers
 * keep arriving from exactly one capture source ("fallback").
 */
/* capped (1.9.6): the device signal is faster than the rate Streaming Center captures at (source settings 30 FPS on a
 * 60 Hz signal); sn/sd then follow the capture, because only those frames reach the plugin. 11-10 GC573: 30 FPS
 * selected, the readout said 60.00 and the engine planned 60 -> 120 on 30 real frames. */
typedef struct {int sn,sd,tn,td,live,supported,provider,width,height,progressive,fallback,capped,signal_sn,signal_sd,sources,captures;} AutoRate;
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
static volatile LONG auto_capture_count;
/* Set by the UI when the preview delivers fewer frames than the input (GPU too slow):
 * generation stops and the preview returns to the host rate, as with no signal. */
static volatile LONG auto_gpu_slow;
/* Test only (CS_TEST_KEEP_RATE=1): keep the generation rate without signal, to measure 120 fps pacing on the No Signal picture. */
static int keep_rate_without_signal;
/* 2.3.19 (GC551G2): capture drivers give the frame time in 100 ns units, so 29.97 can arrive as 10000000/333667 or 2997/100
 * instead of 30000/1001, and the engine (cs_rate_fraction) accepts only the exact standard rates. A rate within 0.04% of
 * a standard one becomes that rate (30 and 29.97 are 0.1% apart, so they never mix); other rates stay as they are. */
static void auto_standard_rate(int *n,int *d){
 static const int rates[6][2]={{30000,1001},{30,1},{60000,1001},{60,1},{120000,1001},{120,1}};
 if(*n<=0||*d<=0)return;
 for(int i=0;i<6;i++){double want=(double)rates[i][0]/rates[i][1],diff=(double)*n/ *d-want;if(diff<0)diff=-diff;
  if(diff<want*0.0004){*n=rates[i][0];*d=rates[i][1];return;}}
}
/* 2.3.21: capture sources that exist (id = their watch). Before, a second source (another card in another scene, or the old
 * card's source not freed yet after switching cards) made the input unknown: the fast path still generated in the preview,
 * but the full screen, which needs a known input, did not open. Now a source counts only while its buffers arrive, and
 * several active ones are fine when they agree on rate and size. */
typedef struct {const void *id;int n,d,w,h;ULONGLONG at;} AutoCapture;
static AutoCapture auto_captures[8];
/* Call with auto_lock held (shared is enough). 1: *out is the input; *active = sources with buffers in the last second. */
static int auto_capture_pick(ULONGLONG now,AutoCapture *out,int *active){
 int n=0,same=1;AutoCapture first={0};
 for(int i=0;i<8;i++){const AutoCapture *c=&auto_captures[i];if(!c->id||!c->at||now-c->at>=1000)continue;
  if(!n++)first=*c;else if((int64_t)c->n*first.d!=(int64_t)first.n*c->d||c->w!=first.w||c->h!=first.h)same=0;}
 if(active)*active=n;
 if(!n||!same||first.n<=0||first.d<=0)return 0;
 *out=first;return 1;
}
static int auto_capture_active(const void *id){
 int r=0;ULONGLONG now=GetTickCount64();AcquireSRWLockShared(&auto_lock);for(int i=0;i<8;i++)if(auto_captures[i].id==id&&auto_captures[i].at&&now-auto_captures[i].at<1000)r=1;ReleaseSRWLockShared(&auto_lock);return r;
}
static void auto_capture_forget(const void *id){
 AcquireSRWLockExclusive(&auto_lock);for(int i=0;i<8;i++)if(auto_captures[i].id==id)auto_captures[i]=(AutoCapture){0};ReleaseSRWLockExclusive(&auto_lock);
}
static void auto_seen(const void *id,int n,int d,int w,int h){
 {static int rawN,rawD;int n0=n,d0=d;auto_standard_rate(&n,&d);if(n0!=rawN||d0!=rawD){rawN=n0;rawD=d0;logline("AUTO_CAPTURE_RATE raw=%d/%d used=%d/%d size=%dx%d",n0,d0,n,d,w,h);}}
 AcquireSRWLockExclusive(&auto_lock);int slot=-1,oldest=0;
 for(int i=0;i<8&&slot<0;i++)if(auto_captures[i].id==id)slot=i;
 for(int i=0;i<8&&slot<0;i++)if(!auto_captures[i].id)slot=i;
 if(slot<0){for(int i=1;i<8;i++)if(auto_captures[i].at<auto_captures[oldest].at)oldest=i;slot=oldest;}
 auto_captures[slot]=(AutoCapture){id,n,d,w,h,GetTickCount64()};ReleaseSRWLockExclusive(&auto_lock);
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
 AutoCapture cap={0};int known=auto_capture_pick(now,&cap,&a.captures);a.sources=(int)InterlockedCompareExchange(&auto_capture_count,0,0);
 if(!a.provider&&(known||a.sources==1)){
  /* No verified device state: the capture stream's own caps, while buffers are flowing. */
  a.provider=1;a.fallback=1;a.sn=0;a.sd=1;
  if(known){a.sn=cap.n;a.sd=cap.d;a.width=cap.w;a.height=cap.h;a.progressive=1;a.live=1;}
 }else if(a.live&&a.sn>0&&known&&(int64_t)cap.n*a.sd<(int64_t)a.sn*cap.d){a.signal_sn=a.sn;a.signal_sd=a.sd;a.sn=cap.n;a.sd=cap.d;a.capped=1;}
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
