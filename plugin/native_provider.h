/* SPDX-License-Identifier: MIT
 * Public GStreamer 1.18 GstDeviceProvider factory. No fabricated capture device.
 * Activation happens when the host enumerates providers, also with a cached registry.
 */
static GType native_tracer;
static unsigned native_object_class_size;
static void *(*native_object_new)(GType,const char*,...);
static void (*native_metadata)(void*,const char*,const char*,const char*,const char*);
static volatile LONG native_once;
static void *native_tracer_ref;
/* 1.0.1: Streaming Center 1.9.19.1 (GStreamer 1.28) no longer runs a GstDeviceMonitor, so the provider is never
 * probed. The installer adds "consolesmooth" to GST_TRACERS and GStreamer then creates our tracer at gst_init;
 * that tracer activates the plugin itself (existing != NULL). native_creating marks the tracer we create here. */
static volatile LONG native_creating;
static void native_activate_with(void *existing){
 if(!native_host_supported())return;
 if(InterlockedCompareExchange(&native_once,1,0))return;
 HMODULE module=NULL;wchar_t root[32768],engine[32768],settings[32768];
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)(uintptr_t)native_activate_with,&module))return;
 DWORD n=GetModuleFileNameW(module,root,32768);if(!n||n>32000)return;
 wchar_t *slash=wcsrchr(root,L'\\');if(!slash)return;*slash=0;
 wchar_t host[32768],expected[32768];n=GetModuleFileNameW(NULL,host,32768);if(!n||n>32000)return;
 slash=wcsrchr(host,L'\\');if(!slash)return;*slash=0;
 swprintf(expected,32768,L"%ls\\Plugin\\ConsoleSmooth",host);
 if(_wcsicmp(root,expected))return;
 swprintf(engine,32768,L"%ls\\engine",root);
 n=GetEnvironmentVariableW(L"LOCALAPPDATA",settings,32768);if(!n||n>32000)return;
 wcscat(settings,L"\\ConsoleSmoothNative");
 if(!CreateDirectoryW(settings,NULL)&&GetLastError()!=ERROR_ALREADY_EXISTS)return;
 SetEnvironmentVariableW(L"CS_ENGINE_DIR",engine);SetEnvironmentVariableW(L"CS_SETTINGS_DIR",settings);
 insert_enabled=1;preview120=1;framegen_fps=60;framegen_output_fps=120;
 interactive_ui=1;control_bits=2;synthetic=0;proof_marker=0;sink_pacing=0;
 ui_preferences(0);
 logline("NATIVE_PROVIDER_ACTIVATE host_verified=%d via=%s",native_host_verified,existing?"gst_tracers":"device_provider");
 if(existing){native_tracer_ref=existing;return;}
 InterlockedExchange(&native_creating,1);native_tracer_ref=native_object_new(native_tracer,NULL);InterlockedExchange(&native_creating,0);
}
static void native_activate(void){native_activate_with(NULL);}
static void native_provider_instance(void *self,void *klass){(void)self;(void)klass;native_activate();}
static void *native_probe(void *self){(void)self;native_activate();return NULL;}
static int native_start(void *self){(void)self;native_activate();return 1;}
static void native_stop(void *self){(void)self;}
static void native_provider_class(void *klass,void *data){
 (void)data;
 /* Public GstDeviceProviderClass follows GstObjectClass with factory/probe/start/stop. */
 void **slots=(void**)((unsigned char*)klass+native_object_class_size);
 slots[1]=(void*)native_probe;slots[2]=(void*)native_start;slots[3]=(void*)native_stop;
 native_metadata(klass,"ConsoleSmooth integration","Video/Source","Frame generation integration; no capture devices","ConsoleSmooth contributors");
}
static int native_provider_register(void *plugin,GType tracer,HMODULE gst,HMODULE go){
 GType (*provider_type)(void),(*object_type)(void);
 int (*provider_register)(void*,const char*,unsigned,GType);
 RESOLVE(provider_type,gst,"gst_device_provider_get_type");RESOLVE(object_type,gst,"gst_object_get_type");
 RESOLVE(provider_register,gst,"gst_device_provider_register");
 RESOLVE(native_metadata,gst,"gst_device_provider_class_set_static_metadata");
 RESOLVE(native_object_new,go,"g_object_new");
 TypeQuery p={0},o={0};type_query(provider_type(),&p);type_query(object_type(),&o);
 if(!p.instance_size||p.class_size<o.class_size+4*sizeof(void*))return 0;
 native_object_class_size=o.class_size;native_tracer=tracer;
 GType t=type_register(provider_type(),"ConsoleSmoothDeviceProvider",p.class_size,(void*)native_provider_class,p.instance_size,native_provider_instance,0);
 return t&&provider_register(plugin,"consolesmoothdeviceprovider",256,t);
}
