$ErrorActionPreference='Stop'
$RepositoryRoot=Split-Path -Parent $PSScriptRoot
$LocalRoot=Join-Path $RepositoryRoot '.local'
function Invoke-CheckedNative([string]$Executable,[string[]]$NativeArgs,[string]$Log) {
    $saved=$ErrorActionPreference;$ErrorActionPreference='Continue'
    & $Executable @NativeArgs 2>&1 | Out-File -LiteralPath $Log -Encoding UTF8
    $nativeExit=$LASTEXITCODE;$ErrorActionPreference=$saved
    if($nativeExit -ne 0){throw ($Executable+' exit '+$nativeExit+'; see '+$Log)}
}
function Initialize-CppTools([string]$VisualStudioPath) {
    if(-not $VisualStudioPath){
        $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if(-not(Test-Path -LiteralPath $vswhere)){throw 'Provide -VisualStudioPath or install VS C++ tools (not installed by this script)'}
        $VisualStudioPath=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if($LASTEXITCODE -ne 0 -or -not $VisualStudioPath){throw 'VS C++ x64 toolchain not found'}
    }
    & (Join-Path $VisualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1') -VsInstallationPath $VisualStudioPath -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -NoLogo
    $script:CppCompiler=(Get-Command cl.exe -ErrorAction Stop).Source
    $version=$env:WindowsSDKVersion.TrimEnd('\')
    $script:ShaderCompiler=Join-Path $env:WindowsSdkDir ('bin\'+$version+'\x64\fxc.exe')
    if(-not(Test-Path -LiteralPath $script:ShaderCompiler)){throw 'Windows SDK FXC not found'}
}
function Write-BuildRecord([string]$Directory,[string[]]$Files) {
    $records=@(foreach($path in $Files){$f=Get-Item -LiteralPath $path;[pscustomobject]@{Path=$f.FullName;Bytes=$f.Length;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}})
    [pscustomobject]@{AtUtc=[DateTime]::UtcNow.ToString('o');Scope='offline build/tests only';ActualVrRunExecuted=$false;Files=$records} | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Directory 'build-record.json') -Encoding UTF8
}
