/* SPDX-License-Identifier: MIT
 * Observe capture-side caps. Renegotiate only the existing preview capsfilter
 * from the UI thread; never unlink a playing graph or block a streaming probe.
 * Weak entries are removed after graph destruction; no lifetime strong refs.
 */
#include "device_provider.h"
static SRWLOCK auto_filter_lock=SRWLOCK_INIT;
/* tn/td: rate last requested on the filter. on/od: the host's original preview rate. */
typedef struct AutoFilter {void *weak;int tn,td,on,od;struct AutoFilter *next;} AutoFilter;
static AutoFilter *auto_filters;
static int auto_filter_n;
static AutoRate auto_applied_rate;
static int auto_capture_probe(void *pad,void *info,void *data){
 (void)info;(void)data;void *caps=current_caps(pad),*s=caps?caps_structure(caps,0):NULL;int n=0,d=1,w=0,h=0;
 if(s){structure_fraction(s,"framerate",&n,&d);structure_int(s,"width",&w);structure_int(s,"height",&h);}
 if(InterlockedCompareExchange(&auto_capture_count,0,0)>1)n=0;
 auto_seen(n,d,w,h);if(caps)caps_unref(caps);return 1;
}
static void auto_capture_gone(void *data){InterlockedDecrement(&auto_capture_count);device_watch_gone(data);}
static void auto_watch_capture(void *pad){
 if(!interactive_ui||synthetic)return;DeviceWatch *watch=device_watch_add(pad);if(!watch)return;InterlockedIncrement(&auto_capture_count);
 if(!add_probe(pad,1<<4,auto_capture_probe,watch,auto_capture_gone))auto_capture_gone(watch);
 else {logline("AUTO_CAPTURE_WATCH attached=1");device_start();}
}
static void auto_remember_filter(void *sink,int original_n,int original_d){
 void *filter=object_parent(sink);if(!filter)return;
 object_set_data(filter,root_filter_key,(void*)1);
 if(!interactive_ui||synthetic){object_unref(filter);return;}
 AcquireSRWLockExclusive(&auto_filter_lock);int duplicate=0;AutoFilter **at=&auto_filters;
 while(*at){AutoFilter *entry=*at;void *live=weak_get(&entry->weak);
  if(!live){*at=entry->next;weak_clear(&entry->weak);HeapFree(GetProcessHeap(),0,entry);--auto_filter_n;continue;}
  if(live==filter)duplicate=1;object_unref(live);at=&entry->next;
 }
 if(!duplicate){AutoFilter *entry=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*entry));
  if(entry){weak_init(&entry->weak,filter);entry->on=original_n;entry->od=original_d;entry->next=auto_filters;auto_filters=entry;++auto_filter_n;}
  else logline("AUTO_FILTER_TRACK_FAILED allocation=1");
 }
 ReleaseSRWLockExclusive(&auto_filter_lock);object_unref(filter);
}
static void auto_apply_preview(void){
 if(!interactive_ui||synthetic)return;
 AcquireSRWLockExclusive(&auto_filter_lock);
 /* Serialize automatic negotiation with recording admission as well as UI. */
 AutoRate a=auto_snapshot();int locked=InterlockedCompareExchange(&record_branches,0,0)>0;
 AutoFilter **at=&auto_filters;
 while(*at){AutoFilter *entry=*at;void *filter=weak_get(&entry->weak);
  if(!filter){*at=entry->next;weak_clear(&entry->weak);HeapFree(GetProcessHeap(),0,entry);--auto_filter_n;continue;}
  at=&entry->next;
  /* No signal: nothing to generate, so return the preview to the host's own rate
   * instead of repeating the "No Signal" picture at the generation rate. */
  int idle=((!a.live&&!keep_rate_without_signal)||InterlockedCompareExchange(&auto_gpu_slow,0,0))&&entry->on>0,n=idle?entry->on:a.tn,d=idle?entry->od:a.td;
  if(locked||(!idle&&(!a.live||!a.supported))||(entry->tn==n&&entry->td==d)){object_unref(filter);continue;}
  void *caps=NULL;property_get(filter,"caps",&caps,NULL);if(!caps){object_unref(filter);continue;}
  void *copy=caps_copy(caps);caps_unref(caps);if(!copy){object_unref(filter);continue;}
  caps_set_simple(copy,"framerate",fraction_type(),n,d,NULL);
  property_set(filter,"caps-change-mode",1,"caps",copy,NULL);caps_unref(copy);object_unref(filter);
  entry->tn=n;entry->td=d;if(!idle)auto_applied_rate=a;
  if(idle)logline("AUTO_RATE_IDLE no_signal=%d gpu_slow=%d target=%d/%d",!a.live,a.live,n,d);
  else logline("AUTO_RATE_REQUEST source=%d/%d target=%d/%d",a.sn,a.sd,a.tn,a.td);
 }
 ReleaseSRWLockExclusive(&auto_filter_lock);
}
