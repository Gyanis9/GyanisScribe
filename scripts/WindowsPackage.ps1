# Packages the Windows build: portable directory + zip + NSIS installer.
# Keep this file ASCII-only: Windows PowerShell 5.1 reads .ps1 as the ANSI codepage unless a BOM is
# present, so Chinese comments here would garble the script body.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\WindowsPackage.ps1
#
# Version comes from project(... VERSION) in CMakeLists.txt -- the single source, not repeated here.

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$QtDir = if ($env:QTDIR) { $env:QTDIR } else { 'G:\Tools\Qt\6.11.1\msvc2022_64' }
$Nsis  = if ($env:MAKENSIS) { $env:MAKENSIS } else { 'G:\Tools\NSIS\makensis.exe' }

$projectLine = Select-String -Path CMakeLists.txt -Pattern 'project\(GyanisScribe VERSION' | Select-Object -First 1
if (-not $projectLine) { throw 'cannot read version from project(... VERSION)' }
$version = ($projectLine.Line -split '\s+')[2]
if (-not $version) { throw 'version token missing in project(... VERSION) line' }
Write-Output "[info] version $version"

$stage = Join-Path 'dist' "GyanisScribe-$version-win64"
$bin   = Join-Path 'build' 'release\bin'
$exe   = Join-Path $bin 'GyanisScribe.exe'

# Always build: the staged package must be the artifact the tests just ran against.
# A conditional build here shipped a stale exe once already (ninja no-ops when nothing changed,
# and the same call runs ctest, so packaging is gated by the test suite).
& cmd /c "scripts\WindowsBuild.bat release"
if ($LASTEXITCODE -ne 0) { throw 'release build failed' }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'docs') | Out-Null

# The exe plus Qt runtime go into one folder; docs travel with it because the About page
# points the user at docs/ThirdPartyLicenses.md.
Copy-Item $exe $stage -Force
Copy-Item 'README.md' $stage -Force
Copy-Item (Join-Path 'docs' '*.md') (Join-Path $stage 'docs') -Force
if (Test-Path 'LICENSE.txt') { Copy-Item 'LICENSE.txt' $stage -Force }

$winDeployQt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $winDeployQt)) { throw "windeployqt not found under $QtDir (set QTDIR)" }
# windeployqt reads the binary's imports, so it copies exactly the needed modules and plugins;
# without the platform plugin the app dies with "could not find Qt platform plugin windows"
& $winDeployQt --release --no-translations --svg --network (Join-Path $stage 'GyanisScribe.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# The staged folder must run on its own: that is the only proof the package is complete.
Push-Location $stage
try {
    & .\GyanisScribe.exe --selftest
    if ($LASTEXITCODE -ne 0) { throw "staged package failed its selftest (exit $LASTEXITCODE)" }
} finally { Pop-Location }
Write-Output '[ok] staged package selftest passed'

$zip = Join-Path 'dist' "GyanisScribe-$version-win64.zip"
Compress-Archive -Force -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Output "[ok] zip: $zip"

if (Test-Path $Nsis) {
    # Absolute stage path: makensis resolves relative File paths against the *script's* directory
    # (packaging/), not the caller's cwd, so a relative one looked for packaging/dist/... and
    # aborted with "no files found" while the stage folder was plainly full.
    $stageForNsis = (Resolve-Path $stage).Path -replace '\\', '/'
    & $Nsis "/DGyanisScribeVersion=$version" "/DGyanisScribeStage=$stageForNsis" 'packaging\GyanisScribe.nsi'
    if ($LASTEXITCODE -ne 0) { throw 'makensis failed' }
    Write-Output "[ok] installer: dist\GyanisScribe-$version-win64.exe"
} else {
    Write-Output "[skip] NSIS missing at $Nsis -- portable package is ready, installer skipped"
}
Write-Output "[ok] package staged in $stage"
