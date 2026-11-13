// SPDX-License-Identifier: MIT
#ifndef CS_ENGINE_BUILD
#define CS_ENGINE_BUILD
#endif
#include "engine.h"
#include "rate_math.h"
#include "frame_limits.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <cstring>
#include <cmath>
#include <memory>
namespace {
struct Pixels {uint64_t seq=0;bool valid=false,generated=false;std::vector<uint8_t> data;};
using SourceData=std::shared_ptr<std::vector<uint8_t>>;
struct SourcePixels {uint64_t seq=0;bool valid=false;SourceData data;};
struct Stream {
 int w,h,gpu,source=60,target=120,delay=3;size_t bytes;std::wstring model;
 std::mutex mutex;std::condition_variable event;std::thread worker;
 bool stop=false,pending=false,enabled=true;uint64_t tick=0,epoch=0,lastSource=UINT64_MAX,late=0;
 uint64_t pendingSeq=0,pendingEpoch=0;int pendingSource=60,pendingTarget=120,pendingDelay=3;
 SourceData sourcePool[10],jobA,jobB;std::vector<uint8_t> middle;SourcePixels sources[4];Pixels results[12];CsStreamStats stats{};
 Stream(const wchar_t* m,int g,int width,int height):w(width),h(height),gpu(g),bytes((size_t)w*h*3),model(m),middle(bytes){
  for(auto& p:sourcePool)p=std::make_shared<std::vector<uint8_t>>(bytes);for(auto& p:results)p.data.resize(bytes);worker=std::thread(&Stream::run,this);
 }
 ~Stream(){{std::lock_guard<std::mutex> l(mutex);stop=true;}event.notify_one();if(worker.joinable())worker.join();}
 void run() noexcept {
  void* engine=nullptr;
  try{
   engine=cs_create(model.c_str(),gpu);
   if(!engine){std::lock_guard<std::mutex> l(mutex);stats.ready=-1;++stats.failures;return;}
   SourceData a,b;std::vector<uint8_t> out(bytes);
#ifndef CS_SCHEDULE_TEST
   // Compile/prime the unchanged full-resolution GPU path before reporting
   // ready. The host continues forwarding live originals during this period.
   // Private buffers cannot alias sources already submitted by another caller.
   {std::vector<uint8_t> warmA(bytes,32),warmB(bytes,48);
    for(int i=0;i<2;i++){
     {std::lock_guard<std::mutex> l(mutex);if(stop){cs_destroy(engine);return;}}
     if(cs_interpolate(engine,warmA.data(),warmB.data(),w,h,.5f,out.data())){
      std::lock_guard<std::mutex> l(mutex);stats.ready=-1;++stats.failures;cs_destroy(engine);return;
     }
    }
   }
#endif
   {std::lock_guard<std::mutex> l(mutex);stats.ready=1;}
   for(;;){
    uint64_t seq,generation;int src,dst,buffer;
    {std::unique_lock<std::mutex> l(mutex);event.wait(l,[&]{return stop||pending;});if(stop)break;
     a=std::move(jobA);b=std::move(jobB);seq=pendingSeq;generation=pendingEpoch;src=pendingSource;dst=pendingTarget;buffer=pendingDelay;pending=false;}
    bool still=memcmp(a->data(),b->data(),bytes)==0;double difference=0;size_t samples=0;
    if(!still)for(size_t i=0;i<bytes;i+=97){difference+=std::abs((int)(*a)[i]-(int)(*b)[i]);++samples;}
    bool cut=samples&&difference/samples>55;
    {std::lock_guard<std::mutex> l(mutex);if(generation!=epoch)continue;stats.static_pairs+=still;stats.cuts+=cut;}
    // Every target time strictly between the two source times gets its own
    // interpolation phase. This handles non-integer ratios such as 60->144.
    uint64_t first=(seq-1)*(unsigned)dst/(unsigned)src+1;
    uint64_t end=(seq*(unsigned)dst+(unsigned)src-1)/(unsigned)src;
    for(uint64_t n=first;n<end;n++){
     {std::lock_guard<std::mutex> l(mutex);if(stop||generation!=epoch)break;if(n+(unsigned)buffer<tick)continue;}
     float phase=(float)(n*(unsigned)src-(seq-1)*(unsigned)dst)/(float)dst;
     bool generated=false,failed=false;double ms=0;
     if(still||cut)memcpy(out.data(),a->data(),bytes);
     else {failed=cs_interpolate(engine,a->data(),b->data(),w,h,phase,out.data())!=0;ms=cs_last_ms(engine);if(failed)memcpy(out.data(),a->data(),bytes);else generated=true;}
     {std::lock_guard<std::mutex> l(mutex);if(generation!=epoch)break;
      auto& r=results[n%12];r.seq=n;r.valid=true;r.generated=generated;r.data.swap(out);stats.failures+=failed;stats.last_inference_ms=ms;}
    }
   }
  }catch(...){std::lock_guard<std::mutex> l(mutex);stats.ready=-1;++stats.failures;}
  cs_destroy(engine);
 }
 int render(const uint8_t* input,uint8_t* output){
  if(!enabled){memcpy(output,input,bytes);return 0;}
  {std::lock_guard<std::mutex> l(mutex);if(stats.ready<0){memcpy(output,input,bytes);++stats.ticks;return 0;}}
  uint64_t j=cs_rate_source(tick,source,target);
  if(j!=lastSource){
   // Only the producer acquires free slots. Four history references, two worker
   // references and two pending references occupy at most eight distinct slots.
   // Ten preallocated slots leave headroom without allocating/waiting per tick.
   // Selection shares the worker's handoff lock: use_count alone is not a
   // synchronization barrier for reads by a recently released worker owner.
   SourceData captured;{std::lock_guard<std::mutex> l(mutex);for(auto& slot:sourcePool)if(slot.use_count()==1){captured=slot;break;}}
   if(!captured){memcpy(output,input,bytes);std::lock_guard<std::mutex> l(mutex);++stats.failures;++stats.ticks;++tick;return 0;}
   memcpy(captured->data(),input,bytes);
   if(lastSource!=UINT64_MAX){
    auto& prev=sources[lastSource%4];
    // Share immutable source frames, not two additional 6 MB copies. A slow
    // worker keeps its own references; replacing a pending pair cannot race it.
    {std::lock_guard<std::mutex> l(mutex);if(pending)++stats.overwritten;jobA=prev.data;jobB=captured;
     pendingSeq=j;pendingEpoch=epoch;pendingSource=source;pendingTarget=target;pendingDelay=delay;pending=true;}
    event.notify_one();
   }
   auto& original=sources[j%4];original.data=std::move(captured);original.seq=j;original.valid=true;lastSource=j;
  }
  bool interpolated=false,generated=false,missed=false;const uint8_t* chosen=sources[0].data?sources[0].data->data():input;
  if(tick>=(unsigned)delay){
   uint64_t n=tick-delay,sourceFrame=cs_rate_source(n,source,target);auto& original=sources[sourceFrame%4];
   if(original.valid&&original.seq==sourceFrame)chosen=original.data->data();else chosen=input;
   interpolated=(n*(unsigned)source)%(unsigned)target!=0;
   if(interpolated){std::lock_guard<std::mutex> l(mutex);auto& r=results[n%12];
    if(r.valid&&r.seq==n){middle.swap(r.data);r.valid=false;chosen=middle.data();generated=r.generated;}else missed=true;}
  }
  memcpy(output,chosen,bytes);
  {std::lock_guard<std::mutex> l(mutex);++stats.ticks;if(interpolated){if(generated)++stats.generated;else ++stats.repeated;}late+=missed;++tick;}
  return 0;
 }
 void configure(bool on,int src,int dst,int buffer){
  std::lock_guard<std::mutex> l(mutex);++epoch;pending=false;tick=0;lastSource=UINT64_MAX;enabled=on;source=src;target=dst;delay=buffer;
  jobA.reset();jobB.reset();for(auto& r:results)r.valid=false;for(auto& s:sources){s.valid=false;s.data.reset();}
 }
};
}
void* cs_stream_create(const wchar_t* dir,int gpu,int w,int h){if(!dir||!cs_native_frame_valid(w,h))return nullptr;try{return new Stream(dir,gpu,w,h);}catch(...){return nullptr;}}
int cs_stream_tick(void* handle,const uint8_t* input,uint8_t* output){if(!handle||!input||!output)return -1;try{return static_cast<Stream*>(handle)->render(input,output);}catch(...){return -2;}}
int cs_stream_stats(void* handle,CsStreamStats* stats){if(!handle||!stats)return -1;auto* s=static_cast<Stream*>(handle);std::lock_guard<std::mutex> l(s->mutex);*stats=s->stats;return 0;}
uint64_t cs_stream_deadline_misses(void* handle){if(!handle)return 0;auto* s=static_cast<Stream*>(handle);std::lock_guard<std::mutex> l(s->mutex);return s->late;}
int cs_stream_configure_fraction(void* handle,int on,int sn,int sd,int tn,int td,int mode){CsRatePlan p{};if(!handle||(mode!=0&&mode!=1)||!cs_rate_fraction(sn,sd,tn,td,mode,&p))return -1;static_cast<Stream*>(handle)->configure(on!=0,p.source_step,p.target_step,p.delay);return 0;}
int cs_stream_configure_rate(void* handle,int on,int src,int dst,int mode){return cs_stream_configure_fraction(handle,on,src,1,dst,1,mode);}
int cs_stream_configure(void* handle,int on,int fps,int mode){return cs_stream_configure_rate(handle,on,fps,fps*2,mode);}
void cs_stream_destroy(void* handle){delete static_cast<Stream*>(handle);}
#ifdef CS_LATENCY_EXPERIMENT
// Linked only into the standalone experiment executable, never the host DLL.
extern "C" int cs_test_delay(void* handle,int delay){
 if(!handle||delay<2||delay>4)return -1;
 static_cast<Stream*>(handle)->configure(true,60,120,delay);return 0;
}
#endif
