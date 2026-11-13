# One-shot build: toolchain check -> engine (+checks) -> plugin -> installer. Output: dist\ConsoleSmooth-Plugin-Setup-<version>.exe
[CmdletBinding()]
param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& (Join-Path $root 'tools\Get-Toolchain.ps1')
& (Join-Path $root 'engine\Build-Engine.ps1')
& (Join-Path $root 'plugin\Build-Plugin.ps1')
& (Join-Path $root 'installer\Build-Installer.ps1')
