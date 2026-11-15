[CmdletBinding()]param([string]$OpenVrInclude,[string]$VisualStudioPath)
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
Initialize-CppTools $VisualStudioPath
if(-not $OpenVrInclude){throw 'Supply official OpenVR headers via -OpenVrInclude; no download/installation attempted'}
$build=Join-Path $LocalRoot 'build\steamvr-start';New-Item -ItemType Directory -Path $build -Force|Out-Null
$source=Join-Path $PSScriptRoot 'readiness.cpp';$exe=Join-Path $build 'steamvr-readiness.exe'
Invoke-CheckedNative $CppCompiler @('/nologo','/std:c++17','/EHsc','/W4','/WX','/O2','/MT','/I',$OpenVrInclude,$source,('/Fo'+(Join-Path $build 'readiness.obj')),('/Fe'+$exe),'/link','/MACHINE:X64','bcrypt.lib') (Join-Path $build 'compile.log')
Invoke-CheckedNative $exe @('--self-test') (Join-Path $build 'self-test.json')
Write-BuildRecord $build @($source,(Join-Path $OpenVrInclude 'openvr.h'),$CppCompiler,$exe)
Write-Output $build
