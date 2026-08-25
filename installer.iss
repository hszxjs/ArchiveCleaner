; ArchiveCleaner 安装包脚本（Inno Setup）
; 用法：ISCC.exe installer.iss

[Setup]
AppName=ArchiveCleaner
AppVersion=1.1.0
AppPublisher=ArchiveCleaner
DefaultDirName={commonpf}\ArchiveCleaner
DefaultGroupName=ArchiveCleaner
DisableProgramGroupPage=yes
OutputDir=A:\ArchiveCleaner
OutputBaseFilename=ArchiveCleaner-Setup-v1.1
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\ArchiveCleaner.exe

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
SetupWindowTitle=ArchiveCleaner 安装程序
WelcomeLabel1=欢迎使用 ArchiveCleaner 安装程序
WelcomeLabel2=这个向导将引导你完成 ArchiveCleaner 的安装。%n%nArchiveCleaner 是一个压缩包清理工具，支持秒级搜索和批量删除。%n%n点击"下一步"继续。
SelectDirLabel3=选择 ArchiveCleaner 的安装目录：
ReadyLabel1=安装程序已准备好在你的计算机上安装 ArchiveCleaner。
ReadyLabel2a=点击"安装"开始安装。
SelectTasksLabel2=选择你想要执行的附加任务：

[Tasks]
Name: "desktopicon"; Description: "在桌面创建快捷方式"; GroupDescription: "附加任务:"
Name: "installeverything"; Description: "安装 Everything 搜索引擎（推荐，可实现秒级搜索）"; GroupDescription: "附加任务:"

[Files]
; 主程序
Source: "A:\ArchiveCleaner\release\ArchiveCleaner.exe"; DestDir: "{app}"; Flags: ignoreversion
; 资源（字体等）
Source: "A:\ArchiveCleaner\release\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
; 搜索工具
Source: "A:\ArchiveCleaner\release\es.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "A:\ArchiveCleaner\release\fd.exe"; DestDir: "{app}"; Flags: ignoreversion
; Everything 安装包（嵌入，安装时提取并静默安装）
Source: "A:\ArchiveCleaner\release\Everything-Setup.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall ignoreversion; Tasks: installeverything

[Run]
; 静默安装 Everything（如果用户勾选了）
Filename: "{tmp}\Everything-Setup.exe"; Parameters: "/SILENT /NORESTART"; Tasks: installeverything; Flags: waituntilterminated; Check: not IsEverythingRunning()
; 安装完成后可选启动
Filename: "{app}\ArchiveCleaner.exe"; Description: "立即启动 ArchiveCleaner"; Flags: nowait postinstall skipifsilent

[Icons]
Name: "{group}\ArchiveCleaner"; Filename: "{app}\ArchiveCleaner.exe"
Name: "{commondesktop}\ArchiveCleaner"; Filename: "{app}\ArchiveCleaner.exe"; Tasks: desktopicon

[UninstallDelete]
Type: filesandordirs; Name: "{app}\assets"
Type: files; Name: "{app}\config.json"
Type: files; Name: "{app}\delete_log.txt"

[Code]
// 检测 Everything 是否已在运行（避免重复安装）
function IsEverythingRunning(): Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec(ExpandConstant('{cmd}'), '/C tasklist /FI "IMAGENAME eq Everything.exe" | find /I "Everything.exe"', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    if ResultCode = 0 then
      Result := True;
  end;
end;
