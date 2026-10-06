# Проверка PIE для Claude Code: список окон редактора, скриншот окна, нажатие клавиши.
# PIE запускается через unreal-mcp (EditorAppToolset.StartPIE), этот скрипт только смотрит и жмёт кнопки.
#   -Action list                                 — окна UnrealEditor
#   -Action shot -Match "Client 1" -Out x.png    — скриншот окна (работает, даже если окно перекрыто)
#   -Action key  -Match "Unreal Editor" -Key W -Ms 2000 — клик в центр окна и удержание клавиши
param([string]$Action, [string]$Out, [string]$Key = "W", [int]$Ms = 1500, [string]$Match = "")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class U {
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);
 [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,int x,int y,uint d,UIntPtr e);
}
"@
[U]::SetProcessDPIAware() | Out-Null
$pid0 = (Get-Process UnrealEditor).Id
$global:wins = @()
[U]::EnumWindows({ param($h,$l) $p=0; [U]::GetWindowThreadProcessId($h,[ref]$p)|Out-Null
  if($p -eq $pid0 -and [U]::IsWindowVisible($h)){ $sb=New-Object Text.StringBuilder 512; [U]::GetWindowText($h,$sb,512)|Out-Null
    if($sb.Length -gt 0){ $global:wins += [pscustomobject]@{H=$h;T=$sb.ToString()} } }; $true }, [IntPtr]::Zero) | Out-Null
if($Action -eq "list"){ $global:wins | ForEach-Object { "$($_.H) | $($_.T)" }; exit }
$w = $global:wins | Where-Object { $_.T -like "*$Match*" } | Select-Object -First 1
if(-not $w){ "no window matching $Match"; exit 1 }
if($Action -eq "shot"){
  $r = New-Object U+RECT; [U]::GetWindowRect($w.H,[ref]$r)|Out-Null
  $bmp = New-Object Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
  $g=[Drawing.Graphics]::FromImage($bmp); $dc=$g.GetHdc(); [U]::PrintWindow($w.H,$dc,2)|Out-Null; $g.ReleaseHdc($dc)
  $bmp.Save($Out,[Drawing.Imaging.ImageFormat]::Png); "$($w.T) $($bmp.Width)x$($bmp.Height)"
}
if($Action -eq "key"){
  [U]::ShowWindow($w.H,9)|Out-Null; [U]::SetForegroundWindow($w.H)|Out-Null; Start-Sleep -Milliseconds 300
  $r = New-Object U+RECT; [U]::GetWindowRect($w.H,[ref]$r)|Out-Null; [U]::SetCursorPos([int](($r.L+$r.R)/2),[int](($r.T+$r.B)/2))|Out-Null
  [U]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 50; [U]::mouse_event(4,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 300
  $vk=[byte][char]$Key.ToUpper()
  [U]::keybd_event($vk,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds $Ms; [U]::keybd_event($vk,0,2,[UIntPtr]::Zero)
  "pressed $Key ${Ms}ms in $($w.T)"
}
