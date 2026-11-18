/* SPDX-License-Identifier: MIT
 * 11-16 night, TEST ONLY (GPU path, CS_TEST_GPU_GEN=1). Basic HUD keep on the host's D3D11 device:
 *  - count pass (compute, each new source): per pixel, a run of sources whose colour stays within TOL of the run's
 *    first colour (same rule as the CPU keep, hudTol 8); count saturates at 255;
 *  - map pass (compute, each new source): pixel is HUD when any pixel within 2 px has run >= 30 sources;
 *  - compose (pixel shader, generated ticks): map ? newest original : generated, drawn straight into the output buffer.
 * Recorded on a deferred context and executed with RestoreContextState=TRUE so the host's pipeline state is untouched.
 * Not ported from the CPU keep: semi-transparent attenuation, near-density rule, hudAttMin (follow-up).
 */
#include <d3dcompiler.h>
typedef HRESULT (WINAPI *PFN_D3DCompile_)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,ID3DBlob**,ID3DBlob**);
static const char gh_src[]=
"Texture2D<float4> Src:register(t0);RWTexture2D<uint> Ref:register(u0);RWTexture2D<uint> Cnt:register(u1);\n"
"cbuffer P:register(b0){uint W;uint H;uint TOL;uint RUN;};\n"
"[numthreads(16,16,1)]void cs_count(uint3 id:SV_DispatchThreadID){if(id.x>=W||id.y>=H)return;\n"
" uint3 v=(uint3)(Src[id.xy].rgb*255.0+0.5);uint r=Ref[id.xy];int3 d=abs((int3)v-int3(r&255,(r>>8)&255,(r>>16)&255));\n"
" if(max(d.x,max(d.y,d.z))>(int)TOL){Ref[id.xy]=v.x|(v.y<<8)|(v.z<<16);Cnt[id.xy]=0;}else Cnt[id.xy]=min(Cnt[id.xy]+1,255);}\n"
"Texture2D<uint> CntR:register(t1);RWTexture2D<uint> Map:register(u2);\n"
"[numthreads(16,16,1)]void cs_map(uint3 id:SV_DispatchThreadID){if(id.x>=W||id.y>=H)return;uint m=0;\n"
" [unroll]for(int dy=-2;dy<=2;dy++)[unroll]for(int dx=-2;dx<=2;dx++){int2 p=clamp(int2(id.xy)+int2(dx,dy),int2(0,0),int2(W-1,H-1));m|=CntR[p]>=RUN?1u:0u;}\n"
" Map[id.xy]=m;}\n"
"Texture2D<float4> Gen:register(t2);Texture2D<float4> Org:register(t3);Texture2D<uint> MapR:register(t4);\n"
"float4 vs_full(uint i:SV_VertexID):SV_Position{float2 t=float2((i<<1)&2,i&2);return float4(t*float2(2,-2)+float2(-1,1),0,1);}\n"
"float4 ps_compose(float4 p:SV_Position):SV_Target{int2 q=int2(p.xy);return MapR[q]?Org[q]:Gen[q];}\n";
typedef struct {
 int ok,tried;ID3D11ComputeShader *cnt,*map;ID3D11VertexShader *vs;ID3D11PixelShader *ps;ID3D11Buffer *cb;
 ID3D11Texture2D *ref,*count,*mapt;ID3D11UnorderedAccessView *uref,*ucount,*umap;ID3D11ShaderResourceView *scount,*smap,*ssrc[5];
 ID3D11DeviceContext *dc;unsigned w,h;unsigned long long sources;double ms;unsigned msn;
} GpuHud;
static int gh_blob(PFN_D3DCompile_ cf,const char *entry,const char *target,ID3DBlob **out){
 ID3DBlob *err=NULL;HRESULT r=cf(gh_src,sizeof gh_src-1,"gpu_hud",NULL,NULL,entry,target,0,0,out,&err);
 if(FAILED(r)){logline("GPU_HUD compile %s failed %s",entry,err?(const char*)err->lpVtbl->GetBufferPointer(err):"");if(err)err->lpVtbl->Release(err);return 0;}
 if(err)err->lpVtbl->Release(err);return 1;
}
static int gh_setup(GpuHud *h,ID3D11Device *dev,ID3D11Texture2D **srcTex,unsigned w,unsigned hh){
 h->tried=1;HMODULE m=LoadLibraryW(L"d3dcompiler_47.dll");PFN_D3DCompile_ cf=m?(PFN_D3DCompile_)(void*)GetProcAddress(m,"D3DCompile"):NULL;
 if(!cf){logline("GPU_HUD no d3dcompiler");return 0;}
 ID3DBlob *b=NULL;
 if(!gh_blob(cf,"cs_count","cs_5_0",&b))return 0;dev->lpVtbl->CreateComputeShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&h->cnt);b->lpVtbl->Release(b);
 if(!gh_blob(cf,"cs_map","cs_5_0",&b))return 0;dev->lpVtbl->CreateComputeShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&h->map);b->lpVtbl->Release(b);
 if(!gh_blob(cf,"vs_full","vs_5_0",&b))return 0;dev->lpVtbl->CreateVertexShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&h->vs);b->lpVtbl->Release(b);
 if(!gh_blob(cf,"ps_compose","ps_5_0",&b))return 0;dev->lpVtbl->CreatePixelShader(dev,b->lpVtbl->GetBufferPointer(b),b->lpVtbl->GetBufferSize(b),NULL,&h->ps);b->lpVtbl->Release(b);
 D3D11_TEXTURE2D_DESC d={0};d.Width=w;d.Height=hh;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_R32_UINT;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE;
 if(FAILED(dev->lpVtbl->CreateTexture2D(dev,&d,NULL,&h->ref))||FAILED(dev->lpVtbl->CreateTexture2D(dev,&d,NULL,&h->count))||FAILED(dev->lpVtbl->CreateTexture2D(dev,&d,NULL,&h->mapt))){logline("GPU_HUD textures failed");return 0;}
 dev->lpVtbl->CreateUnorderedAccessView(dev,(ID3D11Resource*)h->ref,NULL,&h->uref);dev->lpVtbl->CreateUnorderedAccessView(dev,(ID3D11Resource*)h->count,NULL,&h->ucount);dev->lpVtbl->CreateUnorderedAccessView(dev,(ID3D11Resource*)h->mapt,NULL,&h->umap);
 dev->lpVtbl->CreateShaderResourceView(dev,(ID3D11Resource*)h->count,NULL,&h->scount);dev->lpVtbl->CreateShaderResourceView(dev,(ID3D11Resource*)h->mapt,NULL,&h->smap);
 for(int i=0;i<5;i++)if(FAILED(dev->lpVtbl->CreateShaderResourceView(dev,(ID3D11Resource*)srcTex[i],NULL,&h->ssrc[i]))){logline("GPU_HUD srv %d failed",i);return 0;}
 unsigned c[4]={w,hh,8,30};D3D11_BUFFER_DESC bd={16,D3D11_USAGE_IMMUTABLE,D3D11_BIND_CONSTANT_BUFFER,0,0,0};D3D11_SUBRESOURCE_DATA sd={c,0,0};
 if(FAILED(dev->lpVtbl->CreateBuffer(dev,&bd,&sd,&h->cb))){logline("GPU_HUD cb failed");return 0;}
 if(FAILED(dev->lpVtbl->CreateDeferredContext(dev,0,&h->dc))){logline("GPU_HUD deferred context failed");return 0;}
 h->w=w;h->h=hh;h->ok=1;logline("GPU_HUD setup %ux%u tol=8 run=30 radius=2",w,hh);return 1;
}
/* new source in shared texture slot: count + map. Caller holds the device lock. */
static void gh_source(GpuHud *h,ID3D11DeviceContext *imm,int slot){
 if(!h->ok)return;ID3D11DeviceContext *dc=h->dc;
 ID3D11UnorderedAccessView *u[3]={h->uref,h->ucount,NULL};dc->lpVtbl->CSSetShader(dc,h->cnt,NULL,0);dc->lpVtbl->CSSetConstantBuffers(dc,0,1,&h->cb);
 dc->lpVtbl->CSSetShaderResources(dc,0,1,&h->ssrc[slot]);dc->lpVtbl->CSSetUnorderedAccessViews(dc,0,2,u,NULL);
 dc->lpVtbl->Dispatch(dc,(h->w+15)/16,(h->h+15)/16,1);
 ID3D11UnorderedAccessView *none[3]={NULL,NULL,NULL};dc->lpVtbl->CSSetUnorderedAccessViews(dc,0,3,none,NULL);
 dc->lpVtbl->CSSetShader(dc,h->map,NULL,0);dc->lpVtbl->CSSetShaderResources(dc,1,1,&h->scount);u[0]=NULL;u[1]=NULL;u[2]=h->umap;dc->lpVtbl->CSSetUnorderedAccessViews(dc,0,3,u,NULL);
 dc->lpVtbl->Dispatch(dc,(h->w+15)/16,(h->h+15)/16,1);
 ID3D11CommandList *cl=NULL;if(SUCCEEDED(dc->lpVtbl->FinishCommandList(dc,FALSE,&cl))){imm->lpVtbl->ExecuteCommandList(imm,cl,TRUE);cl->lpVtbl->Release(cl);}
 h->sources++;
}
/* generated tick: draw map ? original : generated into the output texture (rtv). Caller holds the device lock. */
static int gh_compose(GpuHud *h,ID3D11DeviceContext *imm,ID3D11Device *dev,ID3D11Resource *ores,unsigned osub,int genSlot,int orgSlot){
 if(!h->ok||h->sources<31)return 0;
 ID3D11RenderTargetView *rtv=NULL;D3D11_RENDER_TARGET_VIEW_DESC rd={0};rd.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
 if(osub>0){rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2DARRAY;rd.Texture2DArray.FirstArraySlice=osub;rd.Texture2DArray.ArraySize=1;}else rd.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
 if(FAILED(dev->lpVtbl->CreateRenderTargetView(dev,ores,&rd,&rtv)))return 0;
 ID3D11DeviceContext *dc=h->dc;D3D11_VIEWPORT vp={0,0,(float)h->w,(float)h->h,0,1};
 dc->lpVtbl->IASetPrimitiveTopology(dc,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);dc->lpVtbl->IASetInputLayout(dc,NULL);
 dc->lpVtbl->VSSetShader(dc,h->vs,NULL,0);dc->lpVtbl->PSSetShader(dc,h->ps,NULL,0);
 ID3D11ShaderResourceView *s[3]={h->ssrc[genSlot],h->ssrc[orgSlot],h->smap};dc->lpVtbl->PSSetShaderResources(dc,2,3,s);
 dc->lpVtbl->RSSetViewports(dc,1,&vp);dc->lpVtbl->OMSetRenderTargets(dc,1,&rtv,NULL);dc->lpVtbl->Draw(dc,3,0);
 ID3D11CommandList *cl=NULL;int ok=SUCCEEDED(dc->lpVtbl->FinishCommandList(dc,FALSE,&cl));if(ok){imm->lpVtbl->ExecuteCommandList(imm,cl,TRUE);cl->lpVtbl->Release(cl);}
 rtv->lpVtbl->Release(rtv);return ok;
}
static void gh_free(GpuHud *h){
#define GH_REL(x) do{if(x){(x)->lpVtbl->Release(x);(x)=NULL;}}while(0)
 GH_REL(h->cnt);GH_REL(h->map);GH_REL(h->vs);GH_REL(h->ps);GH_REL(h->cb);GH_REL(h->uref);GH_REL(h->ucount);GH_REL(h->umap);GH_REL(h->scount);GH_REL(h->smap);
 for(int i=0;i<5;i++)GH_REL(h->ssrc[i]);GH_REL(h->ref);GH_REL(h->count);GH_REL(h->mapt);GH_REL(h->dc);
}
