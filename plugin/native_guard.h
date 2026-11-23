/* SPDX-License-Identifier: MIT
 * Host check before the plugin activates.
 * 1.8.2 (11-09: newer Streaming Center versions should still turn on): the tested build (1.8.22.2,
 * SHA-256 below) is "verified"; any other StreamingCenter.exe is accepted when its Authenticode signature is valid
 * and signed by AVerMedia, and the loaded GStreamer is 1.x with x >= 18 (the plugin uses the stable public 1.x ABI).
 * Version-specific parts stay fail-closed on their own: the GC573 SDK adapter checks avtsdk.dll's hash and otherwise
 * the capture caps stand in (as for USB cards); the toolbar attach simply does not appear if the layout changed. */
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
static int native_host_verified; /* 1: the tested build, 0: another AVerMedia-signed build */
static wchar_t native_host_signer[128];
static int native_file_sha256(const wchar_t *path,unsigned char digest[32]){
 HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);if(file==INVALID_HANDLE_VALUE)return 0;
 BCRYPT_ALG_HANDLE algorithm=NULL;BCRYPT_HASH_HANDLE hash=NULL;unsigned char buf[16384];DWORD got;int ok=0;
 if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,0)<0)goto done;
 if(BCryptCreateHash(algorithm,&hash,NULL,0,NULL,0,0)<0)goto done;
 for(;;){if(!ReadFile(file,buf,sizeof(buf),&got,NULL))goto done;if(!got)break;if(BCryptHashData(hash,buf,got,0)<0)goto done;}
 ok=BCryptFinishHash(hash,digest,32,0)>=0;
done:if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);CloseHandle(file);return ok;
}
/* Valid embedded Authenticode signature whose signer name starts with "AVerMedia". Offline revocation only (no network). */
static int native_signed_by_avermedia(const wchar_t *path,wchar_t *signer,int cap){
 GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;WINTRUST_FILE_INFO fi;memset(&fi,0,sizeof fi);fi.cbStruct=sizeof fi;fi.pcwszFilePath=path;
 WINTRUST_DATA wd;memset(&wd,0,sizeof wd);wd.cbStruct=sizeof wd;wd.dwUIChoice=WTD_UI_NONE;wd.fdwRevocationChecks=WTD_REVOKE_NONE;wd.dwUnionChoice=WTD_CHOICE_FILE;wd.pFile=&fi;
 wd.dwStateAction=WTD_STATEACTION_VERIFY;wd.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
 LONG status=WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&wd);int ok=0;
 if(status==0){CRYPT_PROVIDER_DATA *pd=WTHelperProvDataFromStateData(wd.hWVTStateData);CRYPT_PROVIDER_SGNR *sg=pd?WTHelperGetProvSignerFromChain(pd,0,FALSE,0):NULL;
  CRYPT_PROVIDER_CERT *pc=sg?WTHelperGetProvCertFromChain(sg,0):NULL;
  if(pc&&pc->pCert&&CertGetNameStringW(pc->pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,NULL,signer,cap)>1)ok=!_wcsnicmp(signer,L"AVerMedia",9);}
 wd.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust((HWND)INVALID_HANDLE_VALUE,&action,&wd);return ok;
}
/* 1: verified build, 2: other AVerMedia-signed build, 0: refuse. Split from the process path for the host check test. */
static int native_host_check(const wchar_t *path){
 static const unsigned char expected[32]={0xca,0xdc,0xcc,0x27,0xd6,0x6f,0x33,0x50,0xbc,0x09,0x2e,0x29,0xbb,0xad,0x88,0x58,0x2f,0x41,0x10,0xe1,0xed,0xf8,0x71,0x93,0x34,0x2c,0x18,0x3d,0x3c,0xb1,0x5c,0xc1};
 unsigned char digest[32];if(native_file_sha256(path,digest)&&!memcmp(digest,expected,32)){wcscpy(native_host_signer,L"verified");return 1;}
 return native_signed_by_avermedia(path,native_host_signer,128)?2:0;
}
static int native_gst_ok(void){
 HMODULE gst=GetModuleHandleW(L"gstreamer-1.0-0.dll");void (*version)(unsigned*,unsigned*,unsigned*,unsigned*)=NULL;
 if(!gst)return 0;*(FARPROC*)&version=GetProcAddress(gst,"gst_version");if(!version)return 0;
 unsigned major=0,minor=0,micro=0,nano=0;version(&major,&minor,&micro,&nano);logline("HOST_GSTREAMER version=%u.%u.%u",major,minor,micro);return major==1&&minor>=18;
}
static int native_host_supported(void){
 wchar_t path[32768];DWORD n=GetModuleFileNameW(NULL,path,32768);if(!n||n>=32768)return 0;
 int r=native_host_check(path);native_host_verified=r==1;
 if(r==0){logline("HOST_REFUSED reason=not_avermedia_signed");return 0;}
 if(!native_gst_ok()){logline("HOST_REFUSED reason=gstreamer_version");return 0;}
 if(r==2)logline("HOST_UNVERIFIED signer=%ls (not the tested 1.8.22.2 build; GC573 SDK adapter may fall back to capture caps)",native_host_signer);
 return 1;
}
