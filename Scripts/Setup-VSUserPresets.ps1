# ==============================================================================
# Setup-VSUserPresets.ps1
# Generates CMakeUserPresets.json for Visual Studio 2022-2026 support
# without modifying CMakePresets.json.
# ==============================================================================

$rootDir = Split-Path -Parent $PSScriptRoot
$userPresetsPath = Join-Path $rootDir "CMakeUserPresets.json"

$userPresetsJson = @'
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 22,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "windows-vs2026-debug",
      "displayName": "Windows Debug (Visual Studio 2026)",
      "description": "Generate Visual Studio solution/project files for Debug builds in Visual Studio 2026.",
      "inherits": "base-msvc",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "generator": "Visual Studio 18 2026",
      "architecture": "x64"
    },
    {
      "name": "windows-vs2026-release",
      "displayName": "Windows Release (Visual Studio 2026)",
      "description": "Generate Visual Studio solution/project files for Release builds in Visual Studio 2026.",
      "inherits": "base-msvc",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "generator": "Visual Studio 18 2026",
      "architecture": "x64"
    },
    {
      "name": "windows-vs-auto-debug",
      "displayName": "Windows Debug (Visual Studio Auto/Default)",
      "description": "Generate Visual Studio project files using the default Visual Studio version installed.",
      "inherits": "base-msvc",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "architecture": "x64"
    },
    {
      "name": "windows-vs-auto-release",
      "displayName": "Windows Release (Visual Studio Auto/Default)",
      "description": "Generate Visual Studio project files using the default Visual Studio version installed.",
      "inherits": "base-msvc",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "architecture": "x64"
    },
    {
      "name": "windows-msvc-ninja-debug",
      "displayName": "Windows Debug (Ninja + MSVC Environment)",
      "description": "Single-config Debug build using Ninja with active MSVC environment (VS 2022 to VS 2026).",
      "inherits": "base-ninja",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    },
    {
      "name": "windows-msvc-ninja-release",
      "displayName": "Windows Release (Ninja + MSVC Environment)",
      "description": "Single-config Release build using Ninja with active MSVC environment (VS 2022 to VS 2026).",
      "inherits": "base-ninja",
      "condition": {
        "type": "equals",
        "lhs": "${hostSystemName}",
        "rhs": "Windows"
      },
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "windows-vs2026-debug",
      "displayName": "Build Windows Debug (Visual Studio 2026)",
      "configurePreset": "windows-vs2026-debug",
      "configuration": "Debug"
    },
    {
      "name": "windows-vs2026-release",
      "displayName": "Build Windows Release (Visual Studio 2026)",
      "configurePreset": "windows-vs2026-release",
      "configuration": "Release"
    },
    {
      "name": "windows-vs-auto-debug",
      "displayName": "Build Windows Debug (Visual Studio Auto/Default)",
      "configurePreset": "windows-vs-auto-debug",
      "configuration": "Debug"
    },
    {
      "name": "windows-vs-auto-release",
      "displayName": "Build Windows Release (Visual Studio Auto/Default)",
      "configurePreset": "windows-vs-auto-release",
      "configuration": "Release"
    },
    {
      "name": "windows-msvc-ninja-debug",
      "displayName": "Build Windows Debug (Ninja + MSVC)",
      "configurePreset": "windows-msvc-ninja-debug"
    },
    {
      "name": "windows-msvc-ninja-release",
      "displayName": "Build Windows Release (Ninja + MSVC)",
      "configurePreset": "windows-msvc-ninja-release"
    }
  ]
}
'@

Set-Content -Path $userPresetsPath -Value $userPresetsJson -Encoding UTF8
Write-Host "Created $userPresetsPath successfully!" -ForegroundColor Green
