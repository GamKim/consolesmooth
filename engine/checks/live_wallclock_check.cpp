// SPDX-License-Identifier: MIT
#define CS_ENGINE_BUILD
#include "engine.h"
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <cstdio>
// Deliberately slow CPU model. No GPU and no virtual-clock test define:
// this exercises the production wall-clock deadline while callbacks stop.
void* cs_create(const wchar_t*,int){return reinterpret_cast<void*>(1);}
void cs_destroy(void*){}
double cs_last_ms(void*){return 40;}
double cs_last_gpu_wait_ms(void*){return -1;}
int cs_last_stages(void*,double* o){o[0]=o[1]=o[2]=-1;return 1;}
const char* cs_gpu_info(void*){return "";}
const char* cs_last_error(){return "";}
void cs_set_flow_floor(void*,int){}
int cs_interpolate(void*,const uint8_t* a,const uint8_t*,int w,int h,float,uint8_t* out){
 std::this_thread::sleep_for(std::chrono::milliseconds(40));
 memcpy(out,a,(size_t)w*h*3);return 0;
}
int main(){
 void* p=cs_live_create_workers(L"test",-1,32,32,1);if(!p)return 2;
 CsStreamStats s{};
 for(int i=0;i<2000;i++){cs_live_stats(p,&s);if(s.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
 if(s.ready!=1)return 3;
 std::vector<uint8_t> a(3072,20),b(3072,30),out(3072);
 if(cs_live_configure(p,1,60,1,240,1,0)||cs_live_tick(p,a.data(),out.data(),1000000000ull)||cs_live_tick(p,b.data(),out.data(),1016666666ull))return 4;
 // No more cs_live_tick calls: the old callback-only clock never expired this.
 std::this_thread::sleep_for(std::chrono::milliseconds(120));
 CsLiveMetrics m{};cs_live_metrics(p,&m);cs_live_stats(p,&s);
 bool pass=m.completed_jobs==1&&m.discarded_results==1&&m.expired_jobs==2&&s.generated==0&&s.failures==0;
 printf("{\"pass\":%s,\"completed\":%llu,\"discarded\":%llu,\"expired\":%llu,\"generated\":%llu,\"scope\":\"Production wall clock, 40ms CPU model, no callbacks for 120ms\"}\n",pass?"true":"false",(unsigned long long)m.completed_jobs,(unsigned long long)m.discarded_results,(unsigned long long)m.expired_jobs,(unsigned long long)s.generated);
 cs_live_destroy(p);return pass?0:5;
}

int cs_interpolate_tagged(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out,uint64_t,uint64_t){return cs_interpolate(p,a,b,w,h,t,out);}
int cs_interpolate_bgra(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out,uint64_t,uint64_t){(void)p;memcpy(out,a,(size_t)w*h*4);(void)b;(void)t;return 0;}
int cs_interpolate_bgra_aux(void* p,const uint8_t* a,const uint8_t*,int w,int h,float,uint8_t* out,uint64_t,uint64_t,float){(void)p;memcpy(out,a,(size_t)w*h*4);return 0;}

const char* cs_model_name(void*){return "Scheduler stub";}
