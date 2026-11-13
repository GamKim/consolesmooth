/* SPDX-License-Identifier: MIT. Packed BGR8, native resolution; no resize. */
#ifndef CS_FRAME_LIMITS_H
#define CS_FRAME_LIMITS_H
#include <stdint.h>
static inline int cs_native_frame_valid(int w,int h){return w>=32&&h>=32&&w<=3840&&h<=2160&&w%4==0;}
/* Stable mode alone may reserve extra time for native high-resolution inference.
 * Bound buffering to 200ms. Low latency and FHD retain the prior policy. */
static inline uint64_t cs_native_delay_ns(int w,int h,int sn,int sd,int tn,int td,int stable,uint64_t base,double inference_ms){
 if(!stable||(int64_t)w*h<=1920LL*1080||!(inference_ms>0)||inference_ms>10000)return base;
 double desired=1000.0*sd/sn+inference_ms*1.25+1000.0*td/tn;
 if(desired>200)desired=200;
 uint64_t ns=(uint64_t)(desired*1000000.0);
 return ns>base?ns:base;
}
// Low latency buffer choices, RTX 5080 night sweep 2026-11-05: 60->120 FHD only. Label 25 is the plan buffer.
static inline double cs_low_reserve_ms(int label){return label>=15&&label<25?label-8.0:0;} /* 20/17/15 = reserve 12/9/7 measured; values between stay inside that range. */
static inline int cs_low_buffer_eligible(int w,int h,int sn,int sd,int tn,int td){
 return (int64_t)w*h<=1920LL*1080&&sd>0&&td>0&&sn>=50*sd&&(int64_t)tn*sd==2LL*sn*td;
}
static inline uint64_t cs_low_buffer_ns(int w,int h,int sn,int sd,int tn,int td,int label,uint64_t plan){
 double reserve=cs_low_reserve_ms(label);
 if(!(reserve>0)||!cs_low_buffer_eligible(w,h,sn,sd,tn,td))return plan;
 uint64_t want=(uint64_t)(1000000000.0L*td/tn+reserve*1000000.0L);
 return want<plan?want:plan;
}
#endif
