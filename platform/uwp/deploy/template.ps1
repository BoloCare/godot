<#
.SYNOPSIS
Zips a built UWP Godot into the export template the editor's UWP exporter reads.

.DESCRIPTION
Expects the engine already built (scons platform=uwp target=<Target> [module_mono_enabled=yes]).
The template is bin\uwp_arm64_<debug|release>.zip: godot.exe (the arm64 build), AppxManifest.xml
with its $placeholders$ and the default logos, all from misc\dist\uwp_template.
-Install also copies it where the editor looks: %APPDATA%\Godot\export_templates\<version>\
(the .mono version directory for a mono build), so an editor built from this checkout exports
without a custom template path.
#>
param(
    [string]$Target = 'template_debug',
    [switch]$DotNet,
    [switch]$Install
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..\..')
$exe = Join-Path $root "bin\godot.uwp.$Target.arm64$(if ($DotNet) { '.mono' }).exe"
if (-not (Test-Path $exe)) { throw "no ${exe}: build the engine first (scons platform=uwp target=$Target$(if ($DotNet) { ' module_mono_enabled=yes' }))" }

$name = "uwp_arm64_$(if ($Target -eq 'template_release') { 'release' } else { 'debug' }).zip"
$zip = Join-Path $root "bin\$name"
$stage = Join-Path $root "bin\uwp_template_stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
Copy-Item (Join-Path $root 'misc\dist\uwp_template') $stage -Recurse
Copy-Item $exe (Join-Path $stage 'godot.exe')
if (Test-Path $zip) { Remove-Item $zip }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, 'Optimal', $false)
Remove-Item $stage -Recurse -Force
Write-Host "built $zip"

if ($Install) {
    # The editor's template directory is its VERSION_FULL_CONFIG: major.minor.patch.status[.mono].
    $version = Get-Content (Join-Path $root 'version.py') | ForEach-Object { if ($_ -match '^(major|minor|patch|status)\s*=\s*"?([^"]+)"?') { @{ $Matches[1] = $Matches[2] } } }
    $v = @{}; $version | ForEach-Object { $v += $_ }
    $dir = Join-Path $env:APPDATA "Godot\export_templates\$($v.major).$($v.minor).$($v.patch).$($v.status)$(if ($DotNet) { '.mono' })"
    New-Item -ItemType Directory -Force $dir | Out-Null
    Copy-Item $zip $dir -Force
    Write-Host "installed to $dir\$name"
}
