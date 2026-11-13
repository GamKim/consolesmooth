// SPDX-License-Identifier: MIT
// Force worker backlog, pending replacement and epoch changes while verifying
// immutable worker inputs and byte-exact bypass. Test-only slow model, no GPU.
#include "engine.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
extern "C" __declspec(dllimport) uint64_t cs_test_calls();
extern "C" __declspec(dllimport) uint64_t cs_test_mutations();
int main(){
 void* s=cs_stream_create(L"slow-model",-1,64,64);if(!s)return 2;
 CsStreamStats stats{};for(int i=0;i<100;i++){cs_stream_stats(s,&stats);if(stats.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(stats.ready!=1)return 3;
 std::vector<uint8_t> in(64*64*3),out(in.size());bool pass=true;
 timeBeginPeriod(1); // Test process only, to force a producer faster than 15 ms.
 for(int t=0;t<2000;t++){
  if(t%53==0){cs_stream_configure(s,0,60,0);memset(in.data(),111,in.size());pass&=cs_stream_tick(s,in.data(),out.data())==0&&!memcmp(in.data(),out.data(),in.size());cs_stream_configure_rate(s,1,t%2?30:60,t%2?150:120,t%2);}
  memset(in.data(),30+t%35,in.size());pass&=cs_stream_tick(s,in.data(),out.data())==0;
  memset(in.data(),231,in.size()); // caller may reuse its buffer immediately
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 cs_stream_stats(s,&stats);cs_stream_destroy(s);timeEndPeriod(1);
 pass&=stats.failures==0&&stats.overwritten>0&&cs_test_calls()>20&&cs_test_mutations()==0;
 printf("{\"pass\":%s,\"ticks\":%llu,\"worker_calls\":%llu,\"input_mutations\":%llu,\"overwritten\":%llu,\"failures\":%llu}\n",pass?"true":"false",(unsigned long long)stats.ticks,(unsigned long long)cs_test_calls(),(unsigned long long)cs_test_mutations(),(unsigned long long)stats.overwritten,(unsigned long long)stats.failures);return pass?0:4;
}
