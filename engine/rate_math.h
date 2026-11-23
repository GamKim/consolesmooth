/* SPDX-License-Identifier: MIT
 * Integer scheduling shared by the engine, host adapter and tests.
 */
#ifndef CS_RATE_MATH_H
#define CS_RATE_MATH_H
#include <stdint.h>
/* 1.0.1 (11-13): 120 fps input (and 119.88) as well; the output cap stays 240, so 120 -> 240 is 2x. */
static inline int cs_rate_valid(int source,int output){return (source==30||source==60||source==120)&&output>source&&output<=source*4&&output<=240;}
static inline int cs_rate_delay(int source,int output,int stable){
 if(!cs_rate_valid(source,output))return 0;
 if(stable)return (2*output+source-1)/source;
 /* At least 12ms for the first intermediate inference; keeps the tested
  * 30->60 = 2 ticks and 60->120 = 3 ticks. No GPU wait on the display thread. */
 /* Source arrivals are rounded up to output ticks. For fractional ratios,
  * the worst phase needs ceil(ratio) ticks before its second source arrives.
  * Reserve complete output ticks for inference after that arrival. */
 int arrival=output%source?(output+source-1)/source:output/source-1;
 return arrival+(12*output+999)/1000;
}
static inline uint64_t cs_rate_source(uint64_t tick,int source,int output){return tick*(uint64_t)source/(unsigned)output;}
typedef struct {int source_step,target_step,delay;} CsRatePlan;
static inline int cs_rate_gcd(int a,int b){while(b){int r=a%b;a=b;b=r;}return a;}
static inline int cs_rate_fraction(int sn,int sd,int tn,int td,int stable,CsRatePlan* plan){
 if(!plan||sd<1||td<1||sd>1001||td>1001||sn<1||tn<1||sn>120000||tn>240000)return 0;
 if(!((sd==1&&(sn==30||sn==60||sn==120))||(sd==1001&&(sn==30000||sn==60000||sn==120000))))return 0;
 int64_t a=(int64_t)sn*td,b=(int64_t)tn*sd;
 if(b<=a||b>4*a||(int64_t)tn>240LL*td)return 0;
 int gcd=cs_rate_gcd((int)a,(int)b);plan->source_step=(int)a/gcd;plan->target_step=(int)b/gcd;
 int arrival=b%a?(int)((b+a-1)/a):(int)(b/a)-1;
 plan->delay=stable?(int)((2*b+a-1)/a):arrival+(12*tn+1000*td-1)/(1000*td);
 return 1;
}
#endif
