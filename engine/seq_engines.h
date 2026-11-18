// SPDX-License-Identifier: MIT
// Sequential (stateful) frame generator: AMD FSR 3 frame interpolation.
// It keeps the previously fed frame, so a call for the pair (a,b) is cheap when a was the
// last frame fed (live path, tagged ids) and otherwise re-feeds a first.
#ifndef CS_SEQ_ENGINES_H
#define CS_SEQ_ENGINES_H
#include <cstdint>
#include <string>
struct SeqEngine{
 virtual ~SeqEngine(){}
 // Returns 0 on success, <0 on error (e.g. unsupported phase); out is w*h pixels of ch bytes (3 = bgr24, 4 = BGRA).
 virtual int process(const uint8_t* a,const uint8_t* b,int w,int h,int ch,float t,uint8_t* out,uint64_t aid,uint64_t bid)=0;
 double wait=-1;
 // 1.7.8: last process stage times in ms: in (CPU convert + record), gpu (submit until fence), out (CPU convert).
 double stage[3]={-1,-1,-1};
};
// The Vulkan device is matched by PCI vendor/device id to the GPU ncnn selected, because raw
// Vulkan enumeration order can differ from ncnn numbering (e.g. iGPU listed first).
SeqEngine* cs_create_fsr3(uint32_t vendorId,uint32_t deviceId,std::string& error);
#ifdef CS_INTERNAL_VFG
// 2.2.8 internal only: NVIDIA VFG (vfg_engine.cpp). Runtime = user download, never shipped.
SeqEngine* cs_create_vfg(std::string& error);
// 2.2.8 internal only: NVIDIA DLSS FG through engines\dlssg\cs_dlssg.dll (dlssg_engine.cpp). Runtime = NVIDIA app cache copy.
#ifdef CS_INTERNAL_DLSSG
SeqEngine* cs_create_dlssg(const std::wstring& modelDir,std::string& error);
#endif
// 2.2.8 internal only: AMD FidelityFX SDK 2.4 frame generation DLL through engines\amdfg\cs_amdfg.dll (amdfg_engine.cpp).
SeqEngine* cs_create_amdfg(const std::wstring& modelDir,std::string& error);
#endif
// Parallel pixel conversions (4-byte BGRA/RGBA <-> bgr24).
void cs_bgr_to_4(const uint8_t* bgr,uint8_t* dst,size_t pixels,bool swapRB);
void cs_4_to_bgr(const uint8_t* src,uint8_t* bgr,size_t pixels,bool swapRB);
// ch-byte input (3 bgr24, 4 BGRA) to 4-byte BGRA (swapRB=false) or RGBA (true), and back.
void cs_to_4(const uint8_t* src,int ch,uint8_t* dst,size_t pixels,bool swapRB);
void cs_from_4(const uint8_t* src,uint8_t* dst,int ch,size_t pixels,bool swapRB);
#endif
