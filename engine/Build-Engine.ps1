# Builds consolesmooth_engine.dll and the checks, then stages the engine folder the installer ships into <repo>\build\stage\engine.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$cmake=Join-Path $root 'toolchain\cmake-4.4.5-windows-x86_64\bin\cmake.exe'
$ninja=Join-Path $root 'toolchain\ninja\ninja.exe'
if(!(Test-Path -LiteralPath $cmake)){throw 'Run tools\Get-Toolchain.ps1 first.'}
$saved=$env:ZIG_GLOBAL_CACHE_DIR
try {
 $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'toolchain\cache'
 $build=Join-Path $root 'build\engine'
 $internal=if($env:CS_INTERNAL -eq '1'){'-DCS_INTERNAL_VFG=ON'}else{'-DCS_INTERNAL_VFG=OFF'}  # 2.2.8: NVIDIA VFG slot in internal builds only
 & $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/zig-toolchain.cmake" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5' $internal
 if($LASTEXITCODE){throw 'CMake configure failed'}
 & $cmake --build $build --parallel 6
 if($LASTEXITCODE){throw 'Build failed'}
}finally{$env:ZIG_GLOBAL_CACHE_DIR=$saved}
# Stage: engine DLL, RIFE 4.6 + 4.15 lite models, FSR 3 notice, licenses, file manifest.
$dest=Join-Path $root 'build\stage\engine'
if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Recurse -Force}
New-Item -ItemType Directory -Path (Join-Path $dest 'model'),(Join-Path $dest 'rife415lite'),(Join-Path $dest 'engines\fsr3') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'consolesmooth_engine.dll') -Destination $dest
foreach($file in @('flownet.param','flownet.bin')){
 Copy-Item -LiteralPath (Join-Path $root "third_party\rife-ncnn-vulkan\models\rife-v4.6\$file") -Destination (Join-Path $dest 'model')
 Copy-Item -LiteralPath (Join-Path $root "third_party\rife-ncnn-vulkan\models\rife-v4.15-lite\$file") -Destination (Join-Path $dest 'rife415lite')
}
Copy-Item -LiteralPath (Join-Path $root 'third_party\fidelityfx-sdk-1.1.4\LICENSE.txt') -Destination (Join-Path $dest 'engines\fsr3\LICENSE-FidelityFX.txt')
# 1.1.0: the two bridge DLLs need their SDK sources outside this repository (CS_DLSSG_SRC / CS_AMDFG_SRC); without them
# those two models are skipped and the rest builds normally.
$dsrc0=if($env:CS_DLSSG_SRC){$env:CS_DLSSG_SRC}else{'E:\fpstest\dlssg-ngx'}
if(-not (Test-Path (Join-Path $dsrc0 'dlssg_ngx.cpp'))){Write-Warning "DLSS FG bridge source not found ($dsrc0): DLSS FG is left out"}else{ & { # DLSS FG slot. cs_dlssg.dll = our NGX DLSSG wrapper (public DLSS SDK headers stay outside this tree).
 $dsrc=if($env:CS_DLSSG_SRC){$env:CS_DLSSG_SRC}else{'E:\fpstest\dlssg-ngx'};$zig=Join-Path $root 'toolchain\zig-x86_64-windows-0.14.1\zig.exe'
 New-Item -ItemType Directory -Path (Join-Path $dest 'engines\dlssg') -Force|Out-Null
 $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'toolchain\cache'
 & $zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared -Wno-nonportable-include-path -I (Join-Path $dsrc 'sdk\include') (Join-Path $dsrc 'dlssg_ngx.cpp') -o (Join-Path $dest 'engines\dlssg\cs_dlssg.dll') -ld3d12 -ldxgi -ladvapi32 -lpsapi
 $env:ZIG_GLOBAL_CACHE_DIR=$saved
 if($LASTEXITCODE){throw 'cs_dlssg build failed'}
 Get-ChildItem (Join-Path $dest 'engines\dlssg') -Filter '*.pdb'|Remove-Item;Get-ChildItem (Join-Path $dest 'engines\dlssg') -Filter '*.lib'|Remove-Item
 # 2.3.4: nvngx_dlssg.dll (NVIDIA-signed, public Streamline/DLSS SDK 310.9.16) is bundled in the installer under the NVIDIA RTX SDK
 # license (copied beside it). It is never part of the source repository (build/ and dist/ are ignored).
 $nsrc=if($env:CS_DLSSG_RUNTIME_SRC){$env:CS_DLSSG_RUNTIME_SRC}else{'E:\fpstest\ext\sl-sdk\bin\x64'}
 Copy-Item -LiteralPath (Join-Path $nsrc 'nvngx_dlssg.dll') -Destination (Join-Path $dest 'engines\dlssg')
 Copy-Item -LiteralPath (Join-Path $nsrc 'nvngx_dlss.license.txt') -Destination (Join-Path $dest 'engines\dlssg\LICENSE-NVIDIA-RTX-SDK.txt')
 foreach($f in @('NOTICE-NVIDIA-DLSSG.txt','Get-NvidiaDlssg.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot "dlssg\$f") -Destination (Join-Path $dest 'engines\dlssg')}
} }
# 2.3.2: the extra RIFE models (4.17-4.26) are no longer shipped (11-18 user: keep only the filmed models).
# 2.3.3: AMD FSR FG and the NVIDIA VFG notice are staged in public builds too (DLSS FG is staged above, 1.1.0: every build).
$zig=Join-Path $root 'toolchain\zig-x86_64-windows-0.14.1\zig.exe';$saved=$env:ZIG_GLOBAL_CACHE_DIR
$asrc0=if($env:CS_AMDFG_SRC){$env:CS_AMDFG_SRC}else{'E:\fpstest\fsr31-dx12'}
if(-not (Test-Path (Join-Path $asrc0 'fsr31_dx12.cpp'))){Write-Warning "AMD FSR FG bridge source not found ($asrc0): AMD FSR FG is left out"}
& {
 if(-not (Test-Path (Join-Path $asrc0 'fsr31_dx12.cpp'))){return}
 # 2.2.8 internal: AMD FSR FG slot (our cs_amdfg.dll + AMD's signed DLLs and license, unmodified)
 $asrc=if($env:CS_AMDFG_SRC){$env:CS_AMDFG_SRC}else{'E:\fpstest\fsr31-dx12'}
 New-Item -ItemType Directory -Path (Join-Path $dest 'engines\amdfg') -Force|Out-Null
 $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'toolchain\cache'
 & $zig c++ -target x86_64-windows-gnu -O2 -std=c++17 -shared -Wno-nonportable-include-path -Wno-deprecated-declarations -I (Join-Path $asrc 'sdk\Kits\FidelityFX\api\include') -I (Join-Path $asrc 'sdk\Kits\FidelityFX\framegeneration\include') (Join-Path $asrc 'fsr31_dx12.cpp') -o (Join-Path $dest 'engines\amdfg\cs_amdfg.dll') -ld3d12 -ldxgi -lpsapi
 $env:ZIG_GLOBAL_CACHE_DIR=$saved
 if($LASTEXITCODE){throw 'cs_amdfg build failed'}
 Get-ChildItem (Join-Path $dest 'engines\amdfg') -Include '*.pdb','*.lib' -Recurse|Remove-Item
 foreach($f in @('amd_fidelityfx_framegeneration_dx12.dll','amd_fidelityfx_loader_dx12.dll','LICENSE.md','3rdpartynotice.md')){Copy-Item -LiteralPath (Join-Path $asrc "rt\$f") -Destination (Join-Path $dest 'engines\amdfg')}
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'amdfg\NOTICE-AMD-FG.txt') -Destination (Join-Path $dest 'engines\amdfg')
 New-Item -ItemType Directory -Path (Join-Path $dest 'engines\nvvfg') -Force|Out-Null;foreach($f in @('NOTICE-NVIDIA-VFG.txt','Get-NvidiaVfg.ps1')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot "nvvfg\$f") -Destination (Join-Path $dest 'engines\nvvfg')}}
$files=@(Get-ChildItem $dest -Recurse -File | Where-Object {$_.Extension -in @('.dll','.bin','.param')})
@($files|ForEach-Object{[ordered]@{file=$_.FullName.Substring($dest.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $dest 'manifest.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $dest
foreach($notice in @('THIRD_PARTY_NOTICES.txt','source-manifest.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $notice) -Destination $dest}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $dest -Recurse
Get-FileHash -LiteralPath (Join-Path $dest 'consolesmooth_engine.dll') -Algorithm SHA256
