#include "ui_lang.h"
#include <commdlg.h>
/* SPDX-License-Identifier: MIT
 * "진단 정보 내보내기": one local text file with hardware, settings, live counters and recent plugin
 * log lines. Nothing is sent anywhere. The user name is masked; the computer name is not collected.
 */
#include <cpuid.h>
#define CS_PLUGIN_VERSION "1.1.0"
static void diag_cpu(char out[49]){
 unsigned r[12]={0},eax,ebx,ecx,edx;out[0]=0;
 if(!__get_cpuid(0x80000000,&eax,&ebx,&ecx,&edx)||eax<0x80000004)return;
 for(unsigned i=0;i<3;i++)__get_cpuid(0x80000002+i,&r[i*4],&r[i*4+1],&r[i*4+2],&r[i*4+3]);
 memcpy(out,r,48);out[48]=0;
}
static void diag_write(HANDLE f,const char *fmt,...){
 char buf[1024];va_list ap;va_start(ap,fmt);int n=vsnprintf(buf,sizeof buf,fmt,ap);va_end(ap);
 if(n<0)return;if(n>=(int)sizeof buf)n=sizeof buf-1;DWORD written;WriteFile(f,buf,n,&written,NULL);
}
/* Log lines can hold profile paths. */
static void diag_write_masked(HANDLE f,const char *line,const char *user){
 DWORD written;const char *at=line;size_t length=strlen(user);
 if(length)for(const char *hit;(hit=strstr(at,user));at=hit+length){WriteFile(f,at,(DWORD)(hit-at),&written,NULL);WriteFile(f,"USER",4,&written,NULL);}
 WriteFile(f,at,(DWORD)strlen(at),&written,NULL);WriteFile(f,"\r\n",2,&written,NULL);
}
static HANDLE diag_create(const wchar_t *dir,const wchar_t *name,wchar_t *path,int cap){
 if(swprintf(path,cap,L"%ls\\%ls",dir,name)<0)return INVALID_HANDLE_VALUE;
 return CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
}
static void diag_export(HWND owner,const CsCountWindow *counts){
 SYSTEMTIME t;GetLocalTime(&t);wchar_t name[80],dir[32768],path[33000];
 swprintf(name,80,ui_tr(L"ConsoleSmooth-진단-%04d%02d%02d-%02d%02d%02d.txt",L"ConsoleSmooth-diagnostics-%04d%02d%02d-%02d%02d%02d.txt"),t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
 /* 1.8.2 (11-09 user request): a save dialog lets the user pick the folder and name, starting on the Desktop.
  * Cancel saves nothing. If the dialog cannot open, the old automatic path is used: Desktop, then the settings folder. */
 HANDLE f=INVALID_HANDLE_VALUE;DWORD n=GetEnvironmentVariableW(L"USERPROFILE",dir,32000);
 if(n&&n<32000&&wcslen(dir)+9<32000)wcscat(dir,L"\\Desktop");else dir[0]=0;
 {wchar_t pick[33000];wcscpy(pick,name);OPENFILENAMEW of;memset(&of,0,sizeof of);of.lStructSize=sizeof of;of.hwndOwner=owner;of.lpstrFilter=ui_tr(L"텍스트 파일 (*.txt)\0*.txt\0모든 파일\0*.*\0",L"Text files (*.txt)\0*.txt\0All files\0*.*\0");
  of.lpstrFile=pick;of.nMaxFile=33000;of.lpstrInitialDir=dir[0]?dir:NULL;of.lpstrDefExt=L"txt";of.lpstrTitle=ui_tr(L"진단 정보 저장",L"Save diagnostics");
  of.Flags=OFN_OVERWRITEPROMPT|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
  if(GetSaveFileNameW(&of)){wcscpy(path,pick);f=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
   if(f==INVALID_HANDLE_VALUE){MessageBoxW(owner,ui_tr(L"선택한 위치에 진단 파일을 만들지 못했습니다. 다른 폴더를 골라 주세요.",L"Could not create the diagnostics file there. Choose another folder."),L"ConsoleSmooth",MB_OK|MB_ICONWARNING);return;}}
  else if(!CommDlgExtendedError())return; /* cancelled */
  else logline("DIAG_SAVE_DIALOG_FAILED code=%lu",CommDlgExtendedError());}
 if(f==INVALID_HANDLE_VALUE&&dir[0])f=diag_create(dir,name,path,33000);
 if(f==INVALID_HANDLE_VALUE){n=GetEnvironmentVariableW(L"CS_SETTINGS_DIR",dir,32000);if(!n)n=GetEnvironmentVariableW(L"CS_ENGINE_DIR",dir,32000);if(n&&n<32000)f=diag_create(dir,name,path,33000);}
 if(f==INVALID_HANDLE_VALUE){MessageBoxW(owner,ui_tr(L"진단 파일을 만들지 못했습니다.",L"Could not create the diagnostics file."),L"ConsoleSmooth",MB_OK|MB_ICONWARNING);return;}
 char user[256]="";wchar_t wide[128];if(GetEnvironmentVariableW(L"USERNAME",wide,128)-1u<127u)WideCharToMultiByte(CP_UTF8,0,wide,-1,user,sizeof user,NULL,NULL);
 WriteFile(f,"\xEF\xBB\xBF",3,&n,NULL);
 diag_write(f,"ConsoleSmooth diagnostics\r\nplugin=%s\r\ntime=%04d-%02d-%02d %02d:%02d:%02d\r\n",CS_PLUGIN_VERSION,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
 typedef LONG (WINAPI *RtlVersion)(OSVERSIONINFOW*);OSVERSIONINFOW os={.dwOSVersionInfoSize=sizeof os};RtlVersion version=(RtlVersion)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetVersion");
 if(version&&!version(&os))diag_write(f,"windows=%lu.%lu.%lu\r\n",os.dwMajorVersion,os.dwMinorVersion,os.dwBuildNumber);
 char cpu[49];diag_cpu(cpu);diag_write(f,"cpu=%s\r\n",cpu);
 MEMORYSTATUSEX memory={.dwLength=sizeof memory};if(GlobalMemoryStatusEx(&memory))diag_write(f,"ram_total_mb=%llu ram_free_mb=%llu\r\n",(unsigned long long)(memory.ullTotalPhys>>20),(unsigned long long)(memory.ullAvailPhys>>20));
 DISPLAY_DEVICEW device={.cb=sizeof device};
 for(DWORD i=0;EnumDisplayDevicesW(NULL,i,&device,0);i++,device.cb=sizeof device){char text[256];WideCharToMultiByte(CP_UTF8,0,device.DeviceString,-1,text,sizeof text,NULL,NULL);if(device.StateFlags&DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)diag_write(f,"display_adapter=%s primary=%d\r\n",text,!!(device.StateFlags&DISPLAY_DEVICE_PRIMARY_DEVICE));}
 diag_write(f,"monitor_hz=%d dpi=%d\r\n",ui_monitor_hz(),ui_dpi);
 diag_write(f,"settings rate_mode=%d multiplier=%d target_fps=%d stable=%d buffer_ms=%d low_buffer_ms=%d model=%d record_generated=%d show_fps=%d display_mode=%d cadence30=%d hud_keep=%d gpu_keep=%d fsr_fast=%d fps_corner=%ld\r\n",ui_mode,ui_mult,ui_target,ui_stable,ui_buffer,ui_low_buffer,ui_model,ui_record,ui_show_fps,ui_saved_display,ui_cadence,ui_hudkeep,ui_gpukeep,ui_fsrfast,ui_fps_corner);
 AutoRate a=auto_snapshot();diag_write(f,"input live=%d supported=%d provider=%d rate=%d/%d target=%d/%d size=%dx%d progressive=%d fallback=%d\r\n",a.live,a.supported,a.provider,a.sn,a.sd,a.tn,a.td,a.width,a.height,a.progressive,a.fallback);
 diag_write(f,"input_detail capped=%d signal=%d/%d capture=%d/%d content_fps=%.2f content_age_ms=%llu\r\n",a.capped,a.signal_sn,a.signal_sd,auto_capture_sn,auto_capture_sd,InterlockedCompareExchange(&auto_content_x100,0,0)/100.0,(unsigned long long)(GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_content_at,0,0)));
 diag_write(f,"shown_fps=%.2f shown_age_ms=%llu\r\n",InterlockedCompareExchange(&auto_shown_x100,0,0)/100.0,(unsigned long long)(GetTickCount64()-(ULONGLONG)InterlockedCompareExchange64(&auto_shown_at,0,0))); /* 2.0.7 */
 CsStats s;int error,w,h;double buffer;AcquireSRWLockShared(&status_lock);s=ui_stats;error=ui_error;w=ui_width;h=ui_height;buffer=ui_live_buffer_ms;ReleaseSRWLockShared(&status_lock);
 double delivered=0;int measuring=fps_snapshot(&delivered);LONG bits=ui_requested();
 diag_write(f,"state generation_on=%d gpu_slow=%d error=%d processing_size=%dx%d buffer_ms=%.1f preview_fps=%.1f measuring=%d generated_fps=%.1f repeat_fps=%.1f\r\n",bits&1,(int)InterlockedCompareExchange(&auto_gpu_slow,0,0),error,w,h,buffer,delivered,measuring,counts->gen_fps,counts->repeat_fps);
 diag_write(f,"engine ready=%d ticks=%llu generated=%llu repeated=%llu static=%llu cuts=%llu overwritten=%llu failures=%llu last_ms=%.3f\r\n",s.ready,(unsigned long long)s.ticks,(unsigned long long)s.generated,(unsigned long long)s.repeated,(unsigned long long)s.static_pairs,(unsigned long long)s.cuts,(unsigned long long)s.overwritten,(unsigned long long)s.failures,s.last_inference_ms);
 {StageStats st;int valid;char g[512];AcquireSRWLockShared(&stage_lock);st=ui_stage;valid=ui_stage_valid;memcpy(g,ui_gpu_info,sizeof g);ReleaseSRWLockShared(&stage_lock);
  if(g[0])diag_write(f,"gpu_detail %s\r\n",g);
  if(valid)diag_write(f,"stages samples=%llu in_ms=%.2f gpu_ms=%.2f out_ms=%.2f total_ms=%.2f gpu_max_ms=%.2f total_max_ms=%.2f busy_pct=%.0f keep_ms=%.3f keep_dispatches=%llu\r\n",(unsigned long long)st.samples,st.in_ms,st.gpu_ms,st.out_ms,st.total_ms,st.gpu_max_ms,st.total_max_ms,st.busy_pct,st.keep_ms,(unsigned long long)st.keep_dispatches);}
 diag_write(f,"\r\nrecent log\r\n");
 AcquireSRWLockShared(&lock);unsigned total=log_ring_next,count=total<LOG_RING_LINES?total:LOG_RING_LINES;
 for(unsigned i=total-count;i!=total;i++)diag_write_masked(f,log_ring[i%LOG_RING_LINES],user);
 ReleaseSRWLockShared(&lock);
 CloseHandle(f);logline("DIAGNOSTICS_EXPORTED");
 wchar_t message[33200];swprintf(message,33200,ui_tr(L"진단 파일을 저장했습니다.\n\n%ls\n\n장비, 설정, 최근 플러그인 기록이 들어 있고 자동으로 전송되지 않습니다.",L"Diagnostics saved.\n\n%ls\n\nIt holds the hardware, settings and recent plugin log, and is not sent anywhere."),path);
 MessageBoxW(owner,message,L"ConsoleSmooth",MB_OK|MB_ICONINFORMATION);
}
