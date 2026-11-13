/* SPDX-License-Identifier: MIT
 * Test only (Claude's checks, 11-09): CS_TEST_DUMP_DIR=<folder> saves the next CS_TEST_DUMP_FRAMES (default 600) new input
 * pictures exactly as they reach the engine (BGRA, before generation) to <folder>\frames.bgra, with frames.txt
 * (width height, then one line per frame: index arrival_ns). A writer thread does the file work; when its queue is full a
 * frame is skipped and counted, never waited for. Used to study the Zelda title menu text shimmer on real capture data. */
static wchar_t dump_dir[MAX_PATH];static LONG dump_left=-1,dump_skipped,dump_written;
static SRWLOCK dump_lock=SRWLOCK_INIT;static CONDITION_VARIABLE dump_cv=CONDITION_VARIABLE_INIT;
#define DUMP_QUEUE 24
static uint8_t *dump_queue[DUMP_QUEUE];static uint64_t dump_ns[DUMP_QUEUE];static int dump_head,dump_count,dump_w,dump_h;
static DWORD WINAPI dump_writer(void *unused){
 (void)unused;wchar_t path[MAX_PATH+32];swprintf(path,MAX_PATH+32,L"%ls\\frames.bgra",dump_dir);
 HANDLE f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
 swprintf(path,MAX_PATH+32,L"%ls\\frames.txt",dump_dir);FILE *t=_wfopen(path,L"w");
 if(f==INVALID_HANDLE_VALUE||!t){logline("DUMP_OPEN_FAILED");if(t)fclose(t);if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);InterlockedExchange(&dump_left,0);return 0;}
 int header=0;
 for(;;){uint8_t *p=NULL;uint64_t ns=0;int done=0;
  AcquireSRWLockExclusive(&dump_lock);
  while(!dump_count&&InterlockedCompareExchange(&dump_left,0,0)>0)SleepConditionVariableSRW(&dump_cv,&dump_lock,200,0);
  if(dump_count){p=dump_queue[dump_head];ns=dump_ns[dump_head];dump_head=(dump_head+1)%DUMP_QUEUE;--dump_count;}else done=1;
  ReleaseSRWLockExclusive(&dump_lock);
  if(done)break;
  if(!header){fprintf(t,"%d %d\n",dump_w,dump_h);header=1;}
  DWORD wrote;WriteFile(f,p,(DWORD)((size_t)dump_w*dump_h*4),&wrote,NULL);fprintf(t,"%ld %llu\n",dump_written,(unsigned long long)ns);InterlockedIncrement(&dump_written);
  HeapFree(GetProcessHeap(),0,p);
 }
 fclose(t);CloseHandle(f);logline("DUMP_DONE written=%ld skipped=%ld",dump_written,dump_skipped);return 0;
}
static void dump_init(void){
 wchar_t v[16];DWORD n=GetEnvironmentVariableW(L"CS_TEST_DUMP_DIR",dump_dir,MAX_PATH);if(!n||n>=MAX_PATH)return;
 LONG frames=GetEnvironmentVariableW(L"CS_TEST_DUMP_FRAMES",v,16)-1u<15u?_wtoi(v):600;if(frames<1)frames=600;
 CreateDirectoryW(dump_dir,NULL);InterlockedExchange(&dump_left,frames);logline("DUMP_ARMED frames=%ld",frames);
}
/* Video thread, new pictures only. */
static void dump_frame(const uint8_t *bgra,int w,int h,uint64_t ns){
 if(InterlockedCompareExchange(&dump_left,0,0)<=0)return;
 static LONG started;if(!InterlockedExchange(&started,1)){dump_w=w;dump_h=h;HANDLE t=CreateThread(NULL,0,dump_writer,NULL,0,NULL);if(t)CloseHandle(t);else{InterlockedExchange(&dump_left,0);return;}}
 if(w!=dump_w||h!=dump_h)return;
 size_t bytes=(size_t)w*h*4;uint8_t *copy=HeapAlloc(GetProcessHeap(),0,bytes);if(!copy){InterlockedIncrement(&dump_skipped);return;}
 memcpy(copy,bgra,bytes);
 AcquireSRWLockExclusive(&dump_lock);
 if(dump_count<DUMP_QUEUE){dump_queue[(dump_head+dump_count)%DUMP_QUEUE]=copy;dump_ns[(dump_head+dump_count)%DUMP_QUEUE]=ns;++dump_count;copy=NULL;InterlockedDecrement(&dump_left);}
 ReleaseSRWLockExclusive(&dump_lock);WakeConditionVariable(&dump_cv);
 if(copy){HeapFree(GetProcessHeap(),0,copy);InterlockedIncrement(&dump_skipped);}
}
