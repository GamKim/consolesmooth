/* SPDX-License-Identifier: MIT
 * 2.3.13 (11-19: HDR control in the full screen; the driver's RTX Video HDR, nothing to download):
 * the full-screen window can show the picture as HDR10 through NVIDIA RTX Video HDR (TrueHDR). The driver applies
 * it in the D3D11 video processor when the stream extension below is set (same call as mpv's d3d11vpp
 * nvidia-true-hdr and VLC: GUID fdd62bb4-..., {version 4, method 3, enable 1}). Output: R10G10B10A2, PQ / BT.2020.
 * - Only NVIDIA RTX GPUs; Windows HDR must be on for the monitor. When the setting is on and the monitor is SDR,
 *   Windows HDR is turned on while the full screen is open and turned off again when it closes (hdr_restore).
 * - Before use, a white test picture is processed and read back: if the driver did not convert it (passthrough would
 *   show SDR code values as PQ, up to 10000 nits), HDR stays off for this window. The first calls can pass through
 *   while the driver loads the model, so the check retries for up to 3 s.
 * - Recording and streaming are not touched (the host's frames stay SDR).
 * Measured 11-19 (RTX 5080, test thdr.c): 1080p BGRA in, white 255 -> about 650 nits, gray 128 -> 50 nits, 1.75 ms.
 */
#include <d3d11_1.h>
static const GUID cs_guid_truehdr={0xfdd62bb4,0x620b,0x4fd7,{0x9a,0xb3,0x1e,0x59,0xd0,0xd5,0x44,0xb3}};
static const IID cs_iid_video_device={0x10ec4d5b,0x975a,0x4689,{0xb9,0xe4,0xd0,0xaa,0xc3,0x0f,0xe3,0x33}};
static const IID cs_iid_video_context={0x61f21c45,0x3c0e,0x4a74,{0x9c,0xea,0x67,0x10,0x0d,0x9a,0xd5,0xe4}};
static const IID cs_iid_video_context1={0xa7f026da,0xa5f8,0x4487,{0xa5,0x64,0x15,0xe3,0x43,0x57,0x65,0x1e}};
static const IID cs_iid_swapchain3={0x94d99bdb,0xf1f8,0x4ab0,{0xb2,0x36,0x7d,0xa0,0x17,0x0e,0xda,0xb1}};
static const IID cs_iid_output6={0x068346e8,0xaaec,0x4b84,{0xad,0xd7,0x13,0x7f,0x51,0x3f,0x77,0xa1}};
typedef struct {unsigned version,method;unsigned enable:1,reserved:31;} CsTrueHdr;
static int hdr_on,hdr_restore_win;static LUID hdr_target_adapter;static UINT32 hdr_target_id;
static ID3D11VideoDevice *hdr_vd;static ID3D11VideoContext *hdr_vc;static ID3D11VideoProcessorEnumerator *hdr_en;static ID3D11VideoProcessor *hdr_vp;
static struct {ID3D11Resource *res;ID3D11VideoProcessorInputView *iv;} hdr_iv[12];static ID3D11Texture2D *hdr_cpu_tex;
/* 2.3.13: the video processor writes an own HDR10 texture; the readout label is drawn there and the result copied to the
 * back buffer. Drawing into the back buffer with a render target view made every later Blt fail (11-19, frozen window). */
static ID3D11Texture2D *hdr_mid;static ID3D11RenderTargetView *hdr_mid_rtv;static ID3D11VideoProcessorOutputView *hdr_mid_ov; /* made once: after a draw into hdr_mid, creating a new output view failed with E_INVALIDARG (11-19) */
static int hdr_out_w,hdr_out_h;
/* readout in HDR: see gv_hdr_src_label (gpu_view.h). Tried 11-19 and dropped: a 3D draw next to the TrueHDR Blt removed the
 * device (887a0005); the label as stream 1 of the TrueHDR Blt switched RTX Video HDR off; a second plain video processor
 * for the label hung the first present. */

/* Windows HDR ("advanced color") state of the monitor's target through the DisplayConfig API. set: -1 query only. */
typedef struct {DISPLAYCONFIG_DEVICE_INFO_HEADER header;UINT32 value,colorEncoding,bitsPerColorChannel;} CsAdvColorInfo;
typedef struct {DISPLAYCONFIG_DEVICE_INFO_HEADER header;UINT32 enable;} CsAdvColorSet;
static int hdr_windows(HMONITOR mon,int set){
 MONITORINFOEXW mi={.cbSize=sizeof mi};if(!GetMonitorInfoW(mon,(MONITORINFO*)&mi))return -1;
 UINT32 np=0,nm=0;if(GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&np,&nm)!=ERROR_SUCCESS||!np)return -1;
 DISPLAYCONFIG_PATH_INFO *paths=(DISPLAYCONFIG_PATH_INFO*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,np*sizeof *paths);
 DISPLAYCONFIG_MODE_INFO *modes=(DISPLAYCONFIG_MODE_INFO*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,nm*sizeof *modes);int r=-1;
 if(paths&&modes&&QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&np,paths,&nm,modes,NULL)==ERROR_SUCCESS){
  for(UINT32 i=0;i<np&&r<0;i++){
   DISPLAYCONFIG_SOURCE_DEVICE_NAME sn={0};sn.header.type=DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;sn.header.size=sizeof sn;sn.header.adapterId=paths[i].sourceInfo.adapterId;sn.header.id=paths[i].sourceInfo.id;
   if(DisplayConfigGetDeviceInfo(&sn.header)!=ERROR_SUCCESS||lstrcmpiW(sn.viewGdiDeviceName,mi.szDevice))continue;
   CsAdvColorInfo ai={0};ai.header.type=(DISPLAYCONFIG_DEVICE_INFO_TYPE)9;ai.header.size=sizeof ai;ai.header.adapterId=paths[i].targetInfo.adapterId;ai.header.id=paths[i].targetInfo.id;
   if(DisplayConfigGetDeviceInfo(&ai.header)!=ERROR_SUCCESS)continue;
   int supported=ai.value&1,enabled=(ai.value>>1)&1;hdr_target_adapter=ai.header.adapterId;hdr_target_id=ai.header.id;
   if(set<0||!supported||enabled==set){r=supported?enabled:0;if(set>=0&&!supported)r=-2;continue;}
   CsAdvColorSet as={0};as.header.type=(DISPLAYCONFIG_DEVICE_INFO_TYPE)10;as.header.size=sizeof as;as.header.adapterId=ai.header.adapterId;as.header.id=ai.header.id;as.enable=(UINT32)set;
   r=DisplayConfigSetDeviceInfo(&as.header)==ERROR_SUCCESS?set:-1;logline("HDR_WINDOWS set=%d result=%d",set,r);
  }
 }
 if(paths)HeapFree(GetProcessHeap(),0,paths);if(modes)HeapFree(GetProcessHeap(),0,modes);return r;
}
/* Back to SDR if this window turned Windows HDR on. */
static void hdr_restore(void){
 if(!hdr_restore_win)return;hdr_restore_win=0;
 CsAdvColorSet as={0};as.header.type=(DISPLAYCONFIG_DEVICE_INFO_TYPE)10;as.header.size=sizeof as;as.header.adapterId=hdr_target_adapter;as.header.id=hdr_target_id;as.enable=0;
 LONG e=DisplayConfigSetDeviceInfo(&as.header);logline("HDR_WINDOWS restore=%ld",e);
}
static void hdr_release(void){
 for(int i=0;i<12;i++){if(hdr_iv[i].iv)ID3D11VideoProcessorInputView_Release(hdr_iv[i].iv);hdr_iv[i].iv=NULL;hdr_iv[i].res=NULL;}
 if(hdr_cpu_tex){ID3D11Texture2D_Release(hdr_cpu_tex);hdr_cpu_tex=NULL;}
 if(hdr_mid_ov){ID3D11VideoProcessorOutputView_Release(hdr_mid_ov);hdr_mid_ov=NULL;}if(hdr_mid_rtv){ID3D11RenderTargetView_Release(hdr_mid_rtv);hdr_mid_rtv=NULL;}if(hdr_mid){ID3D11Texture2D_Release(hdr_mid);hdr_mid=NULL;}
 if(hdr_vp){ID3D11VideoProcessor_Release(hdr_vp);hdr_vp=NULL;}if(hdr_en){ID3D11VideoProcessorEnumerator_Release(hdr_en);hdr_en=NULL;}
 if(hdr_vc){ID3D11VideoContext_Release(hdr_vc);hdr_vc=NULL;}if(hdr_vd){ID3D11VideoDevice_Release(hdr_vd);hdr_vd=NULL;}
 hdr_on=0;hdr_restore();
}
static ID3D11VideoProcessorInputView *hdr_input(ID3D11Resource *res){
 for(int i=0;i<12;i++)if(hdr_iv[i].res==res&&hdr_iv[i].iv)return hdr_iv[i].iv;
 D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC d={0};d.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;ID3D11VideoProcessorInputView *iv=NULL;
 {HRESULT h=ID3D11VideoDevice_CreateVideoProcessorInputView(hdr_vd,res,hdr_en,&d,&iv);static int n;if(h<0){if(n++<5)logline("HDR_IV_FAILED hr=%08lx",(unsigned long)h);return NULL;}}
 for(int i=0;i<12;i++)if(!hdr_iv[i].iv){hdr_iv[i].res=res;hdr_iv[i].iv=iv;return iv;}
 ID3D11VideoProcessorInputView_Release(hdr_iv[0].iv);hdr_iv[0].res=res;hdr_iv[0].iv=iv;return iv;
}
static int hdr_blt_ov(ID3D11Resource *src,ID3D11VideoProcessorOutputView *ov){
 ID3D11VideoProcessorInputView *iv=hdr_input(src);if(!iv)return 0;
 D3D11_VIDEO_PROCESSOR_STREAM st[1]={{0}};st[0].Enable=TRUE;st[0].pInputSurface=iv;
 HRESULT hr=ID3D11VideoContext_VideoProcessorBlt(hdr_vc,hdr_vp,ov,0,1,st);{static int n;if(hr<0&&n++<5)logline("HDR_BLT_FAILED hr=%08lx",(unsigned long)hr);}return hr>=0;
}
static int hdr_blt_to(ID3D11Resource *src,ID3D11Resource *dst){
 ID3D11VideoProcessorInputView *iv=hdr_input(src);if(!iv)return 0;
 D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od={0};od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;ID3D11VideoProcessorOutputView *ov=NULL;
 {HRESULT h=ID3D11VideoDevice_CreateVideoProcessorOutputView(hdr_vd,dst,hdr_en,&od,&ov);static int n;if(h<0){if(n++<5)logline("HDR_OV_FAILED hr=%08lx",(unsigned long)h);return 0;}}
 D3D11_VIDEO_PROCESSOR_STREAM st={0};st.Enable=TRUE;st.pInputSurface=iv;
 HRESULT hr=ID3D11VideoContext_VideoProcessorBlt(hdr_vc,hdr_vp,ov,0,1,&st);ID3D11VideoProcessorOutputView_Release(ov);{static int n;if(hr<0&&n++<5)logline("HDR_BLT_FAILED hr=%08lx",(unsigned long)hr);}return hr>=0;
}
/* Present path: process src into the current back buffer. */
static int hdr_blt(ID3D11Resource *src){
 if(!hdr_mid){D3D11_TEXTURE2D_DESC td={.Width=(UINT)hdr_out_w,.Height=(UINT)hdr_out_h,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_R10G10B10A2_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_RENDER_TARGET};
  if(ID3D11Device_CreateTexture2D(direct_dev,&td,NULL,&hdr_mid)<0)return 0;if(ID3D11Device_CreateRenderTargetView(direct_dev,(ID3D11Resource*)hdr_mid,NULL,&hdr_mid_rtv)<0)return 0;
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od={0};od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;
  HRESULT h=ID3D11VideoDevice_CreateVideoProcessorOutputView(hdr_vd,(ID3D11Resource*)hdr_mid,hdr_en,&od,&hdr_mid_ov);if(h<0){logline("HDR_OV_FAILED hr=%08lx",(unsigned long)h);return 0;}}
 return hdr_mid_ov?hdr_blt_ov(src,hdr_mid_ov):0;
}
/* After hdr_blt (and any label drawn into hdr_mid_rtv): copy the HDR picture to the back buffer. */
static int hdr_finish(void){
 ID3D11DeviceContext_OMSetRenderTargets(direct_ctx,0,NULL,NULL);
 ID3D11Texture2D *back=NULL;if(IDXGISwapChain1_GetBuffer(direct_chain,0,&cs_iid_texture,(void**)&back)<0)return 0;
 ID3D11DeviceContext_CopyResource(direct_ctx,(ID3D11Resource*)back,(ID3D11Resource*)hdr_mid);ID3D11Texture2D_Release(back);return 1;
}
/* After the device exists (created with video support), before the swap chain: 1 = HDR output for this window. */
static int hdr_setup(int in_w,int in_h,int out_w,int out_h){
 hdr_on=0;hdr_out_w=out_w;hdr_out_h=out_h;
 if(ID3D11Device_QueryInterface(direct_dev,&cs_iid_video_device,(void**)&hdr_vd)<0){logline("HDR_NO_VIDEO_DEVICE");return 0;}
 ID3D11DeviceContext_QueryInterface(direct_ctx,&cs_iid_video_context,(void**)&hdr_vc);ID3D11VideoContext1 *vc1=NULL;
 if(!hdr_vc||ID3D11VideoContext_QueryInterface(hdr_vc,&cs_iid_video_context1,(void**)&vc1)<0){logline("HDR_NO_VIDEO_CONTEXT1");hdr_release();return 0;}
 D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd={0};cd.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;cd.InputFrameRate.Numerator=60;cd.InputFrameRate.Denominator=1;
 cd.InputWidth=(UINT)in_w;cd.InputHeight=(UINT)in_h;cd.OutputFrameRate=cd.InputFrameRate;cd.OutputWidth=(UINT)out_w;cd.OutputHeight=(UINT)out_h;cd.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
 if(ID3D11VideoDevice_CreateVideoProcessorEnumerator(hdr_vd,&cd,&hdr_en)<0||ID3D11VideoDevice_CreateVideoProcessor(hdr_vd,hdr_en,0,&hdr_vp)<0){logline("HDR_VP_FAILED");ID3D11VideoContext1_Release(vc1);hdr_release();return 0;}
 ID3D11VideoContext1_VideoProcessorSetStreamColorSpace1(vc1,hdr_vp,0,DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
 ID3D11VideoContext1_VideoProcessorSetOutputColorSpace1(vc1,hdr_vp,DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
 ID3D11VideoContext1_Release(vc1);
 UINT sup=0;HRESULT hr=ID3D11VideoContext_VideoProcessorGetStreamExtension(hdr_vc,hdr_vp,0,&cs_guid_truehdr,sizeof sup,&sup);
 if(hr<0||!sup){logline("HDR_TRUEHDR_UNSUPPORTED hr=%08lx",(unsigned long)hr);hdr_release();return 0;}
 CsTrueHdr e={4,3,1,0};hr=ID3D11VideoContext_VideoProcessorSetStreamExtension(hdr_vc,hdr_vp,0,&cs_guid_truehdr,sizeof e,&e);
 if(hr<0){logline("HDR_TRUEHDR_SET_FAILED hr=%08lx",(unsigned long)hr);hdr_release();return 0;}
 /* white test picture -> check the centre pixel is below full PQ (converted, not passed through) */
 D3D11_TEXTURE2D_DESC td={.Width=(UINT)in_w,.Height=(UINT)in_h,.MipLevels=1,.ArraySize=1,.Format=DXGI_FORMAT_B8G8R8A8_UNORM,.SampleDesc={1,0},.Usage=D3D11_USAGE_DEFAULT,.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET};
 ID3D11Texture2D *white=NULL,*out=NULL,*stage=NULL;ID3D11RenderTargetView *wr=NULL;
 if(ID3D11Device_CreateTexture2D(direct_dev,&td,NULL,&white)>=0&&ID3D11Device_CreateRenderTargetView(direct_dev,(ID3D11Resource*)white,NULL,&wr)>=0){float one[4]={1,1,1,1};ID3D11DeviceContext_ClearRenderTargetView(direct_ctx,wr,one);ID3D11RenderTargetView_Release(wr);}
 D3D11_TEXTURE2D_DESC od=td;od.Width=(UINT)out_w;od.Height=(UINT)out_h;od.Format=DXGI_FORMAT_R10G10B10A2_UNORM;od.BindFlags=D3D11_BIND_RENDER_TARGET;ID3D11Device_CreateTexture2D(direct_dev,&od,NULL,&out);
 D3D11_TEXTURE2D_DESC sd=od;sd.Width=sd.Height=1;sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Device_CreateTexture2D(direct_dev,&sd,NULL,&stage);
 unsigned code=1023;int tries=0;
 if(white&&out&&stage)for(;tries<30;tries++){
  if(!hdr_blt_to((ID3D11Resource*)white,(ID3D11Resource*)out))break;
  D3D11_BOX box={(UINT)out_w/2,(UINT)out_h/2,0,(UINT)out_w/2+1,(UINT)out_h/2+1,1};ID3D11DeviceContext_CopySubresourceRegion(direct_ctx,(ID3D11Resource*)stage,0,0,0,0,(ID3D11Resource*)out,0,&box);
  D3D11_MAPPED_SUBRESOURCE m;if(ID3D11DeviceContext_Map(direct_ctx,(ID3D11Resource*)stage,0,D3D11_MAP_READ,0,&m)>=0){code=(*(unsigned*)m.pData)&1023;ID3D11DeviceContext_Unmap(direct_ctx,(ID3D11Resource*)stage,0);}
  if(code<1000)break;Sleep(100);
 }
 for(int i=0;i<12;i++)if(hdr_iv[i].res==(ID3D11Resource*)white){ID3D11VideoProcessorInputView_Release(hdr_iv[i].iv);hdr_iv[i].iv=NULL;hdr_iv[i].res=NULL;}
 if(white)ID3D11Texture2D_Release(white);if(out)ID3D11Texture2D_Release(out);if(stage)ID3D11Texture2D_Release(stage);
 logline("HDR_TRUEHDR_CHECK white_code=%u tries=%d",code,tries);
 if(code>=1000){hdr_release();return 0;}
 hdr_on=1;return 1;
}
/* Is Windows HDR on for this monitor according to DXGI (PQ output)? */
static int hdr_output_is_hdr(IDXGIAdapter1 *adapter,HMONITOR mon){
 IDXGIOutput *o=NULL;int r=0;
 for(UINT i=0;IDXGIAdapter1_EnumOutputs(adapter,i,&o)==S_OK;i++){DXGI_OUTPUT_DESC d;IDXGIOutput_GetDesc(o,&d);
  if(d.Monitor==mon){IDXGIOutput6 *o6=NULL;if(IDXGIOutput_QueryInterface(o,&cs_iid_output6,(void**)&o6)>=0){DXGI_OUTPUT_DESC1 d1;if(IDXGIOutput6_GetDesc1(o6,&d1)>=0)r=d1.ColorSpace==DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;IDXGIOutput6_Release(o6);}}
  IDXGIOutput_Release(o);}
 return r;
}
