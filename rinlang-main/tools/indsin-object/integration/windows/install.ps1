<#
 Registers the .indsin file type for the current user (HKCU — no administrator rights needed):
 file icon, "Open" action (indsin Viewer) and an extra "View source" action.
 Usage (PowerShell):  .\install.ps1        Undo:  .\uninstall.ps1
#>
$ErrorActionPreference = 'Stop'
$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$root  = (Resolve-Path (Join-Path $here '..\..\..\..')).Path
$pkg   = Join-Path $root 'tools\indsin-object'
$icons = Join-Path $root 'assets\branding\indsin'
$dest  = Join-Path $env:LOCALAPPDATA 'indsin'

$node = (Get-Command node -ErrorAction SilentlyContinue)
if (-not $node) { throw 'Node.js 16+ is required (https://nodejs.org).' }

# Self-contained copy so moving the repository never breaks the association
New-Item -ItemType Directory -Force -Path "$dest\bin", "$dest\src" | Out-Null
Copy-Item "$pkg\bin\indsin-open.js", "$pkg\bin\indsin-object.js" "$dest\bin" -Force
Copy-Item "$pkg\src\indsin_highlight.js", "$pkg\src\indsin_object.js" "$dest\src" -Force
Copy-Item "$icons\indsin_file.ico", "$icons\indsin.ico" $dest -Force

$open = "`"$($node.Source)`" `"$dest\bin\indsin-open.js`""
$cls  = 'HKCU:\Software\Classes'
function Set-Default($path, $value) { New-Item -Path $path -Force | Out-Null; Set-ItemProperty -Path $path -Name '(default)' -Value $value }

Set-Default "$cls\.indsin" 'indsin.Document'
Set-ItemProperty -Path "$cls\.indsin" -Name 'Content Type' -Value 'text/x-indsin'
Set-ItemProperty -Path "$cls\.indsin" -Name 'PerceivedType' -Value 'text'
Set-Default "$cls\indsin.Document" 'indsin UI document'
Set-Default "$cls\indsin.Document\DefaultIcon" "$dest\indsin_file.ico,0"
Set-Default "$cls\indsin.Document\shell" 'open'
Set-Default "$cls\indsin.Document\shell\open" 'Open with indsin Viewer'
Set-ItemProperty -Path "$cls\indsin.Document\shell\open" -Name 'Icon' -Value "$dest\indsin.ico"
Set-Default "$cls\indsin.Document\shell\open\command" "$open `"%1`""
Set-Default "$cls\indsin.Document\shell\source" 'View source (highlighted)'
Set-Default "$cls\indsin.Document\shell\source\command" "$open `"%1`" --source"
Set-Default "$cls\indsin.Document\shell\edit" 'Edit in Notepad'
Set-Default "$cls\indsin.Document\shell\edit\command" 'notepad.exe "%1"'

# Tell Explorer to refresh icons/associations
Add-Type -Namespace Win32 -Name Shell -MemberDefinition '[System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, int f, System.IntPtr a, System.IntPtr b);'
[Win32.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host 'indsin file association installed. For a live preview set RIN_INDSIN_RUN to rin_indsin_run.exe.'
