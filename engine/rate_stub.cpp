// SPDX-License-Identifier: MIT
// Test-only deterministic model for scheduler tests without using the GPU.
#define CS_ENGINE_BUILD
#include "engine.h"
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
static std::atomic<uint64_t> calls{0},mutations{0};
extern "C" __declspec(dllexport) uint64_t cs_test_calls(){return calls.load();}
extern "C" __declspec(dllexport) uint64_t cs_test_mutations(){return mutations.load();}
static uint64_t checksum(const uint8_t* p,size_t n){uint64_t v=0;for(size_t i=0;i<n;i++)v=v*131+p[i];return v;}
void* cs_create(const wchar_t* dir,int){return std::wstring(dir).find(L"missing")!=std::wstring::npos?nullptr:new int(std::wstring(dir).find(L"slow")!=std::wstring::npos);}
static int blend(void* handle,const uint8_t* a,const uint8_t* b,size_t bytes,float phase,uint8_t* out){
 ++calls;
 if(*static_cast<int*>(handle)){uint64_t ca=checksum(a,bytes),cb=checksum(b,bytes);std::this_thread::sleep_for(std::chrono::milliseconds(15));if(ca!=checksum(a,bytes)||cb!=checksum(b,bytes)){++mutations;return -1;}}
 for(size_t i=0;i<bytes;i++)out[i]=(uint8_t)(a[i]*(1-phase)+b[i]*phase+.5f);return 0;
}
static std::atomic<int> reportedMs{100}; // default: a reported transient stall must never starve later jobs
extern "C" __declspec(dllexport) void cs_test_set_reported_ms(int ms){reportedMs=ms;}
double cs_last_ms(void*){return reportedMs.load();}
void cs_destroy(void* h){delete static_cast<int*>(h);}

int cs_interpolate(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out){return blend(p,a,b,(size_t)w*h*3,t,out);}
int cs_interpolate_tagged(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out,uint64_t,uint64_t){return cs_interpolate(p,a,b,w,h,t,out);}
int cs_interpolate_bgra(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out,uint64_t,uint64_t){return blend(p,a,b,(size_t)w*h*4,t,out);}
int cs_interpolate_bgra_aux(void* p,const uint8_t* a,const uint8_t* b,int w,int h,float t,uint8_t* out,uint64_t,uint64_t,float){return blend(p,a,b,(size_t)w*h*4,t,out);}
double cs_last_gpu_wait_ms(void*){return -1;}
int cs_last_stages(void*,double* o){o[0]=o[1]=o[2]=-1;return 1;}
const char* cs_gpu_info(void*){return "";}
void cs_set_flow_floor(void*,int){}
