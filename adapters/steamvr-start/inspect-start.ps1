# SPDX-License-Identifier: Apache-2.0
$ErrorActionPreference='Stop'
$env:PSModulePath=Join-Path $PSHOME 'Modules'
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$running=@(Get-Process -Name vrserver,vrmonitor -ErrorAction SilentlyContinue)
if($running.Count){[pscustomobject]@{runtimeRunning=$true}|ConvertTo-Json -Compress;return}
$steamPath=(Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Valve\Steam' -ErrorAction Stop).SteamPath
$steam=Join-Path $steamPath 'steam.exe'
$signature=Get-AuthenticodeSignature -LiteralPath $steam
if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Valve Corp\.(,|$)'){throw 'Steam executable is not valid Valve-signed software'}
[pscustomobject]@{runtimeRunning=$false;steam=$steam}|ConvertTo-Json -Compress
