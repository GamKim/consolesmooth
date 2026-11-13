/* SPDX-License-Identifier: MIT
 * Buffer delivery cadence at the preview filter output, NOT display presents
 * or unique game frames. Each pad measures its own window; never sum pads.
 */
typedef struct {uint64_t start,last,count;double fps;} CsFpsWindow;
/* Monotonic rendered-output counters. Excludes worker results never used. */
typedef struct {uint64_t start,last,generated,repeated;double gen_fps,repeat_fps;} CsCountWindow;
static int fps_counts(CsCountWindow *s,uint64_t now,uint64_t frequency,uint64_t generated,uint64_t repeated){
 if(!frequency)return 0;
 if(!s->start||now<=s->last||now-s->last>frequency*3/2||generated<s->generated||repeated<s->repeated){
  *s=(CsCountWindow){.start=now,.last=now,.generated=generated,.repeated=repeated};return 0;
 }
 s->last=now;if(now-s->start<frequency)return 0;
 double seconds=(double)(now-s->start)/frequency;
 s->gen_fps=(generated-s->generated)/seconds;s->repeat_fps=(repeated-s->repeated)/seconds;
 s->start=now;s->generated=generated;s->repeated=repeated;return 1;
}
/* Generation short of the target. Repeats are planned frames that were not ready, so a still image or a
 * 30 fps game does not count; a low generated rate alone would. On after 5 s at 3% or more, off after 10 s below 1%. */
typedef struct {uint64_t high_since,low_since;int warn;} CsShortfall;
static int fps_shortfall(CsShortfall *s,uint64_t ms,double delivered,double repeat_fps){
 double share=delivered>0?repeat_fps/delivered:0;
 if(share>=.03){s->low_since=0;if(!s->high_since)s->high_since=ms;if(ms-s->high_since>=5000)s->warn=1;}
 else{s->high_since=0;if(share>=.01)s->low_since=0;else{if(!s->low_since)s->low_since=ms;if(ms-s->low_since>=10000)s->warn=0;}}
 return s->warn;
}
/* Almost nothing generated. Repeats keep the preview above the input rate, so the delivered rate cannot see this:
 * an iGPU measured 0 generated and 53 repeated per second at 110 fps delivered. On after 10 s under 10% of the planned frames. */
typedef struct {uint64_t since;} CsNoGeneration;
static int fps_no_generation(CsNoGeneration *s,uint64_t ms,double gen_fps,double repeat_fps){
 if(repeat_fps<5||gen_fps>=(gen_fps+repeat_fps)*.1){s->since=0;return 0;}
 if(!s->since)s->since=ms;
 return ms-s->since>=10000;
}
/* 1.7.5: hopeless GPU. Each generated frame takes more than two source intervals and under 5% of the planned frames
 * are made, without a break for 4 s. Measured 11-07 with the 5080 clocked to 1060-class: RIFE 72-89 ms per frame at
 * 60 fps input (16.7 ms), which waited 20-30 s for the check above; 2060-class and faster settled at 19 ms or less. */
typedef struct {uint64_t since;} CsTooSlow;
/* 1.7.7: no_warmup is set for FSR 3. It has no slow start (2060-class settled at 7.4-8.8 ms from the first second)
 * while RIFE on 2060/3060-class spends its first 10-15 s at 20-33 ms with 0-3% made before it reaches 24-34%.
 * 1060-class FSR 3 ran 23-35 ms with 4 of 69 made (5.8%), so it gets 1.3x the source interval and under 10%.
 * RIFE moves from 2x to 3x (50 ms at 60 fps): 3060-class warm-up reached 33.0 ms, 1060-class runs 72-89 ms. */
static int fps_too_slow(CsTooSlow *s,uint64_t ms,double gen_fps,double repeat_fps,double frame_ms,double source_ms,int no_warmup){
 double factor=no_warmup?1.3:3,share=no_warmup?.1:.05;
 if(repeat_fps<5||source_ms<=0||frame_ms<=source_ms*factor||gen_fps>=(gen_fps+repeat_fps)*share){s->since=0;return 0;}
 if(!s->since)s->since=ms;
 return ms-s->since>=4000;
}
/* Delivery gaps, in 0.25 ms histogram bins; bounded, no per-frame allocation. */
typedef struct {uint64_t last,count,bins[513],long_gaps;double maximum;} CsCadence;
static void fps_duration(CsCadence *s,double ms,double expected_ms){unsigned b=(unsigned)(ms*4);if(b>512)b=512;++s->bins[b];++s->count;if(ms>s->maximum)s->maximum=ms;if(expected_ms>0&&ms>expected_ms*1.5)++s->long_gaps;}
static void fps_gap(CsCadence *s,uint64_t now,uint64_t frequency,double expected_ms){
 if(s->last&&now>s->last&&frequency)fps_duration(s,1000.0*(now-s->last)/frequency,expected_ms);s->last=now;
}
static double fps_gap_p95(const CsCadence *s){if(!s->count)return 0;uint64_t sum=0,rank=(s->count*95+99)/100;for(unsigned i=0;i<513;i++){sum+=s->bins[i];if(sum>=rank)return i==512?s->maximum:(i+1)*.25;}return 0;}
static int fps_sample(CsFpsWindow *s,uint64_t now,uint64_t frequency){
 if(!frequency)return 0;
 if(!s->count||now<=s->last||now-s->last>frequency*3/2){
  *s=(CsFpsWindow){.start=now,.last=now,.count=1};return 0;
 }
 s->last=now;++s->count;
 if(now-s->start<frequency)return 0;
 s->fps=(double)(s->count-1)*frequency/(double)(now-s->start);
 s->start=now;s->count=1;return 1;
}
/* 2.1.0: no longer used by the plugin (the readout counts engine originals + generated frames, framegen.h); kept for
 * recording analysis and fps_meter_check.
 * 2.0.7: new pictures shown per second. The readout "59.9 / 119.9" counts signal and output frames; a 30 fps game
 * without 30 fps smoothing showed 119.9 while only about 60 pictures a second moved (11-11 GTA5 quality mode: groups of
 * four were new, new, almost the same (0.05-0.2 levels), identical). Each output frame is compared with the previous one
 * on a 64x36 grid (green channel). A frame counts as new when its change is over a fifth of the largest change of the
 * last 8 frames.
 * 2.0.9 (11-11 user: the readout flipped between "원본 30 / 지금 60" and "59.9 / 119.9" on still moments): every frame
 * counts, so a still picture reads 0 and a rate is ready every 2 s; only identical frames (no change) are never new.
 * 2.1.0: a sixteenth instead of a fifth. Test copy trace (PS5, RIFE 2x): in-between frames changed 300-640 while the step
 * into the next original changed 2268-2741 (motion plus the sharpness step), so a fifth missed them (read 30-47 instead of
 * 60-120). The near-copies of the GTA5 recording are 1/50 to 1/200 of the big change and stay out. */
#define CS_SHOWN_GX 64
#define CS_SHOWN_GY 36
typedef struct {uint8_t prev[CS_SHOWN_GX*CS_SHOWN_GY];int have,ring_n,ring_i;uint32_t ring[8];uint64_t start;uint32_t frames,moving,fresh;double fps;} CsShownMeter;
static inline uint32_t shown_change(CsShownMeter *s,const uint8_t *px,int w,int h,int bpp){
 uint32_t sum=0;
 for(int j=0;j<CS_SHOWN_GY;j++){const uint8_t *row=px+(size_t)((2*j+1)*h/(2*CS_SHOWN_GY))*w*bpp;
  for(int i=0;i<CS_SHOWN_GX;i++){uint8_t v=row[(size_t)((2*i+1)*w/(2*CS_SHOWN_GX))*bpp+1];uint8_t *p=&s->prev[j*CS_SHOWN_GX+i];sum+=v>*p?v-*p:*p-v;*p=v;}}
 return sum;
}
/* Returns 1 when a new rate is ready in s->fps (window of about 2 s with at least a quarter of it moving). */
static inline int shown_tick(CsShownMeter *s,const uint8_t *px,int w,int h,int bpp,uint64_t now,uint64_t frequency){
 if(!px||w<CS_SHOWN_GX||h<CS_SHOWN_GY||!frequency)return 0;
 uint32_t d=shown_change(s,px,w,h,bpp);
 if(!s->have){s->have=1;s->start=now;s->frames=s->moving=s->fresh=0;s->ring_n=s->ring_i=0;return 0;}
 if(now<=s->start||now-s->start>frequency*4){s->start=now;s->frames=s->moving=s->fresh=0;}
 s->ring[s->ring_i]=d;s->ring_i=(s->ring_i+1)&7;if(s->ring_n<8)++s->ring_n;
 uint32_t top=0;for(int i=0;i<s->ring_n;i++)if(s->ring[i]>top)top=s->ring[i];
 ++s->frames;++s->moving;if(d>0&&(uint64_t)d*16>top)++s->fresh;
 if(now-s->start<frequency*2)return 0;
 double sec=(double)(now-s->start)/frequency;int ready=s->frames>0;
 if(ready)s->fps=(double)s->fresh/sec;
 s->start=now;s->frames=s->moving=s->fresh=0;return ready;
}
#ifndef CS_FPS_MATH_ONLY
static SRWLOCK fps_lock=SRWLOCK_INIT;
static double fps_delivery;
static ULONGLONG fps_updated;
static const void *fps_owner;
static void fps_publish(const void *owner,double fps){
 AcquireSRWLockExclusive(&fps_lock);fps_owner=owner;fps_delivery=fps;fps_updated=GetTickCount64();ReleaseSRWLockExclusive(&fps_lock);
}
static void fps_forget(const void *owner){
 AcquireSRWLockExclusive(&fps_lock);if(fps_owner==owner){fps_owner=NULL;fps_updated=0;}ReleaseSRWLockExclusive(&fps_lock);
}
static int fps_snapshot(double *fps){
 AcquireSRWLockShared(&fps_lock);*fps=fps_delivery;int fresh=fps_owner&&GetTickCount64()-fps_updated<2500;ReleaseSRWLockShared(&fps_lock);return fresh;
}
#endif
