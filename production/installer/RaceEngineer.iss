#define AppName "RaceEngineer"
#define AppPublisher "Tminh-U"
#define AppPublisherUrl "https://github.com/tminh-U/RaceEngineer"
#define AppId "{{D10E7E16-7255-4C5B-AE8D-EB5B86A6A7C7}}"

#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif

#ifndef StagingDir
  #define StagingDir "..\dist\RaceEngineer-staging"
#endif

#ifndef OutputDir
  #define OutputDir "..\dist"
#endif

#ifndef OutputBaseName
  #define OutputBaseName "RaceEngineer-Setup"
#endif

[Setup]
AppId={#AppId}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppPublisherUrl}
AppSupportURL={#AppPublisherUrl}/issues
AppUpdatesURL={#AppPublisherUrl}/releases
DefaultDirName={localappdata}\Programs\RaceEngineer
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseName}
SetupIconFile={#StagingDir}\bin\final_icon.ico
UninstallDisplayIcon={app}\bin\final_icon.ico
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
Uninstallable=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
Source: "{#StagingDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\RaceEngineer"; Filename: "{app}\bin\RaceEngineer.exe"; WorkingDir: "{app}\bin"; IconFilename: "{app}\bin\final_icon.ico"; Comment: "RaceEngineer AC/ACC companion"

[Run]
Filename: "{app}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "Installing Microsoft Visual C++ runtime..."; Verb: "runas"; Flags: shellexec waituntilterminated skipifdoesntexist
