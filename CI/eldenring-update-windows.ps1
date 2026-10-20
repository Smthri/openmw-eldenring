#Requires -Version 5.1
<#
.SYNOPSIS
Copy the updated WSL sources/assets, rebuild, and refresh the existing game package.
.DESCRIPTION
Run this script in native Windows PowerShell through its WSL UNC path. Uses the
existing Documents\openmw build and unpacked package when present. Preserves
settings and saves, and creates an updated ZIP. The profile generator repairs
an empty default save path when exactly one previous Documents folder has saves.
#>
[CmdletBinding()]
param(
    [string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot),
    [string]$DestinationDirectory = (Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'openmw'),
    [string]$PackageDirectory = '',
    [string]$AnimationsSource = '',
    [string]$MusicSource = '',
    [string]$SoundsSource = '',
    [ValidateSet('2026', '2022')]
    [string]$VisualStudioVersion = '2026',
    [string]$MorrowindDirectory = 'C:\Program Files (x86)\Steam\steamapps\common\Morrowind',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1)
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this update in native Windows PowerShell.'
}
# Refuse before copying sources or assets as well as before replacing executables.
$running = @(Get-Process openmw,openmw-launcher,openmw-wizard -ErrorAction SilentlyContinue)
if ($running.Length -gt 0) {
    throw 'Close OpenMW, its launcher and wizard before updating; no files were copied.'
}
$DestinationDirectory = [IO.Path]::GetFullPath($DestinationDirectory)
if (-not $PackageDirectory) {
    $existingPackage = Join-Path $DestinationDirectory 'OpenMW-EldenCombat-Windows-x64'
    if (Test-Path -LiteralPath (Join-Path $existingPackage 'openmw.exe') -PathType Leaf) {
        $PackageDirectory = $existingPackage
    } else {
        $PackageDirectory = Join-Path $DestinationDirectory 'build-eldenring-package-x64'
    }
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$sourceRoot = [IO.Path]::GetFullPath($SourceDirectory)
if ($sourceRoot.TrimEnd('\') -ine $DestinationDirectory.TrimEnd('\')) {
    & (Join-Path $sourceRoot 'CI\eldenring-copy-to-documents.ps1') `
        -SourceDirectory $sourceRoot -DestinationDirectory $DestinationDirectory `
        -AnimationsSource $AnimationsSource -MusicSource $MusicSource -SoundsSource $SoundsSource
    # Copy validation selected and installed these inputs into the destination.
    # Build from that verified copy, without reading a changing external bank again.
    $AnimationsSource = ''
    $MusicSource = ''
    $SoundsSource = ''
}
& (Join-Path $DestinationDirectory 'CI\eldenring-build-windows.ps1') `
    -InstallDirectory $PackageDirectory -MorrowindDirectory $MorrowindDirectory -Jobs $Jobs `
    -VisualStudioVersion $VisualStudioVersion `
    -AnimationsSource $AnimationsSource -MusicSource $MusicSource -SoundsSource $SoundsSource
Write-Host "Updated package: $PackageDirectory"
Write-Host 'Open the launcher to adjust display, graphics, gameplay and music settings.'
