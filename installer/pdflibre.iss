; Installateur Windows de PDF Libre (Inno Setup 6.3 ou plus récent).
; Un seul fichier pour tous les PC : il installe la version ARM64 native sur
; les ordinateurs ARM (Surface, Snapdragon) et la version x64 ailleurs.
;
; Compilation :
;   ISCC /DAppVersion=0.1.0 /DX64Dir=...\dist\x64 /DArm64Dir=...\dist\arm64 pdflibre.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef X64Dir
  #define X64Dir "..\dist\x64"
#endif
#ifndef Arm64Dir
  #define Arm64Dir "..\dist\arm64"
#endif

[Setup]
AppId={{8C1E5B9A-6F3D-4C27-9E61-3A7B2D4F5E10}
AppName=PDF Libre
AppVersion={#AppVersion}
AppVerName=PDF Libre {#AppVersion}
AppPublisher=Projet PDF Libre
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\PDF Libre
DefaultGroupName=PDF Libre
DisableProgramGroupPage=yes
DisableDirPage=auto
; Installation pour l'utilisateur courant par défaut : aucun droit
; administrateur requis. « Pour tous les utilisateurs » reste proposé.
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
OutputBaseFilename=PDFLibre-{#AppVersion}-installation
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\PDFLibre.exe
UninstallDisplayName=PDF Libre
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
ChangesAssociations=yes

[Languages]
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
fr.AssocTask=Proposer PDF Libre pour ouvrir les fichiers PDF
en.AssocTask=Offer PDF Libre to open PDF files
fr.RemoveSettings=Supprimer aussi vos réglages de PDF Libre (documents récents, dernières pages lues) ?
en.RemoveSettings=Also remove your PDF Libre settings (recent documents, last pages read)?

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "fileassoc"; Description: "{cm:AssocTask}"

[Files]
Source: "{#X64Dir}\*"; DestDir: "{app}"; Check: not IsArm64; Flags: recursesubdirs ignoreversion
Source: "{#Arm64Dir}\*"; DestDir: "{app}"; Check: IsArm64; Flags: recursesubdirs ignoreversion

[Icons]
Name: "{autoprograms}\PDF Libre"; Filename: "{app}\PDFLibre.exe"
Name: "{autodesktop}\PDF Libre"; Filename: "{app}\PDFLibre.exe"; Tasks: desktopicon

[Registry]
; PDF Libre apparaît dans « Ouvrir avec » et dans Paramètres > Applications par
; défaut. Windows ne permet pas (et nous ne voulons pas) d'imposer le choix :
; c'est l'utilisateur qui décide.
Root: HKA; Subkey: "Software\Classes\PDFLibre.Document"; ValueType: string; ValueName: ""; ValueData: "Document PDF"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\PDFLibre.Document\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\PDFLibre.exe,0"; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\PDFLibre.Document\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\PDFLibre.exe"" ""%1"""; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "PDFLibre.Document"; ValueData: ""; Flags: uninsdeletevalue; Tasks: fileassoc
Root: HKA; Subkey: "Software\Classes\Applications\PDFLibre.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\PDF Libre\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "PDF Libre"; Flags: uninsdeletekey; Tasks: fileassoc
Root: HKA; Subkey: "Software\PDF Libre\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Lecteur PDF libre et rapide"; Tasks: fileassoc
Root: HKA; Subkey: "Software\PDF Libre\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "PDFLibre.Document"; Tasks: fileassoc
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "PDF Libre"; ValueData: "Software\PDF Libre\Capabilities"; Flags: uninsdeletevalue; Tasks: fileassoc

[Run]
Filename: "{app}\PDFLibre.exe"; Description: "{cm:LaunchProgram,PDF Libre}"; Flags: nowait postinstall skipifsilent

[Code]
// Désinstallation propre : les réglages (documents récents, dernière page
// lue) sont conservés par défaut, et supprimés seulement si l'utilisateur le demande.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usPostUninstall) and not UninstallSilent then
  begin
    if MsgBox(CustomMessage('RemoveSettings'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      RegDeleteKeyIncludingSubkeys(HKCU, 'Software\PDF Libre\PDF Libre');
  end;
end;
