/* SPDX-License-Identifier: MIT
 * GStreamer tracer with opt-in, initial-state identity insertion.
 * No vendor binary patching. FrameGen/Control process the shared display output;
 * native recording branches select original buffers or the generated output.
 * ABI signatures checked against GStreamer 1.18.0 and GLib public APIs.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stddef.h>
typedef uintptr_t GType;
typedef struct { GType type; const char *name; unsigned class_size, instance_size; } TypeQuery;
typedef struct {
 int major,minor; const char *name,*description; int (*init)(void*);
 const char *version,*license,*source,*package,*origin,*date; void *reserved[4];
} PluginDesc;
static GType (*tracer_type)(void);
static void (*type_query)(GType,TypeQuery*);
static GType (*type_register)(GType,const char*,unsigned,void*,unsigned,void(*)(void*,void*),unsigned);
static int (*tracer_register)(void*,const char*,GType);
static void (*hook_register)(void*,const char*,void*);
static char *(*object_path)(void*);
static void *(*element_factory)(void*);
static void (*mem_free)(void*);
static void *(*object_parent)(void*);
static void (*object_unref)(void*);
static void *(*factory_make)(const char*,const char*);
static void *(*static_pad)(void*,const char*);
static void *(*pad_peer)(void*);
static int (*bin_add)(void*,void*),(*bin_remove)(void*,void*);
static int (*pad_unlink)(void*,void*),(*pad_link)(void*,void*);
static int (*get_state)(void*,int*,int*,uint64_t);
static unsigned long (*add_probe)(void*,int,int(*)(void*,void*,void*),void*,void(*)(void*));
static void *(*probe_buffer)(void*),*(*current_caps)(void*);
static char *(*caps_string)(void*);
static void (*caps_unref)(void*);
static void *(*caps_copy)(const void*);
static void (*caps_set_simple)(void*,const char*,...);
static void (*property_get)(void*,const char*,...),(*property_set)(void*,const char*,...);
static GType (*fraction_type)(void);
static int insert_enabled,preview120,sink_pacing;
static void *(*object_get_data)(void*,unsigned);
static void (*object_set_data)(void*,unsigned,void*);
static unsigned root_filter_key,record_guard_key;
static void *(*mini_ref)(void*),*(*mini_get_data)(void*,unsigned);
static void (*mini_set_data)(void*,unsigned,void*,void(*)(void*));
static unsigned (*quark_static)(const char*);
static unsigned original_key;
static _Thread_local void *original_pending;
static int original_dedup;static unsigned tick_new_key;static _Thread_local int tick_new;
static volatile LONG record_generated,record_branches,record_fault;
static _Thread_local int inserting;
typedef struct { LONG count; LARGE_INTEGER start; char path[1024]; int meter; uint64_t frequency;const void *group; } Watch;
static SRWLOCK lock=SRWLOCK_INIT;
static unsigned lines;
static wchar_t logpath[32768];
/* Recent lines for the diagnostics export, kept without a trace log. Bounded, no allocation. */
#define LOG_RING_LINES 400
static char log_ring[LOG_RING_LINES][200];
static unsigned log_ring_next;
static void logline(const char *fmt,...) {
 char buf[4096]; va_list ap; va_start(ap,fmt); int n=vsnprintf(buf,sizeof(buf)-3,fmt,ap);va_end(ap);
 if(n<0)return; if(n>(int)sizeof(buf)-3)n=sizeof(buf)-3;
 buf[n]=0;AcquireSRWLockExclusive(&lock);snprintf(log_ring[log_ring_next++%LOG_RING_LINES],200,"%s",buf);
 if(!logpath[0]){ReleaseSRWLockExclusive(&lock);return;}
 buf[n++]='\r';buf[n++]='\n';
 if(lines++<20000){HANDLE f=CreateFileW(logpath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 if(f!=INVALID_HANDLE_VALUE){DWORD written;WriteFile(f,buf,n,&written,NULL);CloseHandle(f);}}
 ReleaseSRWLockExclusive(&lock);
}
#include "framegen.h"
#include "auto_capture.h"
/* The host is closing (its window is being destroyed). */
/* The settings panel exists again (the host rebuilt its window): device polling may run. */
static void ui_host_opened(void){InterlockedExchange(&auto_stopping,0);if(InterlockedCompareExchange(&auto_capture_count,0,0)>0)device_start();}
static void ui_host_closing(void){
 direct_close(-2);hdr_restore(); /* closing the host with the HDR full screen open left Windows HDR on */
 if(device_thread)WaitForSingleObject(device_thread,1500); /* the device poll ends before the host shuts the SDK down */
}
/* On by default since 1.7.9 (CS_TEST_ORIGINAL_DEDUP=0 turns it off): the host steps its compositor to the generation rate, and the encoder branch
 * thins that back with videorate. Sampling two unlocked clocks repeated 20-27% of frames in a 60 fps stream (plain host: 0).
 * frame_buffer marks each tick whose picture changed; the original route forwards only those. */
typedef struct {int generated;uint64_t count,restored,missing,dropped;int64_t last_pts;} RecordWatch;
static AutoRate record_rate;
static int record_mode;
static void record_watch_free(void *data){RecordWatch *w=data;logline("RECORD_ROUTE_STOP generated_mode=%d buffers=%llu originals=%llu missing=%llu dedup_dropped=%llu",w->generated,(unsigned long long)w->count,(unsigned long long)w->restored,(unsigned long long)w->missing,(unsigned long long)w->dropped);AcquireSRWLockExclusive(&auto_filter_lock);InterlockedDecrement(&record_branches);ReleaseSRWLockExclusive(&auto_filter_lock);HeapFree(GetProcessHeap(),0,w);}
static int record_buffer(void *pad,void *info,void *data){
 (void)pad;RecordWatch *w=data;++w->count;
 /* 2.3.8 (11-18): recording and streaming go on while the low latency view is shown (2.2.0-2.3.7 dropped them; turning the
  * view on mid-recording froze the file). The view window is fed separately, so the record route simply keeps its frames. */
 if(w->generated)return 1;
 /* 2.3.7: GPU path (originals): its buffers carry no original qdata; pass everything except buffers it marked generated. */
 if(InterlockedCompareExchange(&gpu_path_active,0,0)==1){if(cs_buf_generated(probe_buffer(info))){++w->dropped;return 0;}++w->restored;return 1;}
 void *buffer=probe_buffer(info),*original=buffer?mini_get_data(buffer,original_key):NULL;
 if(original_dedup&&buffer){int64_t pts=*(int64_t*)((char*)buffer+72); /* keep a picture at least every 50 ms when the image stands still */
  if(w->count>30&&!mini_get_data(buffer,tick_new_key)&&pts>=0&&pts-w->last_pts<50000000LL){++w->dropped;return 0;}w->last_pts=pts;}
 if(!original){if(!w->missing++)logline("RECORD_ORIGINAL_MISSING fail_closed=1");return 0;}
 ((CsProbeInfo*)info)->data=mini_ref(original);buffer_unref(buffer);++w->restored;return 1;
}
static void record_block(void *src,void *sink){
 /* Disconnect only the just-created recording edge when its guard is absent. */
 InterlockedExchange(&record_fault,1);int disconnected=pad_unlink(src,sink);
 logline("RECORD_ROUTE_BLOCKED guard_install_failed=1 disconnected=%d",disconnected);
}
static int record_watch(void *src,void *sink){
 if(object_get_data(src,record_guard_key))return 1;
 RecordWatch *w=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*w));if(!w){record_block(src,sink);return 0;}
 AcquireSRWLockExclusive(&auto_filter_lock);
 if(!InterlockedCompareExchange(&record_branches,0,0)){
  record_rate=auto_snapshot();if(auto_applied_rate.supported){record_rate.tn=auto_applied_rate.tn;record_rate.td=auto_applied_rate.td;}
  /* 1.7.1: "generated frames" only while generation is really running when the recording starts; otherwise the
   * file kept the generation rate with every image twice (11-07: save the originals instead). */
  int want=InterlockedCompareExchange(&record_generated,0,0),running=InterlockedCompareExchange(&frame_generating,0,0)!=0||InterlockedCompareExchange(&gpu_gen_running,0,0)!=0;
  /* 2.3.9 (11-18): "생성 프레임 포함" records at the chosen output rate from the start, whether generation is on yet or not
   * (while it is off each picture repeats). Turning generation on or off during a recording or stream then just works. */
  record_mode=want;if(want&&!running)logline("RECORD_GENERATED_RATE generation_not_running_yet=1 saving=output_rate");
 }
 w->generated=record_mode;InterlockedIncrement(&record_branches);ReleaseSRWLockExclusive(&auto_filter_lock);
 if(!add_probe(src,1<<4,record_buffer,w,record_watch_free)){record_watch_free(w);record_block(src,sink);return 0;}
 object_set_data(src,record_guard_key,(void*)1);InterlockedExchange(&record_fault,0);
 logline("RECORD_ROUTE_START generated_mode=%d",w->generated);return 1;
}
static AutoRate record_snapshot(int *generated){
 AcquireSRWLockShared(&auto_filter_lock);int active=InterlockedCompareExchange(&record_branches,0,0)>0;
 AutoRate a=active?record_rate:auto_snapshot();*generated=active?record_mode:InterlockedCompareExchange(&record_generated,0,0);
 ReleaseSRWLockShared(&auto_filter_lock);return a;
}
static void stage_remember(void *element,const char *path);
static void on_add(void *self,uint64_t ts,void *bin,void *element,int result){
 (void)self;(void)ts; if(!result)return;
 char *b=object_path(bin),*e=object_path(element);void *f=element_factory(element);char *factory=f?object_path(f):NULL;
 logline("ADD %s <- %s factory=%s",b?b:"?",e?e:"?",factory?factory:"?");stage_remember(element,e);mem_free(b);mem_free(e);mem_free(factory);
}
typedef struct {Watch watch;CsFpsWindow cadence;CsCadence gaps,pre,cpu,post,total;unsigned windows;} MeterWatch;
typedef struct {void *pad;const void *group;uint64_t frequency,entered;CsFpsWindow rate;CsCadence gaps,render;unsigned windows;} ReturnMeter;
static _Thread_local ReturnMeter return_meter;
static void on_push_post(void *self,uint64_t ts,void *pad,int result){
 (void)self;(void)ts;ReturnMeter *m=&return_meter;if(pad!=m->pad||!m->entered)return;
 proof_render_return(m->group,result);
 LARGE_INTEGER now;QueryPerformanceCounter(&now);uint64_t stamp=(uint64_t)now.QuadPart;
 fps_duration(&m->render,1000.0*(stamp-m->entered)/m->frequency,0);m->entered=0;
 AutoRate rate=auto_snapshot();fps_gap(&m->gaps,stamp,m->frequency,rate.tn>0?1000.0*rate.td/rate.tn:0);
 if(fps_sample(&m->rate,stamp,m->frequency)&&++m->windows%10==0){
  logline("RENDER_RETURN_PERF group=%p fps=%.3f intervals=%llu gap_p95_upper_ms=%.2f gap_max_ms=%.3f gaps_over_1_5x=%llu sink_p95_ms=%.2f sink_max_ms=%.3f flow=%d",m->group,m->rate.fps,(unsigned long long)m->gaps.count,fps_gap_p95(&m->gaps),m->gaps.maximum,(unsigned long long)m->gaps.long_gaps,fps_gap_p95(&m->render),m->render.maximum,result);
  m->gaps=(CsCadence){.last=stamp};m->render=(CsCadence){0};
 }
}
static void free_watch(void *p){fps_forget(p);HeapFree(GetProcessHeap(),0,p);}
static int on_buffer(void *pad,void *info,void *data){
 Watch *w=data;LONG n=InterlockedIncrement(&w->count);
 if(w->meter){
  LARGE_INTEGER now;QueryPerformanceCounter(&now);uint64_t stamp=(uint64_t)now.QuadPart;MeterWatch *m=data;
  if(w->meter==1){
   void *out=probe_buffer(info);if(out&&original_dedup)mini_set_data(out,tick_new_key,tick_new?(void*)1:NULL,NULL); /* pool buffers keep qdata, so clear it too */if(out&&original_pending){mini_set_data(out,original_key,original_pending,buffer_unref);original_pending=NULL;}
   if(return_meter.group!=w->group)return_meter=(ReturnMeter){.group=w->group,.frequency=w->frequency};
   return_meter.pad=pad;return_meter.entered=stamp;
   if(n==1||n==1200){void *peer=pad_peer(pad),*sink=peer?object_parent(peer):NULL;
    char *sinkpath=sink?object_path(sink):NULL;
    if(sink&&sinkpath&&strstr(sinkpath,"/GstD3D11VideoSink:")){
     if(n==1&&sink_pacing)property_set(sink,"sync",1,"processing-deadline",(uint64_t)0,"max-lateness",(int64_t)-1,NULL);
     int sync=0;uint64_t deadline=0;property_get(sink,"sync",&sync,"processing-deadline",&deadline,NULL);
     logline("SINK_RUNTIME group=%p sync=%d processing_deadline_ns=%llu experiment=%d",w->group,sync,(unsigned long long)deadline,sink_pacing);
    }mem_free(sinkpath);if(sink)object_unref(sink);if(peer)object_unref(peer);
   }
  }
  if(w->meter==2){stage_stamp=(StageStamp){.group=w->group,.input=stamp};if(original_pending)buffer_unref(original_pending);void *in=probe_buffer(info);original_pending=in?mini_ref(in):NULL;}
  else if(stage_stamp.group==w->group&&stage_stamp.input&&stage_stamp.bgr>=stage_stamp.input&&stage_stamp.processed>=stage_stamp.bgr&&stamp>=stage_stamp.processed){
   double scale=1000.0/w->frequency;
   fps_duration(&m->pre,(stage_stamp.bgr-stage_stamp.input)*scale,0);
   fps_duration(&m->cpu,(stage_stamp.processed-stage_stamp.bgr)*scale,0);
   fps_duration(&m->post,(stamp-stage_stamp.processed)*scale,0);
   fps_duration(&m->total,(stamp-stage_stamp.input)*scale,0);stage_stamp=(StageStamp){0};
  }
  AutoRate rate=auto_snapshot();double expected=rate.tn>0?1000.0*rate.td/rate.tn:0;
  fps_gap(&m->gaps,stamp,w->frequency,expected);
  if(fps_sample(&m->cadence,stamp,w->frequency)){
   if(w->meter==1)fps_publish(w,m->cadence.fps);
   if(++m->windows%10==0){
    logline("%s_PERF uptime_ms=%llu fps=%.3f intervals=%llu gap_p95_upper_ms=%.2f gap_max_ms=%.3f gaps_over_1_5x=%llu group=%p",w->meter==1?"PREVIEW":"UPSTREAM",(unsigned long long)GetTickCount64(),m->cadence.fps,(unsigned long long)m->gaps.count,fps_gap_p95(&m->gaps),m->gaps.maximum,(unsigned long long)m->gaps.long_gaps,w->group);
    if(w->meter==1)logline("STAGE_PERF group=%p matched=%llu pre_p95_ms=%.2f cpu_p95_ms=%.2f post_p95_ms=%.2f total_p95_ms=%.2f total_max_ms=%.3f",w->group,(unsigned long long)m->total.count,fps_gap_p95(&m->pre),fps_gap_p95(&m->cpu),fps_gap_p95(&m->post),fps_gap_p95(&m->total),m->total.maximum);
    m->gaps=(CsCadence){.last=stamp};m->pre=(CsCadence){0};m->cpu=(CsCadence){0};m->post=(CsCadence){0};m->total=(CsCadence){0};
   }
  }
 }
 if(n==1)QueryPerformanceCounter(&w->start);
 if(n==1||n==60||n==300||n==1200){
  void *caps=current_caps(pad);char *s=caps?caps_string(caps):NULL;
  LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
  double seconds=(double)(now.QuadPart-w->start.QuadPart)/(double)freq.QuadPart;
  logline("BUFFER count=%ld pad=%s buffer=%p elapsed=%.4f rate=%.3f caps=%s",n,w->path,probe_buffer(info),seconds,n>1&&seconds>0?(n-1)/seconds:0,s?s:"?");
  mem_free(s);if(caps)caps_unref(caps);
 }
 return 1; /* GST_PAD_PROBE_OK */
}
static void set_preview120(void *sink,int *original_n,int *original_d){
 void *filter=object_parent(sink);if(!filter)return;
 int state=0,pending=0;get_state(filter,&state,&pending,0);
 if(state==1&&pending<=1){
  void *caps=NULL;property_get(filter,"caps",&caps,NULL);
  if(caps){void *copy=caps_copy(caps);caps_unref(caps);
    if(copy){int generated;AutoRate recording=record_snapshot(&generated);(void)generated;
    /* The host's own preview rate, restored while the capture has no signal. */
    void *st=caps_structure(copy,0);int on=0,od=0;if(st&&structure_fraction(st,"framerate",&on,&od)&&on>0&&od>0){*original_n=on;*original_d=od;}
    int rate=framegen_output_fps?framegen_output_fps:120,den=1;
    if(InterlockedCompareExchange(&record_branches,0,0)>0&&recording.supported){rate=recording.tn;den=recording.td;}
    caps_set_simple(copy,"framerate",fraction_type(),rate,den,NULL);
    property_set(filter,"caps",copy,NULL);char *s=caps_string(copy);
    logline("PREVIEW_RATE_REQUEST fps=%d caps=%s",rate,s?s:"?");mem_free(s);caps_unref(copy);
   }
  }
 }
 object_unref(filter);
}
#include "vblank_pacing.h"
#include "stage_probe.h"
static void watch_pad(void *pad,int meter,const void *group){
 Watch *w=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(MeterWatch));if(!w)return;
 LARGE_INTEGER frequency;QueryPerformanceFrequency(&frequency);w->meter=meter;w->frequency=(uint64_t)frequency.QuadPart;w->group=group;
 char *s=object_path(pad);snprintf(w->path,sizeof(w->path),"%s",s?s:"?");mem_free(s);
 if(!add_probe(pad,1<<4,on_buffer,w,free_watch))free_watch(w);
}
/* Only initial NULL-state preview links are eligible. Running graphs are never
 * rewired. Recursion guard covers links made by this callback. A failed insert
 * restores the original edge and logs the rollback result. */
static void insert_identity(void *src,void *sink){
 void *up=object_parent(src),*down=object_parent(sink),*parent=down?object_parent(down):NULL;
 void *up_parent=up?object_parent(up):NULL;
 int a=0,b=0,ap=0,bp=0,c=0,cp=0;
 if(!up||!down||!parent||parent!=up_parent)goto cleanup;
 get_state(up,&a,&ap,0);get_state(down,&b,&bp,0);get_state(parent,&c,&cp,0);
 if(a!=1||b!=1||c!=1||ap>1||bp>1||cp>1){logline("INSERT_SKIPPED states=%d,%d,%d pending=%d,%d,%d",a,b,c,ap,bp,cp);goto cleanup;}
 void *filter=framegen_fps?make_frame_filter():factory_make("identity",NULL);
 if(!filter&&framegen_fps){logline("FG_FALLBACK identity");filter=factory_make("identity",NULL);}
 if(!filter)goto cleanup;
 if(!bin_add(parent,filter)){object_unref(filter);goto cleanup;}
 void *fi=static_pad(filter,"sink"),*fo=static_pad(filter,"src");
 int detached=0,left=-99,right=-99;
 inserting=1;
 if(fi&&fo&&(detached=pad_unlink(src,sink))){left=pad_link(src,fi);if(left==0)right=pad_link(fo,sink);}
 if(left==0&&right==0){
  char *p=object_path(filter);logline("INSERT_OK filter=%s mode=%s",p?p:"?",framegen_fps?"framegen-request":"identity");mem_free(p);
  watch_pad(fi,2,filter);watch_pad(fo,1,filter);stage_attach(fi,"filter_in");stage_attach(fo,"filter_out");
  char *downpath=object_path(down);
  if(downpath&&strstr(downpath,"/GstD3D11VideoSink:")){
  int sync=0,qos=0;int64_t lateness=0,offset=0;uint64_t deadline=0,delay=0,throttle=0;
  property_get(down,"sync",&sync,"qos",&qos,"max-lateness",&lateness,"ts-offset",&offset,"processing-deadline",&deadline,"render-delay",&delay,"throttle-time",&throttle,NULL);
  logline("SINK_CONFIG group=%p sync=%d qos=%d max_lateness_ns=%lld ts_offset_ns=%lld processing_deadline_ns=%llu render_delay_ns=%llu throttle_ns=%llu",filter,sync,qos,(long long)lateness,(long long)offset,(unsigned long long)deadline,(unsigned long long)delay,(unsigned long long)throttle);
  }else logline("SHARED_FRAME_OUTPUT group=%p destination=%s",filter,downpath?downpath:"?");mem_free(downpath);
 }else{
  if(left==0)pad_unlink(src,fi);if(right==0)pad_unlink(fo,sink);
  int restored=detached?pad_link(src,sink):0;
  logline("INSERT_ROLLBACK left=%d right=%d restore=%d",left,right,restored);bin_remove(parent,filter);
 }
 inserting=0;if(fi)object_unref(fi);if(fo)object_unref(fo);
cleanup:
 if(up_parent)object_unref(up_parent);if(parent)object_unref(parent);if(up)object_unref(up);if(down)object_unref(down);
}
static void on_link(void *self,uint64_t ts,void *src,void *sink,int result){
 (void)self;(void)ts;char *a=object_path(src),*b=object_path(sink);
 logline("LINK result=%d %s -> %s",result,a?a:"?",b?b:"?");
 if(preview120&&result==0&&a&&b&&strncmp(a,"/GstPipeline:",13)==0&&strstr(a,"/GstD3D11Compositor:")&&strstr(b,"/GstCapsFilter:")){int on=0,od=0;set_preview120(sink,&on,&od);auto_remember_filter(sink,on,od);}
 if(framegen_fps&&result==0&&!inserting&&a&&b&&strstr(a,"/GstCapsFilter:")&&strstr(b,"/GstTee:")){
  void *owner=object_parent(src);if(owner&&object_get_data(owner,root_filter_key))insert_identity(src,sink);if(owner)object_unref(owner);
 }
 /* Native proxy bridges and encoder input carry their own video caps. Log
  * their declarations before adjusting recording negotiation. */
 if(result==0&&a&&b&&strstr(b,"/GstCapsFilter:")){
  void *owner=object_parent(sink),*caps=NULL;if(owner)property_get(owner,"caps",&caps,NULL);
  if(caps){char *s=caps_string(caps);logline("DECLARED_CAPS %s %s",b,s?s:"?");
   if(framegen_fps&&s&&strstr(s,"video/x-raw")&&strstr(s,"framerate=")&&
      (strstr(a,"/GstProxySrc:")||strstr(a,"/GstD3D11ColorConvert:")||strstr(a,"/GstQueue:"))){
    int guarded=!strstr(a,"/GstProxySrc:")||record_watch(src,sink);
    int generated;AutoRate rate=record_snapshot(&generated);if(guarded&&rate.supported){int encoder=strstr(a,"/GstQueue:")!=NULL;int n=encoder&&!generated?rate.sn:rate.tn,d=encoder&&!generated?rate.sd:rate.td;void *copy=caps_copy(caps);if(copy){caps_set_simple(copy,"framerate",fraction_type(),n,d,NULL);property_set(owner,"caps",copy,NULL);caps_unref(copy);logline("RECORD_RATE_REQUEST target=%d/%d filter=%s",n,d,b);}}
   }mem_free(s);caps_unref(caps);}if(owner)object_unref(owner);
 }
 /* Capture devices: PCIe cards arrive through ksvideosrc; USB cards may use mfvideosrc instead. */
 /* Test only: pace every preview sink, each on its own schedule. */
 if(comp_latency_ms>=0&&result==0&&a&&b&&strstr(b,"/GstD3D11Compositor:"))comp_latency_attach(sink);
 if(stage_mode&&result==0&&a&&b){
  if(strstr(a,"/GstKsVideoSrc:")){stage_attach(src,"capture");tl_attach(src);}
  else if(strstr(a,"/GstProxySrc:")&&strstr(b,"/GstD3D11Compositor:"))stage_attach(sink,"comp_in");
  else if(strstr(b,"/GstD3D11Compositor:"))stage_attach(sink,"comp_in_other");
  else if(strstr(a,"/GstD3D11Compositor:"))stage_attach(src,"comp_out");
  else if(strstr(b,"/GstBin:")&&strstr(b,"/GstQueue:"))stage_attach(sink,"preview_queue_in");
  else if(strstr(a,"/GstD3D11ColorConvert:"))stage_attach(src,"converted");
  if(strstr(b,"/GstBin:")&&strstr(b,"/GstD3D11VideoSink:"))stage_attach(sink,"sink_in");
 }
 if(vblank_pacing&&result==0&&b&&strstr(b,"/GstD3D11VideoSink:"))pace_sink(sink);
 if(stage_mode&&result==0&&b&&strstr(b,"/GstBin:")&&strstr(b,"/GstD3D11VideoSink:"))stage_attach(sink,"sink_out");
 if(result==0&&a&&b&&((strstr(a,"/GstKsVideoSrc:")&&strstr(b,"/GstCapsFilter:"))||strstr(a,"/GstMFVideoSrc:"))){auto_watch_capture(src);
  /* 2.2.4: the pipeline holding the capture source feeds the large preview (a detached GstBin, no pipeline in its path) and
   * recording; scene previews get the capture through proxies. It is the main generation pipeline (framegen.h main_pipe). */
  if(!strncmp(a,"/GstPipeline:",13)){char *e=strstr(a+1,"/");size_t n=e?(size_t)(e-a)+1:0;if(n&&n<sizeof main_pipe){AcquireSRWLockExclusive(&main_pipe_lock);if(strncmp(main_pipe,a,n)||main_pipe[n]){memcpy(main_pipe,a,n);main_pipe[n]=0;InterlockedIncrement(&main_pipe_serial);}ReleaseSRWLockExclusive(&main_pipe_lock);logline("FG_MAIN_PIPELINE %s",main_pipe);}}}
 if(framegen_fps&&result==0&&a&&b&&strncmp(a,"/GstPipeline:",13)==0&&strstr(a,"/GstProxySrc:")&&strstr(b,"/GstAvtD3D11Transformation:"))record_watch(src,sink);
 /* Visual synthetic testing identified the root pipeline sink as the scene
  * thumbnail; the detached GstBin sink is the large editor preview. */
 int target=a&&!framegen_fps&&strncmp(a,"/GstPipeline:",13)==0;
 if(result==0&&!inserting&&target&&b&&strstr(a,"/GstAvtD3D11Transformation:")&&strstr(b,"/GstD3D11VideoSink:")){
  if(insert_enabled)insert_identity(src,sink);else watch_pad(sink,0,NULL);
 }
 mem_free(a);mem_free(b);
}
static volatile LONG native_creating;static void native_activate_with(void *existing); /* native_provider.h */
static void instance_init(void *self,void *klass){
 (void)klass;logline("TRACER_ACTIVE pid=%lu",GetCurrentProcessId());
 hook_register(self,"bin-add-post",(void*)on_add);hook_register(self,"pad-link-post",(void*)on_link);
 hook_register(self,"pad-push-post",(void*)on_push_post);
 if(!InterlockedCompareExchange(&native_creating,0,0))native_activate_with(self); /* created by GStreamer from GST_TRACERS */
 start_ui();
}
#define RESOLVE(dst,mod,name) do { *(FARPROC*)(&dst)=GetProcAddress(mod,name); if(!dst){logline("MISSING %s",name);return 0;} }while(0)
#include "native_guard.h"
#include "native_provider.h"
static int init(void *plugin){
 DWORD n=GetEnvironmentVariableW(L"CS_TRACE_LOG",logpath,32768);if(!n||n>=32768)logpath[0]=0;
 logline("PLUGIN_INIT pid=%lu",GetCurrentProcessId());
 HMODULE gst=GetModuleHandleW(L"gstreamer-1.0-0.dll"),go=GetModuleHandleW(L"gobject-2.0-0.dll"),gl=GetModuleHandleW(L"glib-2.0-0.dll");
 if(!gst||!go||!gl){logline("MISSING_MODULE");return 0;}
 RESOLVE(tracer_type,gst,"gst_tracer_get_type");RESOLVE(type_query,go,"g_type_query");
 RESOLVE(type_register,go,"g_type_register_static_simple");RESOLVE(tracer_register,gst,"gst_tracer_register");
 RESOLVE(hook_register,gst,"gst_tracing_register_hook");RESOLVE(object_path,gst,"gst_object_get_path_string");
 RESOLVE(element_factory,gst,"gst_element_get_factory");RESOLVE(mem_free,gl,"g_free");
 RESOLVE(object_parent,gst,"gst_object_get_parent");RESOLVE(object_unref,gst,"gst_object_unref");
 RESOLVE(factory_make,gst,"gst_element_factory_make");RESOLVE(static_pad,gst,"gst_element_get_static_pad");
 RESOLVE(pad_peer,gst,"gst_pad_get_peer");
 RESOLVE(bin_add,gst,"gst_bin_add");RESOLVE(bin_remove,gst,"gst_bin_remove");
 RESOLVE(pad_unlink,gst,"gst_pad_unlink");RESOLVE(pad_link,gst,"gst_pad_link");
 RESOLVE(get_state,gst,"gst_element_get_state");RESOLVE(add_probe,gst,"gst_pad_add_probe");
 RESOLVE(probe_buffer,gst,"gst_pad_probe_info_get_buffer");RESOLVE(current_caps,gst,"gst_pad_get_current_caps");
 RESOLVE(caps_string,gst,"gst_caps_to_string");RESOLVE(caps_unref,gst,"gst_caps_unref");
 RESOLVE(caps_copy,gst,"gst_caps_copy");RESOLVE(caps_set_simple,gst,"gst_caps_set_simple");
 RESOLVE(property_get,go,"g_object_get");RESOLVE(property_set,go,"g_object_set");
 RESOLVE(mini_ref,gst,"gst_mini_object_ref");RESOLVE(mini_get_data,gst,"gst_mini_object_get_qdata");RESOLVE(mini_set_data,gst,"gst_mini_object_set_qdata");RESOLVE(quark_static,gl,"g_quark_from_static_string");original_key=quark_static("consolesmooth-original-frame");
 RESOLVE(object_get_data,go,"g_object_get_qdata");RESOLVE(object_set_data,go,"g_object_set_qdata");root_filter_key=quark_static("consolesmooth-root-filter");record_guard_key=quark_static("consolesmooth-record-guard");
 RESOLVE(weak_init,go,"g_weak_ref_init");RESOLVE(weak_clear,go,"g_weak_ref_clear");RESOLVE(weak_get,go,"g_weak_ref_get");
 RESOLVE(fraction_type,gst,"gst_fraction_get_type");
 RESOLVE(parse_bin,gst,"gst_parse_bin_from_description");RESOLVE(bin_find,gst,"gst_bin_get_by_name");
 RESOLVE(error_free,gl,"g_error_free");RESOLVE(buffer_unref,gst,"gst_mini_object_unref");
 RESOLVE(buffer_new_allocate,gst,"gst_buffer_new_allocate");RESOLVE(buffer_copy_into,gst,"gst_buffer_copy_into");RESOLVE(buffer_map,gst,"gst_buffer_map");
 RESOLVE(pool_new,gst,"gst_buffer_pool_new");RESOLVE(pool_get_config,gst,"gst_buffer_pool_get_config");
 RESOLVE(pool_config_params,gst,"gst_buffer_pool_config_set_params");RESOLVE(pool_set_config,gst,"gst_buffer_pool_set_config");
 RESOLVE(pool_set_active,gst,"gst_buffer_pool_set_active");RESOLVE(pool_acquire,gst,"gst_buffer_pool_acquire_buffer");
 RESOLVE(buffer_unmap,gst,"gst_buffer_unmap");RESOLVE(caps_structure,gst,"gst_caps_get_structure");
 RESOLVE(structure_int,gst,"gst_structure_get_int");RESOLVE(structure_string,gst,"gst_structure_get_string");
 RESOLVE(structure_fraction,gst,"gst_structure_get_fraction");
 wchar_t mode[16];insert_enabled=GetEnvironmentVariableW(L"CS_INSERT_IDENTITY",mode,16)==1&&mode[0]==L'1';
 proof_marker=GetEnvironmentVariableW(L"CS_PROOF_MARKER",mode,16)==1&&mode[0]==L'1';
 if(proof_marker)logline("DIAGNOSTIC_OUTPUT_MARKER enabled=1 encoded_actual_generation=1");
 sink_pacing=GetEnvironmentVariableW(L"CS_TEST_SINK_PACING",mode,16)==1&&mode[0]==L'1';
 /* 1.6.4: mode 4 (phase lock: each frame leaves mid-refresh, one refresh after the previous) by default,
  * active only while generating; CS_TEST_VBLANK_PACING=0 turns it off, 1-3 select the older test modes. */
 vblank_pacing=GetEnvironmentVariableW(L"CS_TEST_VBLANK_PACING",mode,16)==1&&(mode[0]>=L'0'&&mode[0]<=L'4')?mode[0]-L'0':4;
 /* Spacing stays on with mode 4: without it the host pairs callbacks again (c-new: repeat 0.3%). */
 frame_sink_paced=vblank_pacing==4&&GetEnvironmentVariableW(L"CS_TEST_SINK_ONLY",mode,16)==1&&mode[0]==L'1';
 if(GetEnvironmentVariableW(L"CS_TEST_PACE_LEAD",mode,16)-1u<15u)pace_lead=_wtof(mode);
 if(GetEnvironmentVariableW(L"CS_TEST_PACE_LATCH",mode,16)-1u<15u)pace_latch=_wtof(mode);
 direct_forced=GetEnvironmentVariableW(L"CS_TEST_DIRECT",mode,16)==1&&mode[0]==L'1';direct_mode=direct_forced;if(direct_forced)logline("TEST_DIRECT enabled=1");
 test_lowview=GetEnvironmentVariableW(L"CS_TEST_LOWVIEW",mode,16)==1&&mode[0]==L'1';if(test_lowview){InterlockedExchange(&low_view,1);logline("TEST_LOWVIEW enabled=1");}
 /* 1.7.9: on by default. 11-08 FG_INPUT_SPANS showed why the original stream repeated frames: every ~255 s the capture
  * and host clocks put source arrivals on a tick boundary for 10-20 s (spans of 1 and 3 ticks instead of 2), and the
  * encoder's 120->60 thinning then sampled one picture twice. CS_TEST_ORIGINAL_DEDUP=0 turns it off (A/B only). */
 dump_init();
 original_dedup=!(GetEnvironmentVariableW(L"CS_TEST_ORIGINAL_DEDUP",mode,16)==1&&mode[0]==L'0');tick_new_key=quark_static("consolesmooth-tick-new");logline("ORIGINAL_DEDUP enabled=%d",original_dedup);
 stage_init(gst,go);
 tl_init();
 if(GetEnvironmentVariableW(L"CS_TEST_DIRECT_WIN",mode,16)-1u<15u)direct_win_w=_wtoi(mode);
 no_thumb=GetEnvironmentVariableW(L"CS_TEST_NO_THUMB",mode,16)==1&&mode[0]==L'1';idle_all=GetEnvironmentVariableW(L"CS_TEST_IDLE_ALL",mode,16)==1&&mode[0]==L'1';all_groups=GetEnvironmentVariableW(L"CS_TEST_ALL_GROUPS",mode,16)==1&&mode[0]==L'1';logline("TEST_DISPLAY direct_win=%d no_thumb=%d",direct_win_w,no_thumb);
 comp_latency_init(gst);
 keep_rate_without_signal=GetEnvironmentVariableW(L"CS_TEST_KEEP_RATE",mode,16)==1&&mode[0]==L'1';
 /* 1.6.4: output spacing on by default; CS_TEST_SPACING=0 turns it off. */
 frame_spacing=!(GetEnvironmentVariableW(L"CS_TEST_SPACING",mode,16)==1&&mode[0]==L'0');
 if(GetEnvironmentVariableW(L"CS_TEST_SPACE_CAP",mode,16)-1u<15u)frame_space_cap=_wtof(mode);
 frame_force_on=GetEnvironmentVariableW(L"CS_TEST_GENERATE",mode,16)==1&&mode[0]==L'1';
 if(GetEnvironmentVariableW(L"CS_TEST_MODEL",mode,16)-1u<15u){int m=_wtoi(mode);if(cs_model_valid(m))frame_force_model=m;}
 if(GetEnvironmentVariableW(L"CS_TEST_TARGET",mode,16)-1u<15u){int t=_wtoi(mode),d=1;wchar_t *slash=wcschr(mode,L'/');if(slash)d=_wtoi(slash+1);if(d>=1&&d<=1001&&t>=30*d&&t<=240*d){auto_force_target=t;auto_force_td=d;}}
 if(GetEnvironmentVariableW(L"CS_TEST_STABLE",mode,16)==1&&(mode[0]==L'0'||mode[0]==L'1'))frame_force_stable=mode[0]-L'0';
 if(vblank_pacing||keep_rate_without_signal||frame_spacing||frame_force_on)logline("TEST_SWITCHES vblank_pacing=%d keep_rate=%d latch=%.2f spacing=%d generate=%ld model=%ld stable=%ld",vblank_pacing,keep_rate_without_signal,pace_latch,frame_spacing,frame_force_on,frame_force_model,frame_force_stable);
 preview120=insert_enabled&&GetEnvironmentVariableW(L"CS_PREVIEW_120",mode,16)==1&&mode[0]==L'1';
 DWORD fpslen=GetEnvironmentVariableW(L"CS_SOURCE_FPS",mode,16);
 if(insert_enabled&&fpslen==2){if(!wcscmp(mode,L"30"))framegen_fps=30;else if(!wcscmp(mode,L"60"))framegen_fps=60;}
 if(framegen_fps)preview120=1;
 synthetic=framegen_fps&&GetEnvironmentVariableW(L"CS_SYNTHETIC",mode,16)==1&&mode[0]==L'1';
 interactive_ui=framegen_fps&&GetEnvironmentVariableW(L"CS_CONTROLS",mode,16)==1&&mode[0]==L'1';
 control_bits=(interactive_ui?0:1)|(framegen_fps==60?2:0);
 framegen_output_fps=framegen_fps*2;
 ui_preferences(0);
 TypeQuery q={0};GType parent=tracer_type();type_query(parent,&q);
 if(!q.class_size||!q.instance_size)return 0;
 GType t=type_register(parent,"ConsoleSmoothTracer",q.class_size,NULL,q.instance_size,instance_init,0);
 int ok=t && tracer_register(plugin,"consolesmooth",t);
 if(ok)ok=native_provider_register(plugin,t,gst,go);
 logline("REGISTER result=%d",ok);return ok;
}
static const PluginDesc desc={1,18,"consolesmoothprobe","ConsoleSmooth frame generation",init,"1.3.0","MIT/X11","console-smooth","console-smooth","https://gstreamer.freedesktop.org/",NULL,{0}};
__declspec(dllexport) const PluginDesc *gst_plugin_consolesmoothprobe_get_desc(void){return &desc;}

