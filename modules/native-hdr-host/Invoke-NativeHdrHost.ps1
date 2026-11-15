[CmdletBinding()]param([ValidateSet('Help','SelfTest','Capabilities','Pattern')][string]$Mode='Help',[string]$Profile,[ValidateRange(1,30)][double]$Seconds=10,[ValidateRange(20,200)][double]$ReferenceWhite=80,[ValidateRange(20,400)][double]$Peak=200)
. (Join-Path $PSScriptRoot '..\..\scripts\Common.ps1')
. (Join-Path $RepositoryRoot 'scripts\RuntimeProfile.ps1')
$build=Join-Path $LocalRoot 'build\native-hdr-host';$exe=Join-Path $build 'native-hdr-host.exe'
$record=[IO.File]::ReadAllText((Join-Path $build 'build-record.json'))|ConvertFrom-Json
foreach($file in $record.Files){if(-not(Test-Path -LiteralPath $file.Path) -or (Get-FileHash -LiteralPath $file.Path -Algorithm SHA256).Hash -ne $file.SHA256){throw ('Stale build/input: '+$file.Path)}}
$arguments=@('--help');$timeout=15000
if($Mode -eq 'SelfTest'){$arguments=@('--self-test')}
if($Mode -eq 'Capabilities' -or $Mode -eq 'Pattern'){
    $p=Read-ReviewedRuntimeProfile $Profile
    $arguments=@('--run','--capabilities')
    if($Mode -eq 'Pattern'){
        if($Peak -lt $ReferenceWhite){throw 'Peak must be at least reference white'}
        $culture=[Globalization.CultureInfo]::InvariantCulture
        $arguments=@('--run','--pattern','--seconds',$Seconds.ToString('R',$culture),'--reference-white',$ReferenceWhite.ToString('R',$culture),'--peak',$Peak.ToString('R',$culture))
        $timeout=[int](($Seconds+20)*1000)
    }else{$timeout=20000}
    $arguments+=@('--loader',$p.Loader,'--runtime',$p.Runtime,'--manifest',$p.Manifest,'--loader-sha256',$p.LoaderSHA256,'--runtime-sha256',$p.RuntimeSHA256,'--manifest-sha256',$p.ManifestSHA256)
}
$result=Invoke-OwnedProcess $exe $arguments (Join-Path $build ((Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+$Mode)) $timeout
Write-Output $result
if($result.ExitCode -ne 0 -or $result.ForcedTermination){throw 'Host failed; inspect its owned-process logs'}
