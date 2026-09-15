<#
.SYNOPSIS
Installs out\godot.appx on the HoloLens 2 through Device Portal, launches it, and prints its log.

.DESCRIPTION
Device Portal REST over the USB cable (http://localhost:10080 when the headset is plugged in;
Basic auth with the portal's own user and password, the "auto-" username prefix skipping the CSRF
token on POST). Removes any earlier install, uploads the appx with its test certificate and the
arm64 Microsoft.VCLibs framework from the Windows SDK, waits for the install, launches the app,
then polls LocalState\godot.log until the boot line from project\main.gd ("BOOT: Godot ...")
is there or the process is gone, and writes the log to out\godot.log. Exit code 0 when the boot
line was seen, 1 otherwise.

  .\run.ps1 [-Portal http://localhost:10080] [-User <portal user>] [-Password <portal password>]

The password is not in the repo: pass it, or set HOLOLENS_PORTAL_PASSWORD (and _USER) first.
#>
param(
    [string]$Portal = 'http://localhost:10080',
    [string]$User = $(if ($env:HOLOLENS_PORTAL_USER) { $env:HOLOLENS_PORTAL_USER } else { 'dev' }),
    [string]$Password = $env:HOLOLENS_PORTAL_PASSWORD
)
if (-not $Password) { throw 'no Device Portal password: -Password or HOLOLENS_PORTAL_PASSWORD' }

$ErrorActionPreference = 'Stop'
$out = Join-Path $PSScriptRoot 'out'
$appx = Join-Path $out 'godot.appx'
$cer = Join-Path $out 'godot.cer'
if (-not (Test-Path $appx)) { throw "no ${appx}: run build.ps1 first" }
$vclibs = Get-ChildItem "${env:ProgramFiles(x86)}\Microsoft SDKs\Windows Kits\10\ExtensionSDKs\Microsoft.VCLibs\14.0\Appx\Retail\ARM64\*.appx" | Select-Object -First 1
if (-not $vclibs) { throw 'no arm64 Microsoft.VCLibs appx under the Windows SDK ExtensionSDKs' }

$auth = "auto-${User}:$Password"
function Portal($method, $path, $curlArgs = @()) {
    $r = & curl.exe -s -S -u $auth -X $method "$Portal$path" @curlArgs -w "`n%{http_code}"
    if ($LASTEXITCODE) { throw "curl $method $path failed ($LASTEXITCODE): $r" }
    $lines = @($r -split "`n")
    [pscustomobject]@{ Code = [int]$lines[-1]; Body = ($lines[0..($lines.Length - 2)] -join "`n") }
}

$name = 'BoloCare.Godot'
$existing = (Portal GET '/api/app/packagemanager/packages').Body | ConvertFrom-Json
$old = $existing.InstalledPackages | Where-Object PackageFullName -like "${name}_*"
if ($old) {
    Write-Host "removing $($old.PackageFullName)"
    Portal DELETE "/api/app/packagemanager/package?package=$([uri]::EscapeDataString($old.PackageFullName))" | Out-Null
}

Write-Host "installing $appx with $($vclibs.Name) and the test certificate"
$r = Portal POST "/api/app/packagemanager/package?package=godot.appx" @(
    '-F', "godot.appx=@$appx;type=application/octet-stream",
    '-F', "dependency=@$($vclibs.FullName);filename=$($vclibs.Name);type=application/octet-stream",
    '-F', "certificate=@$cer;type=application/octet-stream")
if ($r.Code -notin 200, 202) { throw "install POST returned $($r.Code): $($r.Body)" }
$deadline = (Get-Date).AddMinutes(5)
do {
    Start-Sleep 2
    $s = Portal GET '/api/app/packagemanager/state'
    if ($s.Code -eq 200) {
        $state = $s.Body | ConvertFrom-Json
        if ($state.Success) { break }
        if ($state.Code -and $state.Code -ne 0 -and -not $state.Success) { throw "install failed: $($s.Body)" }
    }
    if ((Get-Date) -gt $deadline) { throw "install did not finish: $($s.Body)" }
} while ($true)

$pkg = ((Portal GET '/api/app/packagemanager/packages').Body | ConvertFrom-Json).InstalledPackages | Where-Object PackageFullName -like "${name}_*"
if (-not $pkg) { throw 'installed, but the package is not listed' }
$full = $pkg.PackageFullName
$praid = $pkg.PackageRelativeId
Write-Host "installed $full"

# Crash dumps for this package, so a crash on boot leaves something to read.
Portal POST "/api/debug/dump/usermode/crashcontrol?packageFullName=$([uri]::EscapeDataString($full))" @('-H', 'Content-Length: 0') | Out-Null

$fs = "/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=$([uri]::EscapeDataString($full))&path=%5CLocalState&filename="
Portal DELETE ($fs + 'godot.log') | Out-Null

function B64($s) { [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s)) }
$r = Portal POST "/api/taskmanager/app?appid=$([uri]::EscapeDataString((B64 $praid)))&package=$([uri]::EscapeDataString((B64 $full)))" @('-H', 'Content-Length: 0')
if ($r.Code -notin 200, 202) { throw "launch returned $($r.Code): $($r.Body)" }

Write-Host 'launched; waiting for LocalState\godot.log to reach its BOOT line'
$deadline = (Get-Date).AddMinutes(2)
$last = ''
$same = 0
do {
    Start-Sleep 3
    $r = Portal GET ($fs + 'godot.log')
    if ($r.Code -eq 200 -and $r.Body -match 'BOOT: ') { break }
    # A log that has not grown in 30 s with godot.exe gone is a dead or finished engine.
    if ($r.Body -eq $last) { $same++ } else { $same = 0; $last = $r.Body }
    $alive = ((Portal GET '/api/resourcemanager/processes').Body | ConvertFrom-Json).Processes | Where-Object ImageName -eq 'godot.exe'
    if ($same -ge 10 -and -not $alive) { break }
} while ((Get-Date) -lt $deadline)

if ($r.Code -ne 200) {
    $dumps = ((Portal GET '/api/debug/dump/usermode/dumps').Body | ConvertFrom-Json).CrashDumps | Where-Object PackageFullName -eq $full
    if ($dumps) { Write-Warning "no godot.log, but the package left a crash dump: $($dumps[0].FileName)" }
    throw "no godot.log ($($r.Code)): $($r.Body)"
}
$log = Join-Path $out 'godot.log'
$r.Body | Set-Content $log
Write-Host "---- godot.log ($log)"
Write-Host $r.Body
exit ($(if ($r.Body -match 'BOOT: ') { 0 } else { 1 }))
