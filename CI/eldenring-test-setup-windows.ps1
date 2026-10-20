#Requires -Version 5.1
<#
.SYNOPSIS
Exercise release setup, preservation, quoting and retry behavior on Windows.
.DESCRIPTION
Uses a tiny compiled INI-importer fixture, never an installed game or profile.
The release must also be tested separately with its real importer and engine.
#>
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$setup = Join-Path $PSScriptRoot 'eldenring-setup-windows.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('elden-setup-tests-' + [Guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding -ArgumentList $false
$checks = 0

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function New-Package {
    param([string]$Name)
    $package = Join-Path $root $Name
    [IO.Directory]::CreateDirectory((Join-Path $package 'resources\vfs')) | Out-Null
    foreach ($file in @('openmw.exe', 'defaults.bin', 'resources\vfs\builtin.omwscripts')) {
        [IO.File]::WriteAllText((Join-Path $package $file), 'fixture', $utf8)
    }
    [IO.File]::WriteAllText((Join-Path $package 'openmw.cfg'),
        "resources=./resources`r`ndata=./resources/vfs-mw`r`nconfig=`"?userconfig?`"`r`nfallback=kept,1`r`n", $utf8)
    Copy-Item -LiteralPath $script:fixture -Destination (Join-Path $package 'openmw-iniimporter.exe')
    return $package
}

function Assert-SetupFailure {
    param([string]$Package, [string]$Game, [string]$Expected)
    $failed = $false
    try {
        & $setup -PackageDirectory $Package -MorrowindDirectory $Game
    } catch {
        $failed = $true
        Assert-True ($_.Exception.Message -match $Expected) "Unexpected failure: $_"
    }
    Assert-True $failed 'Setup unexpectedly succeeded.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $Package 'elden-profile\openmw.cfg'))) 'Failed setup left an active profile.'
    Assert-True (@(Get-ChildItem -LiteralPath $Package -Directory -Filter '.elden-setup-*').Count -eq 0) 'Failed setup left staging files.'
}

try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $fixture = Join-Path $root 'fixture.exe'
    Add-Type -OutputAssembly $fixture -OutputType ConsoleApplication -TypeDefinition @'
using System;
using System.IO;
using System.Text;
public static class EldenSetupImporterFixture {
    public static int Main(string[] args) {
        string input = null, output = null;
        bool noArchives = false, gameFiles = false;
        for (int i = 0; i < args.Length; ++i) {
            if (args[i] == "--cfg") input = args[++i];
            else if (args[i] == "--output") output = args[++i];
            else if (args[i] == "--no-archives") noArchives = true;
            else if (args[i] == "--game-files") gameFiles = true;
        }
        if (!noArchives || gameFiles) return 13;
        string mode = Environment.GetEnvironmentVariable("ELDEN_SETUP_TEST_IMPORTER");
        if (mode == "fail") return 7;
        if (mode == "no-output") return 0;
        string text = File.ReadAllText(input);
        if (mode != "empty-fallbacks") text += "fallback=Weather_Clear_Cloud_Texture,fixture.dds\r\n";
        File.WriteAllText(output, text, new UTF8Encoding(false));
        return 0;
    }
}
'@
    # Non-ASCII characters are composed without a UTF-8 BOM requirement in PS5.1.
    $unicode = [string][char]0x043F + [char]0x0430 + [char]0x043A
    $game = Join-Path $root ("$unicode Morrowind & [game]")
    $data = Join-Path $game 'Data Files'
    [IO.Directory]::CreateDirectory($data) | Out-Null
    foreach ($name in @('Morrowind.esm', 'Morrowind.bsa', 'Tribunal.esm', 'Tribunal.bsa')) {
        [IO.File]::WriteAllText((Join-Path $data $name), 'fixture', $utf8)
    }
    [IO.File]::WriteAllText((Join-Path $game 'Morrowind.ini'), "[Weather Clear]`r`nCloud Texture=fixture.dds`r`n", $utf8)

    $package = New-Package "$unicode package & [files]"
    & $setup -PackageDirectory $package -MorrowindDirectory $data -GameEncoding win1251
    $profile = Join-Path $package 'elden-profile'
    $config = [IO.File]::ReadAllText((Join-Path $profile 'openmw.cfg'))
    Assert-True ($config.Contains('user-data="?userdata?"')) 'Standard save token missing.'
    Assert-True ($config.Contains('data="' + $data.Replace('\', '/').Replace('&', '&&') + '"')) 'Unicode/ampersand game path was not escaped correctly.'
    Assert-True ($config.Contains('content=Morrowind.esm') -and $config.Contains('content=Tribunal.esm') -and -not $config.Contains('Bloodmoon')) 'Expansion selection is incorrect.'
    Assert-True ($config.Contains('encoding=win1251')) 'Requested game encoding was lost.'
    Assert-True ($config.Contains('fallback=Weather_Clear_Cloud_Texture,fixture.dds')) 'INI fallbacks were not imported.'
    $local = [IO.File]::ReadAllText((Join-Path $package 'openmw.cfg'))
    Assert-True ($local.Contains('config="elden-profile"') -and -not $local.Contains('?userconfig?') -and $local.Contains('fallback=kept,1')) 'Package defaults or profile isolation were lost.'
    Assert-True (-not (Test-Path -LiteralPath (Join-Path $profile 'user-data'))) 'Setup created a portable save directory by default.'

    $files = @{
        'openmw.cfg' = "user-data=`"D:/custom saves`"`r`ncontent=Morrowind.esm`r`ncontent=custom.esp`r`n"
        'settings.cfg' = "[Video]`r`nresolution x = 1600`r`n[Game]`r`nelden ring combat = false`r`n"
        'input_v3.xml' = 'custom bindings'
        'user-data\saves\character\slot.omwsave' = 'do not change this saved game'
    }
    foreach ($name in $files.Keys) {
        $path = Join-Path $profile $name
        [IO.Directory]::CreateDirectory((Split-Path -Parent $path)) | Out-Null
        [IO.File]::WriteAllText($path, $files[$name], $utf8)
    }
    & $setup -PackageDirectory $package -MorrowindDirectory (Join-Path $root 'missing game') -PortableSaves
    foreach ($name in $files.Keys) {
        Assert-True ([IO.File]::ReadAllText((Join-Path $profile $name)) -ceq $files[$name]) "Existing $name changed."
    }
    Assert-True ([IO.File]::ReadAllText((Join-Path $package 'openmw.cfg')) -ceq $local) 'Repeated setup rewrote local configuration.'

    $portable = New-Package 'portable'
    & $setup -PackageDirectory $portable -MorrowindDirectory $game -PortableSaves
    Assert-True ([IO.File]::ReadAllText((Join-Path $portable 'elden-profile\openmw.cfg')).Contains('user-data="./user-data"')) 'Explicit portable save mode failed.'

    $retry = New-Package 'retry'
    Assert-SetupFailure $retry (Join-Path $root 'missing game') 'Morrowind.esm'
    $env:ELDEN_SETUP_TEST_IMPORTER = 'fail'
    Assert-SetupFailure $retry $game 'exit code 7'
    $env:ELDEN_SETUP_TEST_IMPORTER = 'no-output'
    Assert-SetupFailure $retry $game 'did not produce'
    $env:ELDEN_SETUP_TEST_IMPORTER = 'empty-fallbacks'
    Assert-SetupFailure $retry $game 'no usable fallbacks'
    Remove-Item Env:ELDEN_SETUP_TEST_IMPORTER
    & $setup -PackageDirectory $retry -MorrowindDirectory $game
    Assert-True (Test-Path -LiteralPath (Join-Path $retry 'elden-profile\openmw.cfg')) 'Retry after failed setup did not succeed.'

    # Exercise the actual native -File entrypoint, not just & script invocation.
    # In PS5.1 advanced-script defaults can see an empty PSScriptRoot here.
    $native = New-Package 'native -File package'
    $nativeScript = Join-Path $native 'Setup-EldenCombat.ps1'
    Copy-Item -LiteralPath $setup -Destination $nativeScript
    $powershell = Join-Path $PSHOME 'powershell.exe'
    & $powershell -NoProfile -ExecutionPolicy Bypass -File $nativeScript -MorrowindDirectory $game
    Assert-True ($LASTEXITCODE -eq 0) 'Native -File setup failed without an explicit PackageDirectory.'
    Assert-True (Test-Path -LiteralPath (Join-Path $native 'elden-profile\openmw.cfg')) 'Native -File setup used the wrong default package directory.'

    # The asset helper must also resolve its directory before checking its
    # runtime. An incomplete fixture must reach that check, without extracting.
    $importDirectory = Join-Path $root 'native import missing runtime'
    [IO.Directory]::CreateDirectory($importDirectory) | Out-Null
    $importScript = Join-Path $importDirectory 'Import-EldenRing.ps1'
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'eldenring-import-assets-windows.ps1') -Destination $importScript
    $startInfo = New-Object Diagnostics.ProcessStartInfo
    $startInfo.FileName = $powershell
    $startInfo.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $importScript + '" -EldenRingDirectory "' + $game + '"'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    $errorOutput = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    Assert-True ($process.ExitCode -ne 0) 'Incomplete native import fixture unexpectedly passed.'
    Assert-True ($errorOutput.Contains('Extract the complete release ZIP')) 'Native -File import did not resolve its default package directory.'
    $process.Dispose()

    Write-Host "PASS: $checks release-setup checks."
} finally {
    Remove-Item Env:ELDEN_SETUP_TEST_IMPORTER -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
