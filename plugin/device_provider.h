/* SPDX-License-Identifier: MIT. In-process SDK input-state polling.
 * The video thread never calls the SDK or waits for its driver queries.
 * No SDK init/uninit here: Streaming Center owns the global SDK lifetime.
 */
#include <objbase.h>
#include "device_sdk.h"
typedef struct {void *weak;} DeviceWatch; /* GLib GWeakRef, pointer-sized */
static void (*weak_init)(void*,void*),(*weak_clear)(void*);
static void *(*weak_get)(void*);
static SRWLOCK device_sources_lock=SRWLOCK_INIT;
static DeviceWatch *device_sources[8];
static unsigned device_generation;
static int device_overflow;
static volatile LONG device_thread_started;
static HANDLE device_thread; /* bounded, process-lifetime worker handle */
static void device_invalidate_locked(void){++device_generation;auto_device_lost();}
static void device_watch_gone(void *data){
 DeviceWatch *watch=data;AcquireSRWLockExclusive(&device_sources_lock);
 for(int i=0;i<8;i++)if(device_sources[i]==watch)device_sources[i]=NULL;
 device_overflow=0; /* a slot is free again */
 weak_clear(&watch->weak);device_invalidate_locked();ReleaseSRWLockExclusive(&device_sources_lock);
 HeapFree(GetProcessHeap(),0,watch);
}
/* A strong reference is held only for this poll, never in a pad's user data. */
static void *device_selected(unsigned *generation){
 void *refs[8]={0};const void *ids[8]={0};int count=0;AcquireSRWLockShared(&device_sources_lock);
 for(int i=0;i<8;i++)if(device_sources[i]){void *s=weak_get(&device_sources[i]->weak);if(s){ids[count]=device_sources[i];refs[count++]=s;}}
 *generation=device_generation;int overflow=device_overflow;ReleaseSRWLockShared(&device_sources_lock);
 if(count==1&&!overflow)return refs[0];
 /* 2.3.23: after switching the capture card in Streaming Center the old source can stay alive. Use the one with buffers. */
 int pick=-1,live=0;for(int i=0;i<count;i++)if(auto_capture_active(ids[i])){pick=i;live++;}
 void *chosen=live==1&&!overflow?refs[pick]:NULL;
 for(int i=0;i<count;i++)if(refs[i]!=chosen)object_unref(refs[i]);
 return chosen;
}
static int device_verified_model(const char *path){
 /* Only the GC573 PCIe backend has been exercised on this PC. */
 const char *prefix="\\\\?\\pci#ven_1461&dev_0054&subsys_57301461&";
 return path&&!_strnicmp(path,prefix,strlen(prefix));
}
static int device_open_selected(DeviceSdk *api,const char *path,DeviceController *controller){
 DeviceList list={0};api->list_ctor(&list);api->list_get(&list);
 size_t count=api->list_size(&list);DeviceSource *found=NULL;int matches=0,result=-1001;
 if(count<=128)for(size_t i=0;i<count;i++){DeviceSource *s=api->list_at(&list,i);if(device_same_path(path,api->string_get(&s->path))){found=s;matches++;}}
 if(matches==1){api->ctor(controller,found);result=api->open(controller);}
 api->list_dtor(&list);return result;
}
static DWORD WINAPI device_poll(void *unused){
 (void)unused;HRESULT com=CoInitializeEx(NULL,COINIT_MULTITHREADED);if(FAILED(com)){logline("SDK_COM_FAILED hr=%ld",com);return 0;}
 HMODULE mod=GetModuleHandleW(L"avtsdk.dll"),held=NULL;DeviceSdk api={0};
 if(!mod||!device_sdk_bind(mod,&api)){logline("SDK_DISABLED unverified_build_or_missing_exports=1");CoUninitialize();return 0;}
 /* Keep vendor code mapped while the owned controller is alive. */
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,(LPCWSTR)(uintptr_t)api.state,&held)){CoUninitialize();return 0;}
 DeviceController controller={0};char active[4096]={0};int opened=0,prior=-999;unsigned active_generation=0;ULONGLONG retry=0;
 while(!InterlockedCompareExchange(&auto_stopping,0,0)){
  unsigned generation;void *source=device_selected(&generation);char *path=NULL;
  if(source)property_get(source,"device-path",&path,NULL);
  int eligible=path&&strlen(path)<sizeof(active)&&device_verified_model(path);
  if(!eligible||generation!=active_generation||_stricmp(path?path:"",active)){
   auto_device_lost();if(controller.impl){api.dtor(&controller);controller.impl=NULL;}opened=0;active[0]=0;retry=0;
   active_generation=generation;if(eligible)strcpy(active,path);
  }
  if(eligible&&!opened&&GetTickCount64()>=retry){
   int result=device_open_selected(&api,path,&controller);opened=result==0;retry=GetTickCount64()+2000;
   if(result!=prior){{char shown[96];snprintf(shown,sizeof shown,"%s",path);char *cut=strstr(shown,"&rev_");if(cut)*cut=0;logline("SDK_DEVICE_OPEN result=%d device=%s",result,shown);} /* no instance path: it identifies the PC's card */prior=result;}
   if(!opened&&controller.impl){api.dtor(&controller);controller.impl=NULL;}
  }
  if(opened){
   struct {uint32_t before;DeviceState state;uint32_t after;} sample={0};sample.before=0x13579bdf;sample.after=0x2468ace0;
   int result=api.state(&controller,&sample.state),n,d;
   int intact=sample.before==0x13579bdf&&sample.after==0x2468ace0;
   int valid=device_rate(&sample.state,result,&n,&d);char *after=NULL;property_get(source,"device-path",&after,NULL);
   AcquireSRWLockShared(&device_sources_lock);
   int current=generation==device_generation&&after&&!_stricmp(path,after);
   if(current&&intact&&!result&&(valid||sample.state.locked==0)){DeviceState *s=&sample.state;auto_device_report(valid,(int)s->width,(int)s->height,n,d,s->progressive==1);}
   else auto_device_lost();ReleaseSRWLockShared(&device_sources_lock);mem_free(after);
   if(result!=prior){logline("SDK_DEVICE_STATE result=%d guard=%d",result,intact);prior=result;}
   if(!intact){logline("SDK_DISABLED state_guard_failed=1");mem_free(path);object_unref(source);break;}
   if(result){api.dtor(&controller);controller.impl=NULL;opened=0;retry=GetTickCount64()+2000;}
  }
  mem_free(path);if(source)object_unref(source);Sleep(500);
 }
 auto_device_lost();if(controller.impl)api.dtor(&controller);FreeLibrary(held);CoUninitialize();logline("SDK_POLL_STOP");InterlockedExchange(&device_thread_started,0);return 0;
}
static DeviceWatch *device_watch_add(void *pad){
 void *source=object_parent(pad);if(!source)return NULL;
 DeviceWatch *watch=NULL;void *refs[8]={0};AcquireSRWLockExclusive(&device_sources_lock);int empty=-1,duplicate=0;
 for(int i=0;i<8;i++){if(!device_sources[i]){if(empty<0)empty=i;continue;}refs[i]=weak_get(&device_sources[i]->weak);if(refs[i]==source)duplicate=1;}
 if(!duplicate&&empty>=0){watch=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*watch));if(watch){weak_init(&watch->weak,source);device_sources[empty]=watch;device_invalidate_locked();}}
 if(!duplicate&&!watch){device_overflow=1;device_invalidate_locked();}
 ReleaseSRWLockExclusive(&device_sources_lock);for(int i=0;i<8;i++)if(refs[i])object_unref(refs[i]);object_unref(source);return watch;
}
static void device_start(void){
 if(InterlockedCompareExchange(&auto_stopping,0,0)||InterlockedCompareExchange(&device_thread_started,1,0))return;
 /* The host uses a process-lifetime tracer; prevent a registry unload while its
  * worker is running. No work is started from DllMain. */
 HMODULE pinned=NULL;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,(LPCWSTR)(uintptr_t)device_poll,&pinned))return;
 if(device_thread){CloseHandle(device_thread);device_thread=NULL;}
 device_thread=CreateThread(NULL,0,device_poll,NULL,0,NULL);if(!device_thread)InterlockedExchange(&device_thread_started,0);
}
