[CmdletBinding()]param([ValidateSet('Core','All')][string]$Component='Core',[string]$VisualStudioPath,[string]$DxcPath,[string]$OpenXrInclude,[string]$OpenXrSdkSource,[string]$PythonPath)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
& (Join-Path $root 'adapters\toolkit-capi\build-offline.ps1')
& (Join-Path $root 'modules\sdr-enhancement\build-offline.ps1') -VisualStudioPath $VisualStudioPath -DxcPath $DxcPath
& (Join-Path $root 'modules\openxr-probe\Invoke-OpenXrHdrCapabilities.ps1') -Mode SelfTest
if($Component -eq 'All'){
    & (Join-Path $root 'modules\native-hdr-host\build-offline.ps1') -VisualStudioPath $VisualStudioPath -OpenXrInclude $OpenXrInclude -OpenXrSdkSource $OpenXrSdkSource -PythonPath $PythonPath
    & (Join-Path $root 'modules\native-hdr-host\Invoke-NativeHdrHost.ps1') -Mode SelfTest
}
$node=(Get-Command node.exe -ErrorAction Stop).Source
& $node (Join-Path $root 'tests\offline.cjs')
if($LASTEXITCODE -ne 0){throw 'Repository CPU audit failed'}
$errors=@()
Get-ChildItem -LiteralPath $root -Recurse -Filter '*.ps1' | Where-Object {$_.FullName -notlike '*\.local\*' -and $_.FullName -notlike '*\deps\*'} | ForEach-Object {
    $tokens=$null;$parseErrors=$null
    [Management.Automation.Language.Parser]::ParseFile($_.FullName,[ref]$tokens,[ref]$parseErrors)|Out-Null
    $errors+=@($parseErrors)
}
if($errors.Count){$errors|Out-String|Write-Error;throw 'PowerShell parse failed'}
Write-Output 'Offline build and CPU checks passed. No connect/run/runtime/device/GPU mode executed.'
