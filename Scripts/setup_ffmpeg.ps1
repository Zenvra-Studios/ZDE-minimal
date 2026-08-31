<#
.SYNOPSIS
    Downloads and configures prebuilt FFmpeg Windows x64 Dev/Shared binaries into ThirdParty/ffmpeg.
#>

[CmdletBinding()]
param (
    [string]$TargetDir = "$PSScriptRoot\..\ThirdParty\ffmpeg",
    [string]$Version = "7.0.2"
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================" -ForegroundColor Cyan
Write-Host "  ZDE Studio - FFmpeg Setup for Windows  " -ForegroundColor Cyan
Write-Host "==========================================" -ForegroundColor Cyan

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if ([string]::IsNullOrWhiteSpace($ScriptDir)) {
    $ScriptDir = "$PSScriptRoot"
}
if ([string]::IsNullOrWhiteSpace($ScriptDir)) {
    $ScriptDir = Join-Path (Get-Location) "Scripts"
}

if ([string]::IsNullOrWhiteSpace($TargetDir) -or $TargetDir -eq "\..\ThirdParty\ffmpeg") {
    $TargetDir = Join-Path (Split-Path -Parent $ScriptDir) "ThirdParty\ffmpeg"
} else {
    $TargetDir = [System.IO.Path]::GetFullPath($TargetDir)
}

Write-Host "[*] Destination directory: $TargetDir" -ForegroundColor Yellow

if (Test-Path "$TargetDir\include\libavcodec\avcodec.h") {
    Write-Host "[+] FFmpeg headers and libraries already exist in $TargetDir" -ForegroundColor Green
    exit 0
}

# Ensure destination directory exists
if (-not (Test-Path $TargetDir)) {
    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
}

$TempZip = "$env:TEMP\ffmpeg-dev.zip"
# Using standard BtbN / Gyan.dev release builds or github release assets
$DownloadUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip"

Write-Host "[*] Downloading FFmpeg Windows x64 shared/dev bundle..." -ForegroundColor Cyan
Write-Host "    URL: $DownloadUrl" -ForegroundColor Gray

try {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 -bor [Net.SecurityProtocolType]::Tls13
    Invoke-WebRequest -Uri $DownloadUrl -OutFile $TempZip -UseBasicParsing
} catch {
    Write-Warning "Failed to download automatically from primary mirror ($DownloadUrl)."
    Write-Warning "Please download the FFmpeg Shared build manually and extract into: $TargetDir"
    exit 1
}

Write-Host "[*] Extracting archive..." -ForegroundColor Cyan
$ExtractTemp = "$env:TEMP\ffmpeg_extracted"
if (Test-Path $ExtractTemp) {
    Remove-Item -Path $ExtractTemp -Recurse -Force | Out-Null
}

Expand-Archive -Path $TempZip -DestinationPath $ExtractTemp -Force

$SubFolders = Get-ChildItem -Path $ExtractTemp -Directory
if ($SubFolders.Count -gt 0) {
    $RootExtracted = $SubFolders[0].FullName
    Copy-Item -Path "$RootExtracted\*" -Destination $TargetDir -Recurse -Force
} else {
    Copy-Item -Path "$ExtractTemp\*" -Destination $TargetDir -Recurse -Force
}

# Cleanup
Remove-Item -Path $TempZip -Force -ErrorAction SilentlyContinue
Remove-Item -Path $ExtractTemp -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "[+] FFmpeg setup complete! Headers and libraries installed to:" -ForegroundColor Green
Write-Host "    $TargetDir" -ForegroundColor White
