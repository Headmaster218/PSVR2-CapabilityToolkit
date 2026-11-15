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
function ConvertTo-WindowsArgument([string]$Value) {
    if($Value.Length -gt 0 -and $Value -notmatch '[\s"]'){return $Value}
    $result='"';$slashes=0
    foreach($character in $Value.ToCharArray()){
        if($character -eq '\'){$slashes++;continue}
        if($character -eq '"'){$result+=('\' * (2*$slashes+1))+'"'}else{$result+=('\' * $slashes)+$character}
        $slashes=0
    }
    return $result+('\' * (2*$slashes))+'"'
}
function Invoke-OwnedProcess([string]$Executable,[string[]]$NativeArgs,[string]$Prefix,[int]$TimeoutMs=15000) {
    $info=New-Object Diagnostics.ProcessStartInfo
    $info.FileName=$Executable;$info.Arguments=(@($NativeArgs|ForEach-Object {ConvertTo-WindowsArgument $_}) -join ' ')
    $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
    $child=New-Object Diagnostics.Process;$child.StartInfo=$info
    if(-not $child.Start()){throw 'Owned child failed to start'}
    $out=$child.StandardOutput.ReadToEndAsync();$err=$child.StandardError.ReadToEndAsync();$forced=$false
    if(-not $child.WaitForExit($TimeoutMs)){$forced=$true;$child.Kill();$child.WaitForExit()}
    $out.Result|Set-Content -LiteralPath ($Prefix+'.stdout.txt') -Encoding UTF8
    $err.Result|Set-Content -LiteralPath ($Prefix+'.stderr.txt') -Encoding UTF8
    $record=[pscustomobject]@{AtUtc=[DateTime]::UtcNow.ToString('o');Pid=$child.Id;ExitCode=$child.ExitCode;ForcedTermination=$forced;Arguments=$NativeArgs;Stdout=($Prefix+'.stdout.txt');Stderr=($Prefix+'.stderr.txt');PhysicalHdrVerified=$false}
    $record|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Prefix+'.json') -Encoding UTF8
    $child.Dispose();return $record
}
