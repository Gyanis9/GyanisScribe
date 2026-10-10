# Fetches the pinned ffmpeg build that ships inside the installer.
# Keep this file ASCII-only: Windows PowerShell 5.1 reads .ps1 as the ANSI codepage.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/FetchFfmpeg.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts/FetchFfmpeg.ps1 -ShowChecksum
#
# Why a fetch step instead of committing the binary: a 100 MB exe in git history is paid for by
# every clone forever, while the *installer* is the artifact that has to be self-contained.
#
# The SHA-256 of the zip is pinned in this file and a verified extraction leaves a stamp next to the
# executable. Both are required, so neither of these can happen silently: shipping a binary that is
# not the one this repository was tested with, or reusing a Vendor/Ffmpeg/ffmpeg.exe that somebody
# dropped in without ever verifying it.
#
# Offline fallback: put the zip at Vendor/Ffmpeg/<pinned file name> by hand; the checksum still
# has to match before anything is extracted.

param(
    # Prints the hash of the zip that is already on disk and stops. Used once, to fill in
    # $expectedSha256 for a new pinned version -- an empty value below is an error, never a skip.
    [switch]$ShowChecksum
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$version = '9.0.2'
$zipName = "ffmpeg-$version-essentials_build.zip"
$url     = "https://www.gyan.dev/ffmpeg/builds/packages/$zipName"
$expectedSha256 = ''

$vendor = Join-Path 'Vendor' 'Ffmpeg'
$zip    = Join-Path $vendor $zipName
$exe    = Join-Path $vendor 'ffmpeg.exe'
$stamp  = Join-Path $vendor 'verified-sha256.txt'

if ($ShowChecksum) {
    if (-not (Test-Path $zip)) { throw "no zip at $zip to hash; let the script fetch it first" }
    Write-Output "[hash] $((Get-FileHash -Algorithm SHA256 $zip).Hash)"
    exit 0
}

if ([string]::IsNullOrWhiteSpace($expectedSha256)) {
    throw '$expectedSha256 is not pinned. Run this script with -ShowChecksum once the zip is on disk, put the printed hash into the script, then re-run -- packaging without a pinned hash is refused.'
}

$alreadyVerified = (Test-Path $exe) -and (Test-Path $stamp) -and `
                   ((Get-Content -Raw -LiteralPath $stamp).Trim() -eq $expectedSha256)
if ($alreadyVerified) {
    Write-Output "[ok] ffmpeg already vendored and verified: $exe"
    exit 0
}

New-Item -ItemType Directory -Force -Path $vendor | Out-Null

if (-not (Test-Path $zip)) {
    Write-Output "[fetch] $url"
    # -C - resumes, --retry keeps a slow link from aborting the whole package build
    & curl.exe --fail --location --retry 30 --retry-delay 5 -C - --output $zip $url
    if ($LASTEXITCODE -ne 0) {
        throw "cannot fetch ffmpeg. Put the zip at $zip manually (source: $url), then re-run."
    }
}

$actual = (Get-FileHash -Algorithm SHA256 $zip).Hash
if ($actual -ne $expectedSha256) {
    throw "checksum mismatch for $zip : expected $expectedSha256 got $actual"
}
Write-Output "[ok] checksum verified ($actual)"

$expanded = Join-Path ([System.IO.Path]::GetTempPath()) "gyanisscribe-ffmpeg-$version"
if (Test-Path $expanded) { Remove-Item -Recurse -Force $expanded }
Expand-Archive -Path $zip -DestinationPath $expanded
$extracted = Get-ChildItem -Path $expanded -Recurse -Filter 'ffmpeg.exe' | Select-Object -First 1
if (-not $extracted) { throw "ffmpeg.exe not found inside $zip" }
Copy-Item $extracted.FullName $exe -Force
# The stamp records *which zip* this executable came from, so the fast path above stays honest.
Set-Content -LiteralPath $stamp -Value $actual -NoNewline
Remove-Item -Recurse -Force $expanded
Write-Output "[ok] ffmpeg vendored: $exe"
