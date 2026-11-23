// SPDX-License-Identifier: MIT
#define CS_ENGINE_BUILD
#include "engine.h"
#include "rate_math.h"
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cstdlib>
extern "C" __declspec(dllimport) void cs_test_set_reported_ms(int ms);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 2;}}while(0)
int main(){
 printf("{\"cpu_model\":true,\"cases\":[");bool comma=false;
 for(int target: {120,180,240})for(int actual: (target==240?std::vector<int>{240,180,120}:std::vector<int>{target,120})){
  void* p=cs_live_create_workers(L"test",-1,32,32,2);CHECK(p);CsStreamStats stats{};
  for(int i=0;i<1000;i++){cs_live_stats(p,&stats);if(stats.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(stats.ready==1);
  CHECK(!cs_live_configure(p,1,60,1,target,1,0));CsRatePlan plan{};CHECK(cs_rate_fraction(60,1,target,1,0,&plan));
  std::vector<uint8_t> in(3072),out(3072),duplicate(3072);int checked=0,worst=0,previous=-1;
  for(int k=0;k<actual;k++){
   uint64_t ns=1000000000ull+(uint64_t)((long double)k*1000000000/actual);
   int source=k*60/actual;memset(in.data(),source,3072);CHECK(!cs_live_tick(p,in.data(),out.data(),ns));
   CHECK(out[0]>=previous);previous=out[0];
   if(actual==target&&k>=plan.delay&&((k-plan.delay)% (target/60))==0){int expected=(k-plan.delay)*60/target;worst=std::max(worst,std::abs((int)out[0]-expected));++checked;}
   if(k>10){CsStreamStats prior{},after{};cs_live_stats(p,&prior);CHECK(!cs_live_tick(p,in.data(),duplicate.data(),ns+1));cs_live_stats(p,&after);CHECK(!memcmp(out.data(),duplicate.data(),3072));CHECK(prior.generated==after.generated);}
   std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  CsLiveMetrics m{};cs_live_metrics(p,&m);CHECK(m.accepted_sources==60);CHECK(worst==0);CHECK(m.completed_jobs>10);
  // With a fast model and sufficient delivery cadence, every intermediate
  // phase must be eligible, including phases before the middle of each pair.
  cs_live_stats(p,&stats);
  if(actual==target)CHECK(stats.generated>=(uint64_t)(50*(target/60-1)));
  CHECK(!cs_live_configure(p,0,60,1,target,1,0));memset(in.data(),231,3072);CHECK(!cs_live_tick(p,in.data(),out.data(),3000000000ull));CHECK(!memcmp(in.data(),out.data(),3072));
  CHECK(!cs_live_configure(p,1,60,1,target,1,0));CHECK(!cs_live_tick(p,in.data(),out.data(),4000000000ull));CHECK(!cs_live_tick(p,in.data(),out.data(),3990000000ull));cs_live_metrics(p,&m);CHECK(m.timestamp_resets==1);
  cs_live_stats(p,&stats);CHECK(!stats.failures);cs_live_destroy(p);
  printf("%s{\"target\":%d,\"delivery\":%d,\"accepted_sources\":60,\"original_checks\":%d,\"max_original_error\":%d,\"off_exact\":true,\"clock_reset\":true}",comma?",":"",target,actual,checked,worst);comma=true;
 }
 // Restart at different points in a held input frame, with alternating jitter. The stub normally reports a
 // 100 ms stall; the low latency budget (25 ms buffer minus half a tick) then rejects every job after the first,
 // which is correct behaviour but not what this block checks, so it reports a cost that fits the buffer.
 cs_test_set_reported_ms(5);
 for(int phase: {0,2000000,4000000,7000000}){
  void* p=cs_live_create_workers(L"test",-1,32,32,1);CHECK(p);CsStreamStats st{};
  for(int i=0;i<1000;i++){cs_live_stats(p,&st);if(st.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(st.ready==1);
  CHECK(!cs_live_configure(p,1,60,1,120,1,0));std::vector<uint8_t> in(3072),out(3072);int prev=-1;
  for(int k=0;k<120;k++){
   uint64_t offset=(uint64_t)((long double)k*1000000000/120)+(k%2?4000000:0);
   int source=(int)((offset+phase+100)*60/1000000000ull);
   std::fill(in.begin(),in.end(),(uint8_t)(source*3));
   CHECK(!cs_live_tick(p,in.data(),out.data(),1000000000ull+offset));CHECK(out[0]>=prev);prev=out[0];
   std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  cs_live_stats(p,&st);fprintf(stderr,"phase=%d generated=%llu\n",phase,(unsigned long long)st.generated);CHECK(st.generated>=50);CHECK(!st.failures);cs_live_destroy(p);
 }
 // 1.0.1: 120 fps input -> 240 output (2x). Every original keeps its slot order and phases are generated.
 {void* p=cs_live_create_workers(L"test",-1,32,32,1);CHECK(p);CsStreamStats st{};
  for(int i=0;i<1000;i++){cs_live_stats(p,&st);if(st.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(st.ready==1);
  CHECK(!cs_live_configure(p,1,120,1,240,1,0));CsRatePlan plan{};CHECK(cs_rate_fraction(120,1,240,1,0,&plan));CHECK(cs_rate_fraction(120000,1001,240000,1001,0,&plan));
  CHECK(!cs_rate_fraction(120,1,360,1,0,&plan));std::vector<uint8_t> in(3072),out(3072);int prev=-1;
  for(int k=0;k<480;k++){
   uint64_t ns=1000000000ull+(uint64_t)((long double)k*1000000000/240);int source=k/2;
   std::fill(in.begin(),in.end(),(uint8_t)(source&255));CHECK(!cs_live_tick(p,in.data(),out.data(),ns));
   if(source<250){CHECK(out[0]>=prev);prev=out[0];}
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  cs_live_stats(p,&st);CsLiveMetrics m{};cs_live_metrics(p,&m);fprintf(stderr,"120->240 generated=%llu accepted=%llu%c",(unsigned long long)st.generated,(unsigned long long)m.accepted_sources,10);
  CHECK(m.accepted_sources>=200);CHECK(st.generated>=100);CHECK(!st.failures);cs_live_destroy(p);
 }
 cs_test_set_reported_ms(100);
 // The next blocks place callbacks by hand inside one slot; the callback smoothing (1.4.x) is not what they check.
 _wputenv(L"CS_LIVE_SMOOTH=0");
 // A GPU completion between two callbacks in the same rounded slot must be used.
 {void* p=cs_live_create_workers(L"slow",-1,32,32,1);CHECK(p);CsStreamStats st{};
  for(int i=0;i<1000;i++){cs_live_stats(p,&st);if(st.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(st.ready==1);
  CHECK(!cs_live_configure(p,1,60,1,120,1,0));std::vector<uint8_t> in(3072,20),out(3072);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1000000000ull));std::fill(in.begin(),in.end(),40);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1016666667ull));
  CHECK(!cs_live_tick(p,in.data(),out.data(),1033333334ull));CHECK(out[0]==20);
  for(int i=0;i<2000;i++){CsLiveMetrics m{};cs_live_metrics(p,&m);if(m.completed_jobs)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  CHECK(!cs_live_tick(p,in.data(),out.data(),1034000000ull));CHECK(out[0]==30);cs_live_stats(p,&st);CHECK(st.generated==1);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1035000000ull));CHECK(out[0]==30);cs_live_stats(p,&st);CHECK(st.generated==1);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1042000000ull));CHECK(out[0]==40);cs_live_destroy(p);
 }
 // At 4x, late source arrival must not erase a still-eligible ready phase.
 {void* p=cs_live_create_workers(L"slow",-1,32,32,1);CHECK(p);CsStreamStats st{};
  for(int i=0;i<1000;i++){cs_live_stats(p,&st);if(st.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(st.ready==1);
  CHECK(!cs_live_configure(p,1,60,1,240,1,0));std::vector<uint8_t> in(3072,20),out(3072);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1000000000ull));std::fill(in.begin(),in.end(),40);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1016666667ull));for(int i=0;i<2000;i++){CsLiveMetrics m{};cs_live_metrics(p,&m);if(m.completed_jobs)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  std::fill(in.begin(),in.end(),60);CHECK(!cs_live_tick(p,in.data(),out.data(),1039000000ull));
  // First phase is now 0.25; the old midpoint-first policy omitted this phase.
  CHECK(out[0]==25);cs_live_stats(p,&st);CHECK(st.generated==1);cs_live_destroy(p);
 }
 puts("],\"restart_phase_cases\":4,\"same_slot_completion\":true,\"four_x_clock_expiry\":true,\"pass\":true}");return 0;
}


