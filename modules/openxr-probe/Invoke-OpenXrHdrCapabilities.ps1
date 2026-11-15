[CmdletBinding()]param([ValidateSet('Inspect','SelfTest','Enumerate')][string]$Mode='Inspect',[string]$Profile,[string]$ManifestPath,[string]$LoaderPath)
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
. (Join-Path $RepositoryRoot 'scripts\RuntimeProfile.ps1')
$build=Join-Path $LocalRoot 'build\openxr-probe';New-Item -ItemType Directory -Path $build -Force | Out-Null
if($Mode -eq 'Inspect'){
    $p=Get-LocalRuntimeProfile $ManifestPath $LoaderPath
    $path=Join-Path $LocalRoot 'runtime-profile.json'
    if(Test-Path -LiteralPath $path){throw 'Preserve/review the existing local profile before creating another; no silent pin refresh'}
    $p|ConvertTo-Json -Depth 3|Set-Content -LiteralPath $path -Encoding UTF8
    Write-Output $path;return
}
$source=Join-Path $PSScriptRoot 'OpenXrHdrCapabilities.cs';$exe=Join-Path $build 'OpenXrHdrCapabilities.exe'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
Invoke-CheckedNative $compiler @('/nologo','/target:exe','/platform:x64','/warnaserror+','/r:System.Web.Extensions.dll',('/out:'+$exe),$source) (Join-Path $build 'compile.log')
$nativeArgs=@('--self-test')
if($Mode -eq 'Enumerate'){$p=Read-ReviewedRuntimeProfile $Profile;$nativeArgs=@('--enumerate','--loader',$p.Loader,'--sha256',$p.LoaderSHA256)}
$result=Invoke-OwnedProcess $exe $nativeArgs (Join-Path $build ((Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+$Mode))
if($result.ExitCode -ne 0 -or $result.ForcedTermination){throw 'Probe failed; read owned-process logs'}
Write-BuildRecord $build @($source,$exe,$compiler)
Write-Output $build
