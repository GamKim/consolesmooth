/* 1.9.3: UI language. English unless Streaming Center itself runs in Korean: SC keeps its own UI language in
 * HKCU\Software\AVerMedia\StreamingCenter\Global\AppLanguage ("ko_KR" on a Korean SC). Without that value the
 * Windows UI language decides. CS_TEST_LANG=ko|en overrides (own checks only). Read once, on first use. */
#ifndef CS_UI_LANG_H
#define CS_UI_LANG_H
#include <windows.h>
#include <wchar.h>
static volatile LONG cs_ui_lang_state; /* 0 unknown, 1 Korean, 2 English */
static int cs_ui_english(void){
 LONG s=cs_ui_lang_state;if(s)return s==2;
 int korean=-1;wchar_t value[32];DWORD size=sizeof value;
 DWORD n=GetEnvironmentVariableW(L"CS_TEST_LANG",value,32);
 if(n&&n<32)korean=!_wcsnicmp(value,L"ko",2);
 else if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\AVerMedia\\StreamingCenter\\Global",L"AppLanguage",RRF_RT_REG_SZ,NULL,value,&size)==ERROR_SUCCESS&&value[0])
  korean=!_wcsnicmp(value,L"ko",2);
 if(korean<0)korean=PRIMARYLANGID(GetUserDefaultUILanguage())==LANG_KOREAN;
 InterlockedExchange(&cs_ui_lang_state,korean?1:2);return !korean;
}
static const wchar_t *ui_tr(const wchar_t *ko,const wchar_t *en){return cs_ui_english()?en:ko;}
#endif
