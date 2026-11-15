[CmdletBinding()]param([string]$VisualStudioPath,[string]$OpenXrInclude,[string]$OpenXrSdkSource,[string]$PythonPath)
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
Initialize-CppTools $VisualStudioPath
$build=Join-Path $LocalRoot 'build\native-hdr-host';New-Item -ItemType Directory -Path $build -Force | Out-Null
$extraInputs=@()
if($OpenXrSdkSource){
    if(-not $PythonPath){$PythonPath=(Get-Command python.exe -ErrorAction Stop).Source}
    $OpenXrInclude=Join-Path $build 'generated';$generated=Join-Path $OpenXrInclude 'openxr'
    New-Item -ItemType Directory -Path $generated -Force | Out-Null
    $registry=Join-Path $OpenXrSdkSource 'specification\registry\xr.xml';$generator=Join-Path $OpenXrSdkSource 'specification\scripts\genxr.py'
    $env:PYTHONDONTWRITEBYTECODE='1'
    foreach($name in @('openxr.h','openxr_platform.h')){Invoke-CheckedNative $PythonPath @($generator,'-registry',$registry,'-o',$generated,$name) (Join-Path $build ($name+'.generation.log'))}
    Copy-Item -LiteralPath (Join-Path $OpenXrSdkSource 'include\openxr\openxr_platform_defines.h') -Destination (Join-Path $generated 'openxr_platform_defines.h') -Force
    $extraInputs=@($registry,$generator,$PythonPath)
}
if(-not $OpenXrInclude){throw 'Supply official OpenXR 1.1.65 -OpenXrInclude, or -OpenXrSdkSource and existing Python; no dependency installation/download is performed'}
$header=Join-Path $OpenXrInclude 'openxr\openxr.h'
$headerText=[IO.File]::ReadAllText($header)
if($headerText -notmatch 'XR_SONY_swapchain_color_space' -or $headerText -notmatch 'XR_SONY_hdr_metadata'){throw 'Official OpenXR headers with SONY color-space/metadata extensions required (tested with 1.1.65)'}
foreach($s in @(@{Stage='vs';Entry='vs_main';Name='g_PatternVS'},@{Stage='ps';Entry='ps_main';Name='g_PatternPS'})){
    Invoke-CheckedNative $ShaderCompiler @('/nologo','/Ges','/WX','/O3','/T',($s.Stage+'_5_0'),'/E',$s.Entry,'/Vn',$s.Name,'/Fh',(Join-Path $build ('pattern_'+$s.Stage+'.hpp')),'/Fo',(Join-Path $build ('pattern_'+$s.Stage+'.cso')),(Join-Path $PSScriptRoot 'pattern.hlsl')) (Join-Path $build ('fxc-'+$s.Stage+'.log'))
}
$adapter=Join-Path $RepositoryRoot 'modules\sdr-enhancement\adapter'
$source=Join-Path $PSScriptRoot 'main.cpp';$exe=Join-Path $build 'native-hdr-host.exe'
Invoke-CheckedNative $CppCompiler @('/nologo','/std:c++17','/EHsc','/W4','/WX','/O2','/MT','/DUNICODE','/D_UNICODE','/I',$OpenXrInclude,'/I',$build,'/I',$adapter,$source,('/Fo'+(Join-Path $build 'native-hdr-host.obj')),('/Fe'+$exe),'/link','/MACHINE:X64','d3d11.lib','dxgi.lib','bcrypt.lib','advapi32.lib') (Join-Path $build 'compile.log')
Invoke-CheckedNative $exe @('--help') (Join-Path $build 'help.txt')
Invoke-CheckedNative $exe @('--self-test') (Join-Path $build 'self-test.jsonl')
$dumpbin=Join-Path (Split-Path -Parent $CppCompiler) 'dumpbin.exe'
Invoke-CheckedNative $dumpbin @('/imports',$exe) (Join-Path $build 'imports.txt')
if(Select-String -LiteralPath (Join-Path $build 'imports.txt') -Pattern 'openxr_loader\.dll|vrclient_x64\.dll'){throw 'Unexpected static XR runtime import'}
$files=@($source,(Join-Path $PSScriptRoot 'color_contract.hpp'),(Join-Path $PSScriptRoot 'pattern.hlsl'),$PSCommandPath,(Join-Path $RepositoryRoot 'scripts\Common.ps1'),(Join-Path $adapter 'stereo_adapter.hpp'),$header,(Join-Path $OpenXrInclude 'openxr\openxr_platform.h'),(Join-Path $OpenXrInclude 'openxr\openxr_platform_defines.h'),$CppCompiler,$ShaderCompiler,$exe,(Join-Path $build 'pattern_vs.cso'),(Join-Path $build 'pattern_ps.cso'),(Join-Path $build 'pattern_vs.hpp'),(Join-Path $build 'pattern_ps.hpp'))+$extraInputs
Write-BuildRecord $build $files
Write-Output $build
