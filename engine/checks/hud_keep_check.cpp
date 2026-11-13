// SPDX-License-Identifier: MIT
// 1.8.0 test tool: the live engine with HUD keep on, real GPU and wall clock. 60 fps source in 120 Hz ticks: a fixed
// HUD bar with a soft edge over a background that moves 6 px per source. Within 2 px of the HUD every changed pixel
// of a generated frame must be one of the two originals (HUD keep mode 4). Also creates and destroys the stream
// several times while the HUD map thread is busy.
// hud_keep_check <engine.dll> <model dir> [seconds] [cycles]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <chrono>
#include <thread>
typedef struct {uint64_t ticks,generated,repeated,static_pairs,cuts,overwritten,failures;double last_inference_ms;int ready;} Stats;
int main(int argc,char** argv){
 if(argc<3)return 1;HMODULE dll=LoadLibraryA(argv[1]);if(!dll)return 2;
 auto create=(void*(*)(const wchar_t*,int,int,int,int,int))GetProcAddress(dll,"cs_live_create_format");
 auto tick=(int(*)(void*,const uint8_t*,uint8_t*,uint64_t))GetProcAddress(dll,"cs_live_tick");
 auto configure=(int(*)(void*,int,int,int,int,int,int))GetProcAddress(dll,"cs_live_configure");
 auto options=(int(*)(void*,int,int))GetProcAddress(dll,"cs_live_set_options");
 auto stats=(int(*)(void*,Stats*))GetProcAddress(dll,"cs_live_stats");
 auto proof=(int(*)(void*,uint8_t*,uint8_t*,uint8_t*,float*))GetProcAddress(dll,"cs_live_copy_proof");
 auto destroy=(void(*)(void*))GetProcAddress(dll,"cs_live_destroy");
 if(!create||!tick||!configure||!options||!stats||!proof||!destroy)return 3;
 // CS_CHECK_KEEP=1: GPU clock keep on (engine option bit 0), and its dispatch count is printed.
 auto setgpu=(void(*)(int))GetProcAddress(dll,"cs_set_gpu_options");auto stage=(int(*)(void*,double*))GetProcAddress(dll,"cs_live_stage_stats");
 bool keep=getenv("CS_CHECK_KEEP")&&atoi(getenv("CS_CHECK_KEEP"))==1;if(keep&&setgpu)setgpu(1);double keepInfo[10]={0}; // CsStageStats: 7 doubles, samples, keep_ms, keep_dispatches
 int wc=0;wchar_t** wa=CommandLineToArgvW(GetCommandLineW(),&wc);std::wstring dir(wa[2]);LocalFree(wa);
 double seconds=argc>3?atof(argv[3]):6;int cycles=argc>4?atoi(argv[4]):3;
 const int W=1920,H=1080;const size_t px=(size_t)W*H;const int hx0=200,hx1=700,hy0=900,hy1=960; // HUD bar
 auto frame=[&](std::vector<uint8_t>& f,int n){uint32_t* p=(uint32_t*)f.data();
  for(int y=0;y<H;y++)for(int x=0;x<W;x++){int v=((x+n*6)/24+(y/24))&1?200:40;int g=(x*3+n*6+y)&255;uint32_t c=0xFF000000u|(v<<16)|(g<<8)|(255-v);
   // HUD: solid inside, 2 px soft edge mixing with the background (anti-aliasing), so the edge is never exactly equal.
   int dx=x<hx0?hx0-x:x>=hx1?x-hx1+1:0,dy=y<hy0?hy0-y:y>=hy1?y-hy1+1:0,d=dx>dy?dx:dy;
   if(d==0)c=0xFFE0E0E0u;else if(d<=2){int k=d==1?170:85;uint32_t r=(((c>>16)&255)*(256-k)+224*k)>>8,gg=(((c>>8)&255)*(256-k)+224*k)>>8,b=((c&255)*(256-k)+224*k)>>8;c=0xFF000000u|(r<<16)|(gg<<8)|b;}
   p[(size_t)y*W+x]=c;}};
 std::vector<std::vector<uint8_t>> src(8,std::vector<uint8_t>(px*4));for(int i=0;i<8;i++)frame(src[i],i);
 std::vector<uint8_t> out(px*4),a(px*4),b(px*4),o(px*4);uint64_t checked=0,violations=0,band=0;int failures=0;uint64_t generated=0,ticks=0;
 LARGE_INTEGER f;QueryPerformanceFrequency(&f);double tickSum=0,tickMax=0;uint64_t tickN=0,tickLate=0;
 auto cpuNow=[]{FILETIME c,e,k,u;GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u);ULARGE_INTEGER a,b;a.LowPart=k.dwLowDateTime;a.HighPart=k.dwHighDateTime;b.LowPart=u.dwLowDateTime;b.HighPart=u.dwHighDateTime;return (a.QuadPart+b.QuadPart)/1e7;};
 double cpu0=cpuNow();auto wall0=std::chrono::steady_clock::now();
 for(int cycle=0;cycle<cycles;cycle++){
  void* s=create(dir.c_str(),-1,W,H,1,4);if(!s){fprintf(stderr,"create failed\n");return 4;}
  Stats st{};for(int i=0;i<300&&st.ready==0;i++){stats(s,&st);std::this_thread::sleep_for(std::chrono::milliseconds(50));}
  if(st.ready!=1){fprintf(stderr,"not ready\n");return 5;}
  int hudOn=!(getenv("CS_CHECK_HUD")&&atoi(getenv("CS_CHECK_HUD"))==0);
  if(configure(s,1,60,1,120,1,0)||options(s,1,hudOn))return 6;
  auto start=std::chrono::steady_clock::now();uint64_t lastGen=0;int n=0;
  // The last cycle runs the full time; earlier cycles stop early while the HUD map thread may still be working.
  double run=cycle+1==cycles?seconds:0.9+0.13*cycle;
  for(int t=0;;t++){
   auto due=start+std::chrono::microseconds((int64_t)t*8333);std::this_thread::sleep_until(due);
   if(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()>run)break;
   if(t%2==0)n++;LARGE_INTEGER q;QueryPerformanceCounter(&q);uint64_t ns=(uint64_t)((long double)q.QuadPart*1e9L/f.QuadPart);
   LARGE_INTEGER t0,t1;QueryPerformanceCounter(&t0);
   if(tick(s,src[n%8].data(),out.data(),ns))++failures;
   QueryPerformanceCounter(&t1);{double ms=1000.0*(t1.QuadPart-t0.QuadPart)/f.QuadPart;if(cycle+1==cycles&&t>120){tickSum+=ms;tickMax=ms>tickMax?ms:tickMax;++tickN;if(ms>8.333)++tickLate;}}
   stats(s,&st);
   if(st.generated>lastGen){lastGen=st.generated;float ph=0;
    if(cycle+1==cycles&&t>120&&proof(s,a.data(),b.data(),o.data(),&ph)==0){
     const uint32_t *A=(const uint32_t*)a.data(),*B=(const uint32_t*)b.data(),*O=(const uint32_t*)o.data();++checked;
     for(int y=hy0-4;y<hy1+4;y++)for(int x=hx0-4;x<hx1+4;x++){int dx=x<hx0?hx0-x:x>=hx1?x-hx1+1:0,dy=y<hy0?hy0-y:y>=hy1?y-hy1+1:0,d=dx>dy?dx:dy;
      if(d<1||d>2)continue;size_t i=(size_t)y*W+x;if(!((A[i]^B[i])&0xFFFFFFu))continue;++band;
      if(((O[i]^A[i])&0xFFFFFFu)&&((O[i]^B[i])&0xFFFFFFu))++violations;}}}
  }
  if(keep&&stage)stage(s,keepInfo);
  stats(s,&st);generated+=st.generated;ticks+=st.ticks;failures+=(int)st.failures;destroy(s);
 }
 double cpuS=cpuNow()-cpu0,wallS=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall0).count();
 fprintf(stderr,"tick_avg_ms=%.3f tick_max_ms=%.3f ticks_over_8ms=%llu/%llu cpu_cores=%.2f\n",tickN?tickSum/tickN:0,tickMax,(unsigned long long)tickLate,(unsigned long long)tickN,wallS>0?cpuS/wallS:0);
 bool pass=failures==0&&checked>50&&band>0&&violations==0;
 printf("{\"pass\":%s,\"cycles\":%d,\"ticks\":%llu,\"generated\":%llu,\"checked_frames\":%llu,\"edge_pixels\":%llu,\"edge_not_original\":%llu,\"failures\":%d,\"keep_dispatches\":%llu}\n",pass?"true":"false",cycles,(unsigned long long)ticks,(unsigned long long)generated,(unsigned long long)checked,(unsigned long long)band,(unsigned long long)violations,failures,(unsigned long long)*(uint64_t*)&keepInfo[9]);
 return pass?0:10;
}
