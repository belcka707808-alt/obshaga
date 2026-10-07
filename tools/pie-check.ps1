# Проверка PIE для Claude Code: список окон редактора, скриншот окна, нажатие клавиши.
# PIE запускается через unreal-mcp (EditorAppToolset.StartPIE), этот скрипт только смотрит и жмёт кнопки.
#   -Action list                                 — окна UnrealEditor
#   -Action shot -Match "Client 1" -Out x.png    — скриншот окна (работает, даже если окно перекрыто)
#   -Action key  -Match "Unreal Editor" -Key W -Ms 2000 — клик в центр окна и удержание клавиши (или сочетания: Shift+W)
#   -Action look -Match "Server 0" -Dx 0 -Dy 80   — повернуть камеру мышью (Dy > 0 — вниз)
#   -Action click -Match "Server 0"               — левая кнопка мыши (бросок)
param([string]$Action, [string]$Out, [string]$Key = "W", [int]$Ms = 1500, [string]$Match = "", [int]$Dx = 0, [int]$Dy = 0)
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
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
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
if($Action -eq "look"){
  # Поворот камеры: относительное движение мыши маленькими шагами. Окно должно быть уже активно (сначала -Action key).
  $steps = [Math]::Max([Math]::Abs($Dx), [Math]::Abs($Dy)) / 10 + 1
  for($i=0; $i -lt $steps; $i++){ [U]::mouse_event(1,[int]($Dx/$steps),[int]($Dy/$steps),0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 15 }
  "looked $Dx,$Dy in $($w.T)"
}
if($Action -eq "click"){
  [U]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 60; [U]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  "clicked in $($w.T)"
}
if($Action -eq "key"){
  # Если окно уже активно, не кликаем: движение курсора игра приняла бы за поворот камеры.
  if([U]::GetForegroundWindow() -ne $w.H){
    # Нажатие Alt снимает запрет Windows на смену активного окна из фонового процесса.
    [U]::keybd_event(0x12,0,0,[UIntPtr]::Zero); [U]::keybd_event(0x12,0,2,[UIntPtr]::Zero)
    [U]::ShowWindow($w.H,9)|Out-Null; [U]::SetForegroundWindow($w.H)|Out-Null; Start-Sleep -Milliseconds 300
    if([U]::GetForegroundWindow() -ne $w.H){ "could not activate $($w.T)"; exit 1 }
    $r = New-Object U+RECT; [U]::GetWindowRect($w.H,[ref]$r)|Out-Null; [U]::SetCursorPos([int](($r.L+$r.R)/2),[int](($r.T+$r.B)/2))|Out-Null
    [U]::mouse_event(2,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 50; [U]::mouse_event(4,0,0,0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 300
  }
  # -Key понимает сочетания: "Shift+W", "Ctrl+W", "Space"
  # Скан-код обязателен: по нему движок отличает левый Shift/Ctrl от правого.
  $names = @{ SHIFT = 0x10; CTRL = 0x11; SPACE = 0x20 }
  $vks = @($Key.ToUpper().Split('+') | ForEach-Object { if($names.ContainsKey($_)){ [byte]$names[$_] } else { [byte][char]$_ } })
  foreach($vk in $vks){ [U]::keybd_event($vk,[byte][U]::MapVirtualKey($vk,0),0,[UIntPtr]::Zero); Start-Sleep -Milliseconds 40 }
  Start-Sleep -Milliseconds $Ms
  [array]::Reverse($vks); foreach($vk in $vks){ [U]::keybd_event($vk,[byte][U]::MapVirtualKey($vk,0),2,[UIntPtr]::Zero) }
  "pressed $Key ${Ms}ms in $($w.T)"
}
