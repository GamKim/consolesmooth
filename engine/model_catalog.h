// SPDX-License-Identifier: MIT
// Stable persisted IDs. Retired IDs must never be reused: 1 (RIFE 4.25),
// 2 IFRNet, 3 CAIN, 4 IFRNet Large (removed in 1.4.3; too slow for live output),
// 6 NVIDIA FRUC (removed in 1.6.5; NVIDIA binaries are not redistributed).
#ifndef CS_MODEL_CATALOG_H
#define CS_MODEL_CATALOG_H
typedef struct {
 const wchar_t *directory,*label;
 const char *name,*param_sha256,*bin_sha256;
 int padding;
 const wchar_t *basename;
 // 0 RIFE (basename.param/.bin), 3 FSR 3 (basename = notice file, code in the engine DLL), -1 retired
 int family;
} CsModelSpec;
static const CsModelSpec cs_models[]={
 {L"model",L"RIFE 4.6", "RIFE 4.6",
  "28DF14D57A225725EE5386F52EBA422488450D37C9F40800ED4F62E8BA846692",
  "F334ED2260149CE0188A6DCF049844E8B0CDD912E01CBCFB63553157D2508958",32,L"flownet",0},
 {L"",L"","Removed model","","",0,L"",-1}, // ID 1 retired; saved settings fall back to RIFE 4.6
 {L"",L"","Removed model","","",0,L"",-1}, // ID 2 IFRNet retired
 {L"",L"","Removed model","","",0,L"",-1}, // ID 3 CAIN retired
 {L"",L"","Removed model","","",0,L"",-1}, // ID 4 IFRNet Large retired
 {L"engines\\fsr3",L"FSR 3 · 2배·4배","FSR 3",
  "C93D509CD69D50698796C9C3274247352736EF122A3197ACE2010F0978E21AA6","",0,L"LICENSE-FidelityFX.txt",3},
 {L"",L"","Removed model","","",0,L"",-1} // ID 6 NVIDIA FRUC retired; saved settings fall back to RIFE 4.6
};
#define CS_MODEL_COUNT 7
static int cs_model_valid(int id){return id>=0&&id<CS_MODEL_COUNT&&cs_models[id].family>=0;}
// FSR 3 interpolates the midpoint only; 4x halves twice (2.2.2), 3x is refused.
static int cs_model_rate_supported(int id,int sn,int sd,int tn,int td){
 if(!cs_model_valid(id)||sn<=0||sd<=0||tn<=0||td<=0)return 0;
 long long t=(long long)tn*sd,s=(long long)sn*td;
 if(cs_models[id].family==3)return t==2*s||t==4*s; // 2.2.2: 4x = midpoint twice
 return 1;
}
#endif
