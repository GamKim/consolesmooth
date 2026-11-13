// SPDX-License-Identifier: MIT
// 11-09 test tool: replays dumped real input pictures (plugin CS_TEST_DUMP_DIR, BGRA) through the live engine in real time,
// 60 fps sources into <target> Hz ticks, with the 30 fps cadence / HUD keep switches, and reports the tick time on the
// calling (video) thread, repeats and generated frames. Optionally writes every output tick to a raw file for study.
// dump_replay <engine.dll> <model dir> <frames.bgra> <frames> <seconds> <target> <cadence30 0|1> <hud 0|1> [low_buffer_ms] [out.bgra]
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
#include <algorithm>
typedef struct {uint64_t ticks,generated,repeated,static_pairs,cuts,overwritten,failures;double last_inference_ms;int ready;} Stats;
int main(int argc,char** argv){
 if(argc<9)return 1;HMODULE dll=LoadLibraryA(argv[1]);if(!dll)return 2;
 auto create=(void*(*)(const wchar_t*,int,int,int,int,int))GetProcAddress(dll,"cs_live_create_format");
 auto tick=(int(*)(void*,const uint8_t*,uint8_t*,uint64_t))GetProcAddress(dll,"cs_live_tick");
 auto configure=(int(*)(void*,int,int,int,int,int,int))GetProcAddress(dll,"cs_live_configure");
 auto options=(int(*)(void*,int,int))GetProcAddress(dll,"cs_live_set_options");
 auto lowbuf=(int(*)(void*,int))GetProcAddress(dll,"cs_live_set_low_buffer_ms");
 auto stats=(int(*)(void*,Stats*))GetProcAddress(dll,"cs_live_stats");auto destroy=(void(*)(void*))GetProcAddress(dll,"cs_live_destroy");
 if(!create||!tick||!configure||!options||!stats||!destroy)return 3;
 int wc=0;wchar_t** wa=CommandLineToArgvW(GetCommandLineW(),&wc);std::wstring dir(wa[2]);LocalFree(wa);
 int nf=atoi(argv[4]),target=atoi(argv[6]),cad=atoi(argv[7]),hud=atoi(argv[8]),low=argc>9?atoi(argv[9]):25;double seconds=atof(argv[5]);
 const int W=1920,H=1080;const size_t B=(size_t)W*H*4;
 std::vector<std::vector<uint8_t>> f(nf,std::vector<uint8_t>(B));FILE* in=fopen(argv[3],"rb");if(!in)return 4;
 for(auto& x:f)if(fread(x.data(),1,B,in)!=B)return 5;fclose(in);
 FILE* outFile=argc>10?fopen(argv[10],"wb"):nullptr;
 // CS_REPLAY_RECT=x,y,w,h: pixels in the rectangle whose colour stays within 6 levels in every input picture are static;
 // each output tick is compared with the current input there (text = bright static pixels). Shimmer shows as error.
 std::vector<size_t> textPx,otherPx;const int rangeMax=getenv("CS_REPLAY_RANGE")?atoi(getenv("CS_REPLAY_RANGE")):6;
 if(const char* r=getenv("CS_REPLAY_RECT")){int rx,ry,rw,rh;if(sscanf(r,"%d,%d,%d,%d",&rx,&ry,&rw,&rh)==4)
  for(int y=ry;y<ry+rh;y++)for(int x=rx;x<rx+rw;x++){size_t i=((size_t)y*W+x)*4;int lo[3]={255,255,255},hi[3]={0,0,0};
   for(auto& fr:f)for(int c=0;c<3;c++){lo[c]=std::min<int>(lo[c],fr[i+c]);hi[c]=std::max<int>(hi[c],fr[i+c]);}
   if(hi[0]-lo[0]>rangeMax||hi[1]-lo[1]>rangeMax||hi[2]-lo[2]>rangeMax)continue;
   (std::max({f[0][i],f[0][i+1],f[0][i+2]})>170?textPx:otherPx).push_back(i);}}
 std::vector<size_t> rectPx;if(const char* r=getenv("CS_REPLAY_RECT")){int rx,ry,rw,rh;if(sscanf(r,"%d,%d,%d,%d",&rx,&ry,&rw,&rh)==4)
  for(int y=ry;y<ry+rh;y++)for(int x=rx;x<rx+rw;x++)rectPx.push_back(((size_t)y*W+x)*4);}
 uint64_t ghostSum=0,ghostTicks=0;
 double errSum[2]={0,0};uint64_t errN[2]={0,0},errBig[2]={0,0},badTicks=0,measured=0;
 void* s=create(dir.c_str(),-1,W,H,1,4);if(!s)return 6;
 Stats st{};for(int i=0;i<300&&st.ready==0;i++){stats(s,&st);std::this_thread::sleep_for(std::chrono::milliseconds(50));}
 if(st.ready!=1)return 7;
 const int srcRate=getenv("CS_REPLAY_SOURCE")?atoi(getenv("CS_REPLAY_SOURCE")):60; // 1.0.1: 120 fps input test
 if(configure(s,1,srcRate,1,target,1,0)||options(s,cad,hud))return 8;if(lowbuf&&low<25)lowbuf(s,low);
 std::vector<uint8_t> out(B);LARGE_INTEGER fq;QueryPerformanceFrequency(&fq);
 auto start=std::chrono::steady_clock::now();double tickNs=1e9/target;std::vector<double> times;Stats s0{};bool base=false;
 for(int t=0;;t++){
  auto due=start+std::chrono::nanoseconds((int64_t)(t*tickNs));std::this_thread::sleep_until(due);
  double el=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();if(el>seconds)break;
  int src=(int)(el*srcRate);const uint8_t* in0=f[src%nf].data();
  LARGE_INTEGER q0,q1;QueryPerformanceCounter(&q0);uint64_t ns=(uint64_t)((long double)q0.QuadPart*1e9L/fq.QuadPart);
  tick(s,in0,out.data(),ns);QueryPerformanceCounter(&q1);
  if(el>3){if(!base){stats(s,&s0);base=true;}times.push_back(1000.0*(q1.QuadPart-q0.QuadPart)/fq.QuadPart);
   if(!textPx.empty()||!otherPx.empty()){++measured;uint64_t bigHere=0;for(int k=0;k<2;k++)for(size_t i:k?otherPx:textPx){
    int d=std::max({std::abs((int)out[i]-in0[i]),std::abs((int)out[i+1]-in0[i+1]),std::abs((int)out[i+2]-in0[i+2])});
    errSum[k]+=d;++errN[k];if(d>12){++errBig[k];++bigHere;}}
    if(bigHere>20)++badTicks;
    // ghost: bright output where none of the last five input pictures is bright (text dragged off its place)
    uint64_t g=0;for(size_t i:rectPx){int o=std::max({out[i],out[i+1],out[i+2]});if(o<=170)continue;int m=0;
     for(int k=0;k<5;k++){const uint8_t* q=f[((src-k)%nf+nf)%nf].data();m=std::max({m,(int)q[i],(int)q[i+1],(int)q[i+2]});}
     if(m<110)++g;}
    ghostSum+=g;if(g>50)++ghostTicks;}if(outFile&&(el<6||getenv("CS_REPLAY_OUT_ALL")))fwrite(out.data(),1,B,outFile);}
 }
 stats(s,&st);destroy(s);if(outFile)fclose(outFile);
 std::sort(times.begin(),times.end());size_t n=times.size();double sum=0;for(double v:times)sum+=v;
 printf("{\"cadence30\":%d,\"hud\":%d,\"target\":%d,\"ticks\":%zu,\"tick_avg_ms\":%.3f,\"tick_p95_ms\":%.3f,\"tick_max_ms\":%.3f,\"generated\":%llu,\"repeated\":%llu,\"repeat_pct\":%.2f}\n",
  cad,hud,target,n,sum/n,times[(size_t)(n*.95)],times[n-1],(unsigned long long)(st.generated-s0.generated),(unsigned long long)(st.repeated-s0.repeated),100.0*(st.repeated-s0.repeated)/std::max<uint64_t>(1,st.ticks-s0.ticks));

 if(measured)printf("{\"static_text_px\":%zu,\"text_err_avg\":%.3f,\"text_err_gt12_pct\":%.3f,\"static_other_px\":%zu,\"other_err_avg\":%.3f,\"other_err_gt12_pct\":%.3f,\"ticks_with_20px_gt12\":%.2f}\n",
  textPx.size(),errN[0]?errSum[0]/errN[0]:0,errN[0]?100.0*errBig[0]/errN[0]:0,otherPx.size(),errN[1]?errSum[1]/errN[1]:0,errN[1]?100.0*errBig[1]/errN[1]:0,100.0*badTicks/measured);
 if(measured)printf("{\"ghost_px_per_tick\":%.2f,\"ticks_with_50_ghost_px\":%.2f}\n",(double)ghostSum/measured,100.0*ghostTicks/measured);
 return 0;
}
