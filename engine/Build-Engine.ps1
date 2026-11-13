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
 & $cmake -S $PSScriptRoot -B $build -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$PSScriptRoot/zig-toolchain.cmake" "-DCMAKE_MAKE_PROGRAM=$ninja" '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_POLICY_VERSION_MINIMUM=3.5'
 if($LASTEXITCODE){throw 'CMake configure failed'}
 & $cmake --build $build --parallel 6
 if($LASTEXITCODE){throw 'Build failed'}
}finally{$env:ZIG_GLOBAL_CACHE_DIR=$saved}
# Stage: engine DLL, RIFE 4.6 model, FSR 3 notice, licenses, file manifest.
$dest=Join-Path $root 'build\stage\engine'
if(Test-Path -LiteralPath $dest){Remove-Item -LiteralPath $dest -Recurse -Force}
New-Item -ItemType Directory -Path (Join-Path $dest 'model'),(Join-Path $dest 'engines\fsr3') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'consolesmooth_engine.dll') -Destination $dest
foreach($file in @('flownet.param','flownet.bin')){
 Copy-Item -LiteralPath (Join-Path $root "third_party\rife-ncnn-vulkan\models\rife-v4.6\$file") -Destination (Join-Path $dest 'model')
}
Copy-Item -LiteralPath (Join-Path $root 'third_party\fidelityfx-sdk-1.1.4\LICENSE.txt') -Destination (Join-Path $dest 'engines\fsr3\LICENSE-FidelityFX.txt')
$files=@(Get-ChildItem $dest -Recurse -File | Where-Object {$_.Extension -in @('.dll','.bin','.param')})
@($files|ForEach-Object{[ordered]@{file=$_.FullName.Substring($dest.Length+1);sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $dest 'manifest.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination $dest
foreach($notice in @('THIRD_PARTY_NOTICES.txt','source-manifest.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $notice) -Destination $dest}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'licenses') -Destination $dest -Recurse
Get-FileHash -LiteralPath (Join-Path $dest 'consolesmooth_engine.dll') -Algorithm SHA256
