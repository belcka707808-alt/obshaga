# Измеряет громкость выгруженных WAV (RMS и пик) и подбирает каждому звуку множитель громкости так,
# чтобы звуки шли «лестницей» от тихого к громкому. На слух это не проверка, а выравнивание по цифрам.
#   audio-levels.ps1 -Folder <папка с WAV> -OutJson volumes.json -OutTable table.md
param([string]$Folder, [string]$OutJson = "volumes.json", [string]$OutTable = "")
$Folder = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Folder)
$OutJson = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutJson)

# Желаемая слышимая громкость (RMS после множителя), от тихого к громкому. Музыка заметно тише эффектов.
$targets = [ordered]@{
  S_Footstep = 0.040; S_HidingSpotRustle = 0.050; S_Notice = 0.063; S_Door = 0.080; S_ItemImpactLight = 0.100
  S_ItemImpactHeavy = 0.125; S_DeviceBreak = 0.125; S_ChaseStart = 0.160; S_Caught = 0.200
  S_Heartbeat = 0.110; S_ResultsJingle = 0.070; S_MusicCalm = 0.030; S_MusicTense = 0.036
}

function Measure-Wav($path) {
  $b = [IO.File]::ReadAllBytes($path)
  # Ищем блоки fmt и data.
  $pos = 12; $channels = 1; $bits = 16; $dataStart = 0; $dataLen = 0
  while ($pos + 8 -le $b.Length) {
    $id = [Text.Encoding]::ASCII.GetString($b, $pos, 4); $len = [BitConverter]::ToInt32($b, $pos + 4)
    if ($id -eq 'fmt ') { $channels = [BitConverter]::ToInt16($b, $pos + 10); $bits = [BitConverter]::ToInt16($b, $pos + 22) }
    if ($id -eq 'data') { $dataStart = $pos + 8; $dataLen = [Math]::Min($len, $b.Length - $dataStart); break }
    $pos += 8 + $len + ($len % 2)
  }
  if ($bits -ne 16 -or $dataLen -le 0) { return $null }
  $n = [int]($dataLen / 2); $sum = 0.0; $peak = 0
  # Длинную музыку меряем через шаг: точности хватает, а считается быстро.
  $step = [Math]::Max(1, [int]($n / 400000))
  $count = 0
  for ($i = 0; $i -lt $n; $i += $step) {
    $v = [BitConverter]::ToInt16($b, $dataStart + $i * 2); $a = [Math]::Abs([int]$v)
    if ($a -gt $peak) { $peak = $a }; $sum += [double]$v * $v; $count++
  }
  # Тишину в хвосте не считаем: RMS берём по той части, где звук громче 5 % пика.
  $thr = $peak * 0.05; $sum2 = 0.0; $count2 = 0
  for ($i = 0; $i -lt $n; $i += $step) { $v = [BitConverter]::ToInt16($b, $dataStart + $i * 2); if ([Math]::Abs([int]$v) -ge $thr) { $sum2 += [double]$v * $v; $count2++ } }
  [pscustomobject]@{ Rms = [Math]::Sqrt($sum2 / [Math]::Max($count2, 1)) / 32768; Peak = $peak / 32768; Seconds = 0 }
}

$volumes = [ordered]@{}; $rows = @()
foreach ($name in $targets.Keys) {
  $file = Join-Path $Folder "$name.wav"
  if (-not (Test-Path $file)) { $rows += "| $name | нет файла | | | |"; continue }
  $m = Measure-Wav $file
  if (-not $m) { $rows += "| $name | не 16 бит | | | |"; continue }
  # Множитель под целевую громкость; не усиливаем так, чтобы пик вылез за единицу, и не больше чем вчетверо.
  $vol = $targets[$name] / [Math]::Max($m.Rms, 0.0001)
  $vol = [Math]::Min($vol, [Math]::Min(4.0, 1.0 / [Math]::Max($m.Peak, 0.0001)))
  $vol = [Math]::Round($vol, 2)
  $volumes[$name] = $vol
  $rows += "| $name | {0:N3} | {1:N3} | {2:N2} | {3:N3} |" -f $m.Rms, $m.Peak, $vol, ($m.Rms * $vol)
}
($volumes | ConvertTo-Json) | Set-Content $OutJson -Encoding UTF8
$table = @("| Звук | RMS исходный | Пик | Множитель громкости | RMS после |", "|---|---|---|---|---|") + $rows
if ($OutTable) { $table | Set-Content ($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutTable)) -Encoding UTF8 }
$table
