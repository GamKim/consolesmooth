/* SPDX-License-Identifier: MIT
 * Private implementation included by tracer.c. Public GStreamer 1.18 ABI only.
 * Development adapter: fixed cadence, CPU staging, same resolution, SDR BGRA (synthetic test mode: BGR).
 */
typedef struct {void *memory;int flags;uint8_t *data;size_t size,maxsize;void *user[4],*reserved[4];} CsMap;
typedef struct {int type;unsigned long id;void *data;uint64_t offset;unsigned size;void *reserved[4];} CsProbeInfo;
typedef struct {unsigned domain;int code;char *message;} CsError;
typedef struct {uint64_t ticks,generated,repeated,static_pairs,cuts,overwritten,failures;double last_inference_ms;int ready;} CsStats;
_Static_assert(sizeof(CsMap)==104,"GstMapInfo Win64 ABI");
_Static_assert(offsetof(CsProbeInfo,data)==8,"GstPadProbeInfo Win64 ABI");
static void *(*parse_bin)(const char*,int,void**),*(*bin_find)(void*,const char*);
static void (*error_free)(void*),(*buffer_unref)(void*);
static void *(*buffer_new_allocate)(void*,size_t,void*),*(*caps_structure)(void*,unsigned);
static int (*buffer_copy_into)(void*,void*,int,size_t,size_t);
typedef struct {int format;int64_t start,stop;unsigned flags;void *reserved[4];} CsAcquire;
_Static_assert(sizeof(CsAcquire)==64&&offsetof(CsAcquire,flags)==24,"GstBufferPoolAcquireParams Win64 ABI");
static void *(*pool_new)(void),*(*pool_get_config)(void*);
static void (*pool_config_params)(void*,void*,unsigned,unsigned,unsigned);
static int (*pool_set_config)(void*,void*),(*pool_set_active)(void*,int),(*pool_acquire)(void*,void**,CsAcquire*);
static int (*buffer_map)(void*,CsMap*,int),(*structure_int)(void*,const char*,int*);
static void (*buffer_unmap)(void*,CsMap*);
static const char *(*structure_string)(void*,const char*);
static int (*structure_fraction)(void*,const char*,int*,int*);
static void *(*stream_create)(const wchar_t*,int,int,int);
static int (*stream_tick)(void*,const uint8_t*,uint8_t*),(*stream_stats)(void*,CsStats*);
static int (*stream_tick_at)(void*,const uint8_t*,uint8_t*,uint64_t);
typedef struct {uint64_t accepted_sources,timestamp_resets,expired_jobs,completed_jobs,discarded_results,deadline_misses,last_source_age_ns,max_source_age_ns;int workers;} LiveMetrics;
static int (*stream_live_metrics)(void*,LiveMetrics*);
/* 1.7.8 diagnostics (optional exports): stage averages over the last 240 inferences and the driver/queue line. */
typedef struct {double in_ms,gpu_ms,out_ms,total_ms,gpu_max_ms,total_max_ms,busy_pct;uint64_t samples;double keep_ms;uint64_t keep_dispatches;} StageStats;
static void (*engine_set_gpu_options)(int);
static int (*stream_stage_stats)(void*,StageStats*);static const char* (*stream_gpu_info)(void*);
static SRWLOCK stage_lock=SRWLOCK_INIT;static StageStats ui_stage;static char ui_gpu_info[512];static int ui_stage_valid;
static int idle_all; /* CS_TEST_IDLE_ALL=1: process unchanged groups too (pre-2.0.1) */
/* 2.2.4 (11-14, friend: "generation short" after updates): SC builds one root pipeline per scene preview. The scene thumbnail
 * pipeline gets the capture through a proxy and ran its own RIFE group on moving games, taking the GPU from the main picture
 * (5080 at 2100 MHz, SC 1.8.22.2, PS5 moving: thumbnail group 77.7% generated, main preview/recording group 16.8%).
 * The pipeline of the large preview (the D3D11 sink inside a GstBin, see pace_sink) is the main one; once it is known, groups in
 * any other pipeline pass their picture through like idle groups. CS_TEST_ALL_GROUPS=1 (test only) keeps the old behaviour. */
static char main_pipe[64];static SRWLOCK main_pipe_lock=SRWLOCK_INIT;static int all_groups;
static volatile LONG reco_trial; /* 2.0.0: 권장 설정 trial running, publish stage times every 30 ticks */
/* The preview and the record/stream route each run a stream; only one generates. The trial follows the first group
 * that shows generated or repeated frames (both toolbar stats alternate between groups). Reset per model by the trial. */
static SRWLOCK reco_lock=SRWLOCK_INIT;static CsStats reco_cs;static const void *reco_group;
static int (*stream_set_quarter)(void*,int); /* 2.0.2, optional export */
static int (*stream_configure)(void*,int,int,int,int,int,int),(*stream_set_buffer)(void*,int),(*stream_set_low_buffer)(void*,int),(*stream_set_options)(void*,int,int);
static void (*stream_destroy)(void*);
static uint64_t (*stream_deadlines)(void*),(*stream_buffer_ns)(void*);
static int framegen_fps,framegen_output_fps,synthetic;
/* Bytes per pixel on the frame path. Live frames stay BGRA from d3d11download to d3d11upload:
 * the BGRA<->BGR conversions cost ~4-9 ms per frame at 4K. The synthetic test stream is bgr24. */
static int frame_px(void){return synthetic?3:4;}
static void *(*live_create_format)(const wchar_t*,int,int,int,int,int);
static wchar_t engine_dir[32768];
#include "../engine/rate_math.h"
#include "../engine/frame_limits.h"
#include "auto_rate.h"
#include "fps_meter.h"
#include "controls.h"
/* Correlate synchronous filter stages only; unmatched/thread-switched samples
 * are explicitly excluded, never treated as zero-duration measurements. */
typedef struct {const void *group;uint64_t input,bgr,processed;} StageStamp;
static _Thread_local StageStamp stage_stamp;
#include "frame_proof.h"
static _Thread_local int tick_new_dump; /* set by frame_buffer: this tick carries a new picture */
#include "frame_dump.h"
static LONG frame_force_model=-1; /* CS_TEST_MODEL=<id>: model without the toolbar (test) */
static LONG frame_force_stable=-1; /* CS_TEST_STABLE=0|1: latency mode with an automatic buffer, without the toolbar (test) */
typedef struct {int pipe_state;char pipe[64];int tl_slot,ever_changed,fp_set,idle_logged;uint64_t fp,idle_ticks;void *stream,*pool,*group;HANDLE initializer;volatile LONG init_state;int width,height,model,disabled,sn,sd,tn,td;uint64_t ticks,logical_ticks,cycle,pool_fallbacks;uint8_t *input;LONG applied,observed_toggle,generated_toggle;CsCadence cpu;FrameProof *proof;int64_t space_next;uint64_t space_frames,space_waits;double space_wait_ms;LONG gpu_options;} FrameContext;
static void frame_release(FrameContext *f){
 if(f->stream){CsStats s={0};stream_stats(f->stream,&s);
  logline("FG_STOP ticks=%llu generated=%llu repeat=%llu failures=%llu",(unsigned long long)s.ticks,(unsigned long long)s.generated,(unsigned long long)s.repeated,(unsigned long long)s.failures);
  stream_destroy(f->stream);}
 proof_destroy(f->proof);if(f->pool){pool_set_active(f->pool,0);object_unref(f->pool);}
 HeapFree(GetProcessHeap(),0,f->input);f->stream=NULL;f->proof=NULL;f->pool=NULL;f->input=NULL;
}
static void frame_cleanup(void *p){
 FrameContext *f=p;if(f->initializer){WaitForSingleObject(f->initializer,INFINITE);CloseHandle(f->initializer);}frame_release(f);HeapFree(GetProcessHeap(),0,f);
}
static DWORD WINAPI frame_initialize(void *data){
 FrameContext *f=data;frame_release(f);wchar_t model[32768];swprintf(model,32768,L"%ls\\%ls",engine_dir,cs_models[f->model].directory);
 // Test only: CS_ENGINE_GPU=<Vulkan index> runs the engine on another GPU (e.g. an iGPU to check the GPU-slow fallback).
 wchar_t gpuText[8];int gpu=-1;if(GetEnvironmentVariableW(L"CS_ENGINE_GPU",gpuText,8)-1u<7u)gpu=_wtoi(gpuText);
 if(engine_set_gpu_options)engine_set_gpu_options((int)(f->gpu_options>>2)&7);
 f->stream=live_create_format?live_create_format(model,gpu,f->width,f->height,1,4):stream_create(model,gpu,f->width,f->height);
 if(synthetic)f->input=HeapAlloc(GetProcessHeap(),0,(size_t)f->width*f->height*3);
 if(!f->stream||(synthetic&&!f->input)){InterlockedExchange(&f->init_state,-1);return 0;}
 f->pool=pool_new();
 if(f->pool){void *config=pool_get_config(f->pool);pool_config_params(config,NULL,(unsigned)f->width*f->height*frame_px(),3,6);
  if(!pool_set_config(f->pool,config)||!pool_set_active(f->pool,1)){object_unref(f->pool);f->pool=NULL;}}
 logline("FG_BUFFER_POOL active=%d min=3 max=6 nonblocking=1 asynchronous_init=1 group=%p",f->pool!=NULL,f->group);
 f->proof=proof_create(f->width,f->height,frame_px(),f->group);
 InterlockedExchange(&f->init_state,2);return 0;
}
static int frame_begin_size(FrameContext *f,int w,int h){
 /* Only this streaming callback starts/replaces the initializer. Never join
  * an unfinished worker on the video thread; continue forwarding originals. */
 if(f->initializer){if(WaitForSingleObject(f->initializer,0)!=WAIT_OBJECT_0)return 0;CloseHandle(f->initializer);f->initializer=NULL;}
 f->width=w;f->height=h;f->model=frame_force_model>=0?frame_force_model:(ui_requested()>>16)&255;if(!cs_model_valid(f->model))f->model=0;
 f->gpu_options=InterlockedCompareExchange(&engine_options,0,0)&28; /* bits 2-4: GPU keep, fp16, FSR 3 acceleration */
 f->applied=-1;f->logical_ticks=0;f->cycle=0;
 f->sn=f->sd=f->tn=f->td=0;
 InterlockedExchange(&f->init_state,1);
 f->initializer=CreateThread(NULL,0,frame_initialize,f,0,NULL);
 if(!f->initializer){InterlockedExchange(&f->init_state,-1);f->disabled=1;ui_publish(NULL,w,h,-1);return 0;}
 logline("FG_SIZE_PREPARE width=%d height=%d model=%s group=%p",w,h,cs_models[f->model].name,f->group);return 1;
}
static void synthetic_frame(uint8_t *p,int w,int h,uint64_t tick){
 int phase=(int)(cs_rate_source(tick,framegen_fps,framegen_output_fps)%240);int shift=(phase<120?phase:239-phase)*4;
 for(int y=0;y<h;y++)for(int x=0;x<w;x++){
  int object=x>w/4+shift&&x<w/4+w/5+shift&&y>h/3&&y<2*h/3;
  size_t k=((size_t)y*w+x)*3;
  p[k]=object?20:35;p[k+1]=object?200:35;p[k+2]=object?220:35;
 }
}
/* Test only (CS_TEST_GENERATE=1): generation on from start, without the toolbar toggle. */
static LONG frame_force_on;
/* Set while frames are being generated; the preview-sink refresh hold (vblank_pacing.h) acts only then. */
static volatile LONG frame_generating;
static int frame_buffer_impl(void *pad,void *info,void *data){
 FrameContext *f=data;if(f->disabled)return 1;
 LONG initialized=InterlockedCompareExchange(&f->init_state,0,0);
 LONG settings=ui_requested()|frame_force_on;if(frame_force_model>=0)settings=(settings&~(0xFF<<16))|(frame_force_model<<16);if(frame_force_stable>=0)settings=(settings&~(4|(255<<8)))|(frame_force_stable?4:0);int enabled=!!(settings&1),stable=!!(settings&4);
 LONG serial=InterlockedCompareExchange(&toggle_serial,0,0);
 if(serial&&serial!=f->observed_toggle){LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);f->observed_toggle=serial;
  logline("TOGGLE_OBSERVED serial=%ld enabled=%d handler_to_buffer_ms=%.3f qpc=%lld",serial,enabled,1000.0*(now.QuadPart-InterlockedCompareExchange64(&toggle_qpc,0,0))/freq.QuadPart,(long long)now.QuadPart);}
 AutoRate rate=auto_snapshot();if(!interactive_ui){rate=(AutoRate){.sn=framegen_fps,.sd=1,.tn=framegen_output_fps,.td=1,.live=1,.supported=1,.provider=1};}
 int fps=rate.sn?(rate.sn+rate.sd/2)/rate.sd:0;
 int requestedModel=(settings>>16)&255;if(!cs_model_valid(requestedModel))requestedModel=0;
 if(!rate.live||!rate.supported||InterlockedCompareExchange(&auto_gpu_slow,0,0)||!cs_model_rate_supported(requestedModel,rate.sn,rate.sd,rate.tn,rate.td))enabled=0;
 // Off, missing input and unsupported rates forward the current buffer.
 if(!enabled&&!synthetic){
  InterlockedExchange(&frame_generating,0);if(initialized==2)proof_reset(f->proof);
  if(initialized==2&&f->stream&&f->applied!=-1)stream_configure(f->stream,0,f->sn,f->sd,f->tn,f->td,stable);
  f->applied=-1;CsStats stats={0};if(initialized==2&&f->stream)stream_stats(f->stream,&stats);ui_publish(&stats,f->width,f->height,0);return 1;
 }
 LARGE_INTEGER cpuStart,cpuEnd,cpuFreq;QueryPerformanceCounter(&cpuStart);QueryPerformanceFrequency(&cpuFreq);
 void *caps=current_caps(pad),*s=caps?caps_structure(caps,0):NULL;
 int w=0,h=0,fn=0,fd=0;const char *format=s?structure_string(s,"format"):NULL;
 int valid=s&&structure_int(s,"width",&w)&&structure_int(s,"height",&h)&&
  structure_fraction(s,"framerate",&fn,&fd)&&format&&!strcmp(format,synthetic?"BGR":"BGRA")&&
  cs_native_frame_valid(w,h)&&fd>0&&fn>0;
 if(caps)caps_unref(caps);
 if(!valid){
  /* Unsupported/transient caps are not a permanent engine fault. */
  f->applied=-1;ui_publish(NULL,w,h,-1);return 1;
 }
 // Negotiation can temporarily retain the old caps. Bypass until the requested
 // rational rate is observed; never feed mismatched cadence to the engine.
 if((int64_t)fn*rate.td!=(int64_t)rate.tn*fd){f->applied=-1;ui_publish(NULL,w,h,-3);return 1;}
 if(initialized==1){CsStats pending={0};ui_publish(&pending,w,h,0);return 1;}
 
 if(!initialized||w!=f->width||h!=f->height||f->model!=requestedModel||(InterlockedCompareExchange(&engine_options,0,0)&28)!=f->gpu_options){
  frame_begin_size(f,w,h);
  logline("FG_START width=%d height=%d source_fps=%d output_fps=%d synthetic=%d mode=live-control group=%p",w,h,fps,fn,synthetic,f->group);
  return 1;
 }
 if(initialized!=2){CsStats pending={0};ui_publish(&pending,w,h,initialized<0?-1:0);return 1;}
 CsStats readiness={0};stream_stats(f->stream,&readiness);
 if(readiness.ready!=1){ui_publish(&readiness,w,h,readiness.ready<0?-1:0);return 1;}
 /* Settings window switches (30 fps cadence, HUD keep), applied to each new engine stream and on every change. */
 {static void* optStream;static LONG optApplied=-1;LONG opt=InterlockedCompareExchange(&engine_options,0,0);
  if(stream_set_options&&(f->stream!=optStream||opt!=optApplied)&&!stream_set_options(f->stream,opt&1,(opt>>1)&1)){optStream=f->stream;optApplied=opt;if(stream_set_quarter)stream_set_quarter(f->stream,(opt>>5)&1);logline("FG_OPTIONS cadence30=%ld hud_keep=%ld latency_priority=%ld",opt&1,(opt>>1)&1,(opt>>5)&1);}}
 if(settings!=f->applied||f->sn!=rate.sn||f->sd!=rate.sd||f->tn!=rate.tn||f->td!=rate.td){
  if(stream_configure(f->stream,enabled,rate.sn,rate.sd,rate.tn,rate.td,stable)){ui_publish(NULL,w,h,-1);return 1;}
  int bufferMs=(settings>>8)&255;if(stable&&bufferMs&&stream_set_buffer&&stream_set_buffer(f->stream,bufferMs)){ui_publish(NULL,w,h,-1);return 1;}
  if(!stable&&bufferMs&&stream_set_low_buffer&&stream_set_low_buffer(f->stream,bufferMs)){ui_publish(NULL,w,h,-1);return 1;}
  proof_reset(f->proof);f->applied=settings;f->sn=rate.sn;f->sd=rate.sd;f->tn=rate.tn;f->td=rate.td;f->logical_ticks=0;f->cycle=0;
  logline("FG_RATE_APPLIED enabled=%d source=%d/%d target=%d/%d stable=%d",enabled,rate.sn,rate.sd,rate.tn,rate.td,stable);
 }
 InterlockedExchange(&frame_generating,1);size_t bytes=(size_t)w*h*frame_px();
 void *old=probe_buffer(info),*copy=NULL;if(!old)return 1;
 // Read the immutable source directly; output storage is owned independently.
 CsMap source={0};if(!buffer_map(old,&source,1))return 1;
 if(source.size!=bytes){buffer_unmap(old,&source);f->disabled=1;logline("FG_DISABLED nonpacked_buffer");return 1;}
 CsAcquire acquire={0};acquire.flags=2; /* GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT */
 if(!f->pool||pool_acquire(f->pool,&copy,&acquire)!=0){copy=buffer_new_allocate(NULL,bytes,NULL);++f->pool_fallbacks;}
 // Preserve metadata and timestamps, never copy source pixel storage.
 if(!copy||!buffer_copy_into(copy,old,7,0,(size_t)-1)){if(copy)buffer_unref(copy);buffer_unmap(old,&source);return 1;}
 CsMap map={0};if(!buffer_map(copy,&map,2)){buffer_unref(copy);buffer_unmap(old,&source);return 1;}
 if(map.size!=bytes){buffer_unmap(copy,&map);buffer_unref(copy);buffer_unmap(old,&source);f->disabled=1;logline("FG_DISABLED output_size");return 1;}
 if(synthetic)synthetic_frame(f->input,w,h,f->logical_ticks);
 uint64_t arrival=(stage_stamp.group==f->group&&stage_stamp.input)?stage_stamp.input:(uint64_t)cpuStart.QuadPart;
 uint64_t arrival_ns=(uint64_t)((long double)arrival*1000000000.0L/cpuFreq.QuadPart);
 if(!synthetic&&tick_new_dump)dump_frame(source.data,w,h,arrival_ns); /* test only, CS_TEST_DUMP_DIR */
 int ok=(stream_tick_at?stream_tick_at(f->stream,source.data,map.data,arrival_ns):stream_tick(f->stream,synthetic?f->input:source.data,map.data))==0;
 CsStats current={0};stream_stats(f->stream,&current);
 if(ok&&serial&&serial!=f->generated_toggle&&current.generated>readiness.generated){LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);f->generated_toggle=serial;
  logline("TOGGLE_FIRST_GENERATED serial=%ld handler_to_generated_ms=%.3f qpc=%lld",serial,1000.0*(now.QuadPart-InterlockedCompareExchange64(&toggle_qpc,0,0))/freq.QuadPart,(long long)now.QuadPart);}
 if(ok)proof_stamp(map.data,w,frame_px(),f->logical_ticks,current.generated>readiness.generated);
 if(ok&&f->proof&&!stream_tick_at){CsRatePlan plan;
  if(cs_rate_fraction(rate.sn,rate.sd,rate.tn,rate.td,stable,&plan)&&plan.target_step%plan.source_step==0&&f->logical_ticks>=(unsigned)plan.delay)
   proof_sample(f->proof,map.data,f->logical_ticks-plan.delay,plan.target_step/plan.source_step,current.generated>readiness.generated);
 }
 if(synthetic&&f->logical_ticks>=200&&f->logical_ticks<204){
  double sum=0,count=0;for(int y=0;y<h;y++)for(int x=0;x<w;x++){
   size_t k=((size_t)y*w+x)*3;double weight=map.data[k+1]>35?map.data[k+1]-35:0;sum+=x*weight;count+=weight;
  }
  int delay=enabled?cs_rate_delay(fps,framegen_output_fps,stable):0;
  if(framegen_output_fps==fps*2)logline("FG_V2_SAMPLE tick=%llu source_fps=%d enabled=%d delay_ticks=%d centroid_x=%.3f",(unsigned long long)f->logical_ticks,fps,enabled,delay,count?sum/count:0);
 }
 buffer_unmap(copy,&map);buffer_unmap(old,&source);
 if(ok){((CsProbeInfo*)info)->data=copy;buffer_unref(old);}else{buffer_unref(copy);f->disabled=1;logline("FG_DISABLED tick_failed");}
 ++f->ticks;++f->cycle;++f->logical_ticks;
 {static double logged=-1;double ms=stream_buffer_ns?stream_buffer_ns(f->stream)/1000000.0:0;ui_publish_buffer(ms);if(ms!=logged){logged=ms;logline("FG_BUFFER ms=%.2f",ms);}}
 ui_publish(&current,w,h,ok?0:-1);
 if(InterlockedCompareExchange(&reco_trial,0,0)){AcquireSRWLockExclusive(&reco_lock);if(reco_group==f->group||(!reco_group&&current.generated+current.repeated>0)){reco_group=f->group;reco_cs=current;}ReleaseSRWLockExclusive(&reco_lock);}
 QueryPerformanceCounter(&cpuEnd);fps_duration(&f->cpu,1000.0*(cpuEnd.QuadPart-cpuStart.QuadPart)/cpuFreq.QuadPart,0);
 /* 1.9.6: game frame rate = changed images the engine accepted per second, over 2 s windows (see auto_content_x100). */
 if(stream_live_metrics&&f->ticks%30==0){static void *cStream;static LONGLONG cAt;static uint64_t cCount;LiveMetrics m={0};stream_live_metrics(f->stream,&m);
  /* 2.1.0: pictures shown = changed originals + generated frames used, from the same engine over the same window. The 2.0.7
   * pixel count (CsShownMeter) could not tell small motion from near-copies on a nearly still picture (11-11 test copy,
   * 30 fps smoothing on: engine 30 + 90 per second, pixel count 30-47). */
  static uint64_t cGen;
  if(cStream!=f->stream||!cAt||m.accepted_sources<cCount||current.generated<cGen){cStream=f->stream;cAt=cpuEnd.QuadPart;cCount=m.accepted_sources;cGen=current.generated;}
  else{double sec=(double)(cpuEnd.QuadPart-cAt)/cpuFreq.QuadPart;if(sec>=2){LONG v=(LONG)((m.accepted_sources-cCount)/sec*100+.5),sv=(LONG)((m.accepted_sources-cCount+current.generated-cGen)/sec*100+.5);
   if(abs(v-InterlockedCompareExchange(&auto_content_x100,0,0))>300)logline("FG_CONTENT_FPS %.2f",v/100.0);
   if(abs(sv-InterlockedCompareExchange(&auto_shown_x100,0,0))>300)logline("FG_SHOWN_FPS %.2f",sv/100.0);
   InterlockedExchange(&auto_content_x100,v);InterlockedExchange64(&auto_content_at,(LONG64)GetTickCount64());
   InterlockedExchange(&auto_shown_x100,sv);InterlockedExchange64(&auto_shown_at,(LONG64)GetTickCount64());cAt=cpuEnd.QuadPart;cCount=m.accepted_sources;cGen=current.generated;}}}
 if(InterlockedCompareExchange(&reco_trial,0,0)&&f->ticks%30==0&&stream_stage_stats&&f->group==reco_group){StageStats st={0};if(stream_stage_stats(f->stream,&st)==0){AcquireSRWLockExclusive(&stage_lock);ui_stage=st;ui_stage_valid=1;ReleaseSRWLockExclusive(&stage_lock);}}
 if(f->ticks==60||f->ticks==300||f->ticks%1200==0){CsStats stats={0};stream_stats(f->stream,&stats);
  if(stream_live_metrics){LiveMetrics m={0};stream_live_metrics(f->stream,&m);logline("FG_LIVE sources=%llu resets=%llu expired=%llu completed=%llu discarded=%llu workers=%d source_age_ms=%.3f",(unsigned long long)m.accepted_sources,(unsigned long long)m.timestamp_resets,(unsigned long long)m.expired_jobs,(unsigned long long)m.completed_jobs,(unsigned long long)m.discarded_results,m.workers,m.last_source_age_ns/1000000.0);}
  logline("FG_POOL_STATS fallback_allocations=%llu",(unsigned long long)f->pool_fallbacks);
  if(stream_gpu_info){const char* g=stream_gpu_info(f->stream);static int gpuLogged;if(g&&*g){AcquireSRWLockExclusive(&stage_lock);if(strcmp(ui_gpu_info,g)){snprintf(ui_gpu_info,sizeof ui_gpu_info,"%s",g);gpuLogged=0;}ReleaseSRWLockExclusive(&stage_lock);if(!gpuLogged){gpuLogged=1;logline("FG_GPU %s",g);}}}
  if(stream_stage_stats){StageStats st={0};if(stream_stage_stats(f->stream,&st)==0){AcquireSRWLockExclusive(&stage_lock);ui_stage=st;ui_stage_valid=1;ReleaseSRWLockExclusive(&stage_lock);
   logline("FG_STAGES samples=%llu in_ms=%.2f gpu_ms=%.2f out_ms=%.2f total_ms=%.2f gpu_max_ms=%.2f total_max_ms=%.2f busy_pct=%.0f keep_ms=%.3f keep_dispatches=%llu",(unsigned long long)st.samples,st.in_ms,st.gpu_ms,st.out_ms,st.total_ms,st.gpu_max_ms,st.total_max_ms,st.busy_pct,st.keep_ms,(unsigned long long)st.keep_dispatches);}}
  logline("FG_CPU samples=%llu p95_upper_ms=%.2f max_ms=%.3f",(unsigned long long)f->cpu.count,fps_gap_p95(&f->cpu),f->cpu.maximum);f->cpu=(CsCadence){0};
  logline("FG_STATS ticks=%llu generated=%llu repeat=%llu static=%llu cuts=%llu overwritten=%llu failures=%llu ready=%d last_ms=%.3f deadline_misses=%llu group=%p",
   (unsigned long long)stats.ticks,(unsigned long long)stats.generated,(unsigned long long)stats.repeated,
   (unsigned long long)stats.static_pairs,(unsigned long long)stats.cuts,(unsigned long long)stats.overwritten,
   (unsigned long long)stats.failures,stats.ready,stats.last_inference_ms,(unsigned long long)stream_deadlines(f->stream),f->group);
 }
 return 1;
}
/* Test only (CS_TEST_SPACING=1): the host's rate step can hand us the in-between buffer ~1 ms after
 * the original (measured live: 30-47% of 120 fps buffers bunched, ~90 fps on screen). Hold each output
 * buffer until one output interval after the previous one, never more than 0.9 interval past its
 * arrival, so a fast source cannot build up delay. Late buffers restart the schedule. */
static int frame_spacing,frame_sink_paced;static double frame_space_cap=0.9; /* CS_TEST_SPACE_CAP: max hold past arrival, in output intervals */
static void frame_space(FrameContext *f,int tn,int td){
 LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
 int64_t frame=(int64_t)((long double)freq.QuadPart*td/tn),next=f->space_next?f->space_next+frame:now.QuadPart;
 if(next<now.QuadPart)next=now.QuadPart;if(next>now.QuadPart+(int64_t)(frame*frame_space_cap))next=now.QuadPart+(int64_t)(frame*frame_space_cap);
 f->space_next=next;++f->space_frames;
 if(next>now.QuadPart){
  static _Thread_local HANDLE timer;if(!timer)timer=CreateWaitableTimerExW(NULL,NULL,0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */,TIMER_ALL_ACCESS);
  ++f->space_waits;f->space_wait_ms+=1000.0*(next-now.QuadPart)/freq.QuadPart;
  for(;;){QueryPerformanceCounter(&now);int64_t left=next-now.QuadPart;if(left<=0)break;double ms=1000.0*left/freq.QuadPart;
   if(ms>0.7&&timer){LARGE_INTEGER due;due.QuadPart=-(LONGLONG)((ms-0.5)*10000.0);if(SetWaitableTimer(timer,&due,0,NULL,NULL,FALSE)){WaitForSingleObject(timer,INFINITE);continue;}}
   YieldProcessor();}
 }
 if(f->space_frames%1200==0){logline("FRAME_SPACING frames=%llu waits=%llu avg_wait_ms=%.2f",(unsigned long long)f->space_frames,(unsigned long long)f->space_waits,f->space_waits?f->space_wait_ms/f->space_waits:0);f->space_waits=0;f->space_wait_ms=0;}
}
#include "direct_present.h"
static void tl_mark(int stage,void *buffer);
static int frame_buffer(void *pad,void *info,void *data){
 FrameContext *f=data;LARGE_INTEGER now;proof_delivery.proof=NULL;
 /* 2.0.1 (11-11 night, test copy): SC runs a generation group for every scene preview. The empty second scene
  * (black thumbnail) never changes, yet its group started an engine and its streaming thread used ~0.41 of a core
  * (412 ms/s, as much as the real preview group). Until a group's picture changes once it passes the host buffer
  * through untouched: no engine, no copies. Live content changes within a frame, so real previews start at once.
  * CS_TEST_IDLE_ALL=1 (test only) restores the old behaviour. */
 /* Group in a pipeline other than the large preview's: pass through (see main_pipe). pipe_state 0 unknown, 1 main, 2 other. */
 if(!all_groups&&f->pipe_state!=1){
  if(!f->pipe[0]&&f->group){char *path=object_path(f->group);if(path){char *e=strstr(path+1,"/");size_t n=e?(size_t)(e-path)+1:strlen(path);if(n>=sizeof f->pipe)n=sizeof f->pipe-1;memcpy(f->pipe,path,n);f->pipe[n]=0;mem_free(path);}}
  char known[64];AcquireSRWLockShared(&main_pipe_lock);memcpy(known,main_pipe,sizeof known);ReleaseSRWLockShared(&main_pipe_lock);
  if(known[0]&&f->pipe[0]){int other=strcmp(known,f->pipe)!=0;if(f->pipe_state!=(other?2:1)){f->pipe_state=other?2:1;logline("FG_GROUP_ROLE group=%p pipeline=%s role=%s",f->group,f->pipe,other?"other (pass through)":"main");}if(other)return 1;}
 }
 if(!f->ever_changed&&!idle_all){void *in=probe_buffer(info);CsMap m={0};uint64_t fp=0;int ok=0;
  if(in&&buffer_map(in,&m,1)){size_t step=m.size/1024;step&=~(size_t)3;if(step>=4){for(int i=0;i<1024;i++)fp=fp*31+*(uint32_t*)(m.data+step*i);ok=1;}buffer_unmap(in,&m);}
  if(!ok)f->ever_changed=1; /* cannot look: behave as before */
  else if(f->fp_set&&fp!=f->fp){f->ever_changed=1;if(f->idle_logged)logline("FG_IDLE_END group=%p ticks=%llu",f->group,(unsigned long long)f->idle_ticks);}
  else{f->fp=fp;f->fp_set=1;++f->idle_ticks;if(f->idle_ticks==120&&!f->idle_logged){f->idle_logged=1;logline("FG_IDLE_SKIP group=%p (picture unchanged since start)",f->group);}return 1;}
 }
 if(stage_stamp.group==f->group){QueryPerformanceCounter(&now);stage_stamp.bgr=(uint64_t)now.QuadPart;}
 /* 1.7.8 diagnostics (always on): how many output ticks each input picture is held. At 60->120 every picture should
  * span 2 ticks; spans of 1 and 3 mean the arrival phase sits on a tick boundary, and a 120->60 thinning (stream encoder
  * branch) then repeats one picture and skips the next. Suspected cause of the intermittent 60 fps stream duplicates
  * (11-07: clusters about 240 s apart, matching ~35-40 ppm capture/host clock drift). 4096 sampled words per tick. */
 {static uint32_t seen[4096];static uint64_t run,spans[5],ticks;void *in=probe_buffer(info);CsMap m={0};tick_new=1;
  if(in&&buffer_map(in,&m,1)){size_t step=m.size/4096;int same=step>=4;if(same){step&=~(size_t)3;for(int i=0;i<4096;i++){uint32_t v=*(uint32_t*)(m.data+step*i);if(v!=seen[i]){same=0;seen[i]=v;}}}buffer_unmap(in,&m);tick_new=!same;}tick_new_dump=tick_new;
  if(tick_new){if(run)++spans[run<4?run:4];run=1;}else ++run;
  if(++ticks%1200==0){logline("FG_INPUT_SPANS ticks=%llu span1=%llu span2=%llu span3=%llu span4p=%llu",(unsigned long long)ticks,(unsigned long long)spans[1],(unsigned long long)spans[2],(unsigned long long)spans[3],(unsigned long long)spans[4]);memset(spans,0,sizeof spans);}}
 /* Test timeline: the preview and the record/stream route each run a group; stage + 10 x group slot keeps them apart. */
 {static const void *slots[4];int k=0;while(k<4&&slots[k]&&slots[k]!=f->group)++k;if(k<4&&!slots[k])slots[k]=f->group;f->tl_slot=k<4?k:3;}
 {static volatile LONG named[4];if(!InterlockedExchange(&named[f->tl_slot],1))logline("FG_THREAD slot=%d tid=%lu group=%p",f->tl_slot,GetCurrentThreadId(),f->group);}
 tl_mark(1+10*f->tl_slot,probe_buffer(info));
 int result=frame_buffer_impl(pad,info,data);
 tl_mark(2+10*f->tl_slot,probe_buffer(info));
 if(stage_stamp.group==f->group){QueryPerformanceCounter(&now);stage_stamp.processed=(uint64_t)now.QuadPart;}
 /* Also with generation off: the preview still runs at the generation rate (host duplicates), and
  * unpaced host pairs showed as 83-86% even refreshes (cand-off). CS_TEST_SINK_ONLY=1 (test) skips it
  * while generating under the sink phase lock: lower latency, but host pairing returns. */
 if(frame_spacing&&!synthetic&&!InterlockedCompareExchange(&low_view,0,0)&&!(frame_sink_paced&&InterlockedCompareExchange(&frame_generating,0,0))){int tn=0,td=1;if(f->applied!=-1&&(f->applied&1)){tn=f->tn;td=f->td;}else{AutoRate a=auto_snapshot();if(a.live&&!InterlockedCompareExchange(&auto_gpu_slow,0,0)){tn=a.tn;td=a.td;}}
  if(tn>0&&td>0)frame_space(f,tn,td);else f->space_next=0;}
 if(direct_mode&&InterlockedCompareExchange(&frame_generating,0,0))direct_tap(probe_buffer(info),f->width,f->height);
 /* 2.2.0 beta: low latency view also shows the originals when generation is off (the engine may never have started). */
 else if(direct_mode&&InterlockedCompareExchange(&low_view,0,0)){void *caps=current_caps(pad),*s=caps?caps_structure(caps,0):NULL;int w=0,h=0;
  if(s&&structure_int(s,"width",&w)&&structure_int(s,"height",&h)&&w>0&&h>0)direct_tap(probe_buffer(info),w,h);if(caps)caps_unref(caps);}
 return result;
}
static int load_engine(void){
 if(stream_create)return 1;
 DWORD n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",engine_dir,32768);
 if(!n||n>32000)return 0;
 wchar_t dll[32768];swprintf(dll,32768,L"%ls\\consolesmooth_engine.dll",engine_dir);
 HMODULE m=LoadLibraryExW(dll,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
 if(!m){logline("FG_ENGINE_LOAD_FAILED win32=%lu",GetLastError());return 0;}
 *(FARPROC*)&stream_create=GetProcAddress(m,synthetic?"cs_stream_create":"cs_live_create");
 if(!synthetic)*(FARPROC*)&live_create_format=GetProcAddress(m,"cs_live_create_format");
 *(FARPROC*)&stream_tick=GetProcAddress(m,"cs_stream_tick");
 *(FARPROC*)&stream_stats=GetProcAddress(m,synthetic?"cs_stream_stats":"cs_live_stats");
 *(FARPROC*)&stream_destroy=GetProcAddress(m,synthetic?"cs_stream_destroy":"cs_live_destroy");
 *(FARPROC*)&stream_configure=GetProcAddress(m,synthetic?"cs_stream_configure_fraction":"cs_live_configure");
 *(FARPROC*)&stream_deadlines=GetProcAddress(m,synthetic?"cs_stream_deadline_misses":"cs_live_deadlines");
 if(!synthetic){*(FARPROC*)&stream_set_buffer=GetProcAddress(m,"cs_live_set_buffer_ms");*(FARPROC*)&stream_set_quarter=GetProcAddress(m,"cs_live_set_quarter");*(FARPROC*)&stream_set_low_buffer=GetProcAddress(m,"cs_live_set_low_buffer_ms");*(FARPROC*)&stream_set_options=GetProcAddress(m,"cs_live_set_options");*(FARPROC*)&stream_buffer_ns=GetProcAddress(m,"cs_live_buffer_ns");*(FARPROC*)&stream_tick_at=GetProcAddress(m,"cs_live_tick");*(FARPROC*)&stream_live_metrics=GetProcAddress(m,"cs_live_metrics");*(FARPROC*)&stream_stage_stats=GetProcAddress(m,"cs_live_stage_stats");*(FARPROC*)&stream_gpu_info=GetProcAddress(m,"cs_live_gpu_info");*(FARPROC*)&engine_set_gpu_options=GetProcAddress(m,"cs_set_gpu_options");}
 if(!stream_create||!stream_tick||!stream_stats||!stream_destroy||!stream_configure||!stream_deadlines||(!synthetic&&(!stream_tick_at||!stream_live_metrics||!stream_buffer_ns||!stream_set_buffer||!live_create_format))){stream_create=NULL;FreeLibrary(m);return 0;}
 // Keep module loaded until process exit; live pad callbacks own worker handles.
 {const char* (*version)(void);*(FARPROC*)&version=GetProcAddress(m,"cs_engine_version");logline("FG_ENGINE_LOADED pid=%lu engine=%s",GetCurrentProcessId(),version?version():"unknown");}return 1;
}
/* Catch-up guard at the bin entry. The compositor, download, conversion, generation and upload
 * share one streaming thread; when the requested output rate exceeds what that thread sustains
 * (about 175 fps at FHD, so 4x = 240 fps), output falls behind real time and a later lower rate
 * keeps draining the backlog for tens of seconds. Buffers whose wall-clock lag grew more than
 * max(50 ms, 3 frames) beyond the best lag seen are dropped before any work is done on them. */
typedef struct {int64_t baseline,lastPts,lateSinceNs,lastLogNs;uint64_t dropped,reported;int init;} CatchUp;
/* At most one CATCHUP log line per second: per-episode lines filled the 20000-line trace cap within minutes at 4x. */
static void catch_up_report(CatchUp *c,int64_t nowNs){if(c->dropped!=c->reported&&nowNs-c->lastLogNs>=1000000000LL){logline("CATCHUP dropped=%llu",(unsigned long long)(c->dropped-c->reported));c->reported=c->dropped;c->lastLogNs=nowNs;}}
_Static_assert(sizeof(void*)==8,"GstBuffer pts offset assumes Win64");
static int64_t catch_up_pts(void *buffer){return *(int64_t*)((char*)buffer+72);} /* GstMiniObject(64)+pool(8) */
static int catch_up_probe(void *pad,void *info,void *data){
 (void)pad;CatchUp *c=data;void *buffer=probe_buffer(info);if(!buffer)return 1;
 int64_t pts=catch_up_pts(buffer);if(pts<0)return 1; /* GST_CLOCK_TIME_NONE */
 LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
 int64_t nowNs=(int64_t)((long double)now.QuadPart*1e9/freq.QuadPart),lag=nowNs-pts;
 /* Restart the reference on discontinuities (pipeline restart, seek, timestamp jump). */
 if(!c->init||pts<c->lastPts||pts-c->lastPts>1000000000LL){c->baseline=lag;c->init=1;c->lateSinceNs=0;}
 c->lastPts=pts;if(lag<c->baseline)c->baseline=lag;
 AutoRate a=auto_snapshot();int64_t frameNs=a.tn>0?(int64_t)(1000000000LL*a.td/a.tn):8333333;
 int64_t limit=frameNs*3>50000000LL?frameNs*3:50000000LL;
 if(lag-c->baseline<=limit){c->lateSinceNs=0;catch_up_report(c,nowNs);return 1;}
 if(!c->lateSinceNs)c->lateSinceNs=nowNs;
 /* Give up after 2 s of dropping without catching up: the reference itself was too early. */
 if(nowNs-c->lateSinceNs>2000000000LL){logline("CATCHUP_REBASE dropped=%llu lag_ms=%.1f",(unsigned long long)(c->dropped-c->reported),(lag-c->baseline)/1e6);c->reported=c->dropped;c->baseline=lag;c->lateSinceNs=0;return 1;}
 c->dropped++;catch_up_report(c,nowNs);return 0; /* GST_PAD_PROBE_DROP */
}
static void catch_up_free(void *data){HeapFree(GetProcessHeap(),0,data);}
#include "gpu_probe.h" /* 2.3.3: GPU path in public and internal builds */
static void *make_frame_filter(void){
 if(!load_engine())return NULL;
 if(!synthetic){void *gb=gpu_out_bin();if(gb)return gb;}
 void *error=NULL;
 /* Live: BGRA straight through (no CPU colour conversion). Synthetic test stream: bgr24 engine input. */
 void *bin=parse_bin(synthetic
  ?"d3d11download name=cs_entry ! videoconvert n-threads=8 ! video/x-raw,format=BGR ! identity name=cs_framegen ! videoconvert n-threads=8 ! video/x-raw,format=BGRA ! d3d11upload"
  :"d3d11download name=cs_entry ! video/x-raw,format=BGRA ! identity name=cs_framegen ! d3d11upload",1,&error);
 if(error){logline("FG_PARSE_FAILED %s",((CsError*)error)->message);error_free(error);if(bin)object_unref(bin);return NULL;}
 if(!bin)return NULL;
 void *element=bin_find(bin,"cs_framegen"),*pad=element?static_pad(element,"src"):NULL;
 FrameContext *f=pad?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*f)):NULL;
 if(f){f->applied=-1;f->group=bin;}
 int ok=f&&add_probe(pad,1<<4,frame_buffer,f,frame_cleanup);
 if(!ok&&f)frame_cleanup(f);
 if(pad)object_unref(pad);if(element)object_unref(element);
 if(ok&&interactive_ui&&!synthetic){void *entry=bin_find(bin,"cs_entry"),*sink=entry?static_pad(entry,"sink"):NULL;
  CatchUp *c=sink?HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*c)):NULL;
  if(c&&!add_probe(sink,1<<4,catch_up_probe,c,catch_up_free))catch_up_free(c);
  if(sink)object_unref(sink);if(entry)object_unref(entry);}
 if(!ok){object_unref(bin);return NULL;}return bin;
}



