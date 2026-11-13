/* SPDX-License-Identifier: MIT
 * Opt-in, bounded diagnostic capture of frames actually selected for output.
 * No engine/model changes. Normal launches allocate/copy nothing here.
 * One original/generated/original triplet per process, disk IO on a worker.
 */
typedef struct {
 uint8_t *pixels;size_t bytes;int w,h,px,state,ratio,mask,flow[3];uint64_t n[3];
 const void *group;HANDLE writer;wchar_t dir[32768];
} FrameProof;
static volatile LONG proof_claimed;
static int proof_marker;
/* Explicit diagnostic only: a recoverable 32-bit output ID, actual-generation
 * flag and checksum. Written AFTER inference, never fed back into the model. */
static void proof_stamp(uint8_t *pixels,int width,int px,uint64_t tick,int generated){
 if(!proof_marker)return;uint8_t code[7]={0xa5,(uint8_t)tick,(uint8_t)(tick>>8),(uint8_t)(tick>>16),(uint8_t)(tick>>24),(uint8_t)(generated?0x5a:0x33),0};
 for(int i=0;i<6;i++)code[6]^=code[i];
 for(int y=0;y<16;y++)for(int x=0;x<448;x++){int bit=x/8;uint8_t v=(code[bit/8]>>(bit%8))&1?235:16;size_t k=((size_t)y*width+x)*px;pixels[k]=pixels[k+1]=pixels[k+2]=v;}
}
static _Thread_local struct {FrameProof *proof;int slot;} proof_delivery;
static int proof_write(const wchar_t *dir,const wchar_t *name,const void *data,DWORD size){
 wchar_t path[32768];swprintf(path,32768,L"%ls\\%ls",dir,name);
 HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
 if(file==INVALID_HANDLE_VALUE)return 0;DWORD written=0;int ok=WriteFile(file,data,size,&written,NULL)&&written==size;CloseHandle(file);return ok;
}
static DWORD WINAPI proof_save(void *data){
 FrameProof *p=data;int ok=1;const wchar_t *names[]={L"original-a.raw",L"generated.raw",L"original-b.raw"};
 for(int i=0;i<3;i++)ok=proof_write(p->dir,names[i],p->pixels+i*p->bytes,(DWORD)p->bytes)&&ok;
 char json[1024];int length=snprintf(json,sizeof(json),
  "{\"width\":%d,\"height\":%d,\"format\":\"%s\",\"ratio\":%d,\"output_ticks\":[%llu,%llu,%llu],\"phase\":%.6f,\"renderer_chain_flow\":[%d,%d,%d],\"images_saved\":%s,\"scope\":\"selected output buffers and successful downstream chain return; shared output is not proof of physical display\"}\n",
  p->w,p->h,p->px==4?"BGRA32":"BGR24",p->ratio,(unsigned long long)p->n[0],(unsigned long long)p->n[1],(unsigned long long)p->n[2],(double)(p->n[1]-p->n[0])/p->ratio,p->flow[0],p->flow[1],p->flow[2],ok?"true":"false");
 ok=proof_write(p->dir,L"capture.json",json,(DWORD)length)&&ok;
 logline("FRAME_PROOF_SAVED ok=%d group=%p ticks=%llu,%llu,%llu phase=%.3f flow=%d,%d,%d",ok,p->group,(unsigned long long)p->n[0],(unsigned long long)p->n[1],(unsigned long long)p->n[2],(double)(p->n[1]-p->n[0])/p->ratio,p->flow[0],p->flow[1],p->flow[2]);return 0;
}
static FrameProof *proof_create(int w,int h,int px,const void *group){
 wchar_t dir[32768];DWORD count=GetEnvironmentVariableW(L"CS_FRAME_PROOF_DIR",dir,32768);
 if(!count||count>32000||InterlockedCompareExchange(&proof_claimed,1,0))return NULL;
 FrameProof *p=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(*p));if(!p)return NULL;
 p->bytes=(size_t)w*h*px;p->pixels=HeapAlloc(GetProcessHeap(),0,p->bytes*3);
 if(!p->pixels){HeapFree(GetProcessHeap(),0,p);return NULL;}
 p->w=w;p->h=h;p->px=px;p->group=group;wcscpy(p->dir,dir);logline("FRAME_PROOF_ARMED group=%p",group);return p;
}
static void proof_reset(FrameProof *p){if(p&&p->state!=4){p->state=0;p->mask=0;}}
static void proof_destroy(FrameProof *p){if(!p)return;if(p->writer){WaitForSingleObject(p->writer,INFINITE);CloseHandle(p->writer);}HeapFree(GetProcessHeap(),0,p->pixels);HeapFree(GetProcessHeap(),0,p);}
static void proof_sample(FrameProof *p,const uint8_t *pixels,uint64_t n,int ratio,int generated){
 if(!p||p->state==4||ratio<2||ratio>5)return;
 int slot=-1;
 if(n%(unsigned)ratio==0){
  if(p->state==2&&p->ratio==ratio&&n==p->n[0]+(unsigned)ratio)slot=2;
  else {slot=0;p->mask=0;p->ratio=ratio;}
 }else if(generated&&p->state==1&&p->ratio==ratio&&n>p->n[0]&&n<p->n[0]+(unsigned)ratio)slot=1;
 if(slot<0)return;
 memcpy(p->pixels+slot*p->bytes,pixels,p->bytes);p->n[slot]=n;p->state=slot+1;
 /* Ignore effectively static triplets. Full pixel comparisons run offline. */
 if(slot==2){unsigned differencesA=0,differencesB=0;
  for(size_t i=0;i<p->bytes;i+=97){int a=p->pixels[p->bytes+i]-p->pixels[i],b=p->pixels[p->bytes+i]-pixels[i];differencesA+=a>3||a<-3;differencesB+=b>3||b<-3;}
  if(differencesA<100||differencesB<100){proof_reset(p);return;}
 }
 proof_delivery.proof=p;proof_delivery.slot=slot;
}
static void proof_render_return(const void *group,int flow){
 FrameProof *p=proof_delivery.proof;if(!p||p->group!=group)return;
 int slot=proof_delivery.slot;proof_delivery.proof=NULL;p->flow[slot]=flow;
 if(flow!=0){proof_reset(p);return;}p->mask|=1<<slot;
 if(slot==2&&p->state==3){
  if(p->mask!=7){proof_reset(p);return;}
  p->state=4;p->writer=CreateThread(NULL,0,proof_save,p,0,NULL);
  if(!p->writer)logline("FRAME_PROOF_SAVE_FAILED thread=1");
 }
}
