#Requires -Version 5.1
<#
.SYNOPSIS
Generate local combat data from the recipient's installed Elden Ring.
.DESCRIPTION
Run from the extracted Windows release. Reads the game without modifying it.
Requires 64-bit Python 3.14 with the py launcher and internet access for the
cryptography and zstandard Python packages on first use. No compiler is needed.
Generated data, including the action timing table, must remain local.
#>
[CmdletBinding()]
param(
    [string]$PackageDirectory = '',
    [string]$EldenRingDirectory = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Resolve this after parameter binding: advanced scripts invoked with -File in
# Windows PowerShell 5.1 can see an empty PSScriptRoot in parameter defaults.
if (-not $PackageDirectory) { $PackageDirectory = $PSScriptRoot }
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Run this installer in Windows PowerShell.'
}
$PackageDirectory = [IO.Path]::GetFullPath($PackageDirectory)
if (-not (Test-Path -LiteralPath (Join-Path $PackageDirectory 'openmw.exe') -PathType Leaf)) {
    throw 'Extract the complete release ZIP before importing assets.'
}
$running = @(Get-Process openmw,openmw-launcher,openmw-wizard -ErrorAction SilentlyContinue |
    Where-Object { $_.Path -and (Split-Path -Parent $_.Path) -ieq $PackageDirectory })
if ($running.Count) { throw 'Close the game and launcher in this package before importing assets.' }
if (-not $EldenRingDirectory) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select your installed ELDEN RING\Game folder.'
    $dialog.ShowNewFolderButton = $false
    try {
        if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) {
            throw 'Asset import was cancelled.'
        }
        $EldenRingDirectory = $dialog.SelectedPath
    } finally { $dialog.Dispose() }
}
$EldenRingDirectory = [IO.Path]::GetFullPath($EldenRingDirectory)
foreach ($name in @('regulation.bin', 'oo2core_6_win64.dll', 'Data3.bhd')) {
    if (-not (Test-Path -LiteralPath (Join-Path $EldenRingDirectory $name) -PathType Leaf)) {
        throw "The selected folder is missing $name. Select the installed ELDEN RING\Game directory."
    }
}
$pythonLauncher = Get-Command py.exe -ErrorAction SilentlyContinue
if (-not $pythonLauncher) { throw 'Install 64-bit Python 3.14 with its py launcher, then run this step again.' }
function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE. Correct the error and run import again." }
}
Invoke-Checked $pythonLauncher.Source @('-3.14', '-c',
    'import sys,struct; assert sys.version_info[:2] == (3,14) and struct.calcsize(''P'')==8, ''64-bit Python 3.14 required''')
$tools = Join-Path $PackageDirectory 'tools'
$scripts = Join-Path $tools 'scripts'
$keys = Join-Path $tools 'BinderKeys\EldenRing_PC\Key'
foreach ($relative in @('scripts\unpack_eldenring_assets.py', 'scripts\import_eldenring_timings.py',
                        'reference\tools\extract.py', 'BinderKeys\EldenRing_PC\Key\Data3.pem')) {
    if (-not (Test-Path -LiteralPath (Join-Path $tools $relative) -PathType Leaf)) {
        throw "The extracted release is missing tools\$relative. Extract the complete ZIP again."
    }
}
$work = Join-Path $PackageDirectory '.elden-assets'
$reference = Join-Path $work 'reference'
$python = Join-Path $work 'venv\Scripts\python.exe'
New-Item -ItemType Directory -Force -Path (Join-Path $reference 'src\sim'), (Join-Path $reference 'assets') | Out-Null
Copy-Item -LiteralPath (Join-Path $tools 'reference\tools') -Destination $reference -Recurse -Force
if (-not (Test-Path -LiteralPath $python -PathType Leaf)) {
    Invoke-Checked $pythonLauncher.Source @('-3.14', '-m', 'venv', (Join-Path $work 'venv'))
}
Invoke-Checked $python @('-m', 'pip', 'install', '--disable-pip-version-check', '--only-binary=:all:',
    'cryptography==50.0.2', 'zstandard==0.25.0')
Write-Host 'Reading your Elden Ring installation and generating local animations and timing data.'
Invoke-Checked $python @((Join-Path $scripts 'unpack_eldenring_assets.py'),
    '--game-dir', $EldenRingDirectory, '--keys-dir', $keys, '--reference-dir', $reference, '--generate')
$header = Join-Path $work 'imported_timings.hpp'
$timings = Join-Path $work 'action_timings.bin'
Invoke-Checked $python @((Join-Path $scripts 'import_eldenring_timings.py'),
    (Join-Path $reference 'src\sim\extracted.rs'), '--output', $header, '--runtime-output', $timings)
Invoke-Checked $python @((Join-Path $scripts 'install_eldenring_animations.py'),
    '--source', (Join-Path $reference 'assets\player_anims.bin'), '--timings-header', $header,
    '--require-paired', '--package', $PackageDirectory)
$destination = Join-Path $PackageDirectory 'resources\vfs\animations\eldencombat\action_timings.bin'
$temporary = "$destination.partial"
Copy-Item -LiteralPath $timings -Destination $temporary -Force
Move-Item -LiteralPath $temporary -Destination $destination -Force
Write-Host 'Generating local music and combat sounds.'
Invoke-Checked $python @((Join-Path $scripts 'import_eldenring_music.py'),
    '--game-dir', $EldenRingDirectory, '--keys-dir', $keys, '--output', $work)
Invoke-Checked $python @((Join-Path $scripts 'install_eldenring_music.py'),
    '--source', (Join-Path $work 'music\eldenring'), '--package', $PackageDirectory)
Invoke-Checked $python @((Join-Path $scripts 'import_eldenring_sounds.py'),
    '--game-dir', $EldenRingDirectory, '--keys-dir', $keys, '--output', $work)
Invoke-Checked $python @((Join-Path $scripts 'install_eldenring_sounds.py'),
    '--source', (Join-Path $work 'sound\eldenring\dodge'), '--package', $PackageDirectory)
Write-Host 'Elden Ring import completed. Open Start-EldenCombat-Launcher.cmd to choose your settings, then play.'
Write-Host 'Keep this configured folder and its generated data local. Share only the original release ZIP.'
