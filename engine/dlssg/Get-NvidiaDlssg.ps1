# ConsoleSmooth 2.2.8 INTERNAL: one-click NVIDIA DLSS Frame Generation runtime (started from the model menu after the user agrees).
# Source: the NVIDIA app's own DLSS cache on this PC (C:\ProgramData\NVIDIA\NGX\models\dlssg\versions\<newest>\files\*.bin).
# The newest copy is checked for a valid NVIDIA signature and copied as nvngx_dlssg.dll. Nothing is downloaded.
param([string]$Dest = (Join-Path $env:LOCALAPPDATA 'ConsoleSmooth\nvidia-dlssg'))
$ErrorActionPreference = 'Stop'
$Host.UI.RawUI.WindowTitle = 'ConsoleSmooth - NVIDIA DLSS FG 준비'
$cache = Join-Path $env:ProgramData 'NVIDIA\NGX\models\dlssg\versions'
Write-Host 'NVIDIA 앱이 받아 둔 DLSS 프레임 생성 파일을 찾아요.'
Write-Host "찾는 곳: $cache"
Write-Host "설치 위치: $Dest"
Write-Host ''
try {
    if (-not (Test-Path -LiteralPath $cache)) { throw 'NVIDIA 앱의 DLSS 캐시가 없어요. NVIDIA 앱을 설치하고 DLSS 프레임 생성을 쓰는 게임을 한 번 실행해 주세요.' }
    $dirs = Get-ChildItem -LiteralPath $cache -Directory | Where-Object { $_.Name -match '^\d+$' } | Sort-Object { [long]$_.Name } -Descending
    $picked = $null
    foreach ($d in $dirs) {
        $bin = Get-ChildItem -LiteralPath (Join-Path $d.FullName 'files') -Filter '*.bin' -File -ErrorAction SilentlyContinue | Select-Object -First 1
        if (-not $bin) { continue }
        New-Item -ItemType Directory -Force $Dest | Out-Null
        $target = Join-Path $Dest 'nvngx_dlssg.dll'
        Copy-Item -LiteralPath $bin.FullName -Destination $target -Force
        $sig = Get-AuthenticodeSignature -LiteralPath $target
        if ($sig.Status -eq 'Valid' -and $sig.SignerCertificate.Subject -match 'O=NVIDIA Corporation') { $picked = $d; break }
        Remove-Item -LiteralPath $target -Force
    }
    if (-not $picked) { throw 'NVIDIA 서명이 확인된 DLSS 프레임 생성 파일을 찾지 못했어요.' }
    $ver = (Get-Item (Join-Path $Dest 'nvngx_dlssg.dll')).VersionInfo.FileVersion
    "nvngx_dlssg.dll $ver from NVIDIA app cache $($picked.FullName) copied=$(Get-Date -Format s)" | Set-Content -LiteralPath (Join-Path $Dest 'SOURCE.txt') -Encoding UTF8
    Write-Host "준비를 마쳤어요 (버전 $ver). 프레임 생성 설정에서 DLSS FG를 고른 뒤 적용을 누르세요." -ForegroundColor Green
    exit 0
} catch {
    Write-Host ("준비하지 못했어요: " + $_.Exception.Message) -ForegroundColor Red
    exit 1
} finally {
    if (-not $env:CS_DLSSG_NO_PAUSE) { Write-Host ''; Read-Host '창을 닫으려면 Enter' | Out-Null }
}
