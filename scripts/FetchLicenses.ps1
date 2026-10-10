# Fetches the third-party license texts that must travel inside the Windows package.
# Keep this file ASCII-only: Windows PowerShell 5.1 reads .ps1 as the ANSI codepage unless a BOM is
# present, so non-ASCII comments here would garble the script body.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchLicenses.ps1
#
# Every URL is pinned to a released version so a package cannot quietly pick up a different text,
# and each file is checked for a marker line taken from that license. The marker is what makes a
# failed download an error instead of a shipped stub: one fetch returned a 14-byte "404: Not Found"
# page and still looked like a file in the folder listing.

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)

$licenses = @(
    [pscustomobject]@{ File = 'Qt-LGPL-3.0.txt';        Url = 'https://www.gnu.org/licenses/lgpl-3.0.txt'; Marker = 'GNU LESSER GENERAL PUBLIC LICENSE' }
    [pscustomobject]@{ File = 'FFmpeg-GPL-3.0.txt';     Url = 'https://www.gnu.org/licenses/gpl-3.0.txt';  Marker = 'GNU GENERAL PUBLIC LICENSE' }
    [pscustomobject]@{ File = 'Tesseract-Apache-2.0.txt'; Url = 'https://www.apache.org/licenses/LICENSE-2.0.txt'; Marker = 'Apache License' }
    [pscustomobject]@{ File = 'TessData-Apache-2.0.txt';  Url = 'https://www.apache.org/licenses/LICENSE-2.0.txt'; Marker = 'Apache License' }
    [pscustomobject]@{ File = 'Leptonica-BSD-2.txt';    Url = 'https://raw.githubusercontent.com/DanBloomberg/leptonica/1.87.0/leptonica-license.txt'; Marker = 'Redistribution and use in source and binary forms' }
    [pscustomobject]@{ File = 'Spdlog-MIT.txt';         Url = 'https://raw.githubusercontent.com/gabime/spdlog/v1.17.0/LICENSE'; Marker = 'MIT License' }
    [pscustomobject]@{ File = 'GoogleTest-BSD-3-Clause.txt'; Url = 'https://raw.githubusercontent.com/google/googletest/v1.17.0/LICENSE'; Marker = 'Redistribution and use in source and binary forms' }
    # The vendored projection reports CPPWINRT_VERSION "2.0.250303.5" (see Vendor/WinRt/winrt/base.h),
    # but that build is only published as a NuGet package -- no git tag. 2.0.250303.1 is the nearest
    # tagged ref and its LICENSE is byte-identical to the one shipped with our generated headers.
    [pscustomobject]@{ File = 'WinRtProjection-MIT.txt'; Url = 'https://raw.githubusercontent.com/microsoft/cppwinrt/2.0.250303.1/LICENSE'; Marker = 'MIT License' }
)

New-Item -ItemType Directory -Force -Path 'licenses' | Out-Null

foreach ($license in $licenses) {
    $target = Join-Path 'licenses' $license.File
    # Re-downloading a file that already passes the marker check would make every packaging run
    # depend on the network; the pinned URL only matters when the text is missing or is a stub.
    if ((Test-Path $target) -and ((Get-Content -Raw -LiteralPath $target) -match [regex]::Escape($license.Marker))) {
        Write-Output "[ok] $($license.File) already in place"
        continue
    }
    if (Test-Path $target) { Remove-Item -Force -LiteralPath $target }
    & curl.exe --fail --silent --show-error --location --retry 5 --retry-all-errors --max-time 180 $license.Url --output $target
    if ($LASTEXITCODE -ne 0) { throw "$($license.File): download failed from $($license.Url) (exit $LASTEXITCODE)" }
    $text = Get-Content -Raw -LiteralPath $target
    if (-not $text -or ($text -notmatch [regex]::Escape($license.Marker))) {
        Remove-Item -Force -LiteralPath $target
        throw "$($license.File): fetched from $($license.Url) but it is not the license text (marker missing)"
    }
    Write-Output "[ok] $($license.File) fetched ($((Get-Item -LiteralPath $target).Length) bytes)"
}
