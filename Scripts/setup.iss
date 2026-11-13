; ============================================================================
; ZDE Windows Installer (Inno Setup)
; ----------------------------------------------------------------------------
; A Blender-style Windows setup wizard for Zenvra Development Studio:
;   Welcome -> License -> Destination -> Start Menu -> Additional Tasks
;   -> Ready -> Installing -> Finish
;
; Build with:
;   powershell -ExecutionPolicy Bypass -File Scripts\build_installer.ps1
;
; Or directly:
;   "C:\Program Files\Inno Setup 7\ISCC.exe" Scripts\setup.iss
;
; All paths are derived from the location of this script, so it can be
; compiled from any working directory. Override BuildDir / OutputDir /
; RuntimeDir / MyAppVersion from the command line when needed.
; ============================================================================

#define MyAppName "ZDE"
#define MyAppFullName "ZDE"
#define MyAppPublisher "Zenvra Studio"
#define MyAppURL "https://www.zenvrahub.com/"
#define MyAppExeName "ZDE.exe"
#define MyAppCopyright "Copyright (C) 2026 Zenvra Studio"

; SourcePath is the directory of this script (Scripts\) and includes a
; trailing backslash, so extract twice to reach the repository root.
#define RepoRoot ExtractFileDir(ExtractFileDir(SourcePath))

; Installer artwork is generated next to this script by generate_assets.ps1.
; Reference it relative to the script directory ("."), not an installer
; subfolder.
#define InstallerAssets AddBackslash(SourcePath)
#define SetupIcon InstallerAssets + "zenvra_setup.ico"
#define MascotWizardImage InstallerAssets + "maskot_wizard.bmp"
#define LicenseFile RepoRoot + "\LICENSE"

#ifndef BuildDir
  #define BuildDir RepoRoot + "\build\windows-clang-ninja-release\bin\Release"
#endif

#ifndef RuntimeDir
  #define RuntimeDir RepoRoot + "\build\installer\runtime"
#endif

#ifndef OutputDir
  #define OutputDir RepoRoot + "\build\installer"
#endif

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif

#ifndef MyAppVersionNumeric
  #define MyAppVersionNumeric "0.1.0.0"
#endif

#define DoubleAmp(Value) StringChange(Value, "&", "&&")

#if !FileExists(BuildDir + "\ZDE.exe")
  #error ZDE.exe was not found in BuildDir. Build the Release configuration first, or pass /DBuildDir=...
#endif

#if !FileExists(SetupIcon)
  #error Installer icon not found. Run Scripts\generate_assets.ps1 first.
#endif

#if !FileExists(MascotWizardImage)
  #error Mascot wizard image not found. Run Scripts\generate_assets.ps1 first.
#endif

#if !FileExists(InstallerAssets + "wizard_small.bmp")
  #error Installer small wizard image not found. Run Scripts\generate_assets.ps1 first.
#endif

[Setup]
; Keep this AppId stable across releases so upgrades replace the previous
; installation instead of creating a second entry in Apps & features.
AppId={{9FD54ABA-60F0-445A-902B-B56717209D01}
AppName={#MyAppName}
AppVerName={#MyAppName} {#MyAppVersion}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
AppCopyright={#MyAppCopyright}
VersionInfoVersion={#MyAppVersionNumeric}
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppFullName} Setup
VersionInfoProductName={#MyAppFullName}
VersionInfoProductVersion={#MyAppVersionNumeric}
VersionInfoCopyright={#MyAppCopyright}

; Blender-style install location, with a per-user fallback when the user
; chooses "Install for me only" on the install-mode page.
DefaultDirName={autopf}\Zenvra\ZDE
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
DisableProgramGroupPage=no
UsePreviousAppDir=yes
UsePreviousGroup=yes
UsePreviousTasks=yes
UsePreviousPrivileges=yes

; Show the standard "Install for all users / Install for me only" prompt.
; Choosing "all users" triggers elevation; the default is per-user.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

; 64-bit only, including Windows 11 on Arm (x64 emulation).
; Minimum Windows 7 (6.1) or higher.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1

; File associations/context menus need Explorer to be notified.
ChangesAssociations=yes
CloseApplications=yes
RestartApplications=no
SetupLogging=yes

; Modern wizard with ZDE branding. The wizard images are generated from
; Assets\image\maskot.jpg and the setup/taskbar icon from
; Assets\icons\zenvra_logo.png by Scripts\generate_assets.ps1.
WizardStyle=modern
WizardSizePercent=110
WizardImageFile={#MascotWizardImage}
WizardSmallImageFile={#InstallerAssets}wizard_small.bmp
SetupIconFile={#SetupIcon}
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppFullName} {#MyAppVersion}
LicenseFile={#LicenseFile}
DefaultDialogFontName=Segoe UI
ShowLanguageDialog=no
DisableWelcomePage=no
DisableDirPage=no
DisableReadyPage=no
DisableFinishedPage=no

; Output and compression.
OutputDir={#OutputDir}
OutputBaseFilename=ZDE-{#MyAppVersion}-windows-x64-setup
Compression=lzma2/max
SolidCompression=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
AddContextMenu=Add "&Open with ZDE" to the Explorer context menu
WindowsIntegration=Windows integration:

[Messages]
WelcomeLabel1=Welcome to the [name] Setup Wizard
WelcomeLabel2=This will install [name/ver] on your computer.%n%nZDE is a development platform for building creative tools that unify UI, graphics, and audio in a single ecosystem.%n%nIt is recommended that you close all other applications before continuing.

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "contextmenu"; Description: "{cm:AddContextMenu}"; GroupDescription: "{cm:WindowsIntegration}"

[Files]
; Ship the complete runtime directory produced by the Release build. Test
; binaries and debug CRT DLLs are deliberately excluded: they are not part
; of a user installation and only make the package larger.
Source: "{#BuildDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "ZDEUnitTests.exe,gtest.dll,gtest_main.dll,ucrtbased.dll,libomp140d.x86_64.dll,msvcp140d_*.dll,*d.dll,*.pdb"

; App-local MSVC runtime. build_installer.ps1 stages these from the Visual
; Studio redistributable (or the system copy) so a clean machine can launch
; ZDE without a separate VC++ Redistributable install.
#if FileExists(RuntimeDir + "\msvcp140.dll")
Source: "{#RuntimeDir}\*.dll"; DestDir: "{app}"; Flags: ignoreversion
#endif

; Documentation and license.
Source: "{#LicenseFile}"; DestDir: "{app}"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepoRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Comment: "Launch {#MyAppFullName}"; AppUserModelID: "Zenvra.ZDE"
Name: "{group}\License"; Filename: "{app}\LICENSE.txt"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Comment: "Launch {#MyAppFullName}"; AppUserModelID: "Zenvra.ZDE"; Tasks: desktopicon

[Registry]
; Register the executable in Windows' "Open with" list without changing any
; default file associations.
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}"; ValueType: string; ValueName: "FriendlyAppName"; ValueData: "{#MyAppFullName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\Applications\{#MyAppExeName}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

; App Paths lets Win+R and shellExecute find ZDE.exe.
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Microsoft\Windows\CurrentVersion\App Paths\{#MyAppExeName}"; ValueType: string; ValueName: "Path"; ValueData: "{app}"

; Optional Explorer context-menu entries. These only appear when the user
; leaves the "Open with ZDE" task checked.
Root: HKA; Subkey: "Software\Classes\*\shell\ZDE"; ValueType: string; ValueName: ""; ValueData: "Open with ZDE"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\ZDE"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletevalue; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\*\shell\ZDE\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\ZDE"; ValueType: string; ValueName: ""; ValueData: "Open in ZDE"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\ZDE"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletevalue; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\shell\ZDE\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\ZDE"; ValueType: string; ValueName: ""; ValueData: "Open in ZDE"; Flags: uninsdeletekey; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\ZDE"; ValueType: string; ValueName: "Icon"; ValueData: "{app}\{#MyAppExeName},0"; Flags: uninsdeletevalue; Tasks: contextmenu
Root: HKA; Subkey: "Software\Classes\Directory\Background\shell\ZDE\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%V"""; Tasks: contextmenu

; Install metadata for future tooling and upgrades.
Root: HKA; Subkey: "Software\Zenvra\ZDE"; ValueType: string; ValueName: "InstallPath"; ValueData: "{app}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Zenvra\ZDE"; ValueType: string; ValueName: "Version"; ValueData: "{#MyAppVersion}"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#DoubleAmp(MyAppFullName)}}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; The installer owns these caches; user projects/settings live outside {app}.
Type: filesandordirs; Name: "{app}\plugins\.cache"
Type: filesandordirs; Name: "{app}\plugins\.staging"
