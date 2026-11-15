[CmdletBinding()]param([string]$VisualStudioPath,[string]$DxcPath)
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
Initialize-CppTools $VisualStudioPath
$build=Join-Path $LocalRoot 'build\sdr-enhancement'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$shader=Join-Path $PSScriptRoot 'stereo_expand.hlsl'
Invoke-CheckedNative $ShaderCompiler @('/nologo','/T','cs_5_0','/E','main','/Ges','/WX','/O3','/Fo',(Join-Path $build 'stereo_expand.cs_5_0.cso'),$shader) (Join-Path $build 'fxc.log')
if($DxcPath){Invoke-CheckedNative $DxcPath @('-T','cs_6_0','-E','main','-Ges','-WX','-O3','-Fo',(Join-Path $build 'stereo_expand.cs_6_0.dxil'),$shader) (Join-Path $build 'dxc.log')}
$sources=@((Join-Path $PSScriptRoot 'adapter\stereo_d3d11.cpp'),(Join-Path $PSScriptRoot 'adapter\test_contract.cpp'))
foreach($source in $sources){$object=Join-Path $build (([IO.Path]::GetFileNameWithoutExtension($source))+'.obj');Invoke-CheckedNative $CppCompiler @('/nologo','/c','/std:c++17','/EHsc','/W4','/WX','/permissive-','/O2','/fp:precise','/MT','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',('/Fo'+$object),$source) (Join-Path $build ((Split-Path -Leaf $source)+'.log'))}
$exe=Join-Path $build 'test_contract.exe'
Invoke-CheckedNative $CppCompiler @('/nologo',(Join-Path $build 'stereo_d3d11.obj'),(Join-Path $build 'test_contract.obj'),('/Fe'+$exe),'/link','/MACHINE:X64') (Join-Path $build 'link.log')
Invoke-CheckedNative $exe @() (Join-Path $build 'cpu-contract.txt')
$node=(Get-Command node.exe -ErrorAction Stop).Source
Invoke-CheckedNative $node @((Join-Path $PSScriptRoot 'test_reference.cjs')) (Join-Path $build 'cpu-reference.json')
Write-BuildRecord $build @($sources[0],$sources[1],(Join-Path $PSScriptRoot 'adapter\stereo_adapter.hpp'),$shader,$exe,$CppCompiler,$ShaderCompiler)
Write-Output $build
