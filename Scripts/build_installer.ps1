<#
.SYNOPSIS
    Builds the ZDE Windows installer with Inno Setup.

.DESCRIPTION
    Packages an existing Release build into a Blender-style setup executable.
    The script:
      * checks that the Release runtime exists,
      * regenerates the branded wizard artwork when needed,
      * stages the app-local MSVC runtime DLLs,
      * finds the Inno Setup compiler, and
      * compiles Scripts\setup.iss.

    It does not build ZDE itself. Build the Release configuration first, for
    example:
        cmake --preset windows-clang-ninja-release
        cmake --build --preset windows-clang-ninja-release

.PARAMETER Version
    Display version written into the installer. Defaults to the project
    version declared in CMakeLists.txt (for example "0.1.0").

.PARAMETER BuildDir
    Directory containing ZDE.exe and its runtime DLLs. Defaults to the
    windows-clang-ninja-release Release output.

.PARAMETER OutputDir
    Directory for the generated setup executable. Defaults to
    build\installer.

.PARAMETER RuntimeDir
    Staging directory for the app-local MSVC runtime. Defaults to
    build\installer\runtime.

.PARAMETER InnoSetupPath
    Full path to ISCC.exe. When omitted, common Inno Setup 6/7 locations and
    PATH are searched.

.PARAMETER SkipRuntime
    Do not stage the MSVC runtime. Use only when the target machine is known
    to have the Visual C++ Redistributable installed.

.PARAMETER ForceArtwork
    Regenerate the wizard images and setup icon even if they already exist.
#>
[CmdletBinding()]
param(
    [string]$Version,
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$RuntimeDir,
    [string]$InnoSetupPath,
    [switch]$SkipRuntime,
    [switch]$ForceArtwork
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = Split-Path -Parent $scriptDir

function Get-ProjectVersion {
    param([string]$Root)

    $cmakeLists = Join-Path $Root 'CMakeLists.txt'
    if (Test-Path -LiteralPath $cmakeLists) {
        $content = Get-Content -LiteralPath $cmakeLists -Raw
        $match = [regex]::Match($content, 'project\s*\([^)]*\bVERSION\s+([0-9]+(?:\.[0-9]+)*)')
        if ($match.Success) {
            return $match.Groups[1].Value
        }
    }

    throw "Could not determine the project version from $cmakeLists. Pass -Version explicitly."
}

if (-not $Version) {
    $Version = Get-ProjectVersion -Root $repoRoot
}

if (-not $BuildDir) {
    $BuildDir = Join-Path $repoRoot 'build\windows-clang-ninja-release\bin\Release'
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $repoRoot 'build\installer'
}
if (-not $RuntimeDir) {
    $RuntimeDir = Join-Path $OutputDir 'runtime'
}

if (-not (Test-Path -LiteralPath $BuildDir)) {
    throw "Release build directory not found: $BuildDir"
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$exePath = Join-Path $BuildDir 'ZDE.exe'
if (-not (Test-Path -LiteralPath $exePath)) {
    throw "ZDE.exe not found in $BuildDir. Build the Release configuration first."
}

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

function ConvertTo-NumericVersion {
    param([string]$Value)

    $clean = $Value.Trim() -replace '^[vV]', ''
    $match = [regex]::Match($clean, '^\d+(\.\d+){0,3}')
    if (-not $match.Success) {
        return '0.1.0.0'
    }

    $parts = @($match.Value.Split('.'))
    while ($parts.Count -lt 4) {
        $parts += '0'
    }
    return ($parts[0..3] -join '.')
}

function Find-MsvcRuntimeSource {
    $candidates = New-Object System.Collections.Generic.List[object]

    $buildCrt = Join-Path $BuildDir 'msvcp140.dll'
    if (Test-Path -LiteralPath $buildCrt) {
        $candidates.Add([pscustomobject]@{ Path = $BuildDir; Rank = '0.0.0.0' })
    }

    $vsRoots = @(
        (Join-Path $env:ProgramFiles 'Microsoft Visual Studio\2022'),
        (Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022')
    )

    foreach ($vsRoot in $vsRoots) {
        if (-not (Test-Path -LiteralPath $vsRoot)) {
            continue
        }

        $crtDirs = Get-ChildItem -Path (Join-Path $vsRoot '*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT') -Directory -ErrorAction SilentlyContinue
        foreach ($crtDir in $crtDirs) {
            $versionFolder = $crtDir.Parent.Parent.Name
            $candidates.Add([pscustomobject]@{ Path = $crtDir.FullName; Rank = $versionFolder })
        }
    }

    if ($candidates.Count -gt 0) {
        $best = $candidates |
            Sort-Object -Property @{ Expression = { try { [version]$_.Rank } catch { [version]'0.0.0.0' } }; Descending = $true } |
            Select-Object -First 1
        return $best.Path
    }

    # Last resort: a development machine normally has the redistributable
    # installed system-wide. This keeps the package runnable, but the Visual
    # Studio redist directory is preferred because it matches the toolchain.
    $system32 = Join-Path $env:WINDIR 'System32'
    if (Test-Path -LiteralPath (Join-Path $system32 'msvcp140.dll')) {
        return $system32
    }

    return $null
}

function Stage-MsvcRuntime {
    param([string]$Destination)

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Destination -Filter '*.dll' -File -ErrorAction SilentlyContinue |
        Remove-Item -Force

    $source = Find-MsvcRuntimeSource
    if (-not $source) {
        Write-Warning 'MSVC runtime DLLs were not found. The installer will not be self-contained.'
        return
    }

    $runtimeFiles = @(
        'concrt140.dll',
        'msvcp140.dll',
        'msvcp140_1.dll',
        'msvcp140_2.dll',
        'msvcp140_atomic_wait.dll',
        'msvcp140_codecvt_ids.dll',
        'vccorlib140.dll',
        'vcruntime140.dll',
        'vcruntime140_1.dll',
        'vcruntime140_threads.dll'
    )

    $copied = 0
    foreach ($file in $runtimeFiles) {
        $sourceFile = Join-Path $source $file
        if (Test-Path -LiteralPath $sourceFile) {
            Copy-Item -LiteralPath $sourceFile -Destination $Destination -Force
            $copied++
        }
    }

    # Also stage app-local UCRT DLLs for clean Windows 7 systems that lack KB2999226
    $ucrtDirs = @(
        (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Redist\10.0.26100.0\ucrt\DLLs\x64'),
        (Join-Path $env:ProgramFiles 'Windows Kits\10\Redist\10.0.26100.0\ucrt\DLLs\x64')
    )
    foreach ($ucrtDir in $ucrtDirs) {
        if (Test-Path -LiteralPath $ucrtDir) {
            Get-ChildItem -LiteralPath $ucrtDir -Filter '*.dll' -File | ForEach-Object {
                Copy-Item -LiteralPath $_.FullName -Destination $Destination -Force
                $copied++
            }
            break
        }
    }

    if ($copied -eq 0) {
        Write-Warning "No MSVC runtime DLLs were copied from $source."
    }
    else {
        Write-Host "Staged $copied MSVC runtime / UCRT DLL(s) from $source" -ForegroundColor DarkGray
    }
}

function Find-Iscc {
    $candidates = @(
        (Join-Path $env:ProgramFiles 'Inno Setup 7\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 7\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 7\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return $candidate
        }
    }

    $command = Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    return $null
}

# --------------------------------------------------------------------------
# Artwork
# --------------------------------------------------------------------------
# Artwork lives next to the scripts (Scripts\), not in a separate installer
# folder. generate_assets.ps1 writes it there by default.
$wizardImage  = Join-Path $scriptDir 'maskot_wizard.bmp'
$smallImage   = Join-Path $scriptDir 'wizard_small.bmp'
$setupIcon    = Join-Path $scriptDir 'zenvra_setup.ico'
if ($ForceArtwork -or -not (Test-Path -LiteralPath $wizardImage) -or -not (Test-Path -LiteralPath $smallImage) -or -not (Test-Path -LiteralPath $setupIcon)) {
    Write-Host 'Generating installer artwork...' -ForegroundColor Cyan
    & (Join-Path $scriptDir 'generate_assets.ps1') -Force
}
else {
    Write-Host 'Using existing installer artwork.' -ForegroundColor DarkGray
}

# --------------------------------------------------------------------------
# App-local MSVC runtime
# --------------------------------------------------------------------------
if ($SkipRuntime) {
    Write-Host 'Skipping MSVC runtime staging (-SkipRuntime).' -ForegroundColor Yellow
}
else {
    Write-Host 'Staging MSVC runtime...' -ForegroundColor Cyan
    Stage-MsvcRuntime -Destination $RuntimeDir
}
if (Test-Path -LiteralPath $RuntimeDir) {
    $RuntimeDir = (Resolve-Path -LiteralPath $RuntimeDir).Path
}

# --------------------------------------------------------------------------
# Compile
# --------------------------------------------------------------------------
if (-not $InnoSetupPath) {
    $InnoSetupPath = Find-Iscc
}
if (-not $InnoSetupPath -or -not (Test-Path -LiteralPath $InnoSetupPath)) {
    throw 'Inno Setup compiler (ISCC.exe) not found. Install Inno Setup 7 or pass -InnoSetupPath.'
}
$InnoSetupPath = (Resolve-Path -LiteralPath $InnoSetupPath).Path

$numericVersion = ConvertTo-NumericVersion -Value $Version
$setupScript = Join-Path $scriptDir 'setup.iss'
if (-not (Test-Path -LiteralPath $setupScript)) {
    throw "Installer script not found: $setupScript"
}

$isccArgs = @(
    '--quiet',
    "/DMyAppVersion=$Version",
    "/DMyAppVersionNumeric=$numericVersion",
    "/DBuildDir=$BuildDir",
    "/DRuntimeDir=$RuntimeDir",
    "/DOutputDir=$OutputDir",
    $setupScript
)

Write-Host "Compiling installer with $InnoSetupPath..." -ForegroundColor Cyan
& $InnoSetupPath @isccArgs
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup compilation failed with exit code $LASTEXITCODE."
}

$installerPath = Join-Path $OutputDir "ZDE-$Version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installerPath)) {
    throw "Installer was compiled but not found at the expected path: $installerPath"
}

$sizeMb = [Math]::Round((Get-Item -LiteralPath $installerPath).Length / 1MB, 2)
Write-Host ''
Write-Host 'Installer ready:' -ForegroundColor Green
Write-Host "  $installerPath ($sizeMb MB)"

# Also create a self-contained portable ZIP package
$portableZip = Join-Path $OutputDir "ZDE-$Version-windows-x64-portable.zip"
Write-Host "Creating portable archive: $portableZip..." -ForegroundColor Cyan
$portableStage = Join-Path $OutputDir "portable_stage"
if (Test-Path $portableStage) { Remove-Item $portableStage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $portableStage | Out-Null

$excludes = @("ZDEUnitTests.exe","gtest.dll","gtest_main.dll","ucrtbased.dll","libomp140d.x86_64.dll","msvcp140d_*.dll","*d.dll","*.pdb")
Get-ChildItem -Path $BuildDir -Recurse | ForEach-Object {
    $rel = $_.FullName.Substring($BuildDir.Length).TrimStart('\', '/')
    $dest = Join-Path $portableStage $rel
    if ($_.PSIsContainer) {
        New-Item -ItemType Directory -Force -Path $dest | Out-Null
    } else {
        $name = $_.Name
        $skip = $false
        foreach ($pat in $excludes) { if ($name -like $pat) { $skip = $true; break } }
        if (-not $skip) {
            $parent = Split-Path -Parent $dest
            if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Force -Path $parent | Out-Null }
            Copy-Item -LiteralPath $_.FullName -Destination $dest -Force
        }
    }
}
if (Test-Path $RuntimeDir) {
    Get-ChildItem -Path $RuntimeDir -Filter '*.dll' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $portableStage -Force
    }
}
if (Test-Path $portableZip) { Remove-Item $portableZip -Force }
Compress-Archive -Path "$portableStage\*" -DestinationPath $portableZip -CompressionLevel Optimal
Remove-Item $portableStage -Recurse -Force

$zipSizeMb = [Math]::Round((Get-Item -LiteralPath $portableZip).Length / 1MB, 2)
Write-Host "Portable package ready: $portableZip ($zipSizeMb MB)" -ForegroundColor Green
