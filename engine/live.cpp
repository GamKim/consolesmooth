// SPDX-License-Identifier: MIT
// Arrival-time scheduler. Content identity is checked exactly; output callbacks
// never decide how many source frames have arrived. No GPU wait on the caller.
#define CS_ENGINE_BUILD
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "engine.h"
#include "rate_math.h"
#include "frame_limits.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <deque>
#include <array>
#include <memory>
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <atomic>
#include "thread_pool.h"
#include <emmintrin.h>
#ifdef CS_GPU_KEEPER
#include "gpu_keeper.h"
#endif
namespace {
using Data=std::shared_ptr<std::vector<uint8_t>>;
struct Source {Data pixels;uint64_t ns=0,id=0;};
struct Job {Source a,b;uint64_t epoch,n,due;float phase;};
struct Result {std::vector<uint8_t> pixels;uint64_t epoch=0,n=0;bool valid=false;Source a,b;float phase=0;};
// UHD BGRA frames are 33 MB. One thread copies ~10 GB/s, so each whole-frame copy or compare
// on the video thread cost ~3 ms (cpu stage ~8 ms per 4K tick). Frames of 8 MB or more are
// split over the caller and three persistent helper threads. Used by the single producer only.
struct Parallel {
 static constexpr int kHelpers=3;static constexpr size_t kMinBytes=8u<<20;
 std::mutex m;std::condition_variable go,done;std::thread threads[kHelpers];
 std::function<void(size_t,size_t)> task;size_t total=0;uint64_t generation=0;int pending=0;bool stop=false;
 Parallel(){for(int i=0;i<kHelpers;i++)threads[i]=std::thread([this,i]{uint64_t seen=0;for(;;){std::function<void(size_t,size_t)> f;size_t a,b;
  {std::unique_lock<std::mutex> l(m);go.wait(l,[&]{return stop||generation!=seen;});if(stop)return;seen=generation;f=task;range(i,a,b);}
  f(a,b);{std::lock_guard<std::mutex> l(m);if(--pending==0)done.notify_one();}}});}
 ~Parallel(){{std::lock_guard<std::mutex> l(m);stop=true;}go.notify_all();for(auto& t:threads)t.join();}
 void range(int i,size_t& a,size_t& b)const{size_t q=(total/(kHelpers+1)+63)&~(size_t)63;a=std::min(total,q*i);b=i==kHelpers?total:std::min(total,q*(i+1));}
 void run(size_t n,const std::function<void(size_t,size_t)>& f){
  if(n<kMinBytes){f(0,n);return;}
  {std::lock_guard<std::mutex> l(m);task=f;total=n;pending=kHelpers;++generation;}go.notify_all();
  size_t a,b;range(kHelpers,a,b);f(a,b);
  std::unique_lock<std::mutex> l(m);done.wait(l,[&]{return pending==0;});task=nullptr;
 }
 void copy(uint8_t* d,const uint8_t* s,size_t n){run(n,[=](size_t a,size_t b){memcpy(d+a,s+a,b-a);});}
 bool equal(const uint8_t* x,const uint8_t* y,size_t n){std::atomic<bool> diff{false};run(n,[&](size_t a,size_t b){if(!diff.load(std::memory_order_relaxed)&&memcmp(x+a,y+a,b-a))diff=true;});return !diff;}
};
struct Live {
 int w,h,gpu,workers,channels;size_t bytes;std::wstring model;
 std::mutex mutex;std::condition_variable event;std::vector<std::thread> threads;
 bool stop=false,on=true;uint64_t epoch=1,base=0,now=0,last=0,lastOutput=UINT64_MAX,delayNs=25000000;
 std::atomic<double> jobCostMs{0}; // 2.0.6: running average of one job's time on the worker (see the 30 fps pair check)
 double calibrationMs=0;bool modelCalibration=false,midpointModel=false,midpointOnly=false; // midpointOnly (1.9.6): CAIN and FSR 3 make only t=.5
 int stable=0;uint64_t planDelayNs=25000000; // mode and plan buffer from the last configure
 // 2.0.1 low-latency step-down: the raise below uses the cost measured at start (cold, e.g. 8.2-8.4 ms on the 5080),
 // steady cost is lower (5.0 ms HUD keep off). lowBaseNs = the user's low buffer before the raise; retuneSerial marks
 // every retune so the worker drops a step-down made for an older configuration.
 uint64_t lowBaseNs=0,raisedNs=0,retuneSerial=0;
 bool haveBase=false,sourceAnchored=false;uint64_t sourceId=0,lastContent=0,sourceArrival=0,sourceClock=0;int sn=60,sd=1,tn=120,td=1,readyWorkers=0;
 Parallel par;std::array<Data,40> pool;std::deque<Source> history;std::deque<Job> jobs;
 std::array<Result,64> results;std::vector<uint8_t> selected;
 std::array<uint64_t,64> requested;
 // A tick whose phase is still being generated waits for it, at most this long (0: off).
 // The capture and host clocks drift: for minutes at a time the host's 60->120 step hands us
 // the in-between buffer ~1 ms after the new original, before generation can finish, and the
 // original was repeated (measured live: 40-75% of phases). CS_LIVE_WAIT=0 disables (A/B).
 std::condition_variable resultReady;uint64_t waitNs=0,waitSkips=0;
 std::atomic<uint64_t> profKeepMs{0},profHudMs{0},profMapMs{0},profFillMs{0},profFillN{0};std::atomic<uint64_t> profKeepN{0},profHudN{0},profMapN{0}; // test: CS_TEST_KEEP_PROFILE
 bool noSmooth=false; // CS_LIVE_SMOOTH=0: raw wall-clock display slots (A/B test)
 bool noPull=false; // CS_LIVE_SMOOTH=clamp: 1.9.3 smoothing without the 1.9.4 pull toward the wall clock (A/B test)
 // 1.0.1 (11-12 PS5 GTA5, 30 fps smoothing at 2x): each callback was rounded to its output slot on its own. When the
 // display clock sat near a half-slot boundary (decided at start-up, so it differed per run) neighbouring callbacks flipped
 // between slots: the midpoint phase showed one slot late and the next original one early (1/3-slot rhythm instead of 2/2,
 // engine output 0.2-3.9 bursts/s across runs with an even 30 fps source). Keep the next slot unless the clock is clearly
 // elsewhere (same rule as the source index). CS_LIVE_SLOT=round restores per-callback rounding (A/B).
 bool slotRound=false;uint64_t lastSlot=UINT64_MAX;
 // 1.0.1 (11-12 night): 30 fps games make one RIFE phase per 33 ms pair at 2x, so there is time for a second, reversed
 // pass (b->a at 1-phase) averaged with the first. Ground truth on real frames, half flow: Zelda 60 fps (33 ms gap)
 // 23.29 -> 23.72 dB, GTA5 30 fps (66 ms gap) 23.48 -> 24.01 dB, worst 10% 22.10 -> 22.59. Only with 30 fps smoothing
 // active, RIFE only, and only when the remaining deadline holds about 3 single passes (+2 ms). CS_LIVE_TTA=0 turns it off (A/B).
 bool ttaOff=false,thirty=false;double ttaSingleMs=0,ttaK=3.0;uint64_t ttaJobs=0,ttaSkips=0; // 11-12 PS5 GTA5: margin 2.3 missed 6-10/1200, 3.0 missed 2 (averaging on ~96% of frames)
 // 1.0.1: after the reversed pass, one full-size flow pass (second network) at 1/5 weight when the deadline still holds
 // it (full ~2.1 single half passes). Off by default (CS_LIVE_AUX=1 turns it on): with a 25 ms buffer it fit 1 of 1345
 // frames live. Ground truth (driving GTA5): half 24.60, fwd+rev 25.11, fwd+rev+full 25.33 dB. Only at FHD and below.
 bool auxOff=false;double auxMs=0;uint64_t auxJobs=0;
 // Capture and host clocks drift (measured about 40 ppm). Rounding arrivals to a grid fixed at the first frame
 // reaches the half-period boundary every few minutes; callback-quantized arrivals (160 Hz: 6.25 ms) then flip
 // between indices for about a minute: pairs of 33/11/5 ms, clipped gaps, 1.67 instead of 2.33 generated per
 // original, none counted as repeats. The grid follows the mean arrival instead. CS_LIVE_DRIFT=0 disables (A/B).
 double testDelayMs=0; // CS_TEST_ENGINE_DELAY_MS: added to every inference to simulate a slower GPU
 bool noDrift=false;long double driftFrames=0;uint64_t lastIndex=UINT64_MAX;
 uint64_t lastTick=0,lastDisplay=0,tickInterval=0;
 // Test only (CS_TEST_CADENCE30=1|2): a 30 fps game in a 60 fps signal sends each image twice. The clip below cuts the
 // 33 ms gap to one source period, so 60->120 made one phase per pair and held the original twice (new images 8/25 ms).
 // 1: when most recent gaps are two periods, keep the whole gap (phases .25/.5/.75). 2: also add one output interval of buffer
 // so the .25 phase can finish before it is due (11-07 live 60->120: 2-4 ms extra left 24-28% of ticks waiting, 8.33 ms 0.4%).
 // Test only (CS_TEST_STATIC_KEEP=1): pixels equal in both originals are copied from the original instead of the
 // generated image. Fixed HUD (health bars) over a moving background was dragged along with it (11-07 Naruto: the bar
 // changed in 3.7% of frames off, 6.9-8.1% with CS_TEST_CADENCE30).
 // 2: also blend (instead of warping) pixels next to an equal pixel whose colour changed only a little: the anti-aliased
 // edges of HUD elements mix with the moving background, so they are never exactly equal and kept wobbling (11-07 user).
 // 4 (1.7.0): pixels unchanged for 30 source pairs in a row are HUD; within 2 px of HUD every changed pixel takes the
 // nearest original. Generated frames dragged the outer HUD edge with the background and the original pulled it back,
 // so the bar end moved back and forth (11-07 user). The count runs on the video thread, the 2 px map on its own thread.
 // 1.8.8: the count compares each source with the colour the pixel had when its run started, within hudTol per channel
 // (Zelda title menu text changes by 1-2 levels in 25% of source pairs: exact equality found only 49% of the text, 11-09),
 // and the count and the map run on the HUD thread (the count took 4.1 ms on the video thread and the map 10 ms, so with
 // HUD keep on 18% of 160 Hz ticks repeated). Only the newest source is counted; if the thread is busy one is skipped.
 // 1.9.0: hudTol 3 -> 8. The dim menu line "옵션/ZELDA NOTES 설정" over a fast grass pan was only 54% counted at 3 (its pixels
 // move 4-8 levels between sources), the rest was dragged by the model (doubled strokes in the 11-09 recording); at 8 85%
 // counted and 99% inside the 2 px map, menu shake frames 24% -> 0% on the replay. Background held as HUD outside the logo
 // and menu: 9.8% -> 19.5% of the screen, pixels in it changing more than 4 per source 0.001% -> 0.17% (dump raw3, 11-09).
 // 2.0.4: semi-transparent HUD. Button labels (R, ZR), the d-pad icon and dark item panels let the background show through,
 // so their colour moves 10-20 levels when the camera pans and the tolerance above broke their run every few sources: on
 // the 11-11 Zelda courtyard dump (60 fps, panning) the d-pad was 1% HUD, R and ZR 0%, and the model tore them apart. A
 // pixel whose change since the previous counted source is under half the mean change around it (4x4 blocks, ~30 px or
 // ~120 px, the larger qualifying mean, at least 8) is a dimmed copy of a moving background, i.e. an overlay: its run
 // continues (ref follows it). Dump: d-pad 33%, R 59%, ZR 55%, item box 65 -> 97% (rects include background around the
 // icons), background held as HUD 0.04 -> 0.05% of the screen. CS_TEST_HUD_ATTEN=0 turns it off (test only).
 std::vector<uint32_t> hudPrev;std::vector<uint8_t> hudDiff,hudAtt,hudLim;std::vector<uint32_t> hudBlk;bool hudAtten=true;
 // 2.0.7: SSE2 version of the passes below (diff + block sums fused, mask 16 px a step); same result as the scalar
 // passes (CS_TEST_ATTEN_VERIFY=1 runs both and counts differing bytes, printed with CS_TEST_KEEP_PROFILE;
 // CS_TEST_ATTEN_SCALAR=1 uses the scalar passes).
 bool attenVerify=false,attenScalar=false;std::atomic<uint64_t> attenMismatch{0},attenChecked{0};
 void hudAttenPass1Scalar(const uint32_t* y,uint8_t* d,uint32_t* pv,uint32_t* bs,int bw){
  for(int r=0;r<h;r++){const uint32_t* yr=y+(size_t)r*w;uint32_t* pr=pv+(size_t)r*w;uint8_t* dr=d+(size_t)r*w;uint32_t* br=bs+(size_t)(r/4)*bw;
   for(int q=0;q<w;q++){uint32_t p=yr[q],o=pr[q];int a=std::abs((int)(p&255)-(int)(o&255)),b=std::abs((int)((p>>8)&255)-(int)((o>>8)&255)),c=std::abs((int)((p>>16)&255)-(int)((o>>16)&255));
    uint8_t v=(uint8_t)std::max(a,std::max(b,c));dr[q]=v;br[q>>2]+=v;pr[q]=p;}}
 }
 void hudAttenPass1Sse(const uint32_t* y,uint8_t* d,uint32_t* pv,uint32_t* bs,int bw){
  const __m128i lo=_mm_set1_epi32(0xFF),zero=_mm_setzero_si128();
  for(int r=0;r<h;r++){const uint32_t* yr=y+(size_t)r*w;uint32_t* pr=pv+(size_t)r*w;uint8_t* dr=d+(size_t)r*w;uint32_t* br=bs+(size_t)(r/4)*bw;int q=0;
   for(;q+16<=w;q+=16){__m128i m[4];
    for(int k=0;k<4;k++){__m128i P=_mm_loadu_si128((const __m128i*)(yr+q+4*k)),Q=_mm_loadu_si128((const __m128i*)(pr+q+4*k));
     __m128i D=_mm_or_si128(_mm_subs_epu8(P,Q),_mm_subs_epu8(Q,P));
     __m128i M=_mm_max_epu8(D,_mm_max_epu8(_mm_srli_epi32(D,8),_mm_srli_epi32(D,16)));M=_mm_and_si128(M,lo);m[k]=M;
     __m128i S=_mm_sad_epu8(M,zero);br[(q>>2)+k]+=(uint32_t)(_mm_cvtsi128_si32(S)+_mm_cvtsi128_si32(_mm_srli_si128(S,8)));
     _mm_storeu_si128((__m128i*)(pr+q+4*k),P);}
    _mm_storeu_si128((__m128i*)(dr+q),_mm_packus_epi16(_mm_packs_epi32(m[0],m[1]),_mm_packs_epi32(m[2],m[3])));}
   for(;q<w;q++){uint32_t p=yr[q],o=pr[q];int a=std::abs((int)(p&255)-(int)(o&255)),b=std::abs((int)((p>>8)&255)-(int)((o>>8)&255)),c=std::abs((int)((p>>16)&255)-(int)((o>>16)&255));
    uint8_t v=(uint8_t)std::max(a,std::max(b,c));dr[q]=v;br[q>>2]+=v;pr[q]=p;}}
 }
 void hudAttenPass2(const uint8_t* d,const uint8_t* lim,uint8_t* A,int bw,bool sse){
  const __m128i zero=_mm_setzero_si128(),ones=_mm_set1_epi8((char)0xFF);
  for(int r=0;r<h;r++){const uint8_t* lr=lim+(size_t)(r/4)*bw;const uint8_t* dr=d+(size_t)r*w;uint8_t* ar=A+(size_t)r*w;int q=0;
   if(sse)for(;q+16<=w;q+=16){int l4;memcpy(&l4,lr+(q>>2),4);__m128i L=_mm_cvtsi32_si128(l4);L=_mm_unpacklo_epi8(L,L);L=_mm_unpacklo_epi16(L,L);
    __m128i D=_mm_loadu_si128((const __m128i*)(dr+q));__m128i T=_mm_subs_epu8(L,D);
    _mm_storeu_si128((__m128i*)(ar+q),_mm_andnot_si128(_mm_cmpeq_epi8(T,zero),ones));}
   for(;q<w;q++)ar[q]=dr[q]<lr[q>>2]?0xFF:0;}
 }
 void hudAttenMask(const uint32_t* y){
  size_t n=(size_t)w*h;if(hudPrev.size()!=n){hudPrev.assign(y,y+n);hudAtt.assign(n,0);hudDiff.assign(n,0);return;}
  const int bw=(w+3)/4,bh=(h+3)/4;hudBlk.assign((size_t)(bw+1)*(bh+1),0);hudLim.assign((size_t)bw*bh,0);
  uint8_t* d=hudDiff.data();uint32_t* pv=hudPrev.data();
  std::vector<uint32_t> bs((size_t)bw*bh,0);std::vector<uint32_t> pvCopy;std::vector<uint8_t> dRef;std::vector<uint32_t> bsRef;
  if(attenVerify){pvCopy.assign(pv,pv+n);dRef.assign(n,0);bsRef.assign(bs.size(),0);hudAttenPass1Scalar(y,dRef.data(),pvCopy.data(),bsRef.data(),bw);}
  if(attenScalar)hudAttenPass1Scalar(y,d,pv,bs.data(),bw);else hudAttenPass1Sse(y,d,pv,bs.data(),bw);
  // integral image over blocks
  uint32_t* I=hudBlk.data();for(int r=1;r<=bh;r++){uint32_t s=0;for(int q=1;q<=bw;q++){s+=bs[(size_t)(r-1)*bw+q-1];I[(size_t)r*(bw+1)+q]=I[(size_t)(r-1)*(bw+1)+q]+s;}}
  auto mean=[&](int r,int q,int rad){int r0=std::max(0,r-rad),r1=std::min(bh,r+rad+1),q0=std::max(0,q-rad),q1=std::min(bw,q+rad+1);
   uint64_t s=(uint64_t)I[(size_t)r1*(bw+1)+q1]-I[(size_t)r0*(bw+1)+q1]-I[(size_t)r1*(bw+1)+q0]+I[(size_t)r0*(bw+1)+q0];
   uint64_t px=(uint64_t)(r1-r0)*(q1-q0)*16;return (int)(s/px);};
  for(int r=0;r<bh;r++)for(int q=0;q<bw;q++){int a=mean(r,q,7),b=mean(r,q,30);int t=std::max(a>=8?a:0,b>=8?b:0);hudLim[(size_t)r*bw+q]=(uint8_t)std::min(255,t/2);}
  uint8_t* A=hudAtt.data();hudAttenPass2(d,hudLim.data(),A,bw,!attenScalar);
  if(attenVerify){uint64_t bad=0;for(size_t i=0;i<n;i++)bad+=d[i]!=dRef[i];for(size_t i=0;i<bs.size();i++)bad+=bs[i]!=bsRef[i];
   std::vector<uint8_t> aRef(n);hudAttenPass2(d,hudLim.data(),aRef.data(),bw,false);for(size_t i=0;i<n;i++)bad+=aRef[i]!=A[i];
   attenMismatch+=bad;attenChecked+=n;}
 }
 std::vector<uint8_t> hudCount,hudRow,hudAttRun;int hudAttMin=150,hudNearR=12,hudNearPct=12;std::vector<uint32_t> hudNearI;std::vector<uint32_t> hudRef;std::shared_ptr<std::vector<uint8_t>> hudNear;std::thread hudThread;
 std::mutex hudMutex;std::condition_variable hudWake;bool hudStop=false;Data hudPending;int hudTol=8;uint64_t hudCounted=0;std::atomic<int> hudRadius{2};
 void hudCount1(const uint32_t* y){
  size_t n=(size_t)w*h;if(hudCount.size()!=n){hudCount.assign(n,0);hudAttRun.assign(n,0);hudRef.assign(y,y+n);return;}
  uint8_t* c=hudCount.data();uint8_t* fa=hudAttRun.data();uint32_t* r=hudRef.data();size_t i=0;
  const uint8_t* att=nullptr;if(hudAtten){hudAttenMask(y);if(hudAtt.size()==n)att=hudAtt.data();}
  // SSE2, 16 pixels a step: per-channel |new-ref| over hudTol -> run broken (count 0, ref = new); else count+1 (saturating).
  const __m128i tol=_mm_set1_epi8((char)hudTol),rgb=_mm_set1_epi32(0x00FFFFFF),zero=_mm_setzero_si128(),one=_mm_set1_epi8(1);
  for(;i+16<=n;i+=16){__m128i s4[4];
   for(int k=0;k<4;k++){__m128i P=_mm_loadu_si128((const __m128i*)(y+i+4*k)),Q=_mm_loadu_si128((const __m128i*)(r+i+4*k));
    __m128i D=_mm_or_si128(_mm_subs_epu8(P,Q),_mm_subs_epu8(Q,P));__m128i E=_mm_and_si128(_mm_subs_epu8(D,tol),rgb);
    __m128i same=_mm_cmpeq_epi32(E,zero);s4[k]=same;
    _mm_storeu_si128((__m128i*)(r+i+4*k),_mm_or_si128(_mm_and_si128(same,Q),_mm_andnot_si128(same,P)));}
   __m128i m=_mm_packs_epi16(_mm_packs_epi32(s4[0],s4[1]),_mm_packs_epi32(s4[2],s4[3]));
   // 2.1.2b: mark runs kept alive by the overlay rule alone; hudMap wants them longer (see hudAttMin).
   {__m128i F=_mm_loadu_si128((const __m128i*)(fa+i));__m128i only=att?_mm_andnot_si128(m,_mm_loadu_si128((const __m128i*)(att+i))):zero;
    if(att)m=_mm_or_si128(m,_mm_loadu_si128((const __m128i*)(att+i))); // overlay: run continues, ref already follows (2.0.4)
    _mm_storeu_si128((__m128i*)(fa+i),_mm_and_si128(m,_mm_or_si128(F,only)));}
   __m128i C=_mm_loadu_si128((const __m128i*)(c+i));_mm_storeu_si128((__m128i*)(c+i),_mm_and_si128(m,_mm_adds_epu8(C,one)));}
  const int t=hudTol;
  for(;i<n;i++){uint32_t p=y[i],q=r[i];
   int d0=(int)(p&255)-(int)(q&255),d1=(int)((p>>8)&255)-(int)((q>>8)&255),d2=(int)((p>>16)&255)-(int)((q>>16)&255);
   bool same=d0<=t&&d0>=-t&&d1<=t&&d1>=-t&&d2<=t&&d2>=-t;
   uint8_t v=c[i];bool keep=same||(att&&att[i]);c[i]=keep?(uint8_t)(v<255?v+1:255):0;fa[i]=keep?(uint8_t)(fa[i]|(same?0:0xFF)):0;r[i]=same?q:p;}
 }
 void hudMap(){
  size_t n=(size_t)w*h;if(hudRow.size()!=n)hudRow.assign(n,0);uint8_t* row=hudRow.data();const uint8_t* c=hudCount.data();
  auto nearNew=std::make_shared<std::vector<uint8_t>>(n,0);uint8_t* o=nearNew->data();
  // 2 px dilation of (count >= 30), rows then columns, over a zero-padded row; plain loops the compiler vectorises.
  // 1.9.6: the radius is 4 px once RIFE runs quarter flow (slow GPUs, 1.9.5): its motion is coarser and dragged the
  // background over HUD edges further than 2 px (11-10 office RX 6600, user: HUD still shakes with HUD keep on).
  const int R=hudRadius.load()>=4?4:2;
  std::vector<uint8_t> pad((size_t)w+8,0);uint8_t* f=pad.data()+4;
  // 2.2.2 (11-13 user: GTA5 help boxes break when a tree trunk passes behind them): the box's dark see-through inside is
  // held by the overlay rule, but every dark object passing behind restarts its run, so 150 sources never come and the
  // model drags the trunk through the letters. Its letters are plain static pixels (count 30, no overlay help). An overlay
  // run counts after 30 sources too when, within hudNearR px, at least hudNearPct % of the pixels are plain static (dense
  // text). A character standing still while the camera turns (the reason for 150) has almost no plain static pixels near
  // it. CS_TEST_HUD_NEAR_R / CS_TEST_HUD_NEAR_PCT (0 = off) are test switches.
  const bool nearOn=hudNearR>0&&hudNearPct>0&&hudAttRun.size()==n;const int D=hudNearR;
  if(nearOn){hudNearI.assign((size_t)(w+1)*(h+1),0);uint32_t* I=hudNearI.data();const uint8_t* fa=hudAttRun.data();
   for(int r=0;r<h;r++){uint32_t s=0;const uint8_t* cr=c+(size_t)r*w;const uint8_t* ar=fa+(size_t)r*w;uint32_t* ir=I+(size_t)(r+1)*(w+1);const uint32_t* ip=I+(size_t)r*(w+1);
    for(int q=0;q<w;q++){s+=(cr[q]>=30&&!ar[q])?1u:0u;ir[q+1]=ip[q+1]+s;}}}
  for(int r=0;r<h;r++){const uint8_t* cr=c+(size_t)r*w;uint8_t* rr=row+(size_t)r*w;
   const uint8_t* fr=hudAttRun.size()==n?hudAttRun.data()+(size_t)r*w:nullptr;const int am=hudAttMin;
   const int r0=std::max(0,r-D),r1=std::min(h,r+D+1);const uint32_t* I0=nearOn?hudNearI.data()+(size_t)r0*(w+1):nullptr;const uint32_t* I1=nearOn?hudNearI.data()+(size_t)r1*(w+1):nullptr;
   auto dense=[&](int q)->bool{int q0=std::max(0,q-D),q1=std::min(w,q+D+1);uint32_t s=I1[q1]-I0[q1]-I1[q0]+I0[q0];return (uint64_t)s*100>=(uint64_t)hudNearPct*(uint32_t)((r1-r0)*(q1-q0));};
   // 2.1.2b (11-11 user: GTA5 character's head still breaks with 30 fps smoothing): a character standing still while the
   // camera turns changes less than its moving surroundings, so the overlay rule (2.0.4) kept its outline in the map
   // after 30 sources and the old picture was pasted there (dots and a doubled hair line). Runs that needed the overlay
   // rule now count only after hudAttMin sources (150: 5 s at 30, 2.5 s at 60); plain static pixels still after 30.
   for(int q=0;q<w;q++)f[q]=cr[q]>=30&&(!fr||!fr[q]||cr[q]>=am||(nearOn&&dense(q)));
   if(R==2)for(int q=0;q<w;q++)rr[q]=f[q-2]|f[q-1]|f[q]|f[q+1]|f[q+2];
   else for(int q=0;q<w;q++)rr[q]=f[q-4]|f[q-3]|f[q-2]|f[q-1]|f[q]|f[q+1]|f[q+2]|f[q+3]|f[q+4];}
  for(int r=0;r<h;r++){int a=std::max(0,r-R),b=std::min(h-1,r+R);uint8_t* orow=o+(size_t)r*w;
   for(int k=a;k<=b;k++){const uint8_t* rr=row+(size_t)k*w;for(int q=0;q<w;q++)orow[q]|=rr[q];}}
  std::atomic_store(&hudNear,nearNew);
 }
 void hudLoop(){for(uint64_t done=0;;){Data d;{std::unique_lock<std::mutex> l(hudMutex);hudWake.wait(l,[&]{return hudPending||hudStop;});if(hudStop)return;d=std::move(hudPending);hudPending.reset();}
  auto m0=std::chrono::steady_clock::now();hudCount1((const uint32_t*)d->data());
  if(++done%2==0)hudMap(); // the map every 2 counted sources (1.7.6)
  if(hudFill){auto map=std::atomic_load(&hudNear);if(map&&map->size()==(size_t)w*h){
   std::shared_ptr<std::vector<uint8_t>> px; // reuse a listed buffer nobody holds (a new 8 MB buffer each time cost ~5 ms)
   {std::lock_guard<std::mutex> l(hudFillMutex);for(auto it=hudSpare.begin();it!=hudSpare.end();++it)if(it->use_count()==1){px=*it;hudSpare.erase(it);break;}}
   if(!px||px->size()!=bytes)px=std::make_shared<std::vector<uint8_t>>(bytes);
   fillRows((const uint32_t*)d->data(),(uint32_t*)px->data(),map->data(),0,(size_t)h);
   std::lock_guard<std::mutex> l(hudFillMutex);hudFills.push_back({d,px,map,newFillTag()});while(hudFills.size()>4){hudSpare.push_back(hudFills.front().px);hudFills.pop_front();}
   while(hudSpare.size()>4)hudSpare.pop_front();}}
  d.reset();
  profMapMs+=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-m0).count();++profMapN;}}
 void hudUpdate(const Data& next){
  if(!hudThread.joinable()){try{hudThread=std::thread(&Live::hudLoop,this);}catch(...){return;}}
  {std::lock_guard<std::mutex> l(hudMutex);hudPending=next;}hudWake.notify_one();
 }
 bool noKeepSimd=false,keepVerify=false;std::atomic<uint64_t> keepMismatch{0},keepCompared{0}; // test only: CS_TEST_KEEP_SCALAR=1, CS_TEST_KEEP_VERIFY=1
 int forceQuarter=0;uint64_t quarterSerial=0; // 2.0.2 "지연 우선": RIFE at quarter-size flow on any GPU (cs_live_set_quarter)
 int staticKeep=0;int keepAgree=24;bool mapGate=true; // mapGate: 2.2.2, see gateMap
 bool optionsSet=false; // keepAgree: 2.1.2, CS_TEST_KEEP_AGREE=<0-255> (255 = old rule) // optionsSet: the settings window chose; configure keeps its choice
 double cadenceExtraMs=-1;int cadenceTest=0;bool cadence30=false,cadenceLite=false,cadenceFull=false,cadenceAll=false;std::chrono::steady_clock::time_point cadenceModeAt{};uint32_t gapBits=0;int gapCount=0;uint64_t prevSourceIndex=UINT64_MAX,extraNs=0;
 Data lastOriginal;bool lastGenerated=false;
 Source proofA,proofB;float proofPhase=0;bool proofValid=false;
 CsStreamStats stats{};CsLiveMetrics metrics{};
 // 1.7.8 diagnostics: stage times of the last 240 measured inferences (in, gpu, out, total ms) and when each
 // started, so the share of wall time the worker spent inferring (busy) is known. gpuInfo: driver/queue line.
 struct StageSample{double v[4];uint64_t start;};std::array<StageSample,240> stageRing{};uint64_t stageCount=0;std::string gpuInfo;
 void* keeper=nullptr; // 1.7.8 GPU clock keep (settings switch), active while generation is on
 std::chrono::steady_clock::time_point wallStamp=std::chrono::steady_clock::now();
 uint64_t currentTime()const{
#ifdef CS_SCHEDULE_TEST
  return now; // deterministic test caller supplies a virtual clock
#else
  auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-wallStamp).count();
  return now+(elapsed>0?(uint64_t)elapsed:0);
#endif
 }
 FILE* trace=nullptr;unsigned traceRows=0;
 void diagnostic(const char* kind,uint64_t x,uint64_t y,uint64_t z,uint64_t v){if(trace&&traceRows++<3000000){fprintf(trace,"%s,%llu,%llu,%llu,%llu,%llu,%llu\n",kind,(unsigned long long)currentTime(),(unsigned long long)x,(unsigned long long)y,(unsigned long long)z,(unsigned long long)v,(unsigned long long)lastContent);if(traceRows%120==0)fflush(trace);}}
 Live(const wchar_t* path,int device,int width,int height,int count,int ch):w(width),h(height),gpu(device),workers(count),channels(ch),bytes((size_t)w*h*ch),model(path),selected(bytes){

  metrics.workers=workers;
  if(const wchar_t* file=_wgetenv(L"CS_SCHEDULE_TRACE"))trace=_wfopen(file,L"w");
  requested.fill(UINT64_MAX);
  try{for(int i=0;i<workers;i++)threads.emplace_back(&Live::run,this,i);}catch(...){
   {std::lock_guard<std::mutex> l(mutex);stop=true;}event.notify_all();for(auto& t:threads)t.join();throw;
  }
 }
 ~Live(){if(_wgetenv(L"CS_TEST_KEEP_PROFILE"))fprintf(stderr,"keep_static avg_ms=%.3f n=%llu | hud_count avg_ms=%.3f n=%llu | hud_map avg_ms=%.3f n=%llu\n",profKeepN?profKeepMs/1000.0/profKeepN:0.0,(unsigned long long)profKeepN.load(),profHudN?profHudMs/1000.0/profHudN:0.0,(unsigned long long)profHudN.load(),profMapN?profMapMs/1000.0/profMapN:0.0,(unsigned long long)profMapN.load());if(attenVerify)fprintf(stderr,"atten_verify mismatches=%llu checked=%llu\n",(unsigned long long)attenMismatch.load(),(unsigned long long)attenChecked.load());fprintf(stderr,"hud_fill avg_ms=%.3f n=%llu hud_transitions=%llu gated_pairs=%llu\n",profFillN?profFillMs/1000.0/profFillN:0.0,(unsigned long long)profFillN.load(),(unsigned long long)hudTransitions.load(),(unsigned long long)gatedPairs.load());if(keepVerify)fprintf(stderr,"keep_verify mismatched_bytes=%llu of %llu\n",(unsigned long long)keepMismatch.load(),(unsigned long long)keepCompared.load());{std::lock_guard<std::mutex> l(hudMutex);hudStop=true;}hudWake.notify_one();if(hudThread.joinable())hudThread.join();{std::lock_guard<std::mutex> l(mutex);stop=true;}event.notify_all();for(auto& t:threads)t.join();if(trace)fclose(trace);
#ifdef CS_GPU_KEEPER
  cs_keeper_stop(keeper);
#endif
 }
 uint64_t grid(uint64_t ns)const{return (uint64_t)((long double)(ns-base+2)*tn/(1000000000.0L*td));}
 uint64_t time(uint64_t n)const{return base+(uint64_t)((long double)n*1000000000.0L*td/tn);}
 void reset(){++epoch;driftFrames=0;lastIndex=UINT64_MAX;lastTick=lastDisplay=tickInterval=0;haveBase=false;sourceAnchored=false;lastContent=0;lastOutput=UINT64_MAX;lastSlot=UINT64_MAX;jobs.clear();history.clear();proofValid=false;proofA={};proofB={};lastOriginal.reset();lastGenerated=false;cadence30=false;gapBits=0;gapCount=0;prevSourceIndex=UINT64_MAX;extraNs=0;requested.fill(UINT64_MAX);for(auto& r:results){r.valid=false;r.a={};r.b={};}}
 void configure(int enabled,int a,int b,int c,int d,int mode,const CsRatePlan& plan){
  std::lock_guard<std::mutex> l(mutex);reset();on=enabled!=0;sn=a;sd=b;tn=c;td=d;
#ifdef CS_GPU_KEEPER
  cs_keeper_active(keeper,on);
#endif
  {const wchar_t* v=_wgetenv(L"CS_LIVE_SMOOTH");noSmooth=v&&!wcscmp(v,L"0");noPull=v&&!wcscmp(v,L"clamp");}
  {const wchar_t* v=_wgetenv(L"CS_LIVE_SLOT");slotRound=v&&!wcscmp(v,L"round");}
  {const wchar_t* v=_wgetenv(L"CS_LIVE_TTA");ttaOff=v&&!wcscmp(v,L"0");}
  {const wchar_t* v=_wgetenv(L"CS_LIVE_TTA_K");double k=v?_wtof(v):0;if(k>=1.5&&k<=6)ttaK=k;} // test: deadline margin in single passes
  {const wchar_t* v=_wgetenv(L"CS_LIVE_AUX");auxOff=!(v&&!wcscmp(v,L"1"));} // off by default: live PS5 GTA5 (25 ms buffer) it fit 1 of 1345 frames // read by the deterministic checks too; "clamp" = 1.9.3 smoothing (A/B)
#ifdef CS_SCHEDULE_TEST
  waitNs=0; // virtual clock: a wall-clock wait would only slow the deterministic checks
#else
  {const wchar_t* v=_wgetenv(L"CS_LIVE_WAIT");waitNs=v&&!wcscmp(v,L"0")?0:(uint64_t)(900000000.0L*d/c);}
  {const wchar_t* v=_wgetenv(L"CS_LIVE_DRIFT");noDrift=v&&!wcscmp(v,L"0");}
  {const wchar_t* v=_wgetenv(L"CS_TEST_ENGINE_DELAY_MS");testDelayMs=v?std::max(0.0,_wtof(v)):0;} // test only: simulates a slower GPU
#endif
  // Read in the deterministic checks too (cadence_check).
  if(!optionsSet){const wchar_t* v=_wgetenv(L"CS_TEST_CADENCE30");cadenceTest=v?_wtoi(v):2; /* 1.6.8-t1: on by default; the variable only switches it off for A/B */if(cadenceTest<0||cadenceTest>2)cadenceTest=0;}
  {const wchar_t* v=_wgetenv(L"CS_TEST_HUD_FILL");hudFill=!(v&&v[0]==L'0');}
  {const wchar_t* v=_wgetenv(L"CS_TEST_HUD_ATT_MIN");if(v){int t=_wtoi(v);hudAttMin=t<30?30:t>255?255:t;}}
 {const wchar_t* v=_wgetenv(L"CS_TEST_HUD_NEAR_R");if(v)hudNearR=std::max(0,std::min(64,_wtoi(v)));v=_wgetenv(L"CS_TEST_HUD_NEAR_PCT");if(v)hudNearPct=std::max(0,std::min(100,_wtoi(v)));}
  {const wchar_t* v=_wgetenv(L"CS_TEST_KEEP_AGREE");if(v){int t=_wtoi(v);keepAgree=t<0?0:t>255?255:t;}}
 {const wchar_t* v=_wgetenv(L"CS_TEST_KEEP_MAP_GATE");if(v&&v[0]==L'0')mapGate=false;} // test only: 0 = map as it is
  noKeepSimd=_wgetenv(L"CS_TEST_KEEP_SCALAR")!=nullptr;keepVerify=_wgetenv(L"CS_TEST_KEEP_VERIFY")!=nullptr;
  {const wchar_t* v=_wgetenv(L"CS_TEST_HUD_ATTEN");hudAtten=!(v&&v[0]==L'0');}
  attenVerify=_wgetenv(L"CS_TEST_ATTEN_VERIFY")!=nullptr;attenScalar=_wgetenv(L"CS_TEST_ATTEN_SCALAR")!=nullptr;
  if(const wchar_t* v=_wgetenv(L"CS_TEST_HUD_TOL"))hudTol=std::max(0,std::min(32,_wtoi(v))); // test only
  if(!optionsSet){const wchar_t* v=_wgetenv(L"CS_TEST_STATIC_KEEP");staticKeep=v?_wtoi(v):4; /* 1.6.8-t1 default */if(staticKeep<0||staticKeep>4)staticKeep=0;}
  {const wchar_t* v=_wgetenv(L"CS_TEST_CADENCE30_MS");cadenceExtraMs=v?_wtof(v):-1;} // extra buffer for mode 2; default one output interval
  cadenceFull=_wgetenv(L"CS_TEST_CADENCE_FULL")!=nullptr;cadenceAll=_wgetenv(L"CS_TEST_CADENCE_ALL")!=nullptr;cadenceLite=false;
  diagnostic("cadence_cfg",(uint64_t)cadenceTest,0,0,0);
  delayNs=cs_native_delay_ns(w,h,a,b,c,d,mode,(uint64_t)((long double)plan.delay*d*1000000000.0L/c),calibrationMs);
  if(mode&&modelCalibration&&calibrationMs>0){double ms=std::min(200.0,1000.0*b/a+calibrationMs*(midpointModel&&(int64_t)c*b>2LL*a*d?2.0:1.0)*1.25+1000.0*d/c);delayNs=std::max(delayNs,(uint64_t)(ms*1000000));}
  stable=mode;planDelayNs=delayNs;lowLabel=0;++configured;retune();
 }
 int lowLabel=0; // low latency buffer choice (cs_live_set_low_buffer_ms); 0 keeps the plan buffer
 uint64_t configured=0; // configure count: workers re-measure the inference cost after each change
 // Test only (CS_LIVE_RESERVE_MS=<ms>): buffer = wait for the next original (whole output ticks)
 // plus this reserve instead of whole ticks, never above the plan buffer. Not the default: live
 // 60->160 low latency with inference x1.3+1.5 ms (about 5 ms less buffer) fell from 98.3% to 93.4%
 // even display changes (186 vs 19 bad seconds). Stable above FHD keeps its native/manual buffer.
 void retune(){
  if(lowLabel&&!stable&&!modelCalibration&&calibrationMs>0){delayNs=cs_low_buffer_ns(w,h,sn,sd,tn,td,lowLabel,planDelayNs);
   // 1.9.5: a short buffer the GPU cannot meet generates nothing (office RX 6600 Mario Kart 60->120: RIFE quarter flow
   // 12.6 ms at 15 ms buffer 1.6% generated, at 25 ms 99.9%). Raise it only as far as the measured cost needs
   // (half an output tick + cost x1.3 + 4 ms; x1.15+2 ms gave RIFE 20.9 ms buffer, 52%), never above the plan buffer.
   // Fast GPUs (cost ~3-5 ms) keep the choice.
   if(!getenv("CS_TEST_NO_LOW_RAISE")){uint64_t need=(uint64_t)(500000000.0L*td/tn+(calibrationMs*1.3+4.0)*1000000.0L);
    if(need>delayNs){uint64_t was=delayNs;delayNs=std::min(need,planDelayNs);diagnostic("low_raise",was,delayNs,(uint64_t)(calibrationMs*1000),0);lowBaseNs=was;raisedNs=delayNs;}else lowBaseNs=raisedNs=0;}
   ++retuneSerial;return;}
#ifndef CS_SCHEDULE_TEST
  const wchar_t* override=_wgetenv(L"CS_LIVE_RESERVE_MS");if(!override||!*override)return;
#else
  return;
#endif
  if(modelCalibration||!(calibrationMs>0)||(stable&&(int64_t)w*h>1920LL*1080))return;
  int64_t a=(int64_t)sn*td,b=(int64_t)tn*sd;if(a<=0||b<=a)return;
  int arrival=b%a?(int)((b+a-1)/a):(int)(b/a)-1;long double tick=1000000000.0L*td/tn;
#ifndef CS_SCHEDULE_TEST
  double reserve=_wtof(override);
#else
  double reserve=12;
#endif
  if(reserve<3)reserve=3;
  uint64_t want=(uint64_t)(arrival*tick+reserve*1000000.0L);
  delayNs=std::min(planDelayNs,want);
 }
 void keepStatic(const uint8_t* a,const uint8_t* b,uint8_t* out,float phase,CsPool& pool,bool forceScalar=false,const std::shared_ptr<std::vector<uint8_t>>* pin=nullptr)const{
  if(staticKeep>=2&&channels==4){
   // One pass, no buffers: equal -> original; colour within 32 and an equal 4-neighbour -> linear blend at the phase.
   // (The 3x3 mask version took long enough to make 862 of 8400 ticks miss their image.)
   const uint32_t* x=(const uint32_t*)a;const uint32_t* y=(const uint32_t*)b;uint32_t* o=(uint32_t*)out;const int k=(int)(phase*256+.5f);
   std::shared_ptr<std::vector<uint8_t>> nearMap=staticKeep>=4?(pin?*pin:std::atomic_load(&hudNear)):nullptr;const uint8_t* hud=nearMap&&nearMap->size()==(size_t)w*h?nearMap->data():nullptr;
   auto eq=[&](size_t i){return !((x[i]^y[i])&0x00FFFFFFu);};
   // 2.1.2 (11-11 user: GTA5 quality mode with 30 fps smoothing "breaks"): outside the HUD map an original pixel is put
   // back only where the generated pixel already agrees with it (every channel within keepAgree). Equal originals do
   // not mean nothing passed: a thin pole crossing a sky pixel between two pictures 33 ms apart left sky dots on the
   // pole, and the "within 32 + equal neighbour" rule left dark dots on moving building texture (replay of the real
   // input: gone with HUD keep off, not with the semi-transparent part off). HUD map pixels keep the old rule.
   const int ka=keepAgree;auto agree=[&](uint32_t g,uint32_t n){for(int s=0;s<24;s+=8){int d=(int)((g>>s)&255)-(int)((n>>s)&255);if(d>ka||d<-ka)return false;}return true;};
   // Split by rows over four threads: one thread took 3-5 ms and made results miss their tick (497 repeats in 30 s).
   // 1.8.0: the three helpers are the worker's persistent pool (they were created for every generated frame).
   auto rows=[&](int r0,int r1){for(int r=r0;r<r1;r++){size_t row=(size_t)r*w;
    for(int c=0;c<w;c++){size_t i=row+c;uint32_t p=x[i],q=y[i];
     if(hud&&hud[i]){o[i]=k<128?p:q;continue;}
     if(!agree(o[i],k<128?p:q))continue; // 2.1.2: the model moved something through this pixel (thin pole, 30 fps gap)
     if(!((p^q)&0x00FFFFFFu)){o[i]=p;continue;}
     int d0=(int)(p&255)-(int)(q&255),d1=(int)((p>>8)&255)-(int)((q>>8)&255),d2=(int)((p>>16)&255)-(int)((q>>16)&255);
     if(d0>32||d0<-32||d1>32||d1<-32||d2>32||d2<-32)continue;
     if(!((c>0&&eq(i-1))||(c+1<w&&eq(i+1))||(r>0&&eq(i-w))||(r+1<h&&eq(i+w))))continue;
     if(staticKeep>=3){o[i]=k<128?p:q;continue;} // 3: nearest original, never a soft cross-fade (edges pumped between sharp and blended)
     uint32_t v=o[i]&0xFF000000u;for(int s=0;s<24;s+=8)v|=(uint32_t)((((p>>s)&255)*(256-k)+((q>>s)&255)*k+128)>>8)<<s;o[i]=v;}}};
   // 1.8.8: SSE2 for the inner columns of inner rows when every selected pixel takes the nearest original (3 and 4):
   // equal, HUD, or colour within 32 with an equal 4-neighbour is one mask and one select per 4 pixels (the scalar loop
   // took 2.5-3.1 ms per generated frame on four threads at FHD, Zelda replay 11-09). Borders use the scalar loop.
   if(staticKeep>=3&&w>=8&&h>=3&&!noKeepSimd&&!forceScalar){
    const __m128i rgb=_mm_set1_epi32(0x00FFFFFF),zero=_mm_setzero_si128(),c32=_mm_set1_epi8(32),cka=_mm_set1_epi8((char)std::min(ka,255));const bool takeA=k<128;
    auto eqv=[&](size_t i){return _mm_cmpeq_epi32(_mm_and_si128(_mm_xor_si128(_mm_loadu_si128((const __m128i*)(x+i)),_mm_loadu_si128((const __m128i*)(y+i))),rgb),zero);};
    auto vrows=[&](int r0,int r1){for(int r=r0;r<r1;r++){
     if(r==0||r==h-1){rows(r,r+1);continue;}
     size_t row=(size_t)r*w;int c=1;
     for(;c+4<=w-1;c+=4){size_t i=row+c;__m128i P=_mm_loadu_si128((const __m128i*)(x+i)),Q=_mm_loadu_si128((const __m128i*)(y+i));
      __m128i E=_mm_cmpeq_epi32(_mm_and_si128(_mm_xor_si128(P,Q),rgb),zero);
      __m128i D=_mm_or_si128(_mm_subs_epu8(P,Q),_mm_subs_epu8(Q,P));__m128i S=_mm_cmpeq_epi32(_mm_and_si128(_mm_subs_epu8(D,c32),rgb),zero);
      __m128i N=_mm_or_si128(_mm_or_si128(eqv(i-1),eqv(i+1)),_mm_or_si128(eqv(i-w),eqv(i+w)));
      __m128i O=_mm_loadu_si128((const __m128i*)(o+i));__m128i nearest=takeA?P:Q;
      __m128i G=_mm_or_si128(_mm_subs_epu8(O,nearest),_mm_subs_epu8(nearest,O));__m128i ok=_mm_cmpeq_epi32(_mm_and_si128(_mm_subs_epu8(G,cka),rgb),zero);
      __m128i sel=_mm_and_si128(_mm_or_si128(E,_mm_and_si128(S,N)),ok);
      if(hud){int hv;memcpy(&hv,hud+i,4);__m128i hb=_mm_cvtsi32_si128(hv);hb=_mm_unpacklo_epi8(hb,zero);hb=_mm_unpacklo_epi16(hb,zero);
       sel=_mm_or_si128(sel,_mm_cmpgt_epi32(hb,zero));}
      _mm_storeu_si128((__m128i*)(o+i),_mm_or_si128(_mm_and_si128(sel,nearest),_mm_andnot_si128(sel,O)));}
     // scalar: column 0, the tail and column w-1 (rows() handles one row; redo only those pixels)
     auto one=[&](int cc){size_t i=row+cc;uint32_t p=x[i],q=y[i];
      if(hud&&hud[i]){o[i]=k<128?p:q;return;}
      if(!agree(o[i],k<128?p:q))return;
      if(!((p^q)&0x00FFFFFFu)){o[i]=p;return;}
      int d0=(int)(p&255)-(int)(q&255),d1=(int)((p>>8)&255)-(int)((q>>8)&255),d2=(int)((p>>16)&255)-(int)((q>>16)&255);
      if(d0>32||d0<-32||d1>32||d1<-32||d2>32||d2<-32)return;
      if(!((cc>0&&eq(i-1))||(cc+1<w&&eq(i+1))||eq(i-w)||eq(i+w)))return;
      o[i]=k<128?p:q;};
     one(0);for(;c<w;c++)one(c);}};
    pool.run((size_t)h,1,[&](size_t r0,size_t r1){vrows((int)r0,(int)r1);});
    return;}
   pool.run((size_t)h,1,[&](size_t r0,size_t r1){rows((int)r0,(int)r1);});
   return;}
  if(channels==4){const uint32_t* x=(const uint32_t*)a;const uint32_t* y=(const uint32_t*)b;uint32_t* o=(uint32_t*)out;size_t n=bytes/4;
   for(size_t i=0;i<n;i++)if(!((x[i]^y[i])&0x00FFFFFFu))o[i]=x[i];return;}
  for(size_t i=0;i+2<bytes;i+=3)if(a[i]==b[i]&&a[i+1]==b[i+1]&&a[i+2]==b[i+2]){out[i]=a[i];out[i+1]=a[i+1];out[i+2]=a[i+2];}
 }
 // 1.8.9: HUD fill. Fixed text over a moving background was warped along with the background: in the 11-09 Zelda
 // title recording the menu line showed a second copy about 20 px to the side in fast scrolling scenes, outside the
 // 2 px band HUD keep repairs. The model now sees each original with its HUD pixels replaced by a row-wise ramp
 // between the nearest non-HUD pixels, so there is no text to drag; keepStatic then puts the original HUD back.
 // Fill one row range: every run of HUD pixels becomes a linear ramp between the nearest non-HUD pixels of the row.
 void fillRows(const uint32_t* x,uint32_t* o,const uint8_t* m,size_t r0,size_t r1)const{
  for(size_t r=r0;r<r1;r++){size_t row=r*(size_t)w;memcpy(o+row,x+row,(size_t)w*4);const uint8_t* mr=m+row;
   for(int c=0;c<w;){if(!mr[c]){c++;continue;}int s0=c;while(c<w&&mr[c])c++;int L=s0-1,R=c;
    uint32_t pl=L>=0?x[row+L]:(R<w?x[row+R]:x[row+s0]),pr=R<w?x[row+R]:pl;int len=R-L;
    for(int q=s0;q<c;q++){int k=(int)(((q-L)*256)/len);uint32_t v=0xFF000000u&x[row+q];
     for(int sh=0;sh<24;sh+=8)v|=(uint32_t)((((pl>>sh)&255)*(256-k)+((pr>>sh)&255)*k+128)>>8)<<sh;o[row+q]=v;}}}
 }
 // The HUD thread fills each new original once, right after counting it, with the map current then; the worker looks
 // the picture up by its buffer (the entry holds a reference, so the buffer is not reused while listed). If the thread
 // skipped it, the worker fills it itself (worker fills cost 1.5 ms per generated frame on the Zelda replay).
 // 1.9.1: each fill keeps the map it was made with. The worker puts the original back over the union of the maps its two
 // inputs were filled with; it used the map current at keep time, which could be newer and smaller (a whole-screen fade
 // resets the counts), so filled ramps leaked out as full-width horizontal streaks (Switch home popup, user 11-09).
 // 1.9.2: each filled picture also gets its own source tag for the engine (bit 62 + serial). The engine caches inputs by
 // tag, so a filled copy sent under the original's id stayed cached and came back in later pairs that sent the raw
 // original (whole-screen change) or a copy filled with another map: streak frames remained in 1.9.1 (user 11-09 20:02).
 std::atomic<uint64_t> fillSerial{0};
 uint64_t newFillTag(){return (1ull<<62)|++fillSerial;} // bit 62: engine.cpp gives untagged calls bit 63 tags
 struct HudFillEntry{Data src;std::shared_ptr<std::vector<uint8_t>> px;std::shared_ptr<std::vector<uint8_t>> map;uint64_t tag=0;};
 mutable std::mutex hudFillMutex;std::deque<HudFillEntry> hudFills;std::deque<std::shared_ptr<std::vector<uint8_t>>> hudSpare;
 std::shared_ptr<std::vector<uint8_t>> findFill(const Data& src,std::shared_ptr<std::vector<uint8_t>>& used,uint64_t& tag)const{std::lock_guard<std::mutex> l(hudFillMutex);for(auto& e:hudFills)if(e.src==src){used=e.map;tag=e.tag;return e.px;}return nullptr;}
 struct HudFilled{uint64_t id=0;std::vector<uint8_t> px;std::shared_ptr<std::vector<uint8_t>> map;uint64_t tag=0;};
 const uint8_t* hudFilled(const Data& src,uint64_t id,const std::shared_ptr<std::vector<uint8_t>>& map,HudFilled* slots,CsPool& pool,std::shared_ptr<std::vector<uint8_t>>& hold,std::shared_ptr<std::vector<uint8_t>>& used,uint64_t& tag,bool fresh=false){
  // 2.2.2: fresh = the map was gated for this pair; a copy filled earlier with the whole map would bring the pasted area back.
  if(!fresh&&(hold=findFill(src,used,tag))&&used&&used->size()==(size_t)w*h)return hold->data();
  for(int k=0;k<2;k++)if(slots[k].id==id&&slots[k].px.size()==bytes&&slots[k].map==map){used=slots[k].map;tag=slots[k].tag;return slots[k].px.data();}
  HudFilled& f=slots[0].id<slots[1].id?slots[0]:slots[1];f.id=id;f.px.resize(bytes);f.map=map;used=map;f.tag=tag=newFillTag();
  const uint32_t* x=(const uint32_t*)src->data();uint32_t* o=(uint32_t*)f.px.data();const uint8_t* m=map->data();
  pool.run((size_t)h,1,[&](size_t r0,size_t r1){fillRows(x,o,m,r0,r1);});
  return f.px.data();
 }
 // 2.2.2 (11-13 user: GTA5 in-game web page, letters doubled and red bars left behind when it starts to scroll; gone with
 // HUD keep off, not with the fill off): the map still held the page that had stood still, so its pixels were filled before
 // generation and pasted from the nearer original after it, although the page had moved. Per 32 px block (4x smaller grey
 // copies of the two originals): when the change between them is mostly explained by a vertical or horizontal shift of up
 // to 48 px, the content moved, and that block plus one around it leave the map for this pair (no fill, no paste: the model
 // output stays). A HUD changing in place (numbers, a highlight) is no shift and stays kept. Returns null when nothing moved.
 // CS_TEST_KEEP_MAP_GATE=0 keeps the map as it is.
 std::shared_ptr<std::vector<uint8_t>> gateMap(const uint8_t* a8,const uint8_t* b8,const std::vector<uint8_t>& map,CsPool& pool)const{
  const uint32_t* x=(const uint32_t*)a8;const uint32_t* y=(const uint32_t*)b8;
  const int W4=w/4,H4=h/4,BS=8,R=12,bw=(W4+BS-1)/BS,bh=(H4+BS-1)/BS;if(W4<BS||H4<BS)return nullptr;
  std::vector<uint8_t> A4((size_t)W4*H4),B4((size_t)W4*H4),M4((size_t)W4*H4),blk((size_t)bw*bh,0);
  pool.run((size_t)H4,4,[&](size_t r0,size_t r1){for(size_t r=r0;r<r1;r++)for(int c=0;c<W4;c++){unsigned sa=0,sb=0;
   for(int dy=0;dy<4;dy++){size_t i=((r*4+dy)*(size_t)w+(size_t)c*4);for(int dx=0;dx<4;dx++){sa+=(x[i+dx]>>8)&255;sb+=(y[i+dx]>>8)&255;}}
   A4[r*W4+c]=(uint8_t)(sa>>4);B4[r*W4+c]=(uint8_t)(sb>>4);M4[r*W4+c]=map[(r*4+2)*(size_t)w+(size_t)c*4+2]?1:0;}});
  pool.run((size_t)bh,1,[&](size_t b0,size_t b1){for(size_t by=b0;by<b1;by++)for(int bx=0;bx<bw;bx++){
   const int y0=(int)by*BS,x0=bx*BS,y1=std::min(H4,y0+BS),x1=std::min(W4,x0+BS);unsigned nm=0,d0=0;
   for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++)if(M4[(size_t)yy*W4+xx]){++nm;int d=(int)A4[(size_t)yy*W4+xx]-(int)B4[(size_t)yy*W4+xx];d0+=d<0?-d:d;}
   if(nm<16||d0<12*nm)continue; // few map pixels here, or the map pixels did not change (a HUD over a moving picture)
   unsigned best=d0;
   for(int s=-R;s<=R&&best*2>=d0;s++){if(!s)continue;unsigned dv=0,dh=0;bool ov=y0+s>=0&&y1+s<=H4,oh=x0+s>=0&&x1+s<=W4;
    for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){if(!M4[(size_t)yy*W4+xx])continue;int av=A4[(size_t)yy*W4+xx];
     if(ov){int d=av-(int)B4[(size_t)(yy+s)*W4+xx];dv+=d<0?-d:d;}if(oh){int d=av-(int)B4[(size_t)yy*W4+xx+s];dh+=d<0?-d:d;}}
    if(ov&&dv<best)best=dv;if(oh&&dh<best)best=dh;}
   if(best*2<d0)blk[by*bw+bx]=1;}});
  bool any=false;for(uint8_t v:blk)if(v){any=true;break;}if(!any)return nullptr;
  std::vector<uint8_t> grown(blk);for(int by=0;by<bh;by++)for(int bx=0;bx<bw;bx++)if(blk[(size_t)by*bw+bx])
   for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int yy=by+dy,xx=bx+dx;if(yy>=0&&yy<bh&&xx>=0&&xx<bw)grown[(size_t)yy*bw+xx]=1;}
  auto out=std::make_shared<std::vector<uint8_t>>(map);uint8_t* g=out->data();const int bpx=BS*4;
  pool.run((size_t)h,8,[&](size_t r0,size_t r1){for(size_t r=r0;r<r1;r++){const uint8_t* gr=grown.data()+std::min((size_t)bh-1,r/bpx)*bw;uint8_t* row=g+r*w;
   for(int c=0;c<w;c++)if(gr[std::min(bw-1,c/bpx)])row[c]=0;}});
  ++gatedPairs;return out;
 }
 mutable std::atomic<uint64_t> gatedPairs{0};
 bool hudFill=true; // test only: CS_TEST_HUD_FILL=0 turns it off
 // 1.9.1: a pair where most HUD pixels change is a whole-screen change (menu fade, popup dimming the screen), not HUD over
 // a moving picture: the pair is generated without fill and without HUD keep, like HUD keep off (90% of HUD pixels changed by more than 16). With them the fade
 // snapped between the originals and showed fill streaks (Switch home popup, user 11-09). Sampled every 7th pixel.
 std::atomic<uint64_t> hudTransitions{0};
 bool hudTransition(const uint32_t* a,const uint32_t* b,const uint8_t* m)const{
  size_t n=(size_t)w*h,total=0,changed=0;
  for(size_t i=0;i<n;i+=7){if(!m[i])continue;++total;uint32_t p=a[i],q=b[i];
   for(int s=0;s<24;s+=8){int d=(int)((p>>s)&255)-(int)((q>>s)&255);if(d>16||d<-16){++changed;break;}}}
  return total>=500&&changed*10>total*9; // 90%: a camera cut under a fixed logo changed ~50-60% (background held as HUD), and turning keep off there smeared the logo
 }
 static std::shared_ptr<std::vector<uint8_t>> mapUnion(const std::shared_ptr<std::vector<uint8_t>>& a,const std::shared_ptr<std::vector<uint8_t>>& b){
  if(!a||a==b)return b;if(!b||a->size()!=b->size())return a;
  auto u=std::make_shared<std::vector<uint8_t>>(*a);uint8_t* o=u->data();const uint8_t* y=b->data();for(size_t i=0;i<u->size();i++)o[i]|=y[i];return u;
 }
 int interpolate(void* engine,const uint8_t* a,const uint8_t* b,float t,uint8_t* out,uint64_t aid,uint64_t bid)const{
  if(channels==4)return cs_interpolate_bgra(engine,a,b,w,h,t,out,aid,bid);
  return aid?cs_interpolate_tagged(engine,a,b,w,h,t,out,aid,bid):cs_interpolate(engine,a,b,w,h,t,out);
 }
 void run(int workerIndex)noexcept{
  void* engine=nullptr;
  try{
   engine=cs_create(model.c_str(),gpu);if(!engine)throw 1;
   // 1.8.0: worker 0 builds the whole line and publishes it once; cs_live_gpu_info hands out its pointer, so it is never
   // changed afterwards (1.7.8/1.7.9 appended the keep result after publishing).
   std::string info;
#ifndef CS_SCHEDULE_TEST
   if(workerIndex==0)info=cs_gpu_info(engine);
#endif
#ifdef CS_GPU_KEEPER
   if(workerIndex==0&&(cs_gpu_options_value()&1)){uint32_t v=0,d=0;void* k=cs_engine_ids(engine,&v,&d)?nullptr:cs_keeper_start(v,d);
    std::lock_guard<std::mutex> l(mutex);keeper=k;cs_keeper_active(keeper,on);info+=k?" keep_clock=1":" keep_clock=failed";}
#endif
#ifndef CS_SCHEDULE_TEST
   if(workerIndex==0){std::lock_guard<std::mutex> l(mutex);gpuInfo=info;}
#endif
#ifndef CS_SCHEDULE_TEST
   {std::lock_guard<std::mutex> l(mutex);const char* name=cs_model_name(engine);midpointModel=name&&strcmp(name,"CAIN")==0;midpointOnly=midpointModel||(name&&!strncmp(name,"FSR",3));modelCalibration=name&&(strncmp(name,"IFRNet",6)==0||midpointModel);}
#endif
   std::vector<uint8_t> output(bytes);CsPool rowPool; // 1.8.0: HUD keep rows
   HudFilled filled[2];
   // The flow size follows the mode (stable: about FHD, low latency: half from FHD up), so the
   // inference cost and the buffer derived from it are measured again after each configure.
   // Stable keeps the FHD-size flow only while the generated frames per original fit in 80% of
   // the source interval (in-app FHD flow 9.6-12.3 ms at 2x = 58-74%), judged on the live inference times (the synthetic startup calls are
   // slower than live and wrongly dropped 60->120 to half): 60->160 with FHD flow (1.67 x 9.6 ms)
   // repeated 14.5% of frames live, 60->120 (1 x 9.6 ms) 0.08%. Above that it uses the half-size flow
   // until the next configure.
   // A configure that keeps the mode only re-arms the FHD floor; the synthetic calls (which stall
   // generation for a few inferences) run at startup and when the mode changes.
   uint64_t seen=UINT64_MAX,quarterSeen=0;int flowFloor=0,lastMode=-1,overload=0,slowFlow=0;double gens=1,source=16.7;bool halfFor30=false,full30=false,full30Off=false;const bool noFull30=!(getenv("CS_LIVE_FULL30")&&!strcmp(getenv("CS_LIVE_FULL30"),"1"));const bool stable30Full=getenv("CS_LIVE_STABLE30")&&!strcmp(getenv("CS_LIVE_STABLE30"),"full");
   // 1.9.5 slow GPU: in low latency, RIFE whose half-size flow does not fit the generated frames per original
   // (same 80% rule as the stable floor) switches to quarter-size flow until the next mode change. Office RX 6600
   // FHD 2x: half 17-21 ms (generation 15% live), quarter 10.2 ms. Fast GPUs (5080 half ~4.6 ms) never switch.
   bool rifeFamily=false;
#ifndef CS_SCHEDULE_TEST
   {const char* nm=cs_model_name(engine);rifeFamily=nm&&!strncmp(nm,"RIFE",4)&&!getenv("CS_TEST_NO_SLOW_FLOW");}
#endif
   auto calibrate=[&]{
    bool measure;
    {std::lock_guard<std::mutex> l(mutex);measure=stable!=lastMode;lastMode=flowFloor=stable;seen=configured;gens=(double)tn*sd/((double)sn*td)-1;source=1000.0*sd/sn;}
#ifndef CS_SCHEDULE_TEST
    // 2.0.2: latency priority (settings switch) or CS_TEST_QUARTER starts RIFE in quarter-size flow. 11-11 night, RTX 5080
    // FHD 60->120: buffer 18.1 -> 15.8 ms with HUD keep on, generation 99.8%; ground truth on RX 6600 frames -0.9 dB.
    {uint64_t qs;int fq;{std::lock_guard<std::mutex> l(mutex);qs=quarterSerial;fq=forceQuarter;}
     if(measure||qs!=quarterSeen){quarterSeen=qs;slowFlow=(fq||getenv("CS_TEST_QUARTER"))&&rifeFamily?1:0;}}
    cs_set_flow_floor(engine,flowFloor?flowFloor:slowFlow?2:0);hudRadius=!flowFloor&&slowFlow?4:2;
    {std::lock_guard<std::mutex> l(mutex);diagnostic("floor",(uint64_t)flowFloor,(uint64_t)measure,(uint64_t)(gens*1000),(uint64_t)(source*1000));}
    if(!measure)return;
    std::vector<uint8_t> a(bytes,32),b(bytes,48);double worst=0;
    for(int i=0;i<3;i++){if(interpolate(engine,a.data(),b.data(),.5f,output.data(),0,0))throw 2;if(i>0)worst=std::max(worst,cs_last_ms(engine)+testDelayMs);}
    if(!flowFloor&&rifeFamily&&(int64_t)w*h>=1920LL*1080&&gens*worst>.8*source){
     double half=worst;slowFlow=1;cs_set_flow_floor(engine,2);hudRadius=4;worst=0;
     for(int i=0;i<3;i++){if(interpolate(engine,a.data(),b.data(),.5f,output.data(),0,0))throw 2;if(i>0)worst=std::max(worst,cs_last_ms(engine)+testDelayMs);}
     std::lock_guard<std::mutex> l(mutex);diagnostic("slow_flow",(uint64_t)(half*1000),(uint64_t)(worst*1000),(uint64_t)(gens*1000),0);
    }
    std::lock_guard<std::mutex> l(mutex);calibrationMs=workerIndex?std::max(calibrationMs,worst):worst;
    uint64_t before=delayNs;retune();if(delayNs!=before&&haveBase)reset();
#endif
   };
   calibrate();
   {std::lock_guard<std::mutex> l(mutex);if(++readyWorkers==workers&&stats.ready>=0)stats.ready=1;}
   uint64_t cachedA=0,cachedB=0;
   double estimate=0;auto lastAttempt=std::chrono::steady_clock::now()-std::chrono::seconds(1);
   // One stalled inference (245ms vs 65ms) raised the EMA above the whole
   // buffer and rejected every fresh job for ~1.5s of 500ms retries. The
   // median of recent samples ignores a single spike; a sustained slowdown
   // still raises it after three samples.
   std::array<double,5> recent{};size_t recentCount=0;
   // 2.0.1 step-down state (see lowBaseNs): steady cost ring, when the buffer was lowered, misses at that moment.
   std::array<double,240> tune{};size_t tuneCount=0;uint64_t tuneSerial=UINT64_MAX,loweredFrom=0,missBase=0,tickBase=0;
   auto tuneStart=std::chrono::steady_clock::now(),tuneLast=tuneStart,tuneLowered=tuneStart;bool tuneLatched=false;
   const bool noStepDown=getenv("CS_TEST_NO_STEP_DOWN")!=nullptr;
   auto robust=[&]{if(recentCount<3)return estimate;std::array<double,5> s=recent;size_t n=std::min(recentCount,s.size());std::nth_element(s.begin(),s.begin()+n/2,s.begin()+n);return std::min(estimate,s[n/2]);};
   for(;;){
    {bool changed;{std::lock_guard<std::mutex> l(mutex);changed=configured!=seen&&!stop;}if(changed){calibrate();halfFor30=false;full30=false;full30Off=false;}}
    // 1.0.1 (11-12 night): stable mode uses full-size flow at FHD; a 30 fps game (large motion per pair) is better at half
    // size (ground truth GTA5 23.52 vs 21.78 dB, with reversed pass 24.01 vs 22.14) and half also leaves time for that pass.
    // CS_LIVE_STABLE30=full keeps full size.
    if(flowFloor==1&&rifeFamily&&!stable30Full){bool th;{std::lock_guard<std::mutex> l(mutex);th=thirty;}
     if(th!=halfFor30){halfFor30=th;cs_set_flow_floor(engine,th?0:1);std::lock_guard<std::mutex> l(mutex);diagnostic("stable30",th,0,0,0);}}
    // 2.2.2 (11-13 user: GTA5 text boxes and tree trunks break in fast turns): low latency at FHD runs half-size flow, and a
    // 30 fps game pair (33 ms apart) leaves time for full size. Offline on the same pair: half bent the text-box letters and
    // smeared the trunk, full kept them (RIFE 4.25/4.26 looked like 4.6 full at 1.5-1.7x the time). 2x only (one picture per
    // pair); dropped for this configuration if it does not keep up (see full30Off). Off by default, CS_LIVE_FULL30=1 turns it on:
    // replay of exact 30 fps GTA5 frames at the 25 ms low buffer, full size missed 13 of 89 pictures (half 0) because the
    // midpoint is due only ~8 ms after the second original arrives; it would need a larger buffer (more latency).
    if(!flowFloor&&!slowFlow&&rifeFamily&&!noFull30&&!full30Off&&gens<=1.01){bool th;{std::lock_guard<std::mutex> l(mutex);th=thirty;}
     if(th!=full30){full30=th;cs_set_flow_floor(engine,th?1:0);recentCount=0;estimate=0;overload=0;std::lock_guard<std::mutex> l(mutex);diagnostic("full30",th,0,0,0);}}
    Job job;bool cachedMidpoint=false;
    {std::unique_lock<std::mutex> l(mutex);event.wait(l,[&]{return stop||(!jobs.empty()&&(workerIndex==0||((int64_t)tn*sd>2LL*sn*td&&!midpointOnly)));});if(stop)break; // 2.2.2: FSR 3 4x keeps one worker (its two FSR streams need the jobs in order)
    
     job=jobs.front();jobs.pop_front();diagnostic("job",job.n,time(job.n),job.due,job.a.id);
     // Avoid spending GPU time on phases whose display deadline has passed.
     uint64_t latest=job.due;
     // A transient GPU stall must not poison an estimate and reject every
     // subsequent job forever. Only discard slots already past their deadline.
     uint64_t clock=currentTime();
     if(job.epoch!=epoch||time(job.n)<=lastContent||latest<=clock){++metrics.expired_jobs;diagnostic("expire",job.n,job.a.id,latest>clock?latest-clock:0,job.epoch!=epoch?1:time(job.n)<=lastContent?2:3);continue;}
     // A rejected job is not a timing sample. Keep the measured cost instead
     // of making it artificially cheaper after every rejected phase. The
     // source-period cap leaves room for a fresh pair after a transient stall.
     cachedMidpoint=midpointModel&&cachedA==job.a.id&&cachedB==job.b.id&&std::abs(job.phase-.5f)<1e-5f;
     double budget=cachedMidpoint?0:std::min(robust(),(modelCalibration||(int64_t)w*h>1920LL*1080)?delayNs/1000000.0:1100.0*sd/sn);
     bool retry=(modelCalibration||(int64_t)w*h>1920LL*1080)&&std::chrono::steady_clock::now()-lastAttempt>=std::chrono::milliseconds(500);
     if(budget>0&&latest-clock<(uint64_t)(budget*1.05*1000000)&&!retry){
      ++metrics.expired_jobs;diagnostic("expire",job.n,job.a.id,latest-clock,4);continue;
     }
    }
    bool ttaWant=false;{std::lock_guard<std::mutex> l(mutex);uint64_t clock=currentTime();
     ttaWant=!ttaOff&&thirty&&!midpointOnly&&!cachedMidpoint&&ttaSingleMs>0&&job.due>clock&&(double)(job.due-clock)/1e6>ttaSingleMs*ttaK+2.0;
     if(thirty&&!ttaOff&&!midpointOnly&&!ttaWant)++ttaSkips;}
    lastAttempt=std::chrono::steady_clock::now();
    if(trace){std::lock_guard<std::mutex> l(mutex);uint64_t clock=currentTime();diagnostic("start",job.n,job.a.id,job.due>clock?job.due-clock:0,(uint64_t)std::lround(job.phase*1000)|(cachedMidpoint?65536:0));}
    const uint64_t inferStart=currentTime();
    const uint8_t* inA=job.a.pixels->data();const uint8_t* inB=job.b.pixels->data();std::shared_ptr<std::vector<uint8_t>> holdA,holdB;
    std::shared_ptr<std::vector<uint8_t>> keepMap=staticKeep>=4?std::atomic_load(&hudNear):nullptr; // the map HUD keep uses for this pair
    bool transitionPair=false; // whole-screen change: no keep at all, exactly as HUD keep off
    uint64_t tagA=job.a.id,tagB=job.b.id; // engine cache tags: the source ids, or the fill tags when filled copies are sent
    bool gated=false;if(mapGate&&keepMap&&keepMap->size()==(size_t)w*h&&channels==4){auto g=gateMap(inA,inB,*keepMap,rowPool);if(g){keepMap=g;gated=true;}} // 2.2.2
    if(keepMap&&keepMap->size()==(size_t)w*h&&channels==4&&hudTransition((const uint32_t*)inA,(const uint32_t*)inB,keepMap->data())){keepMap.reset();transitionPair=true;++hudTransitions;}
    else if(staticKeep>=4&&channels==4&&hudFill&&keepMap&&keepMap->size()==(size_t)w*h){
     auto f0=std::chrono::steady_clock::now();std::shared_ptr<std::vector<uint8_t>> mA,mB;
     inA=hudFilled(job.a.pixels,job.a.id,keepMap,filled,rowPool,holdA,mA,tagA,gated);inB=hudFilled(job.b.pixels,job.b.id,keepMap,filled,rowPool,holdB,mB,tagB,gated);
     keepMap=mapUnion(mA,mB);
     profFillMs+=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-f0).count();++profFillN;}
    int code=interpolate(engine,inA,inB,job.phase,output.data(),tagA,tagB);
    double ttaFirstMs=code?0:cs_last_ms(engine);if(!code&&!cachedMidpoint)ttaSingleMs=ttaSingleMs>0?ttaSingleMs*.8+ttaFirstMs*.2:ttaFirstMs;
    bool ttaDone=false;double ttaRevMs=0;
    if(!code&&ttaWant){thread_local std::vector<uint8_t> rev;rev.resize(output.size());
     if(!interpolate(engine,inB,inA,1.f-job.phase,rev.data(),tagB,tagA)){ttaRevMs=cs_last_ms(engine);uint8_t* o=output.data();const uint8_t* r=rev.data();const size_t nb=output.size();
      for(size_t i=0;i<nb;i++)o[i]=(uint8_t)((o[i]+r[i]+1)>>1);ttaDone=true;++ttaJobs;}}
    bool auxDone=false;double auxThisMs=0;
    if(ttaDone&&!auxOff&&channels==4&&(int64_t)w*h<=1920LL*1200){uint64_t clock;{std::lock_guard<std::mutex> l(mutex);clock=currentTime();}
     double needMs=(auxMs>0?auxMs:ttaSingleMs*2.2)*1.15+2.0;
     if(job.due>clock&&(double)(job.due-clock)/1e6>needMs){thread_local std::vector<uint8_t> fx;fx.resize(output.size());
      if(!cs_interpolate_bgra_aux(engine,inA,inB,w,h,job.phase,fx.data(),tagA,tagB,1.f)){auxThisMs=cs_last_ms(engine);auxMs=auxMs>0?auxMs*.8+auxThisMs*.2:auxThisMs;
       uint8_t* o=output.data();const uint8_t* f=fx.data();const size_t nb=output.size();for(size_t i=0;i<nb;i++)o[i]=(uint8_t)((4*o[i]+f[i]+2)/5);auxDone=true;++auxJobs;}}}
    if(trace&&ttaWant){std::lock_guard<std::mutex> l(mutex);diagnostic("tta",job.n,ttaDone,auxDone,(uint64_t)(auxThisMs*1000));}
#ifndef CS_SCHEDULE_TEST
    if(!code){double st[3];if(cs_last_stages(engine,st)==0){std::lock_guard<std::mutex> l(mutex);auto& q=stageRing[stageCount++%stageRing.size()];q.v[0]=st[0];q.v[1]=st[1];q.v[2]=st[2];q.v[3]=cs_last_ms(engine);q.start=inferStart;}}
#endif
    if(!code&&staticKeep&&!transitionPair&&keepVerify){std::vector<uint8_t> t(output);auto pin=keepMap;keepStatic(job.a.pixels->data(),job.b.pixels->data(),t.data(),job.phase,rowPool,true,&pin);keepStatic(job.a.pixels->data(),job.b.pixels->data(),output.data(),job.phase,rowPool,false,&pin);uint64_t m=0;for(size_t i=0;i<bytes;i++)m+=t[i]!=output[i];keepMismatch+=m;keepCompared+=bytes;}
    else if(!code&&staticKeep&&!transitionPair){auto k0=std::chrono::steady_clock::now();keepStatic(job.a.pixels->data(),job.b.pixels->data(),output.data(),job.phase,rowPool,false,&keepMap);profKeepMs+=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-k0).count();++profKeepN;}
    double ms=ttaDone?ttaFirstMs+ttaRevMs+auxThisMs:cs_last_ms(engine);
    if(testDelayMs>0){auto until=std::chrono::steady_clock::now()+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(testDelayMs));if(testDelayMs>3)std::this_thread::sleep_for(std::chrono::duration<double,std::milli>(testDelayMs-2));while(std::chrono::steady_clock::now()<until){}ms+=testDelayMs;}
#ifndef CS_SCHEDULE_TEST
    double waitMs=trace?cs_last_gpu_wait_ms(engine):-1;
#else
    double waitMs=-1;
#endif
    if(!cachedMidpoint&&code!=-4){estimate=estimate?estimate*.5+ms*.5:ms;recent[recentCount++%recent.size()]=ms;}
    if(!cachedMidpoint&&code!=-4){double c=jobCostMs.load();jobCostMs.store(c>0?c*.9+ms*.1:ms);} // an instant -4 is not a timing sample
    if(!cachedMidpoint&&!code&&!noStepDown){
     // 2.0.1 low-latency step-down (FHD 60->120 on the 5080: buffer stayed 18.8 ms from the cold start cost while the
     // steady cost was 5.0 ms). After 8 s on one configuration, every 2 s: need = half tick + p95 x1.3 + 4 ms (the raise
     // formula with the steady p95); lower toward the user's buffer when that saves 1 ms or more. If misses then exceed
     // 1% of output ticks over 3 s, go back to the raised buffer and stop for this configuration.
     tune[tuneCount++%tune.size()]=ms;auto now=std::chrono::steady_clock::now();
     std::lock_guard<std::mutex> l(mutex);
     if(tuneSerial!=retuneSerial){tuneSerial=retuneSerial;tuneStart=tuneLast=now;tuneCount=0;loweredFrom=0;tuneLatched=false;}
     else if(raisedNs&&lowBaseNs&&!tuneLatched&&delayNs<=raisedNs){
      if(loweredFrom){
       if(now-tuneLast>=std::chrono::seconds(3)){uint64_t miss=metrics.deadline_misses-missBase,ticks=stats.ticks-tickBase;
        if(ticks>0&&miss*100>ticks){delayNs=loweredFrom;tuneLatched=true;diagnostic("low_lower_undo",miss,ticks,delayNs,0);}
        else{missBase=metrics.deadline_misses;tickBase=stats.ticks;tuneLast=now;
         // 2.0.2: a lowered level that held 30 s without an undo is safe; let the next p95 check lower it again (11-11
         // 60 min soak: first step-down at 8 s used warm-up costs, 21.0 -> 19.4 ms, then stayed 19.4 ms for the hour).
         if(now-tuneLowered>=std::chrono::seconds(30))loweredFrom=0;}}
      }else if(now-tuneStart>=std::chrono::seconds(8)&&now-tuneLast>=std::chrono::seconds(2)&&tuneCount>=tune.size()){
       tuneLast=now;std::array<double,240> q=tune;std::nth_element(q.begin(),q.begin()+228,q.end());double p95=q[228];
       uint64_t need=(uint64_t)(500000000.0L*td/tn+(p95*1.3+4.0)*1000000.0L),target=std::max(need,lowBaseNs);
       if(target+1000000<delayNs){loweredFrom=delayNs;delayNs=target;tuneLowered=now;missBase=metrics.deadline_misses;tickBase=stats.ticks;diagnostic("low_lower",loweredFrom,delayNs,(uint64_t)(p95*1000),0);}
      }
     }
    }
#ifndef CS_SCHEDULE_TEST
    // Sustained only (about 2 s of jobs): the first jobs after a reload are slow and dropped a
    // 60->120 stream that runs FHD flow at ~9.6 ms to half on its first second.
    if(flowFloor&&!code&&recentCount>=5){if(gens*robust()>.8*source){if(++overload>=(int)(120*std::max(1.0,gens))){flowFloor=0;overload=0;cs_set_flow_floor(engine,0);recentCount=0;estimate=0;std::lock_guard<std::mutex> l(mutex);diagnostic("floor_drop",(uint64_t)(ms*1000),(uint64_t)(gens*1000),(uint64_t)(source*1000),0);}}else overload=0;}
    // 1.9.5: the same sustained rule in low latency moves a slow GPU from half- to quarter-size flow (FHD and up).
    // 2.2.2: full size for 30 fps that does not fit two source periods goes back to half size (not to quarter).
    else if(full30&&!code&&recentCount>=5){if(robust()>.8*2*source){if(++overload>=60){full30=false;full30Off=true;overload=0;cs_set_flow_floor(engine,0);recentCount=0;estimate=0;std::lock_guard<std::mutex> l(mutex);diagnostic("full30_drop",(uint64_t)(ms*1000),0,(uint64_t)(source*1000),0);}}else overload=0;}
    else if(!flowFloor&&!slowFlow&&rifeFamily&&(int64_t)w*h>=1920LL*1080&&!code&&recentCount>=5){if(gens*robust()>.8*source){if(++overload>=(int)(120*std::max(1.0,gens))){slowFlow=1;overload=0;cs_set_flow_floor(engine,2);hudRadius=4;recentCount=0;estimate=0;std::lock_guard<std::mutex> l(mutex);diagnostic("slow_flow_live",(uint64_t)(ms*1000),(uint64_t)(gens*1000),(uint64_t)(source*1000),0);}}else overload=0;}
#endif
    if(midpointModel){cachedA=code?0:job.a.id;cachedB=code?0:job.b.id;}
    {std::lock_guard<std::mutex> l(mutex);stats.last_inference_ms=ms;
     // -4: the engine has no such phase (FSR 3 is midpoint-only; uneven source spacing after preview
     // catch-up drops asks for 1/3, 2/3 ...). Not a fault: the slot keeps the held original.
     if(code==-4){++metrics.expired_jobs;diagnostic("unsupported",job.n,job.a.id,0,(uint64_t)(job.phase*1000));continue;}
     if(code){++stats.failures;diagnostic("fail",job.n,job.a.id,(uint64_t)(int64_t)code,(uint64_t)(job.phase*1000));continue;}++metrics.completed_jobs;
     if(job.epoch!=epoch||(!haveBase)||time(job.n)<=lastContent||job.due<=currentTime()){++metrics.discarded_results;diagnostic("discard",job.n,job.a.id,job.epoch!=epoch?1:!haveBase?2:time(job.n)<=lastContent?3:4,(uint64_t)(ms*1000));continue;}
     auto& r=results[job.n%results.size()];r.pixels.swap(output);r.n=job.n;r.epoch=epoch;r.valid=true;r.a=job.a;r.b=job.b;r.phase=job.phase;
     // Reuse an idle result allocation before growing storage for a new ring slot.
     if(output.empty())for(auto& spare:results)if(!spare.valid&&!spare.pixels.empty()){output.swap(spare.pixels);break;}
     if(output.empty())output.resize(bytes);
     diagnostic("done",job.n,job.a.id,waitMs>=0?(uint64_t)(waitMs*1000):UINT64_MAX,(uint64_t)(ms*1000));
    }
    resultReady.notify_all();
   }
  }catch(...){std::lock_guard<std::mutex> l(mutex);stats.ready=-1;++stats.failures;}
  cs_destroy(engine);
 }
 int render(const uint8_t* input,uint8_t* output,uint64_t stamp){
  // One producer only. All settings/history are protected to make worker reads safe.
  std::unique_lock<std::mutex> l(mutex);++stats.ticks;
  wallStamp=std::chrono::steady_clock::now();
  if(!on||stats.ready!=1){l.unlock();par.copy(output,input,bytes);return 0;}
  if(haveBase&&stamp>=last&&stamp-last<=1&&lastOriginal){proofValid=false;Data held=lastOriginal;l.unlock();par.copy(output,lastGenerated?selected.data():held->data(),bytes);return 0;}
  if(haveBase&&(stamp<last||stamp-last>1000000000)){reset();++metrics.timestamp_resets;}
  if(!haveBase){base=stamp;haveBase=true;}last=stamp;now=history.empty()?stamp:sourceClock+(stamp-sourceArrival);
  // Release obsolete source references before acquiring a pool slot.
  // Reclaim by displayed content, not wall time: source-clock alignment can
  // differ from arrival time. Mixing the clocks prematurely erased ready frames.
  for(auto& r:results)if(r.valid&&time(r.n)<=lastContent){r.valid=false;r.a={};r.b={};}
  Data previous=history.empty()?Data{}:history.back().pixels;
  // Full comparison prevents hash collisions and never drops a changed source.
  l.unlock();bool changed=!previous;
  if(previous){for(size_t i=0;i<bytes;i+=4096)if((*previous)[i]!=input[i]){changed=true;break;}
   if(!changed)changed=!par.equal(previous->data(),input,bytes);}
  l.lock();
  if(changed){
   Data pixels;for(auto& p:pool)if(p&&p.use_count()==1){pixels=p;break;}
   if(!pixels)for(auto& p:pool)if(!p){p=std::make_shared<std::vector<uint8_t>>(bytes);pixels=p;break;}
   if(!pixels){++stats.failures;{size_t held=0;for(auto& p:pool)held+=p&&p.use_count()>1;diagnostic("poolfull",history.size(),jobs.size(),held,0);}l.unlock();par.copy(output,input,bytes);return 0;}
   // The first callback can land anywhere inside a held source frame. Anchor
   // the source clock on its first observed transition, not that arbitrary
   // startup phase; otherwise half-period rounding produces alternating short
   // pairs and too-early display deadlines after some restarts.
   if(previous&&!sourceAnchored){
    uint64_t period=(uint64_t)(1000000000.0L*sd/sn);
    base=stamp>=period?stamp-period:0;history.back().ns=base;
    lastContent=base;lastOutput=UINT64_MAX;sourceAnchored=true;driftFrames=0;lastIndex=UINT64_MAX;
   }
   // Snap observed arrivals to the authoritative input cadence, not to the
   // output callback count. This preserves original positions at integer ratios
   // despite small download/callback jitter. Every changed image is still kept.
   long double frames=(long double)(stamp-base)*sn/(1000000000.0L*sd)-driftFrames;
   uint64_t index=frames>0?(uint64_t)(frames+.5L):0;
   // About 8 s at 60 fps: slow against arrival jitter, far faster than clock drift. A stall stays a skipped index.
   // Rounding each arrival alone is bistable: after a host phase jump arrivals can sit on the half-period boundary,
   // flip between neighbours and cancel their own correction for minutes (trace: dt 6.7/26.7/16.7 ms, generation 87.5->62.5%).
   // So the next index is kept unless the arrival is clearly another slot, and a large offset is pulled in about ten times faster.
   if(!noDrift&&sourceAnchored){
    if(lastIndex!=UINT64_MAX&&std::abs(frames-(long double)(lastIndex+1))<.6L)index=lastIndex+1;
    long double residual=frames-(long double)index;driftFrames+=residual*(std::abs(residual)>.25L?.02L:.002L);lastIndex=index;
   }
   // Timestamps stay on the exact grid so originals land on output slots; putting the offset in them made every slot a generated frame (99.9%).
   // The grid therefore walks away from wall time by the tracked offset (about 60 ms in 30 min). Selection only compares inside this clock,
   // so the fallback must not bring wall time in: it would jump by the whole offset.
   uint64_t sourceTime=base+(uint64_t)((long double)index*1000000000.0L*sd/sn);
   // 1.0.1: the 30 fps detection also runs with 30 fps smoothing off (thirty), so the reversed-pass averaging can use it.
   if(sourceAnchored){
    if(prevSourceIndex!=UINT64_MAX&&index>prevSourceIndex){gapBits=(gapBits<<1)|(index-prevSourceIndex==2);if(gapCount<16)++gapCount;
     int twos=__builtin_popcount(gapBits&0xFFFFu);bool was=cadence30;diagnostic("cadence_gap",index-prevSourceIndex,(uint64_t)twos,(uint64_t)gapCount,cadence30);
     if(!thirty&&gapCount>=16&&twos>=14)thirty=true;else if(thirty&&twos<=8)thirty=false;
     cadence30=cadenceTest?thirty:false;
     if(cadence30!=was){extraNs=cadence30&&cadenceTest==2?(cadenceExtraMs>=0?(uint64_t)(cadenceExtraMs*1000000.0):(uint64_t)(1000000000.0L*td/tn)):0;diagnostic("cadence30",cadence30,(uint64_t)twos,extraNs,0);}}
    prevSourceIndex=index;
   }
   if(!history.empty()&&sourceTime<=history.back().ns)sourceTime=noDrift?std::max(stamp,history.back().ns+1):history.back().ns+1;
   l.unlock();par.copy(pixels->data(),input,bytes);
   if(staticKeep>=4&&channels==4){auto h0=std::chrono::steady_clock::now();hudUpdate(pixels);profHudMs+=(uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-h0).count();++profHudN;}
   // Cut detection: about as many samples as FHD bgr24 at every size (stride 97 read most of a
   // 33 MB UHD frame twice on the video thread). The stride stays odd so it cycles through channels.
   const size_t stride=(97*std::max<size_t>(1,bytes/(1920u*1080u*3u)))|1;
   double diff=0;size_t samples=0;if(previous)for(size_t i=0;i<bytes;i+=stride){if(channels==4&&(i&3)==3)continue; // constant alpha is not image content
    diff+=std::abs((int)(*previous)[i]-input[i]);++samples;}
   l.lock();sourceArrival=stamp;sourceClock=sourceTime;now=sourceTime;Source b{pixels,sourceTime,++sourceId};++metrics.accepted_sources;diagnostic("source",sourceTime,index,metrics.accepted_sources,0);
   if(previous){
    Source a=history.back();uint64_t period=(uint64_t)(1000000000.0L*sd/sn);
    // A held static image remains valid up to one source period before a change.
    if(b.ns-a.ns>(cadence30?period*5/2:period*3/2)){diagnostic("clip",b.ns-a.ns,index,0,0);a.ns=b.ns>period?b.ns-period:a.ns;}
    bool cut=samples&&diff/samples>55;if(cut)++stats.cuts;
    while(!jobs.empty()&&jobs.front().due<=now){diagnostic("qexpire",jobs.front().n,jobs.front().a.id,0,0);jobs.pop_front();++metrics.expired_jobs;}
    if(!cut&&b.ns>a.ns){
     uint64_t first=grid(a.ns)+1,end=grid(b.ns);
     std::vector<Job> pairJobs;
     for(uint64_t n=first;n<=end&&n<first+12;n++){
      uint64_t at=time(n);if(at<=a.ns+100||at+100>=b.ns)continue;
      // A phase remains useful until the next original is displayed. Missing
      // its ideal callback does not make it older than the held original.
      // render rounds to the nearest output slot: the next original becomes
      // selectable half an output period before its nominal timestamp.
      uint64_t halfTick=(uint64_t)(500000000.0L*td/tn);
      pairJobs.push_back(Job{a,b,epoch,n,b.ns+delayNs+extraNs-halfTick,(float)((double)(at-a.ns)/(b.ns-a.ns))});
     }
     // 1.9.6 (11-10 office RX 6600, user: "30fps 보정 켜면 너무 심하게 깨짐"): a 30 fps game at 2x asks for three phases
     // (.25/.5/.75) per 33 ms pair. RIFE (quarter flow, ~10-12 ms each) cannot make three, and the .25 phase has only about
     // 8 ms; late phases were shown in later slots, so motion went back and forth. FSR 3 is midpoint-only: .25/.75 always
     // failed and counted as repeats. When the phases do not fit in 80% of the pair (or the model has only the midpoint),
     // keep only the phase nearest .5: even 30 -> 60 motion (A A M M B), the other slots hold on purpose, not counted as repeats.
     // CS_TEST_CADENCE_FULL=1 keeps every phase (A/B).
     // 2.0.6 (11-11 PS5 Bloodborne, RIFE 2x low latency, 5080): every phase of a pair shares one deadline (next original shown,
     // about delay + extra - half tick after b arrives = 21-24 ms), but three RIFE phases took 20-23 ms on the worker, so the
     // .75 (sometimes .25) phase expired: 73.1% generated and uneven motion (A A' M M'). The 80%-of-pair test did not see
     // the deadline. Keep every phase only when they fit before it (job time x1.15 + 3 ms start, x1.3 to come back), else .5.
     // 2.1.5 (11-11 user: "the multiplier multiplies the game's real frames"): a 30 fps game at 2x is 60 new pictures, not
     // 120. Keep only the phases at j/R of the pair (R = output/signal rate, 2x -> .5, 3x -> 1/3 and 2/3, 4x -> .25/.5/.75), so
     // each picture holds the same number of slots (even 16.7 ms at 2x instead of the 8.3/25 ms of a clipped pair).
     // CS_TEST_CADENCE_ALL=1 keeps every slot (2.0.6-2.1.4 behaviour, 30 -> 120 at 2x).
     if(cadence30&&pairJobs.size()>1&&!cadenceFull&&!cadenceAll){const double R=((double)tn*sd)/((double)td*sn);std::vector<Job> keepJ;
      for(int j=1;j<64&&j/R<.999;j++){const double want=j/R;size_t best=0;for(size_t k=1;k<pairJobs.size();k++)if(std::abs(pairJobs[k].phase-want)<std::abs(pairJobs[best].phase-want))best=k;
       if(keepJ.empty()||keepJ.back().n!=pairJobs[best].n)keepJ.push_back(pairJobs[best]);}
      if(!keepJ.empty())pairJobs.swap(keepJ);}
     double jc=jobCostMs.load(),costMs=jc>0?jc:std::max(calibrationMs,0.0),windowMs=(double)(delayNs+extraNs)/1e6-500.0*td/tn;
     // 2.1.4 (11-11 GTA5 test copy, 5080 low latency): the start-up calibration (9.0 ms, warm-up) put the pair in lite mode
     // through the 80%-of-pair test, which never looked at the measured time again, and the way back (x1.3) needed
     // 3 x 7 x 1.3 + 3 = 30.3 ms against a 29.2 ms window although every phase took about 7 ms: 30 fps smoothing stayed at
     // 30 -> 60 for minutes in one run and reached 120 in the next. Both tests now use the measured job time, leaving lite
     // needs x1.2, and a change of mode holds at least 10 s (a flip every few pairs would itself look uneven).
     const double nPh=(double)pairJobs.size(),pairMs=(double)(b.ns-a.ns)/1000000.0;
     bool wantLite=cadenceLite?(nPh*costMs*1.2+3.0>windowMs||nPh*costMs>.8*pairMs):(nPh*costMs*1.15+3.0>windowMs||nPh*costMs>.85*pairMs);
     {auto nowL=std::chrono::steady_clock::now();if(wantLite!=cadenceLite&&nowL-cadenceModeAt<std::chrono::seconds(10)&&cadenceModeAt.time_since_epoch().count())wantLite=cadenceLite;}
     bool tooLate=cadence30&&pairJobs.size()>1&&wantLite;
     if(cadence30&&pairJobs.size()>1&&!cadenceFull&&(midpointOnly||tooLate)){
      size_t best=0;for(size_t k=1;k<pairJobs.size();k++)if(std::abs(pairJobs[k].phase-.5f)<std::abs(pairJobs[best].phase-.5f))best=k;
      Job keep=pairJobs[best];pairJobs.clear();pairJobs.push_back(keep);
      if(!cadenceLite){cadenceLite=true;cadenceModeAt=std::chrono::steady_clock::now();diagnostic("cadence_lite",1,(uint64_t)(costMs*1000),(uint64_t)(windowMs*1000),0);}
     }else if(cadence30&&pairJobs.size()>1&&cadenceLite){cadenceLite=false;cadenceModeAt=std::chrono::steady_clock::now();diagnostic("cadence_lite",0,(uint64_t)(costMs*1000),(uint64_t)(windowMs*1000),0);}
     for(const auto& j:pairJobs)requested[j.n%requested.size()]=j.n;
     // Process content in temporal order. A new pair must not repeatedly
     // preempt a still-useful phase of the preceding pair. Skipping the early
     // phases up front limited 4x to two phases even on an idle fast GPU.
     if(!pairJobs.empty()){
      for(const auto& j:pairJobs)jobs.push_back(j);
      while(jobs.size()>12){diagnostic("overwrite",jobs.back().n,jobs.back().a.id,0,0);jobs.pop_back();++stats.overwritten;}
     }
    }
   }
   history.push_back(b);size_t historyLimit=std::max<size_t>(12,(size_t)((long double)(delayNs+extraNs)*sn/(1000000000.0L*sd))+3);
   while(history.size()>historyLimit)history.pop_front();event.notify_all();
  }else ++stats.static_pairs;
  // Follow the observed source transition when mapping wall time to content
  // time. Download jitter must not shift all output slots by half a frame for
  // an entire run. This neither waits nor adds a queued frame.
  uint64_t displayStamp=sourceClock+(stamp-sourceArrival);
  // Capture/host clock drift makes the host deliver callbacks in pairs for minutes at a time
  // (~1 ms apart, then ~15 ms). On the raw wall clock both map to the same output slot, so one
  // slot repeated and the next was skipped (live trace: ~38% repeats). Advance the display clock
  // by the average callback interval instead, kept within 3/4 of an interval of the wall clock.
  if(!noSmooth){
   uint64_t gap=stamp-lastTick;bool fresh=lastTick&&lastDisplay&&stamp>lastTick&&(!tickInterval||gap<tickInterval*4);
   if(fresh){tickInterval=tickInterval?(uint64_t)(tickInterval*.9375+gap*.0625):gap;
    uint64_t v=lastDisplay+tickInterval,slack=tickInterval*3/4;
    if(v+slack<displayStamp)v=displayStamp-slack;else if(v>displayStamp+slack)v=displayStamp+slack;
    // 1.9.4 (11-10, RX 6600): the clamp alone kept any lasting offset (startup anchor, capture/PC clock drift) pinned at
    // +-3/4 interval forever. Ahead by 6.9 ms, every job lost that much of its deadline: FSR 3 at 15 ms buffer generated
    // 0/60 (engine bench), 60/60 with smoothing off. Pull 1/16 per tick toward the wall-mapped clock: pair jitter
    // (~1/15 ms) averages out as before, a lasting offset decays in about 0.3 s at 120 Hz.
    if(!noPull){int64_t off=(int64_t)(displayStamp-v);v+=off/16;}
    displayStamp=v;}
   else if(lastTick&&gap>=tickInterval*4)tickInterval=0;
   lastTick=stamp;lastDisplay=displayStamp;
  }
  now=displayStamp;
  Data chosen=history.front().pixels;bool generated=false,intermediate=false;
  uint64_t desired=displayStamp>=base+delayNs+extraNs?displayStamp-delayNs-extraNs:base;
  uint64_t n=grid(desired+(uint64_t)(500000000.0L*td/tn));
  if(!slotRound){long double f=(long double)(desired-base+2)*tn/(1000000000.0L*td);
   if(lastSlot!=UINT64_MAX&&std::abs(f-(long double)(lastSlot+1))<.6L)n=lastSlot+1;}
  lastSlot=n;
  uint64_t at=time(n),sourceNs=history.front().ns;
  diagnostic("tick",n,at,metrics.accepted_sources,n==lastOutput);
  // A repeated slot can now have a newly completed result. Re-evaluate it;
  // lastContent prevents rewinds and counting the same generated image twice.
  for(const auto& s:history)if(s.ns<=at+100){chosen=s.pixels;sourceNs=s.ns;}
  // Select the newest completed phase after the displayed content and current
  // original, even if the output callback skipped its exact slot. Never rewind,
  // never show a future phase, and never postpone the next original.
  Result* result=nullptr;uint64_t contentNs=std::max(sourceNs,lastContent);
  for(auto& r:results)if(r.valid&&r.epoch==epoch&&time(r.n)<=at&&time(r.n)>contentNs){result=&r;contentNs=time(r.n);}
  // 1.8.2: the wait shrinks by how long the buffer has already been held, and is skipped once it is half a tick late.
  // 11-09 user log (1.7.9, 60->120, 15 ms buffer): during a capture/host clock-drift phase nearly every in-between tick
  // waited 5-7.5 ms on the shared streaming thread; with the copies that exceeded the 8.33 ms tick, the backlog grew and
  // the host's catch-up dropped frames (preview 113-116 fps, FG_CPU p95 9-9.75 ms). A late buffer now shows the held
  // original at once so the thread catches up; an on-time buffer waits as before (arrival includes ~1.5-2 ms download).
  uint64_t lateNs=0;
#if defined(_WIN32)&&!defined(CS_SCHEDULE_TEST)
  {LARGE_INTEGER q,f;QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);uint64_t nowNs=(uint64_t)((long double)q.QuadPart*1000000000.0L/f.QuadPart);lateNs=nowNs>stamp?nowNs-stamp:0;}
#endif
  const uint64_t tickNs=(uint64_t)(1000000000.0L*td/tn),slackNs=2000000;
  uint64_t allowNs=lateNs>=tickNs/2?0:waitNs>(lateNs>slackNs?lateNs-slackNs:0)?waitNs-(lateNs>slackNs?lateNs-slackNs:0):0;
  if(!result&&waitNs&&!allowNs&&at>sourceNs+100&&requested[n%requested.size()]==n){++waitSkips;diagnostic("waitskip",n,lateNs,contentNs,0);}
  if(!result&&allowNs&&at>sourceNs+100&&requested[n%requested.size()]==n){
   auto until=std::chrono::steady_clock::now()+std::chrono::nanoseconds(allowNs);uint64_t waitEpoch=epoch;
   diagnostic("wait",n,sourceNs,contentNs,lateNs);
   while(!result&&!stop&&epoch==waitEpoch&&resultReady.wait_until(l,until)!=std::cv_status::timeout)
    for(auto& r:results)if(r.valid&&r.epoch==epoch&&time(r.n)<=at&&time(r.n)>contentNs){result=&r;contentNs=time(r.n);}
   if(!result&&epoch==waitEpoch)for(auto& r:results)if(r.valid&&r.epoch==epoch&&time(r.n)<=at&&time(r.n)>contentNs){result=&r;contentNs=time(r.n);}
   if(epoch!=waitEpoch){l.unlock();par.copy(output,input,bytes);return 0;}
  }
  proofValid=false;
  if(result){selected.swap(result->pixels);result->valid=false;generated=true;proofA=std::move(result->a);proofB=std::move(result->b);proofPhase=result->phase;proofValid=true;}
  bool hold=!generated&&sourceNs<lastContent&&lastGenerated;
  intermediate=at>sourceNs+100&&stamp>=base+delayNs;
  if(generated)++stats.generated;else if(intermediate&&requested[n%requested.size()]==n){++stats.repeated;++metrics.deadline_misses;}
  lastOutput=n;lastOriginal=chosen;lastGenerated=generated||hold;lastContent=contentNs;diagnostic("select",n,sourceNs,contentNs,generated);metrics.last_source_age_ns=displayStamp>=sourceNs?displayStamp-sourceNs:0;
  metrics.max_source_age_ns=std::max(metrics.max_source_age_ns,metrics.last_source_age_ns);
  l.unlock();par.copy(output,(generated||hold)?selected.data():chosen->data(),bytes);return 0;
 }
};
}
void* cs_live_create_format(const wchar_t* dir,int gpu,int w,int h,int workers,int ch){if(!dir||!cs_native_frame_valid(w,h)||workers<1||workers>4||(ch!=3&&ch!=4))return nullptr;try{return new Live(dir,gpu,w,h,workers,ch);}catch(...){return nullptr;}}
void* cs_live_create_workers(const wchar_t* dir,int gpu,int w,int h,int workers){return cs_live_create_format(dir,gpu,w,h,workers,3);}
void* cs_live_create(const wchar_t* dir,int gpu,int w,int h){return cs_live_create_workers(dir,gpu,w,h,1);}
int cs_live_tick(void* p,const uint8_t* in,uint8_t* out,uint64_t ns){if(!p||!in||!out)return -1;try{return static_cast<Live*>(p)->render(in,out,ns);}catch(...){return -2;}}
int cs_live_configure(void* p,int on,int sn,int sd,int tn,int td,int mode){CsRatePlan plan{};if(!p||(mode!=0&&mode!=1)||!cs_rate_fraction(sn,sd,tn,td,mode,&plan))return -1;static_cast<Live*>(p)->configure(on,sn,sd,tn,td,mode,plan);return 0;}
int cs_live_stats(void* p,CsStreamStats* out){if(!p||!out)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);*out=s->stats;return 0;}
int cs_live_metrics(void* p,CsLiveMetrics* out){if(!p||!out)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);*out=s->metrics;return 0;}
uint64_t cs_live_deadlines(void* p){CsLiveMetrics m{};cs_live_metrics(p,&m);return m.deadline_misses;}
void cs_live_destroy(void* p){delete static_cast<Live*>(p);}
int cs_live_copy_proof(void* p,uint8_t* a,uint8_t* b,uint8_t* out,float* phase){
 if(!p||!a||!b||!out||!phase)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);
 if(!s->proofValid)return 1;memcpy(a,s->proofA.pixels->data(),s->bytes);memcpy(b,s->proofB.pixels->data(),s->bytes);memcpy(out,s->selected.data(),s->bytes);*phase=s->proofPhase;return 0;
}


extern "C" CS_API const char* cs_engine_version(void){return "1.0.1";}
// 1.7.8 diagnostics: averages over the last 240 measured inferences. busy_pct: inference time over the wall time
// those inferences spanned (low with low generation = the worker waited for jobs, not for the GPU).
extern "C" CS_API int cs_live_stage_stats(void* p,CsStageStats* out){if(!p||!out)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);
 *out=CsStageStats{};size_t n=(size_t)std::min<uint64_t>(s->stageCount,s->stageRing.size());out->samples=s->stageCount;if(!n)return 1;
 uint64_t first=UINT64_MAX,last=0;double sum[4]={0,0,0,0};
 for(size_t i=0;i<n;i++){const auto& q=s->stageRing[i];for(int k=0;k<4;k++)sum[k]+=q.v[k];out->gpu_max_ms=std::max(out->gpu_max_ms,q.v[1]);out->total_max_ms=std::max(out->total_max_ms,q.v[3]);first=std::min(first,q.start);last=std::max(last,q.start);}
 out->in_ms=sum[0]/n;out->gpu_ms=sum[1]/n;out->out_ms=sum[2]/n;out->total_ms=sum[3]/n;
#ifdef CS_GPU_KEEPER
 out->keep_ms=s->keeper?cs_keeper_dispatch_ms(s->keeper):-1;out->keep_dispatches=cs_keeper_dispatches(s->keeper);
#else
 out->keep_ms=-1;
#endif
 // The span runs from the first start to the last start plus its own duration.
 double span=(last-first)/1e6+out->total_ms;out->busy_pct=n>1&&span>0?std::min(100.0,100.0*sum[3]/span):0;return 0;}
extern "C" CS_API const char* cs_live_gpu_info(void* p){if(!p)return "";auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);return s->gpuInfo.c_str();}
// Settings window switches (1.6.9): 30 fps cadence (mode 2) and HUD keep (mode 3). Overrides the test variables.
// 2.0.2: latency priority switch. Takes effect on the next job (re-runs the worker's flow choice).
extern "C" CS_API int cs_live_set_quarter(void* p,int on){if(!p)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);
 if(s->forceQuarter!=(on?1:0)){s->forceQuarter=on?1:0;++s->quarterSerial;++s->configured;}return 0;}
extern "C" CS_API int cs_live_set_options(void* p,int cadence30,int hudKeep){if(!p)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);
 s->cadenceTest=cadence30?2:0;if(!cadence30){s->cadence30=false;s->extraNs=0;s->gapBits=0;s->gapCount=0;}s->staticKeep=hudKeep?4:0;s->optionsSet=true;return 0;}
uint64_t cs_live_buffer_ns(void* p){if(!p)return 0;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);return s->on?s->delayNs+s->extraNs:0;}

int cs_live_set_low_buffer_ms(void* p,int label){if(!p||(label!=25&&!(cs_low_reserve_ms(label)>0)))return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);if(s->stable)return -1;uint64_t before=s->delayNs;s->lowLabel=label==25?0:label;s->delayNs=s->planDelayNs;s->retune();if(s->delayNs!=before)s->reset();return 0;}
int cs_live_set_buffer_ms(void* p,int ms){if(!p||ms<25||ms>200)return -1;auto* s=static_cast<Live*>(p);std::lock_guard<std::mutex> l(s->mutex);s->reset();s->delayNs=(uint64_t)ms*1000000;return 0;}
