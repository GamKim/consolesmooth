# Builds the Streaming Center plugin (GStreamer tracer) into <repo>\build\stage\gstconsolesmoothprobe.dll.
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$zig=Join-Path $root 'toolchain\zig-x86_64-windows-0.14.1\zig.exe'
if(!(Test-Path -LiteralPath $zig)){throw 'Run tools\Get-Toolchain.ps1 first.'}
$stage=Join-Path $root 'build\stage'
New-Item -ItemType Directory -Force $stage | Out-Null
$out=Join-Path $stage 'gstconsolesmoothprobe.dll'
$saved=$env:ZIG_GLOBAL_CACHE_DIR
try {
 $env:ZIG_GLOBAL_CACHE_DIR=Join-Path $root 'toolchain\cache'
 $internal=@('-DCS_INTERNAL_DLSSG=1');if($env:CS_INTERNAL -eq '1'){$internal=@('-DCS_INTERNAL_VFG=1','-DCS_INTERNAL_DLSSG=1')} # 2.3.4: DLSS FG in every build  # 2.2.8 internal builds only
 & $zig cc -target x86_64-windows-gnu -shared -O2 -Wall -Wextra -Werror @internal (Join-Path $PSScriptRoot 'tracer.c') -o $out -lkernel32 -luser32 -lgdi32 -lcomctl32 -luxtheme -lole32 -lbcrypt -lwintrust -lcrypt32 -lcomdlg32 -ladvapi32 -lshell32
 if($LASTEXITCODE -ne 0){throw "Compiler exited with $LASTEXITCODE"}
}finally{$env:ZIG_GLOBAL_CACHE_DIR=$saved}
Get-ChildItem $stage -File | Where-Object {$_.Name -ne 'gstconsolesmoothprobe.dll'} | Remove-Item
Get-FileHash -LiteralPath $out -Algorithm SHA256
