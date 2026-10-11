<# Removes everything installed by install.ps1. #>
$ErrorActionPreference = 'SilentlyContinue'
Remove-Item -Recurse -Force 'HKCU:\Software\Classes\.indsin', 'HKCU:\Software\Classes\indsin.Document'
Remove-Item -Recurse -Force (Join-Path $env:LOCALAPPDATA 'indsin')
Add-Type -Namespace Win32 -Name Shell -MemberDefinition '[System.Runtime.InteropServices.DllImport("shell32.dll")] public static extern void SHChangeNotify(int e, int f, System.IntPtr a, System.IntPtr b);'
[Win32.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Host 'indsin file association removed.'
