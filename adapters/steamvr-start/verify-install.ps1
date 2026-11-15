# SPDX-License-Identifier: Apache-2.0
[CmdletBinding()]param([string]$SteamVrRoot)
$ErrorActionPreference='Stop'
# A PS5.1 child launched from a newer shell may inherit incompatible module paths.
$env:PSModulePath=Join-Path $PSHOME 'Modules'
[Console]::OutputEncoding=New-Object Text.UTF8Encoding($false)
$steam=(Get-ItemProperty -LiteralPath 'HKCU:\SOFTWARE\Valve\Steam' -ErrorAction Stop).SteamPath
$steamExe=Join-Path $steam 'steam.exe'
if(-not $SteamVrRoot){
    $libraries=@($steam)
    $vdf=Join-Path $steam 'steamapps\libraryfolders.vdf'
    if(Test-Path -LiteralPath $vdf){foreach($match in [regex]::Matches([IO.File]::ReadAllText($vdf),'"path"\s+"([^"]+)"')){$libraries+=$match.Groups[1].Value.Replace('\\','\')}}
    foreach($library in $libraries){$candidate=Join-Path $library 'steamapps\common\SteamVR';if(Test-Path -LiteralPath (Join-Path $candidate 'bin\win64\vrstartup.exe')){$SteamVrRoot=$candidate;break}}
}
if(-not $SteamVrRoot){throw 'SteamVR installation not found'}
$SteamVrRoot=[IO.Path]::GetFullPath($SteamVrRoot)
$startup=Join-Path $SteamVrRoot 'bin\win64\vrstartup.exe';$api=Join-Path $SteamVrRoot 'bin\win64\openvr_api.dll'
foreach($file in @($steamExe,$startup,$api)){
    $signature=Get-AuthenticodeSignature -LiteralPath $file
    if($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Valve Corp\.(,|$)'){throw 'Installed SteamVR file is not valid Valve-signed software'}
}
[pscustomobject]@{steam=$steamExe;steamSha256=(Get-FileHash -LiteralPath $steamExe -Algorithm SHA256).Hash;startup=$startup;api=$api;startupSha256=(Get-FileHash -LiteralPath $startup -Algorithm SHA256).Hash;apiSha256=(Get-FileHash -LiteralPath $api -Algorithm SHA256).Hash;publisher='Valve Corp.'}|ConvertTo-Json -Compress
