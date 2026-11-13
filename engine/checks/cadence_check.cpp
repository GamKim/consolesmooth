// Deterministic check for CS_TEST_CADENCE30 (CPU model, virtual timestamps).
// A 60 fps game must never switch on, a 30 fps game in a 60 fps signal must, and a game that drops
// an occasional frame must not. The switch is visible as the reported buffer (plan + one output interval).
#define CS_ENGINE_BUILD
#include "engine.h"
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 2;}}while(0)
// Content value shown at output tick k (120 Hz) for each scenario.
static int frame60(int k){return (k/2)%250;}
static int frame30(int k){return (k/4)%250;}
static int dropping60(int k){int f=k/2;return (f%6==5?f-1:f)%250;} // one repeated frame in six
static int mixed(int k){return k<480?frame30(k):(1000+k/2)%250;}   // 4 s of 30 fps, then 60 fps
int run(const char* name,int (*content)(int),int ticks,bool expectOnAtEnd,bool expectEverOn,int* outGenerated){
 void* p=cs_live_create_workers(L"test",-1,32,32,1);CHECK(p);CsStreamStats st{};
 for(int i=0;i<1000;i++){cs_live_stats(p,&st);if(st.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(st.ready==1);
 CHECK(!cs_live_configure(p,1,60,1,120,1,0));uint64_t plan=cs_live_buffer_ns(p);
 std::vector<uint8_t> in(3072),out(3072);bool everOn=false;uint64_t last=plan;
 for(int k=0;k<ticks;k++){
  memset(in.data(),content(k),3072);
  CHECK(!cs_live_tick(p,in.data(),out.data(),1000000000ull+(uint64_t)((long double)k*1000000000/120)));
  last=cs_live_buffer_ns(p);if(last>plan)everOn=true;
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 cs_live_stats(p,&st);CHECK(!st.failures);
 bool onAtEnd=last>plan;
 printf("%s: plan_ms=%.2f end_ms=%.2f ever_on=%d generated=%llu ticks=%d\n",name,plan/1e6,last/1e6,everOn,(unsigned long long)st.generated,ticks);
 CHECK(onAtEnd==expectOnAtEnd);CHECK(everOn==expectEverOn);
 if(onAtEnd)CHECK(last-plan>8300000&&last-plan<8400000);
 *outGenerated=(int)st.generated;cs_live_destroy(p);return 0;
}
int main(){
 int g60=0,g30=0,gd=0,gm=0,g30off=0;
 _wputenv(L"CS_TEST_CADENCE30=0");
 if(run("30fps switch off",frame30,480,false,false,&g30off))return 2;
 _wputenv(L"CS_TEST_CADENCE30=2");
 if(run("60fps",frame60,480,false,false,&g60))return 2;
 // 2.0.6: the CPU model takes 15 ms a job, so three phases (45 ms) miss the pair deadline (~29 ms) and the engine keeps
 // only the .5 phase (even 30 -> 60). CS_TEST_CADENCE_FULL=1 checks the every-phase path as before; the default run
 // must still detect the cadence (buffer +8.33 ms) and generate at least as many frames as with the switch off.
 int g30lite=0;
 if(run("30fps in 60 (deadline fit: .5 only)",frame30,480,true,true,&g30lite))return 2;
 _wputenv(L"CS_TEST_CADENCE_FULL=1");
 if(run("30fps in 60 (every phase)",frame30,480,true,true,&g30))return 2;
 _wputenv(L"CS_TEST_CADENCE_FULL=");
 if(run("60fps dropping 1 in 6",dropping60,480,false,false,&gd))return 2;
 if(run("30 then 60",mixed,960,false,true,&gm))return 2;
 // 30 fps content: the switch makes more in-between images than the off state.
 CHECK(g30>g30off);CHECK(g30lite>=g30off);
 printf("{\"pass\":true,\"generated_30_off\":%d,\"generated_30_on\":%d,\"generated_60\":%d}\n",g30off,g30,g60);
 return 0;
}
