[CmdletBinding()]param()
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
$build=Join-Path $LocalRoot 'build\toolkit-capi'
New-Item -ItemType Directory -Path $build -Force | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(-not(Test-Path -LiteralPath $compiler)){throw '.NET Framework 4.x x64 compiler is required; no installation attempted'}
$source=Join-Path $PSScriptRoot 'toolkit-helper.cs';$exe=Join-Path $build 'toolkit-helper.exe'
Invoke-CheckedNative $compiler @('/nologo','/target:exe','/platform:x64','/optimize+','/warnaserror+','/codepage:65001','/r:System.Web.Extensions.dll',('/out:'+$exe),$source) (Join-Path $build 'compile.log')
foreach($mode in @('--help','--self-test','--self-test-modes')){Invoke-CheckedNative $exe @($mode) (Join-Path $build ($mode.TrimStart('-')+'.txt'))}
# The watchdog test is a child that intentionally exits itself while a fake
# native call holds a lock. No library or hardware is loaded in this mode.
$process=Start-Process -FilePath $exe -ArgumentList '--self-test-watchdog' -WindowStyle Hidden -PassThru
if(-not $process.WaitForExit(7000)){$process.Kill();$process.WaitForExit();throw 'Offline watchdog failed to bound the owned helper'}
$process.Refresh();if($process.ExitCode -ne 4){throw ('Unexpected watchdog test exit '+$process.ExitCode)}
Write-BuildRecord $build @($source,$exe,$compiler)
Write-Output $build
