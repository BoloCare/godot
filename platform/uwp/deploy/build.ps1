<#
.SYNOPSIS
Packs a built UWP Godot (bin\godot.uwp.<target>.arm64[.mono].exe) and a boot-check project into a signed appx.

.DESCRIPTION
No MSBuild, no .vcxproj. Expects the engine already built with
  scons platform=uwp target=template_debug
(SCons runs VsDevCmd.bat itself). This script then:
  1. packs project\ into godot.pck with a desktop Godot 4.7 (-Godot, or the newest
     Godot_v4.7*_win64 console exe under Downloads), through pack.gd and PCKPacker;
  2. lays out the package: godot.exe (the arm64 build renamed to what AppxManifest.xml names),
     godot.pck, AppxManifest.xml and three generated logos;
  3. makeappx packs it and signtool signs it with a self-signed test certificate made on first
     use (deps\godot.pfx, git-ignored; deps\godot.cer goes to the device with the package).
Output: out\godot.appx and out\godot.cer beside this script. run.ps1 installs them.

-DotNet packs project-dotnet\ with the mono template (scons ... module_mono_enabled=yes) instead,
and publishes its C# project the way the editor's export does:
  dotnet publish -c ExportDebug -r win-arm64 --self-contained true -p:GodotTargetPlatform=uwp
which, through this checkout's Godot.NET.Sdk (Sdk\UWP.props and UWP.targets), is one NativeAOT
BootDotNet.dll; it goes into the package as data_BootDotNet_uwp_arm64\, where the engine looks for
it. ILCompiler finds the arm64 linker through vswhere, so the VS installer directory is put on PATH.
#>
param(
    [string]$Target = 'template_debug',
    [string]$Godot = '',
    [switch]$DotNet
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$root = Resolve-Path (Join-Path $here '..\..\..')
$exe = Join-Path $root "bin\godot.uwp.$Target.arm64$(if ($DotNet) { '.mono' }).exe"
if (-not (Test-Path $exe)) { throw "no ${exe}: build the engine first (scons platform=uwp target=$Target$(if ($DotNet) { ' module_mono_enabled=yes' }))" }
$project = Join-Path $here $(if ($DotNet) { 'project-dotnet' } else { 'project' })

$out = Join-Path $here 'out'
$deps = Join-Path $here 'deps'
New-Item -ItemType Directory -Force $out, $deps | Out-Null

# 1. The pck.
if (-not $Godot) {
    $Godot = Get-ChildItem "$env:USERPROFILE\Downloads\Godot_v4.7*_win64\*_console.exe" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1 | ForEach-Object FullName
}
if (-not $Godot -or -not (Test-Path $Godot)) { throw 'no desktop Godot 4.7 to pack with: pass -Godot <path to a Godot 4.7 console exe>' }
$pck = Join-Path $out 'godot.pck'
if (Test-Path $pck) { Remove-Item $pck }
& $Godot --headless --path $project -s ../pack.gd -- $pck | Out-Null
if (-not (Test-Path $pck)) { throw 'pack.gd did not write godot.pck' }

# 2. The package layout.
$pkg = Join-Path $out 'pkg'
if (Test-Path $pkg) { Remove-Item $pkg -Recurse -Force }
New-Item -ItemType Directory -Force "$pkg\Assets" | Out-Null
Copy-Item $exe "$pkg\godot.exe"
Copy-Item $pck, (Join-Path $here 'AppxManifest.xml') $pkg

# 2b. The NativeAOT library, published with the editor's own arguments. The project imports the
# Sdk by path (GodotSdkDir) so the Sdk under test is this checkout's, not the NuGet one; the Sdk
# needs SdkPackageVersions.props beside it, which build_assemblies.py generates from version.py.
if ($DotNet) {
    $sdk = Join-Path $out 'sdk'
    if (Test-Path $sdk) { Remove-Item $sdk -Recurse -Force }
    Copy-Item (Join-Path $root 'modules\mono\editor\Godot.NET.Sdk\Godot.NET.Sdk\Sdk') $sdk -Recurse
    Push-Location (Join-Path $root 'modules\mono')
    try {
        python -c "import sys; sys.path.insert(0, 'build_scripts'); import build_assemblies; build_assemblies.generate_sdk_package_versions()"
        if ($LASTEXITCODE) { throw "generate_sdk_package_versions failed ($LASTEXITCODE)" }
    } finally { Pop-Location }
    Copy-Item (Join-Path $root 'modules\mono\SdkPackageVersions.props') $sdk
    $env:PATH += ";${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
    $publish = Join-Path $out 'publish'
    if (Test-Path $publish) { Remove-Item $publish -Recurse -Force }
    $csproj = Get-ChildItem $project -Filter *.csproj | Select-Object -First 1
    Write-Host "publishing $($csproj.Name) for win-arm64 under NativeAOT"
    & dotnet publish $csproj.FullName -c $(if ($Target -eq 'template_release') { 'ExportRelease' } else { 'ExportDebug' }) -r win-arm64 --self-contained true `
        -p:GodotTargetPlatform=uwp "-p:GodotSdkDir=$sdk\" -o $publish -nologo -v:m -clp:ErrorsOnly
    if ($LASTEXITCODE) { throw "dotnet publish failed ($LASTEXITCODE)" }
    $data = Join-Path $pkg "data_$($csproj.BaseName)_uwp_arm64"
    New-Item -ItemType Directory $data | Out-Null
    # The .pdb stays out: 70 MB the package has no use for (the export's include_debug_symbols option).
    Get-ChildItem $publish -File | Where-Object Extension -ne '.pdb' | Copy-Item -Destination $data
}
Add-Type -AssemblyName System.Drawing
foreach ($logo in @(@('StoreLogo.png', 50), @('Logo.png', 150), @('SmallLogo.png', 44))) {
    $bmp = [System.Drawing.Bitmap]::new($logo[1], $logo[1])
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::FromArgb(255, 71, 140, 191))
    $g.Dispose()
    $bmp.Save("$pkg\Assets\$($logo[0])", [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# 3. makeappx + signtool from the newest Windows SDK.
$sdk = (Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin" -Directory | Where-Object Name -like '10.*' | Sort-Object Name | Select-Object -Last 1).FullName
$bin = Join-Path $sdk 'x64'
$appx = Join-Path $out 'godot.appx'
& "$bin\makeappx.exe" pack /o /d $pkg /p $appx | Out-Null
if ($LASTEXITCODE) { throw "makeappx failed ($LASTEXITCODE)" }

# The certificate subject must be the manifest's Publisher.
$pfx = Join-Path $deps 'godot.pfx'
$cer = Join-Path $out 'godot.cer'
$pfxPassword = ConvertTo-SecureString 'godot' -AsPlainText -Force
if (-not (Test-Path $pfx)) {
    Write-Host 'making a self-signed test certificate (CN=BoloCareGodot)'
    $cert = New-SelfSignedCertificate -Type Custom -Subject 'CN=BoloCareGodot' -KeyUsage DigitalSignature -FriendlyName 'Godot UWP test signing' `
        -CertStoreLocation 'Cert:\CurrentUser\My' -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}') -NotAfter (Get-Date).AddYears(2)
    Export-PfxCertificate -Cert $cert -FilePath $pfx -Password $pfxPassword | Out-Null
    Remove-Item $cert.PSPath
}
& "$bin\signtool.exe" sign /fd SHA256 /f $pfx /p godot $appx | Out-Null
if ($LASTEXITCODE) { throw "signtool failed ($LASTEXITCODE)" }
$cert = (Get-PfxData -FilePath $pfx -Password $pfxPassword).EndEntityCertificates[0]
[IO.File]::WriteAllBytes($cer, $cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Cert))
Write-Host "built $appx and $cer"
