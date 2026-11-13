/* SPDX-License-Identifier: MIT
 * Version-pinned x64 adapter, reconstructed from the installed avtsdk.dll.
 * No vendor implementation is copied. Bound only after the installed avtsdk.dll hash matches.
 * This is NOT a public, stable SDK header. Never call with another DLL build.
 */
#include <bcrypt.h>
typedef struct {void *impl;} DeviceController;
typedef struct {void *impl;} DeviceList;
typedef struct {uint32_t type,padding;void *name,*id,*aux,*path;uint32_t bus;uint8_t flag,pad[3];DeviceList caps;} DeviceSource;
_Static_assert(sizeof(DeviceSource)==0x38,"SDK source size");
/* DirectShow and KS expose the same instance through two video interfaces.
 * Keep the entire instance AND pin suffix; never match just VID/PID or name. */
static int device_same_path(const char *a,const char *b){
 if(!a||!b||!*a||!*b)return 0;if(!_stricmp(a,b))return 1;
 const char *x=strstr(a,"#{"),*y=strstr(b,"#{");if(!x||!y||x-a!=y-b||_strnicmp(a,b,(size_t)(x-a)))return 0;
 const char *capture="#{65e8773d-8f56-11d0-a3b9-00a0c9223196}",*video="#{6994ad05-93ef-11d0-a3cc-00a0c9223196}";
 if(strlen(x)<39||strlen(y)<39)return 0;
 if((_strnicmp(x,capture,39)&&_strnicmp(x,video,39))||(_strnicmp(y,capture,39)&&_strnicmp(y,video,39)))return 0;
 return !_stricmp(x+39,y+39);
}
typedef struct {
 uint32_t status;
 uint8_t locked,pad0[3];
 uint32_t width,height;
 uint8_t progressive,pad1[3];
 uint32_t fps_raw;
 uint8_t opaque[0x4c-0x18];
} DeviceState;
_Static_assert(sizeof(DeviceState)==0x4c,"SDK state size");
_Static_assert(offsetof(DeviceState,fps_raw)==0x14,"SDK FPS offset");
typedef struct {
 void *(*ctor)(DeviceController*,const DeviceSource*);
 void (*dtor)(DeviceController*);
 int (*open)(DeviceController*);
 int (*state)(DeviceController*,DeviceState*);
 void *(*list_ctor)(DeviceList*);
 void (*list_dtor)(DeviceList*),(*list_get)(DeviceList*);
 size_t (*list_size)(const DeviceList*);
 DeviceSource *(*list_at)(DeviceList*,size_t);
 const char *(*string_get)(void*);
} DeviceSdk;
static int device_sdk_hash(const wchar_t *path){
 /* [0] Streaming Center 1.8.22.2. [1] 1.9.19.1 (1.0.1, 11-12): the bound exports are byte-identical to [0] apart from
  * call/RIP displacements (same lengths: getDeviceState 1184 B, open 1126 B; controller is one pointer to a 0x1c8 impl;
  * list items 0x38), checked by comparing the two DLLs. The state sample keeps its canaries either way. */
 static const unsigned char known[2][32]={{0xfc,0xe2,0x95,0x3c,0x0e,0xf9,0xe1,0xd3,0x5f,0x18,0x04,0x48,0xd7,0x12,0xc3,0xd8,0x3e,0xba,0xa0,0x1c,0x40,0x48,0x50,0xea,0x65,0xe6,0x19,0x30,0x71,0x03,0x8d,0xd0},
  {0xe6,0x6f,0xdd,0x4d,0x4c,0xb5,0x4f,0xfe,0x1d,0x03,0xbb,0xea,0x39,0x07,0x6d,0x2d,0x4a,0xb1,0xee,0x4a,0xfb,0xf1,0x8f,0x79,0x1d,0x41,0x23,0x6e,0x80,0x98,0x92,0x55}};
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);
 if(f==INVALID_HANDLE_VALUE)return 0;
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char data[16384],digest[32];DWORD n;int ok=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto done;
 for(;;){if(!ReadFile(f,data,sizeof(data),&n,NULL))goto done;if(!n)break;if(BCryptHashData(hash,data,n,0)<0)goto done;}
 if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0)goto done;
 ok=!memcmp(digest,known[0],sizeof(digest))||!memcmp(digest,known[1],sizeof(digest));
done:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);return ok;
}
static int device_sdk_bind(HMODULE module,DeviceSdk *api){
 wchar_t path[32768];DWORD n=GetModuleFileNameW(module,path,32768);
 if(!n||n>=32768||!device_sdk_hash(path))return 0;
 *(FARPROC*)&api->ctor=GetProcAddress(module,"??0AvtAVTDeviceController@Controller@Device@AVTSDK@@QEAA@AEBVAvtDevice@23@@Z");
 *(FARPROC*)&api->dtor=GetProcAddress(module,"??1AvtAVTDeviceController@Controller@Device@AVTSDK@@QEAA@XZ");
 *(FARPROC*)&api->open=GetProcAddress(module,"?open@AvtAVTDeviceController@Controller@Device@AVTSDK@@QEAA?AW4AvtResult@4@XZ");
 *(FARPROC*)&api->state=GetProcAddress(module,"?getDeviceState@AvtAVTDeviceController@Controller@Device@AVTSDK@@QEAA?AW4AvtResult@4@AEAVAvtDeviceStateInfo@234@@Z");
 *(FARPROC*)&api->list_ctor=GetProcAddress(module,"??0?$AvtList@VAvtVideoSourceDevice@Device@AVTSDK@@@AVTSDK@@QEAA@XZ");
 *(FARPROC*)&api->list_dtor=GetProcAddress(module,"??1?$AvtList@VAvtVideoSourceDevice@Device@AVTSDK@@@AVTSDK@@QEAA@XZ");
 *(FARPROC*)&api->list_get=GetProcAddress(module,"?getVideoSourceDeviceList@AvtDeviceManager@Manager@Device@AVTSDK@@SAXAEAV?$AvtList@VAvtVideoSourceDevice@Device@AVTSDK@@@4@@Z");
 *(FARPROC*)&api->list_size=GetProcAddress(module,"?size@?$AvtList@VAvtVideoSourceDevice@Device@AVTSDK@@@AVTSDK@@QEBA_KXZ");
 *(FARPROC*)&api->list_at=GetProcAddress(module,"?at@?$AvtList@VAvtVideoSourceDevice@Device@AVTSDK@@@AVTSDK@@QEAAAEAVAvtVideoSourceDevice@Device@2@_K@Z");
 *(FARPROC*)&api->string_get=GetProcAddress(module,"?getString@AvtString@AVTSDK@@QEBAPEBDXZ");
 return api->ctor&&api->dtor&&api->open&&api->state&&api->list_ctor&&api->list_dtor&&api->list_get&&api->list_size&&api->list_at&&api->string_get;
}
/* GC573 backend 0xbbdc0 returns INTEGER fps, confirmed with live 2160p60.
 * Other backends return hundredths at this same offset! They remain disabled.
 * Never infer 59.94 from a truncated 59; an exact source API is needed for that.
 */
static int device_rate(const DeviceState *s,int result,int *n,int *d){
 *n=0;*d=1;if(result||s->locked!=1||s->progressive>1||!s->width||!s->height||s->width>16384||s->height>16384)return 0;
 if(s->fps_raw<1||s->fps_raw>1000)return 0;*n=(int)s->fps_raw;
 /* 2.0.5 (11-11 PS5): the GC573 getter integer-divides the driver timing, so 59.94 Hz (60000/1001) arrives as 59 and
  * 29.97 as 29. HDMI has no 59.00 or 29.00 timing; the PS5 "60 Hz" output was measured at 59.937-59.939 frames/s in
  * the preview while the capture caps said 60/1. Earlier builds left 59 unsupported (no generation). */
 if(s->fps_raw==59||s->fps_raw==29||s->fps_raw==119){*n=(int)(s->fps_raw+1)*1000;*d=1001;} /* 1.0.1: 119.88 likewise */
 return 1;
}
