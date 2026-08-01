[Setup]
AppName=System Monitor
AppVersion=1.0.0
AppPublisher=Rodion
DefaultDirName={autopf}\System Monitor
DefaultGroupName=System Monitor
OutputDir=Output
OutputBaseFilename=SystemMonitor_Setup
Compression=lzma2/ultra64
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64
; Требуем права админа для установки (так как backend работает с драйверами/железом)
PrivilegesRequired=admin

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Главный исполняемый файл Backend (поднимаемся из installer/ на уровень выше)
Source: "..\x64\Release\SystemMonitor.exe"; DestDir: "{app}"; Flags: ignoreversion
; Главный файл UI
Source: "..\x64\Release\SystemMonitorUI.exe"; DestDir: "{app}"; Flags: ignoreversion
; Все библиотеки (.dll) из релизной папки
Source: "..\x64\Release\*.dll"; DestDir: "{app}"; Flags: ignoreversion
; Вложенная папка platforms для Qt
Source: "..\x64\Release\platforms\*"; DestDir: "{app}\platforms"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
; Ярлык в меню Пуск (запускает Backend, который сам поднимет UI)
Name: "{group}\System Monitor"; Filename: "{app}\SystemMonitor.exe"
; Ярлык на Рабочем столе
Name: "{autodesktop}\System Monitor"; Filename: "{app}\SystemMonitor.exe"; Tasks: desktopicon

[Run]
; Предложение запустить программу сразу после окончания установки
Filename: "{app}\SystemMonitor.exe"; Description: "{cm:LaunchProgram,System Monitor}"; Flags: nowait postinstall skipifsilent