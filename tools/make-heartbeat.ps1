# Генерирует звук сердцебиения «тук-тук» (WAV, моно, 22 кГц, около 25 КБ): два низких затухающих удара.
# Скачивать такой звук незачем — он получается из синуса.   make-heartbeat.ps1 -Out heartbeat.wav
param([string]$Out = "heartbeat.wav")
$rate = 22050; $seconds = 0.55; $n = [int]($rate * $seconds)
$samples = New-Object 'System.Int16[]' $n
# Удар: синус с понижающейся частотой, быстрая атака и экспоненциальный спад.
$beats = @(@{ t = 0.0; amp = 1.0; f0 = 62.0 }, @{ t = 0.21; amp = 0.7; f0 = 54.0 })
for ($i = 0; $i -lt $n; $i++) {
  $t = $i / $rate; $v = 0.0
  foreach ($b in $beats) {
    $dt = $t - $b.t
    if ($dt -ge 0) {
      $attack = [Math]::Min(1.0, $dt / 0.008)
      $freq = $b.f0 * (1.0 - 0.25 * [Math]::Min(1.0, $dt / 0.15))
      $v += $b.amp * $attack * [Math]::Exp(-$dt / 0.055) * [Math]::Sin(2 * [Math]::PI * $freq * $dt)
    }
  }
  $samples[$i] = [int16]([Math]::Max(-1.0, [Math]::Min(1.0, $v * 0.9)) * 32000)
}
$fs = [IO.File]::Create($Out); $w = New-Object IO.BinaryWriter($fs)
$dataBytes = $n * 2
$w.Write([Text.Encoding]::ASCII.GetBytes("RIFF")); $w.Write([int](36 + $dataBytes)); $w.Write([Text.Encoding]::ASCII.GetBytes("WAVEfmt "))
$w.Write([int]16); $w.Write([int16]1); $w.Write([int16]1); $w.Write([int]$rate); $w.Write([int]($rate * 2)); $w.Write([int16]2); $w.Write([int16]16)
$w.Write([Text.Encoding]::ASCII.GetBytes("data")); $w.Write([int]$dataBytes)
foreach ($s in $samples) { $w.Write($s) }
$w.Close(); $fs.Close()
"$Out : $((Get-Item $Out).Length) bytes"
