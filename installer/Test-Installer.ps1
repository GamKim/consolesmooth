# Installer test on copies (never touches the real Streaming Center folder). Needs Streaming Center 1.8.22.2 installed:
# its StreamingCenter.exe and StartupWindow.exe are copied into a temporary test folder under build\installer-test.
# Usage: installer\Test-Installer.ps1 [-ScDir "C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center"] [-Version 1.0.0]
param([string]$ScDir='C:\Program Files (x86)\AVerMedia\AVerMedia Streaming Center',[string]$Version='1.0.0')
$ErrorActionPreference='Stop'
$workspace=Split-Path -Parent $PSScriptRoot
$area=Join-Path $workspace ('build\installer-test\'+[Guid]::NewGuid().ToString('N'))
$setup=Join-Path $workspace "dist\ConsoleSmooth-Plugin-Setup-$Version.exe"
$source=Join-Path $area '한글 앱 경로'
New-Item -ItemType Directory -Force -Path $source | Out-Null
Copy-Item -LiteralPath (Join-Path $ScDir 'StreamingCenter.exe') -Destination (Join-Path $source 'StreamingCenter.exe')
[IO.File]::WriteAllText((Join-Path $source 'existing.txt'),'must remain unchanged')
$before=(Get-FileHash (Join-Path $source 'StreamingCenter.exe')).Hash
function Run([string]$action,[string]$arg,[int]$expected=0){
 $p=Start-Process -FilePath $setup -ArgumentList @($action,('"'+$arg+'"')) -PassThru -Wait -WindowStyle Hidden
 if($p.ExitCode -ne $expected){throw "$action returned $($p.ExitCode), expected $expected"}
}
Run '--test-install' $source
$root=Join-Path $source 'Plugin\ConsoleSmooth'
Run '--verify' $root
Run '--test-install' $source
Run '--verify' $root
$plugin=Join-Path $root 'gstconsolesmoothprobe.dll'
$pluginBytes=[IO.File]::ReadAllBytes($plugin)
[IO.File]::AppendAllText($plugin,'tamper')
Run '--test-install' $source 1
Run '--verify' $root 1
Run '--test-remove' $source 1
if(!(Test-Path $plugin)){throw 'Modified DLL unexpectedly deleted'}
[IO.File]::WriteAllBytes($plugin,$pluginBytes)
Run '--test-install' $source
$custom=Join-Path $root 'user-kept.txt';[IO.File]::WriteAllText($custom,'retain')
Run '--test-remove' $source
if(Test-Path $plugin){throw 'Plugin not removed'}
if(!(Test-Path $custom)){throw 'User file removed'}
if((Get-FileHash (Join-Path $source 'StreamingCenter.exe')).Hash -ne $before){throw 'Host changed'}
if([IO.File]::ReadAllText((Join-Path $source 'existing.txt')) -ne 'must remain unchanged'){throw 'Existing file changed'}
Run '--test-install' $source
Run '--verify' $root
$wrong=Join-Path $area 'unsupported';New-Item -ItemType Directory $wrong | Out-Null
[IO.File]::WriteAllText((Join-Path $wrong 'StreamingCenter.exe'),'not a supported app')
Run '--test-install' $wrong 1
# 1.8.2: another AVerMedia-signed StreamingCenter.exe (a new SC version stand-in: StartupWindow.exe from the
# installed SC) is accepted; a Microsoft-signed exe is still refused.
$signedDir=Join-Path $area 'other-version';New-Item -ItemType Directory $signedDir | Out-Null
Copy-Item -LiteralPath (Join-Path $ScDir 'StartupWindow.exe') -Destination (Join-Path $signedDir 'StreamingCenter.exe')
Run '--test-install' $signedDir
Run '--verify' (Join-Path $signedDir 'Plugin\ConsoleSmooth')
Run '--test-remove' $signedDir
$msDir=Join-Path $area 'microsoft-signed';New-Item -ItemType Directory $msDir | Out-Null
Copy-Item -LiteralPath (Join-Path $env:WINDIR 'notepad.exe') -Destination (Join-Path $msDir 'StreamingCenter.exe')
Run '--test-install' $msDir 1
$unknown=Join-Path $area 'unknown-folder';New-Item -ItemType Directory -Path (Join-Path $unknown 'Plugin\ConsoleSmooth') -Force | Out-Null
Copy-Item (Join-Path $source 'StreamingCenter.exe') (Join-Path $unknown 'StreamingCenter.exe')
[IO.File]::WriteAllText((Join-Path $unknown 'Plugin\ConsoleSmooth\keep.txt'),'keep')
Run '--test-install' $unknown 1
$newOutside=@(Get-ChildItem $source -File | Where-Object Name -NotIn @('StreamingCenter.exe','existing.txt'))
if($newOutside.Count){throw 'Unexpected host-root files'}
function Uninstall([int]$expected){
 $p=Start-Process -FilePath (Join-Path $root 'Uninstall.exe') -ArgumentList '--test-remove' -PassThru -Wait -WindowStyle Hidden
 if($p.ExitCode -ne $expected){throw "Dedicated uninstaller returned $($p.ExitCode), expected $expected"}
}
[IO.File]::AppendAllText($plugin,'tamper');Uninstall 1
if(!(Test-Path $plugin)){throw 'Uninstaller deleted modified DLL'}
[IO.File]::WriteAllBytes($plugin,$pluginBytes);Run '--test-install' $source
# Removing the host first must not prevent removing the addon.
Remove-Item -LiteralPath (Join-Path $source 'StreamingCenter.exe')
Uninstall 0
if(Test-Path $plugin){throw 'Dedicated uninstaller failed to remove plugin'}
if(!(Test-Path $custom)){throw 'Dedicated uninstaller deleted user file'}
if(!(Test-Path (Join-Path $root 'Uninstall.exe'))){throw 'Running uninstaller should be retained'}
Copy-Item -LiteralPath (Join-Path $ScDir 'StreamingCenter.exe') -Destination (Join-Path $source 'StreamingCenter.exe')
Run '--test-install' $source
Run '--verify' $root
[ordered]@{passed=$true;version=$Version;unicode_path=$true;install=$true;repair=$true;verify=$true;tamper_rejected=$true;unsupported_host_rejected=$true;other_avermedia_version_accepted=$true;microsoft_signed_rejected=$true;unknown_folder_rejected=$true;remove_reinstall=$true;user_files_retained=$true;host_hash_unchanged=$true;dedicated_uninstaller=$true;remove_after_host_deleted=$true;added_host_root_files=$newOutside.Count}|ConvertTo-Json
Remove-Item -LiteralPath $area -Recurse -Force
