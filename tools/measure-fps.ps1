# Замер частоты кадров в отдельном процессе игры (не PIE): запускает игру в одном окне 1920x1080,
# ждёт, пока в журнале наберётся несколько замеров (игра пишет их сама раз в 5 секунд), и закрывает её.
#   measure-fps.ps1 -Label "TAA" -Cmds "r.AntiAliasingMethod 2"
#   -StartRound — нажать Enter (начать раунд); -WaitSeconds — сколько ждать перед замером (например, до ночи).
param([string]$Label = "base", [string]$Cmds = "", [switch]$StartRound, [int]$WaitSeconds = 0, [int]$Samples = 4, [string]$Extra = "",
  [string]$Engine = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe")
$project = Join-Path (Split-Path -Parent $PSScriptRoot) "Obshaga.uproject"
$log = Join-Path (Split-Path -Parent $PSScriptRoot) "Saved\Logs\fps_$($Label -replace '[^\w]', '_').log"
if (Test-Path -LiteralPath $log) { Clear-Content -LiteralPath $log }
$exec = if ($Cmds) { " -ExecCmds=`"$Cmds`"" } else { "" }
$proc = Start-Process $Engine -ArgumentList "`"$project`" /Game/Obshaga/Map/L_Obshaga?listen -game -windowed -ResX=1920 -ResY=1080 -WinX=0 -WinY=0 -log -LogCmds=`"LogObshaga Verbose`" -abslog=`"$log`"$exec $Extra" -PassThru
function Count-Fps { if (Test-Path -LiteralPath $log) { @(Select-String -LiteralPath $log -Pattern "\[host\] FPS").Count } else { 0 } }
$n = 0; while ((Count-Fps) -lt 1 -and $n -lt 60) { Start-Sleep -Seconds 3; $n++ }
if ($StartRound) {
  Add-Type -Namespace MF -Name U -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte sc, uint f, UIntPtr e);'
  $proc.Refresh(); [MF.U]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero); [MF.U]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
  [MF.U]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null; Start-Sleep -Milliseconds 500
  [MF.U]::keybd_event(0x0D, 0x1C, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 120; [MF.U]::keybd_event(0x0D, 0x1C, 2, [UIntPtr]::Zero)
}
if ($WaitSeconds -gt 0) { Start-Sleep -Seconds $WaitSeconds }
$start = Count-Fps; $n = 0; while ((Count-Fps) -lt $start + $Samples + 1 -and $n -lt 40) { Start-Sleep -Seconds 3; $n++ }
$lines = Select-String -LiteralPath $log -Pattern "\[host\] FPS (\d+) \(frame ([\d\.,]+) ms; game ([\d\.,]+), draw ([\d\.,]+), gpu ([\d\.,]+)" | Select-Object -Last $Samples
$phase = (Select-String -LiteralPath $log -Pattern "Phase: ERoundPhase::(\w+)" | Select-Object -Last 1)
if (-not $proc.HasExited) { $proc.CloseMainWindow() | Out-Null; if (-not $proc.WaitForExit(10000)) { try { $proc.Kill() } catch { } } }
function Avg($i) { ($lines | ForEach-Object { [double]($_.Matches[0].Groups[$i].Value -replace ',', '.') } | Measure-Object -Average).Average }
"{0} | {1} | FPS {2:N0} | frame {3:N1} ms | game {4:N1} | gpu {5:N1}" -f $Label, $(if ($phase) { $phase.Matches[0].Groups[1].Value } else { 'Lobby' }), (Avg 1), (Avg 2), (Avg 3), (Avg 5)

