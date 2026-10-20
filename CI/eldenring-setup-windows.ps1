#Requires -Version 5.1
<#
.SYNOPSIS
Configure an extracted Windows x64 release with your Morrowind installation.
.DESCRIPTION
This script runs with Windows PowerShell and the bundled OpenMW INI importer.
It does not require Python, Git, a compiler, or administrator access.
Choose the Morrowind folder when prompted, or pass -MorrowindDirectory.
Existing profile settings, content order, input bindings and saves are preserved.
.EXAMPLE
.\eldenring-setup-windows.ps1 -MorrowindDirectory 'D:\Games\Morrowind'
.EXAMPLE
.\eldenring-setup-windows.ps1 -MorrowindDirectory 'D:\Games\Morrowind' -GameEncoding win1251
#>
[CmdletBinding()]
param(
    [string]$PackageDirectory = '',
    [string]$MorrowindDirectory = '',
    [ValidateSet('win1250', 'win1251', 'win1252')]
    [string]$GameEncoding = 'win1252',
    [switch]$PortableSaves
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Windows PowerShell 5.1 -File binds advanced-script parameter defaults before
# PSScriptRoot is populated. Resolve the default here, after parameter binding.
if (-not $PackageDirectory) { $PackageDirectory = $PSScriptRoot }

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    -not [Environment]::Is64BitOperatingSystem) {
    throw 'This release requires 64-bit Windows.'
}

$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
$utf8 = New-Object Text.UTF8Encoding -ArgumentList $false
$profile = Join-Path $PackageDirectory 'elden-profile'
$profileConfig = Join-Path $profile 'openmw.cfg'
$localConfig = Join-Path $PackageDirectory 'openmw.cfg'

function Get-QuotedConfigPath {
    param([string]$Value)
    # OpenMW quoted paths escape literal ampersands and quotation marks with &.
    return '"' + $Value.Replace('\', '/').Replace('&', '&&').Replace('"', '&"') + '"'
}

function Select-LocalProfile {
    # Keep the packaged fallback/resource configuration, but isolate preferences
    # from the recipient's other OpenMW installations for both launcher and game.
    $original = [IO.File]::ReadAllText($localConfig)
    $lines = @($original -split '\r?\n' | Where-Object { $_ -notmatch '^\s*config\s*=' })
    while ($lines.Count -gt 0 -and $lines[-1] -eq '') {
        $lines = @($lines | Select-Object -First ($lines.Count - 1))
    }
    $updated = ($lines + 'config="elden-profile"') -join "`r`n"
    $updated += "`r`n"
    if ($updated -ne $original) {
        [IO.File]::WriteAllText($localConfig, $updated, $utf8)
    }
}

foreach ($relative in @('openmw.exe', 'openmw-iniimporter.exe', 'openmw.cfg',
                        'defaults.bin', 'resources\vfs\builtin.omwscripts')) {
    if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory $relative) -PathType Leaf)) {
        throw "The release is incomplete: $relative is missing. Extract the entire ZIP into a writable folder first."
    }
}

if (Test-Path -LiteralPath $profileConfig -PathType Leaf) {
    Select-LocalProfile
    Write-Host "Existing profile preserved: $profile"
    Write-Host 'Use Start-EldenCombat-Launcher.cmd to change game-data paths or settings.'
    return
}

if (-not $MorrowindDirectory) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    try {
        $dialog.Description = 'Select your Morrowind installation (the folder containing Data Files and Morrowind.ini).'
        $dialog.ShowNewFolderButton = $false
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) {
            throw 'Setup was cancelled. Run Setup-EldenCombat.cmd when you are ready.'
        }
        $MorrowindDirectory = $dialog.SelectedPath
    } finally {
        $dialog.Dispose()
    }
}

$MorrowindDirectory = [IO.Path]::GetFullPath($MorrowindDirectory)
if ((Split-Path -Leaf $MorrowindDirectory.TrimEnd('\', '/')) -ieq 'Data Files') {
    $data = $MorrowindDirectory
    $MorrowindDirectory = Split-Path -Parent $MorrowindDirectory
} else {
    $data = Join-Path $MorrowindDirectory 'Data Files'
}
foreach ($required in @('Morrowind.esm', 'Morrowind.bsa')) {
    if (-not (Test-Path -LiteralPath (Join-Path $data $required) -PathType Leaf)) {
        throw "$required was not found in $data. Select an installed copy of Morrowind."
    }
}
$morrowindIni = Join-Path $MorrowindDirectory 'Morrowind.ini'
if (-not (Test-Path -LiteralPath $morrowindIni -PathType Leaf)) {
    throw "Morrowind.ini was not found in $MorrowindDirectory. Restore it from your Morrowind installation and run setup again."
}

# Stage the new configuration until the native importer has succeeded. Failed
# setup must not leave an apparently configured profile that subsequent runs skip.
$staging = Join-Path $PackageDirectory ('.elden-setup-' + [Guid]::NewGuid().ToString('N'))
try {
    [IO.Directory]::CreateDirectory($staging) | Out-Null
    $inputConfig = Join-Path $staging 'input.cfg'
    $outputConfig = Join-Path $staging 'openmw.cfg'
    $lines = @(
        'replace=content',
        'replace=fallback-archive',
        'data-local="./data"',
        $(if ($PortableSaves) { 'user-data="./user-data"' } else { 'user-data="?userdata?"' }),
        ('data=' + (Get-QuotedConfigPath $data)),
        ('encoding=' + $GameEncoding)
    )
    foreach ($name in @('Morrowind', 'Tribunal', 'Bloodmoon')) {
        foreach ($extension in @('esm', 'bsa')) {
            $file = Join-Path $data "$name.$extension"
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $actualName = (Get-Item -LiteralPath $file).Name
                $key = if ($extension -eq 'esm') { 'content' } else { 'fallback-archive' }
                $lines += "$key=$actualName"
            }
        }
    }
    [IO.File]::WriteAllText($inputConfig, ($lines -join "`r`n") + "`r`n", $utf8)
    $importer = Join-Path $PackageDirectory 'openmw-iniimporter.exe'
    & $importer --ini $morrowindIni --cfg $inputConfig --output $outputConfig --no-archives --encoding $GameEncoding
    if ($LASTEXITCODE -ne 0) {
        throw "The Morrowind INI import failed (exit code $LASTEXITCODE)."
    }
    # The native importer can report some errors without a failing exit code.
    # A separate output file and real fallback entries establish that it wrote
    # usable configuration; mere executable startup is insufficient.
    if (-not (Test-Path -LiteralPath $outputConfig -PathType Leaf)) {
        throw 'The Morrowind INI importer did not produce a configuration.'
    }
    $imported = [IO.File]::ReadAllText($outputConfig)
    if ($imported -notmatch '(?m)^fallback\s*=' -or
        $imported -notmatch '(?m)^content\s*=\s*Morrowind\.esm\s*$') {
        throw 'The Morrowind INI import produced no usable fallbacks or base game. Check Morrowind.ini and run setup again.'
    }

    [IO.Directory]::CreateDirectory($profile) | Out-Null
    [IO.Directory]::CreateDirectory((Join-Path $profile 'data')) | Out-Null
    $settings = Join-Path $profile 'settings.cfg'
    if (-not (Test-Path -LiteralPath $settings)) {
        [IO.File]::WriteAllText($settings, "[Game]`r`nelden ring combat = true`r`n", $utf8)
    }
    # File.Move refuses to overwrite a profile created concurrently.
    [IO.File]::Move($outputConfig, $profileConfig)
    Select-LocalProfile
} finally {
    if (Test-Path -LiteralPath $staging) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
}

Write-Host "Setup complete. Morrowind data: $data"
Write-Host "Settings and controls: $profile"
if ($PortableSaves) {
    Write-Host "Saved games: $(Join-Path $profile 'user-data\saves')"
} else {
    Write-Host 'Saved games use your Windows Documents\My Games\OpenMW\saves folder, including Documents redirection.'
}
Write-Host 'Open Start-EldenCombat-Launcher.cmd to choose display settings, or Start-EldenCombat.cmd to play.'
