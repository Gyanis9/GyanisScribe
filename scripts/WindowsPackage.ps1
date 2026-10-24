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

# The package has to run on a machine where the user installed nothing else: Qt runtime, ffmpeg
# and the OCR language packs all travel inside it. A missing piece is an error here, never a
# silent skip -- a package that builds but cannot record or recognise is worse than no package.
Copy-Item $exe $stage -Force
Copy-Item 'README.md' $stage -Force
if (-not (Test-Path 'LICENSE.txt')) { throw 'LICENSE.txt missing: the installer must carry the end-user license' }
Copy-Item 'LICENSE.txt' $stage -Force
# The license texts are vendored in the repo, so this call is a repair, not a download step: it only
# re-fetches a file that is absent or is not the license (a failed fetch once left a 14-byte
# "404: Not Found" page in the folder, and it copied just as prettily as the real text).
& (Join-Path $PSScriptRoot 'FetchLicenses.ps1')
if (-not (Test-Path 'licenses')) { throw 'licenses/ missing: third-party license texts must ship with the package' }
Copy-Item -Recurse -Force 'licenses' (Join-Path $stage 'licenses')

# Only the user-facing reference pages travel with the product; the internal requirement
# document is not something an end user should receive.
foreach ($document in 'ThirdPartyLicenses.md', 'OcrBackends.md', 'UiStyleGuide.md') {
    Copy-Item (Join-Path 'docs' $document) (Join-Path $stage 'docs') -Force
}

& (Join-Path $PSScriptRoot 'FetchFfmpeg.ps1')
$ffmpegExe = Join-Path (Join-Path 'vendor' 'Ffmpeg') 'ffmpeg.exe'
if (-not (Test-Path $ffmpegExe)) { throw "bundled ffmpeg missing at $ffmpegExe" }
Copy-Item $ffmpegExe $stage -Force
Write-Output '[ok] ffmpeg staged next to the executable'

# Ocr looks for <application directory>/tessdata before the user data folder, so shipping the
# packs here is what makes recognition work straight after installation.
$tessDataSource = Join-Path 'vendor' 'Tessdata'
foreach ($required in 'eng.traineddata', 'chi_sim.traineddata') {
    if (-not (Test-Path (Join-Path $tessDataSource $required))) { throw "language pack $required missing under $tessDataSource" }
}
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'tessdata') | Out-Null
Copy-Item (Join-Path $tessDataSource '*.traineddata') (Join-Path $stage 'tessdata') -Force
Write-Output '[ok] OCR language packs staged'

# The English catalog is a build product (DeployTranslations runs lrelease). Without it the
# packaged app exposes the language setting but can never honour it -- the portable folder and
# the installer both would ship a Chinese-only binary while claiming the UI is bilingual.
# The language packs land next to the executable (that is where Gui::Language looks for them),
# not under the build root -- pointing at build\release\translations would silently ship a
# package that can never switch language.
$translations = Join-Path 'build' 'release\bin\translations'
if (Test-Path $translations) {
    Copy-Item -Recurse -Force $translations (Join-Path $stage 'translations')
    Write-Output '[ok] language packs staged'
} else {
    Write-Output '[info] no language packs in this tree (lrelease missing?) -- packaged app stays Chinese-only'
}

$winDeployQt = Join-Path $QtDir 'bin\windeployqt.exe'
if (-not (Test-Path $winDeployQt)) { throw "windeployqt not found under $QtDir (set QTDIR)" }
# windeployqt reads the binary's imports, so it copies exactly the needed modules and plugins;
# without the platform plugin the app dies with "could not find Qt platform plugin windows"
& $winDeployQt --release --no-translations --svg --network (Join-Path $stage 'GyanisScribe.exe')
if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed' }

# Ship the VC runtime app-local. The executable imports MSVCP140.dll / VCRUNTIME140.dll (+ _1, _2),
# and those are not part of the Windows base image: without them the packaged app only starts on a
# machine where somebody already installed the redistributable, which is exactly what "install and
# use, nothing else to fetch" rules out. --compiler-runtime does not copy them with this Qt build
# (measured: it lands Qt modules only), so they are copied from the toolchain's own redist folder.
# The application directory is searched before the system one, so these are what the loader picks.
# The Universal CRT (api-ms-win-crt-*) is not repeated here: Windows 10/11 supplies it.
$crtSource = if ($env:VCToolsRedistDir) {
    Join-Path $env:VCToolsRedistDir 'onecore\x64\Microsoft.VC145.CRT'
} else {
    $vcVars = if ($env:VCVARS) { $env:VCVARS } else { 'G:\Tools\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat' }
    $vcRoot = Split-Path (Split-Path (Split-Path $vcVars -Parent) -Parent) -Parent
    # Version-like folder names only: Redist\MSVC also holds a 'v145' symlink, which sorts last as
    # text but is not a toolchain version -- picking it produced "vcruntime140.dll missing".
    $redist = Get-ChildItem (Join-Path $vcRoot 'Redist\MSVC') -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+' } | Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
    if (-not $redist) { throw "no VC redist under $(Join-Path $vcRoot 'Redist\MSVC'): set VCToolsRedistDir or VCVARS to a toolchain that ships one" }
    Join-Path $redist.FullName 'onecore\x64\Microsoft.VC145.CRT'
}
foreach ($name in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll') {
    $source = Join-Path $crtSource $name
    # A missing one is an error, never a skip: shipping five DLLs or four makes the difference
    # between "starts anywhere" and "starts only on a developer box".
    if (-not (Test-Path $source)) { throw "VC runtime $name missing under $crtSource" }
    Copy-Item $source $stage -Force
}
Write-Output "[ok] VC runtime staged app-local from $crtSource"

# The staged folder has to run on a machine where the user installed nothing -- that is the whole
# claim of "self-contained", and a developer box cannot show it: with the normal PATH a missing Qt
# runtime DLL is quietly satisfied by the Qt install here. So narrow PATH to the system directory
# and drop the Qt hints, then require both the app and the bundled ffmpeg to still run. A missing
# dependency then fails as a launch error (0xc0000135) instead of shipping.
Push-Location $stage
try {
    $savedPath    = $env:PATH
    $savedQtDir   = $env:QTDIR
    $savedPlugins = $env:QT_PLUGIN_PATH
    $savedQpa     = $env:QT_QPA_PLATFORM_PLUGIN_PATH
    try {
        $env:PATH = (Join-Path $env:SystemRoot 'System32')
        Remove-Item Env:QTDIR, Env:QT_PLUGIN_PATH, Env:QT_QPA_PLATFORM_PLUGIN_PATH -ErrorAction SilentlyContinue
        & .\GyanisScribe.exe --selftest
        if ($LASTEXITCODE -ne 0) { throw "staged package failed its selftest with PATH narrowed to System32 (exit $LASTEXITCODE)" }
        & .\ffmpeg.exe -hide_banner -version 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "the ffmpeg shipped inside the package would not run (exit $LASTEXITCODE)" }
    } finally {
        $env:PATH = $savedPath
        if ($savedQtDir) { $env:QTDIR = $savedQtDir }
        if ($savedPlugins) { $env:QT_PLUGIN_PATH = $savedPlugins }
        if ($savedQpa) { $env:QT_QPA_PLATFORM_PLUGIN_PATH = $savedQpa }
    }
} finally { Pop-Location }
Write-Output '[ok] staged package and bundled ffmpeg run with PATH narrowed to the system directory'

$zip = Join-Path 'dist' "GyanisScribe-$version-win64.zip"
Compress-Archive -Force -Path (Join-Path $stage '*') -DestinationPath $zip
Write-Output "[ok] zip: $zip"

if (Test-Path $Nsis) {
    # Absolute stage path: makensis resolves relative File paths against the *script's* directory
    # (packaging/), not the caller's cwd, so a relative one looked for packaging/dist/... and
    # aborted with "no files found" while the stage folder was plainly full.
    $stageForNsis = (Resolve-Path $stage).Path -replace '\\', '/'
    $ico = (Resolve-Path 'src/Gui/Resources/Brand/AppIcon.ico').Path -replace '\\', '/'
    & $Nsis "/DGyanisScribeVersion=$version" "/DGyanisScribeStage=$stageForNsis" "/DGyanisScribeIco=$ico" 'packaging\GyanisScribe.nsi'
    if ($LASTEXITCODE -ne 0) { throw 'makensis failed' }
    Write-Output "[ok] installer: dist\GyanisScribe-$version-win64.exe"
} else {
    Write-Output "[skip] NSIS missing at $Nsis -- portable package is ready, installer skipped"
}
Write-Output "[ok] package staged in $stage"
