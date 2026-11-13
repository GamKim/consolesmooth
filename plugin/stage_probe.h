/* SPDX-License-Identifier: MIT
 * Test only (CS_TEST_STAGES=1, off by default): where does the time go inside the host's graph?
 * Each probe logs running time minus buffer PTS once per 1200 buffers. The compositor gives its output a
 * new PTS, so values are comparable only within the capture side and within the output side.
 * Once, after 1200 preview frames: the graph's latency query and the properties of its video elements.
 */
static int stage_mode;
static void *(*st_element_clock)(void*),*(*st_pad_parent)(void*);
static uint64_t (*st_clock_time)(void*),(*st_base_time)(void*);
static void *(*st_query_new)(void);
static int (*st_element_query)(void*,void*);
static void (*st_query_parse)(void*,int*,uint64_t*,uint64_t*);
static void **(*st_list_props)(void*,unsigned*);
static void *(*st_value_init)(void*,GType);
static void (*st_get_property)(void*,const char*,void*),(*st_value_unset)(void*);
static char *(*st_value_text)(const void*);
#define STAGE_SAMPLES 1200
typedef struct {char name[20];int n,first_pts;float ms[STAGE_SAMPLES];uint64_t last;double gap;} StageProbe;
static void *stage_elements[400];static LONG stage_element_n,stage_dumped;
static int stage_float(const void *a,const void *b){float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}
static void stage_props(void *element){
 char *path=object_path(element);unsigned count=0;void *cls=*(void**)element;void **props=st_list_props(cls,&count);
 for(unsigned i=0;props&&i<count;i++){
  /* GParamSpec: instance(8), name(8), flags(4+4), value_type(8) */
  const char *pn=*(const char**)((char*)props[i]+8);unsigned flags=*(unsigned*)((char*)props[i]+16);GType vt=*(GType*)((char*)props[i]+24);
  if(!(flags&1)||!strcmp(pn,"name")||!strcmp(pn,"parent"))continue;
  uint64_t value[3]={0};st_value_init(value,vt);st_get_property(element,pn,value);char *text=st_value_text(value);
  logline("STAGE_PROP %s %s=%s",path?path:"?",pn,text?text:"?");mem_free(text);st_value_unset(value);
 }
 mem_free(props);mem_free(path);
}
static void stage_dump(void *element){
 if(InterlockedExchange(&stage_dumped,1))return;
 void *top=mini_ref?element:NULL;void *up;void *held=NULL;
 while(top&&(up=object_parent(top))){if(held)object_unref(held);held=up;top=up;}
 if(top&&st_query_new){void *q=st_query_new();int live=0;uint64_t lo=0,hi=0;if(q&&st_element_query(top,q)){st_query_parse(q,&live,&lo,&hi);logline("STAGE_LATENCY_QUERY live=%d min_ms=%.3f max_ms=%.3f",live,lo/1e6,hi==(uint64_t)-1?-1.0:hi/1e6);}else logline("STAGE_LATENCY_QUERY failed=1");if(q)buffer_unref(q);}
 if(held)object_unref(held);
 for(LONG i=0;i<stage_element_n&&i<400;i++)stage_props(stage_elements[i]);
}
static int stage_probe(void *pad,void *info,void *data){
 StageProbe *s=data;void *buffer=probe_buffer(info);if(!buffer)return 1;
 int64_t pts=*(int64_t*)((char*)buffer+72);if(pts<0)return 1;
 void *element=st_pad_parent(pad);if(!element)return 1;
 void *clock=st_element_clock(element);
 if(clock){uint64_t now=st_clock_time(clock)-st_base_time(element);object_unref(clock);
  s->ms[s->n++]=(float)((int64_t)now-pts)/1e6f;
  if(s->n==STAGE_SAMPLES){qsort(s->ms,STAGE_SAMPLES,sizeof(float),stage_float);
   logline("STAGE_LAG %s median_ms=%.3f p5_ms=%.3f p95_ms=%.3f",s->name,s->ms[STAGE_SAMPLES/2],s->ms[STAGE_SAMPLES/20],s->ms[STAGE_SAMPLES*19/20]);s->n=0;
   if(!strcmp(s->name,"sink_out"))stage_dump(element);}
 }
 object_unref(element);return 1;
}
static void stage_free(void *p){HeapFree(GetProcessHeap(),0,p);}
static void stage_attach(void *pad,const char *name){
 if(!stage_mode)return;StageProbe *s=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof *s);if(!s)return;
 snprintf(s->name,sizeof s->name,"%s",name);if(!add_probe(pad,1<<4,stage_probe,s,stage_free))stage_free(s);
}
static void stage_remember(void *element,const char *path){
 if(!stage_mode||!path)return;
 if(strstr(path,"TestSrc:")||strstr(path,"GstQueue:")||strstr(path,"GstKsVideoSrc:")||strstr(path,"D3D11Compositor:")||strstr(path,"D3D11VideoSink:")||strstr(path,"GstProxySrc:")||strstr(path,"GstProxySink:")||strstr(path,"D3D11Upload:")||strstr(path,"D3D11Download:")||strstr(path,"D3D11ColorConvert:")||strstr(path,"D3D11Transformation:")||strstr(path,"GstVideoRate:")||strstr(path,"H264Enc:")||strstr(path,"RTMPSink:")){
  LONG n=InterlockedIncrement(&stage_element_n)-1;if(n<400)stage_elements[n]=element; /* only read once and only while the graph is playing */
 }
}
static void stage_init(HMODULE gst,HMODULE go){
 wchar_t mode[8];stage_mode=GetEnvironmentVariableW(L"CS_TEST_STAGES",mode,8)==1&&mode[0]==L'1';if(!stage_mode)return;
 *(FARPROC*)&st_element_clock=GetProcAddress(gst,"gst_element_get_clock");*(FARPROC*)&st_pad_parent=GetProcAddress(gst,"gst_pad_get_parent_element");
 *(FARPROC*)&st_clock_time=GetProcAddress(gst,"gst_clock_get_time");*(FARPROC*)&st_base_time=GetProcAddress(gst,"gst_element_get_base_time");
 *(FARPROC*)&st_query_new=GetProcAddress(gst,"gst_query_new_latency");*(FARPROC*)&st_element_query=GetProcAddress(gst,"gst_element_query");*(FARPROC*)&st_query_parse=GetProcAddress(gst,"gst_query_parse_latency");
 *(FARPROC*)&st_list_props=GetProcAddress(go,"g_object_class_list_properties");*(FARPROC*)&st_value_init=GetProcAddress(go,"g_value_init");*(FARPROC*)&st_get_property=GetProcAddress(go,"g_object_get_property");*(FARPROC*)&st_value_unset=GetProcAddress(go,"g_value_unset");*(FARPROC*)&st_value_text=GetProcAddress(go,"g_strdup_value_contents");
 if(!st_element_clock||!st_pad_parent||!st_clock_time||!st_base_time||!st_list_props||!st_value_init||!st_get_property||!st_value_unset||!st_value_text){stage_mode=0;logline("TEST_STAGES missing_symbols=1");return;}
 logline("TEST_STAGES enabled=1");
}

/* Test only (CS_TEST_COMP_LATENCY=<ms>, off by default): the graph reports min latency 40 ms and the host's live
 * compositor waits that long past each output tick (measured: output 52 ms behind its PTS while the picture
 * reached the compositor at 17 ms). This answers the compositor's latency query with a smaller minimum. */
static int comp_latency_ms=-1;
static void *(*cl_info_query)(void*);
static void (*cl_query_parse)(void*,int*,uint64_t*,uint64_t*),(*cl_query_set)(void*,int,uint64_t,uint64_t);
static const char *(*cl_query_name)(int);
static int comp_latency_probe(void *pad,void *info,void *data){
 (void)data;void *q=cl_info_query(info);if(!q)return 1;
 if(strcmp(cl_query_name(*(int*)((char*)q+64)),"latency"))return 1;
 int live=0;uint64_t lo=0,hi=0;cl_query_parse(q,&live,&lo,&hi);
 uint64_t want=(uint64_t)comp_latency_ms*1000000ULL;
 {char *pp=object_path(pad);logline("COMP_LATENCY_QUERY pad=%s live=%d min_ms=%.3f max_ms=%.3f set_min_ms=%d",pp?pp:"?",live,lo/1e6,hi==(uint64_t)-1?-1.0:hi/1e6,comp_latency_ms);mem_free(pp);}
 if(live&&lo>want)cl_query_set(q,live,want,hi);
 return 1;
}
static void comp_latency_attach(void *sink){add_probe(sink,(1<<10)|(1<<13),comp_latency_probe,NULL,NULL);} /* QUERY_UPSTREAM|PULL: after the answer */
static void comp_latency_init(HMODULE gst){
 wchar_t mode[8];if(GetEnvironmentVariableW(L"CS_TEST_COMP_LATENCY",mode,8)-1u>=7u)return;
 *(FARPROC*)&cl_info_query=GetProcAddress(gst,"gst_pad_probe_info_get_query");*(FARPROC*)&cl_query_parse=GetProcAddress(gst,"gst_query_parse_latency");*(FARPROC*)&cl_query_set=GetProcAddress(gst,"gst_query_set_latency");*(FARPROC*)&cl_query_name=GetProcAddress(gst,"gst_query_type_get_name");
 if(!cl_info_query||!cl_query_parse||!cl_query_set||!cl_query_name){logline("TEST_COMP_LATENCY missing_symbols=1");return;}
 comp_latency_ms=_wtoi(mode);logline("TEST_COMP_LATENCY ms=%d",comp_latency_ms);
}

/* Test only (CS_TEST_TIMELINE=<file>, off by default): time and a picture fingerprint at capture, before and after the
 * engine, so test/analysis/stage-timeline.py can match the same picture across stages by its change pattern. */
#define TL_MAX 30000
typedef struct {int stage;uint64_t qpc,sum;} TlEvent;
static TlEvent *tl_events;static volatile LONG tl_n,tl_done;static wchar_t tl_path[1024];
static void tl_flush(void){
 if(InterlockedExchange(&tl_done,1))return;
 FILE *f=_wfopen(tl_path,L"w");if(!f)return;LARGE_INTEGER fq;QueryPerformanceFrequency(&fq);fprintf(f,"freq,%lld\n",(long long)fq.QuadPart);
 for(LONG i=0;i<TL_MAX;i++)fprintf(f,"%d,%llu,%llu\n",tl_events[i].stage,(unsigned long long)tl_events[i].qpc,(unsigned long long)tl_events[i].sum);
 fclose(f);logline("TIMELINE_WRITTEN events=%d",TL_MAX);
}
static void tl_mark(int stage,void *buffer){
 if(!tl_events||!buffer||tl_done)return;CsMap m={0};LARGE_INTEGER now;QueryPerformanceCounter(&now);
 if(!buffer_map(buffer,&m,1))return;uint64_t sum=0;size_t step=m.size/4096;step&=~(size_t)3;
 if(step>=4)for(int i=0;i<4096;i++)sum+=*(uint32_t*)(m.data+step*i)&0x00ffffffu;buffer_unmap(buffer,&m);
 LONG n=InterlockedIncrement(&tl_n)-1;if(n<TL_MAX){tl_events[n]=(TlEvent){stage,(uint64_t)now.QuadPart,sum};}
 if(n==TL_MAX-1)tl_flush();
}
static int tl_capture(void *pad,void *info,void *data){(void)pad;(void)data;tl_mark(0,probe_buffer(info));return 1;}
static void tl_attach(void *pad){if(tl_events)add_probe(pad,1<<4,tl_capture,NULL,NULL);}
static void tl_init(void){
 if(!GetEnvironmentVariableW(L"CS_TEST_TIMELINE",tl_path,1024))return;
 tl_events=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(TlEvent)*TL_MAX);logline("TEST_TIMELINE enabled=%d",tl_events!=NULL);
}
