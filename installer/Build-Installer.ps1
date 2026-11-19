[CmdletBinding()]
param()
# Packs build\stage (plugin DLL + engine folder) into dist\ConsoleSmooth-Plugin-Setup-<version>.exe. Run engine\Build-Engine.ps1 and plugin\Build-Plugin.ps1 first.
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$build=Join-Path $root 'build\installer'
$dist=Join-Path $root 'dist'
$version='1.1.2'
# 11-19: the settings window title shows CS_PLUGIN_VERSION (1.1.1 shipped still saying 1.1.0); keep it equal to $version.
$pv=(Select-String -LiteralPath (Join-Path $root 'plugin\diagnostics.h') -Pattern '#define CS_PLUGIN_VERSION "([^"]+)"').Matches[0].Groups[1].Value
if($pv -ne $version){throw "plugin\diagnostics.h CS_PLUGIN_VERSION is $pv, installer is $version"}
New-Item -ItemType Directory -Force -Path $build,$dist | Out-Null
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$payload=Join-Path $build 'payload.zip'
$entries=[ordered]@{}
$entries['gstconsolesmoothprobe.dll']=Join-Path $root 'build/stage/gstconsolesmoothprobe.dll'
$engine=Join-Path $root 'build/stage/engine'
foreach($f in Get-ChildItem -LiteralPath $engine -Recurse -File){
 if($f.Name -eq 'settings.ini'){continue}
 $relative=$f.FullName.Substring($engine.Length+1).Replace('\','/')
 $entries['engine/'+$relative]=$f.FullName
}
$entries['INSTALL-README.txt']=Join-Path $PSScriptRoot 'INSTALL-README.txt'
$entries['INSTALL-README-EN.txt']=Join-Path $PSScriptRoot 'INSTALL-README-EN.txt'
$entries['THIRD_PARTY_NOTICES.txt']=Join-Path $PSScriptRoot 'THIRD_PARTY_NOTICES.txt'
$entries['LICENSE']=Join-Path $root 'LICENSE'
foreach($license in Get-ChildItem (Join-Path $PSScriptRoot 'licenses') -File){$entries['licenses/'+$license.Name]=$license.FullName}
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$code=Join-Path $PSScriptRoot 'ConsoleSmoothSetup.cs'
$applicationManifest=Join-Path $PSScriptRoot 'app.manifest'
$removeManifest=Join-Path $build 'remove.sha256'
$removeHashes=foreach($entry in $entries.GetEnumerator()){(Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash+"`t"+$entry.Key}
[IO.File]::WriteAllLines($removeManifest,$removeHashes,[Text.UTF8Encoding]::new($false))
$uninstaller=Join-Path $build 'Uninstall.exe'
& $csc /nologo /target:winexe /platform:x64 /optimize+ /utf8output /define:UNINSTALLER "/out:$uninstaller" "/win32manifest:$applicationManifest" "/resource:$removeManifest,payload.sha256" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:Microsoft.CSharp.dll $code
if($LASTEXITCODE){throw 'Uninstaller compilation failed'}
$entries['Uninstall.exe']=$uninstaller
$stream=[IO.File]::Open($payload,[IO.FileMode]::Create)
$zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
$hashes=[Collections.Generic.List[string]]::new()
try {
 foreach($entry in $entries.GetEnumerator()){
  [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$entry.Value,$entry.Key,[IO.Compression.CompressionLevel]::Optimal) | Out-Null
  $hashes.Add((Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash+"`t"+$entry.Key)
 }
}finally{$zip.Dispose();$stream.Dispose()}
[IO.File]::WriteAllLines((Join-Path $build 'payload.sha256'),$hashes,[Text.UTF8Encoding]::new($false))
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$output=Join-Path $dist ("ConsoleSmooth-Plugin-Setup-$version.exe")
$code=Join-Path $PSScriptRoot 'ConsoleSmoothSetup.cs'
$applicationManifest=Join-Path $PSScriptRoot 'app.manifest'
$hashManifest=Join-Path $build 'payload.sha256'
Add-Type -AssemblyName System.Drawing
$bitmap=[Drawing.Bitmap]::new(64,64)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode=[Drawing.Drawing2D.SmoothingMode]::AntiAlias
$graphics.Clear([Drawing.Color]::FromArgb(32,32,32))
$pen=[Drawing.Pen]::new([Drawing.Color]::FromArgb(230,0,45),4)
$pen.LineJoin=[Drawing.Drawing2D.LineJoin]::Round
$graphics.DrawRectangle($pen,8,9,36,35)
$graphics.DrawRectangle($pen,20,21,36,35)
$graphics.DrawLines($pen,[Drawing.Point[]]@([Drawing.Point]::new(32,29),[Drawing.Point]::new(42,38),[Drawing.Point]::new(32,47)))
$icon=[Drawing.Icon]::FromHandle($bitmap.GetHicon())
$iconPath=Join-Path $build 'ConsoleSmooth.ico'
$iconFile=[IO.File]::Create($iconPath)
try{$icon.Save($iconFile)}finally{$iconFile.Dispose();$icon.Dispose();$pen.Dispose();$graphics.Dispose();$bitmap.Dispose()}
& $csc /nologo /target:winexe /platform:x64 /optimize+ /utf8output "/out:$output" "/win32manifest:$applicationManifest" "/win32icon:$iconPath" "/resource:$payload,payload.zip" "/resource:$hashManifest,payload.sha256" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /r:Microsoft.CSharp.dll $code
if($LASTEXITCODE){throw 'Installer compilation failed'}
$p=Start-Process -FilePath $output -ArgumentList '--self-test' -PassThru -Wait -WindowStyle Hidden
if($p.ExitCode){throw 'Embedded payload self-test failed'}
$digest=Get-FileHash -LiteralPath $output -Algorithm SHA256
[ordered]@{version=$version;file=$output;bytes=(Get-Item -LiteralPath $output).Length;sha256=$digest.Hash;payload_files=$entries.Count;supported_host='Streaming Center 1.8.22.2 and 1.9.19.1 x64';signed=$false;self_test='passed';uninstaller='Plugin/ConsoleSmooth/Uninstall.exe'}|ConvertTo-Json|Set-Content (Join-Path $dist ("ConsoleSmooth-Plugin-Setup-$version.json")) -Encoding utf8
$digest
