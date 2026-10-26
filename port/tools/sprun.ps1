# Bounded source-port run (a hung game can freeze the machine -> always force-kill after -Timeout seconds).
# -Exe path = run another acnb.exe (e.g. the development version's) with the same arguments (default: this tree's build).
# sprun.ps1 -Tag t [-Out dir] [-Exit 1190] [-Timeout 10] [-Window] [-Shots 301,1189] [-Env @{K='v'}] [-Extra '...'] [-Card dir] [-RealTime]
#           [-Replay name] [-GarageCard]   (-Replay garage6 = port\build\garage6.txt; -GarageCard = a copy of garage_card_orig,
#           the card rule of hrcheck / rcheck: battle* empty, blade* blade_card_orig, else garage_card_orig - pick it to match)
#           [-Script file] [-Record file]   (--script F / --record F of the source port; paths may hold spaces)
# Call with & (powershell -File drops the -Env hashtable). Card: a fresh empty <Out>\<Tag>_card unless -Card is given
# (never the user's port\memcard\mc0).
# Writes <Out>\<Tag>.out / .err and <Tag>.rq.txt (the ACNB_RQ_HASH=1 `rq frame` lines; hash is on by default).
# -State: ACNB_STATE_HASH=1 + <Tag>.st.txt (the `state vb` lines; compare with port/tools/stcmp.py).
param([string]$Tag = 'sp', [string]$Out = $env:TEMP, [int]$Exit = 1190, [int]$Timeout = 10, [switch]$Window,
      [int[]]$Shots = @(), [hashtable]$Env = @{}, [string]$Extra = '', [string]$Card = '', [switch]$RealTime,
      [string]$Replay = '', [switch]$GarageCard, [string]$Script = '', [string]$Record = '', [switch]$State, [string]$Exe = '')
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$exe = if ($Exe) { (Resolve-Path $Exe).Path } else { Join-Path $root 'sourceport\build\acnb.exe' }
New-Item -ItemType Directory -Force $Out | Out-Null
$a = @('--fast', '--exit-after', "$Exit")
if ($RealTime) { $a = @('--exit-after', "$Exit") }
if ($Exit -le 0) { $a = @($a | Where-Object { $_ -ne '--exit-after' -and $_ -ne "$Exit" }) }   # -Exit 0: the script's `end` exits   # 59.94 Hz pacing (N9 audio device check): Timeout >= Exit / 60 + 3
if (-not $Window) { $a = @('--headless') + $a }
foreach ($s in $Shots) { $a += @('--shot', "$s") }
if ($Replay) {
    $a += @('--replay', ('"' + (Join-Path $root "port\build\$Replay.txt") + '"'))   # Start-Process does not quote
    . (Join-Path $PSScriptRoot 'replayseg.ps1')
    $sx = Get-ReplaySandbox (Join-Path $root "port\build\$Replay.txt")
    if ($sx) { "SANDBOX replay (script $($sx.Script), card $($sx.Card))" }
}
if ($Script) { $a += @('--script', ('"' + (Resolve-Path $Script).Path + '"')) }   # platform/sandbox
if ($Record) { $a += @('--record', ('"' + [IO.Path]::GetFullPath($Record) + '"')) }
if ($Extra) { $a += $Extra }
if (-not $Card) {
  $Card = Join-Path $Out "${Tag}_card"
  if (Test-Path $Card) { Remove-Item -Recurse -Force $Card }
  if ($GarageCard) { Copy-Item -Recurse (Join-Path $root 'port\build\garage_card_orig') $Card } else { New-Item -ItemType Directory $Card | Out-Null }
}
$all = @{ ACNB_RQ_HASH = '1'; ACNB_SHOT_DIR = $Out; ACNB_MEMCARD = $Card }
foreach ($k in $Env.Keys) { $all[$k] = $Env[$k] }
if ($State) { $all.ACNB_STATE_HASH = '1' }
foreach ($k in $all.Keys) { Set-Item "Env:$k" $all[$k] }
$o = Join-Path $Out "$Tag.out"; $e = Join-Path $Out "$Tag.err"
try {
  $p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory (Split-Path $exe) -RedirectStandardOutput $o -RedirectStandardError $e -PassThru -NoNewWindow
  $null = $p.Handle
  $t0 = Get-Date
  if (-not $p.WaitForExit($Timeout * 1000)) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; $r = 'KILLED' } else { $r = "exit $($p.ExitCode)" }
  "{0}: {1} {2:N1}s" -f $Tag, $r, ((Get-Date) - $t0).TotalSeconds
} finally {
  foreach ($k in $all.Keys) { Remove-Item "Env:$k" -ErrorAction SilentlyContinue }
  Get-Process acnb, launcher -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}
$h = @(Select-String -Path $o, $e -Pattern 'rq frame ' | ForEach-Object { $_.Line -replace '^.*?(rq frame )', '$1' })
"rq lines: $($h.Count)"; $h | Set-Content (Join-Path $Out "$Tag.rq.txt")
if ($State) { $st = @(Select-String -Path $o, $e -Pattern 'state vb ' | ForEach-Object { $_.Line -replace '^.*?(state vb )', '$1' }); "state lines: $($st.Count)"; $st | Set-Content (Join-Path $Out "$Tag.st.txt")
  Select-String -Path $o, $e -Pattern 'statehash: ERROR' | Select-Object -First 1 | ForEach-Object { $_.Line } }
