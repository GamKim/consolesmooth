/* SPDX-License-Identifier: MIT
 * 1.1.5 (11-23, RTX 3070 PC: System32 had no vulkan-1.dll, so the engine DLL could not be opened, win32=126).
 * The engine imports vulkan-1.dll, the Vulkan loader that normally comes with the graphics driver. The installer also
 * puts the official Khronos/LunarG loader in engine\vulkan. The driver's copy in System32 is always preferred; the
 * installed copy is opened only when System32 has none, and only if its SHA-256 matches. Once a module named
 * vulkan-1.dll is loaded, the engine's import resolves to it. Nothing is written to the Windows folder.
 * The loader alone is not a driver: without a Vulkan graphics driver the engine still reports "No requested Vulkan GPU".
 */
#ifndef CS_VULKAN_LOAD_H
#define CS_VULKAN_LOAD_H
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <string.h>
#ifndef CS_VULKAN_NAME
#define CS_VULKAN_NAME L"vulkan-1.dll"
#endif
#include "vulkan_pin.h" /* CS_VULKAN_SHA256 of engine\vulkan\vulkan-1.dll */
static volatile LONG vulkan_state=-1;      /* -1 not looked yet, 1 System32, 2 installed copy, 0 none */
static volatile LONG engine_load_error;    /* GetLastError of the last failed engine load, 0 = none */
static int vulkan_pinned(const wchar_t *path){
 HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(f==INVALID_HANDLE_VALUE)return 0;
 BCRYPT_ALG_HANDLE alg=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char buf[16384],digest[32];char hex[65];DWORD got;int ok=0;
 if(BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,NULL,0)<0||BCryptCreateHash(alg,&hash,NULL,0,NULL,0,0)<0)goto done;
 for(;;){if(!ReadFile(f,buf,sizeof buf,&got,NULL))goto done;if(!got)break;if(BCryptHashData(hash,buf,got,0)<0)goto done;}
 if(BCryptFinishHash(hash,digest,32,0)<0)goto done;
 for(int i=0;i<32;i++)snprintf(hex+i*2,3,"%02X",digest[i]);
 ok=!strcmp(hex,CS_VULKAN_SHA256);
done:if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);CloseHandle(f);return ok;
}
static int vulkan_prepare(const wchar_t *engine){
 LONG s=InterlockedCompareExchange(&vulkan_state,-1,-1);if(s>0)return s;
 s=0;
 if(LoadLibraryExW(CS_VULKAN_NAME,NULL,LOAD_LIBRARY_SEARCH_SYSTEM32))s=1;
 else{
  static wchar_t path[33000];
  if(swprintf(path,33000,L"%ls\\vulkan\\%ls",engine,CS_VULKAN_NAME)>0&&vulkan_pinned(path)&&
     LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32))s=2;
 }
 InterlockedExchange(&vulkan_state,s);return s;
}
/* Short reason for the log, the diagnostics and the settings window; "" when the engine DLL opened. */
static const char *engine_load_reason(void){
 LONG e=InterlockedCompareExchange(&engine_load_error,0,0);if(!e)return "";
 if(InterlockedCompareExchange(&vulkan_state,-1,-1)==0)return "vulkan-1.dll is missing (reinstall the graphics driver)";
 return e==126?"engine DLL or a DLL it needs is missing":"engine DLL could not be opened";
}
#endif
