# Logical state hash check (Stage 0: source port only; the development version's version also drives the hybrid). Runs a replay with
# ACNB_STATE_HASH=1, compares the `state vb` lines with stcmp.py and, on a difference, re-runs both up to the first
# differing vblank with ACNB_STATE_DUMP=<vb-1>,<vb> and prints the differing fields (area / path / values, ulp for floats).
#   statecheck.ps1 -Replay battle1 -Self [-Exit n | -Segment n] [-Window] [-NoFull] [-NoDrill]   determinism (this build twice)
#   statecheck.ps1 -Replay battle1 -Other C:\..\acnb.exe [..]                                    another acnb.exe (A) vs this one (B)
# Card per replay (replayseg.ps1 Get-ReplayCardKind: a sandbox replay's header card, else battle* empty, blade* blade,
# others garage; -Card DIR overrides): a fresh copy per run (<Out>\<tag>_c, sprun -Card). Headless unless -Window (the
# state hash is render-independent; -Window for readback checks, e.g. the sbx_ene_t4 test room). Default exits below;
# other replays need -Exit / -Segment (-Segment n = that segment's end + 60, the whole replay up to there).
#   statecheck.ps1 -Make [-Replay a,b] [-Exit n | -Segment n]      golden state files
#   statecheck.ps1 -Verify [-Replay a,b] [-GoldenDir D]            this build vs the goldens
# -Make / -Verify: port\statehash\golden\<name>.st.gz (name = R or R_segN; -GoldenDir D elsewhere, e.g. the development version's
# port\notes\statehash\golden), gzip text = a '#' header (replay + sha256, exit, segment, window, build, card, iop pin,
# full, commit, field list hash) + the run's `state vb` lines. -Make without -Replay = the golden set ($goldenSet).
# -Verify without -Replay = every golden file; run parameters come from the header; it FAILs before running when the
# replay file or port\statehash\fields.txt changed (hash of the non-comment content: 'field list changed, re-Make');
# else one run + stcmp (first differing vb / areas; drill with -Replay R -Self / -Other).
# Output: short lines; PASS / FAIL at the end. Logs and dumps in -Out (default %TEMP%\statecheck).
param([string]$Replay = '', [int]$Exit = 0, [int]$Segment = 0, [switch]$Window, [switch]$Self,
      [string]$Other = '', [string]$Card = '', [string]$Out = '', [int]$N = 6, [int]$Fields = 30,
      [switch]$NoDrill, [switch]$NoFull, [hashtable]$Env = @{},
      [switch]$Make, [switch]$Verify, [string]$GoldenDir = '')
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$b = Join-Path $root 'port\build'
. (Join-Path $PSScriptRoot 'replayseg.ps1')
if (-not $Out) { $Out = Join-Path $env:TEMP 'statecheck' }
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$stcmp = Join-Path $PSScriptRoot 'stcmp.py'
$Build = 'sp'
$exits = @{ battle1 = 10950; blade1 = 9750; garage1 = 25300; garage2 = 18800; garage3 = 6400; garage4 = 5259; garage5 = 8100;
            garage6 = 3514; garage7 = 7418; garage_detail = 10161; sbx_ene_t4 = 2210 }

function New-RunCard([string]$kind, [string]$dst) {
    if (Test-Path $dst) { Remove-Item -Recurse -Force $dst }
    if ($Card) { Copy-Item -Recurse $Card $dst }
    elseif ($kind -eq 'empty' -or $kind -eq '-') { New-Item -ItemType Directory $dst | Out-Null }
    else { Copy-Item -Recurse (Join-Path $b "$($kind)_card_orig") $dst }
}

# header iop_cpi0 of a sandbox replay, else 1 (the check pin)
function Get-IopPin([string]$rf) {
    $h = @(Get-Content $rf -TotalCount 3) | Where-Object { $_ -like '#acnb-sbx *' } | Select-Object -First 1
    if ($h -and $h -match ' iop_cpi0=(\d+)') { return $Matches[1] }
    '1'
}

# one -State run; returns the .st.txt path (status lines printed)
function Invoke-StateRun([string]$side, [string]$rep, [string]$tag, [int]$ex, [string]$kind, [string]$iop, [hashtable]$extra) {
    $cd = Join-Path $Out "${tag}_c"
    New-RunCard $kind $cd
    $e = @{}
    foreach ($k in $Env.Keys) { $e[$k] = $Env[$k] }
    foreach ($k in $extra.Keys) { $e[$k] = $extra[$k] }
    if ($NoFull) { $e.ACNB_STATE_FULL = '0' }
    $st = Join-Path $Out "$tag.st.txt"
    if (Test-Path $st) { Remove-Item -Force $st }
    $a = @{ Tag = $tag; Out = $Out; Replay = $rep; Exit = $ex; Timeout = [int](30 + $ex * 0.015); Env = $e; Card = $cd; State = $true }
    if ($Window) { $a.Window = $true }
    if ($side -eq 'other') { $a.Exe = $Other }
    $r = & (Join-Path $PSScriptRoot 'sprun.ps1') @a
    $s = @($r | Where-Object { "$_" -match 'TIMEOUT|KILLED|: exit|state lines|statehash: ERROR' }) -join '; '
    "  [$side] $tag ($ex vb): $s"
    $st
}

function Get-FirstDiff([string[]]$txt) {
    $m = $txt | Select-String -Pattern '^first difference: vb (\d+), areas (.*)$' | Select-Object -First 1
    if ($m) { return @([int]$m.Matches[0].Groups[1].Value, $m.Matches[0].Groups[2].Value) }
    $null
}

# ---------------------------------------------------------------- compare mode
function Invoke-Compare {
    $rf = Join-Path $b "$Replay.txt"
    if (-not (Test-Path $rf)) { throw "statecheck: no replay $rf" }
    $ex = $Exit
    if ($Segment -gt 0) { $ex = (Resolve-ReplaySegment $rf $Segment).Exit }
    if ($ex -le 0) { $ex = $exits[$Replay] }
    if (-not $ex) { throw "statecheck: ${Replay}: no default exit, give -Exit or -Segment" }
    $kind = Get-ReplayCardKind $rf $Replay 6>$null
    $iop = Get-IopPin $rf
    if (-not $Self -and -not $Other) { throw 'statecheck: compare mode needs -Self or -Other EXE' }
    $sa = if ($Self) { 'sp' } else { 'other' }
    $sb = 'sp' 
    $ta = "st_${Replay}_A_$sa"; $tb = "st_${Replay}_B_$sb"
    "statecheck $Replay ($ex vb, card $kind, $(if ($Self) { "self $Build" } else { "$Other vs this build" })$(if ($NoFull) { ', full off' }))"
    $ra = Invoke-StateRun $sa $Replay $ta $ex $kind $iop @{}
    $ra[0..($ra.Count - 2)]
    $rb = Invoke-StateRun $sb $Replay $tb $ex $kind $iop @{}
    $rb[0..($rb.Count - 2)]
    foreach ($f in @($ra[-1], $rb[-1])) { if (-not (Test-Path $f)) { "statecheck FAIL: $Replay no state lines in $f"; return } }
    $c = @(& python $stcmp $ra[-1] $rb[-1] $N)
    $code = $LASTEXITCODE
    $c | ForEach-Object { "  $_" }
    if ($code -eq 0) { "statecheck PASS: $Replay state lines identical"; return }
    if ($code -ne 1) { "statecheck FAIL: $Replay no state lines to compare (see $Out\$ta.log / $tb)"; return }
    $fd = Get-FirstDiff $c
    if ($NoDrill -or -not $fd) { "statecheck FAIL: $Replay state differs"; return }
    $v = $fd[0]
    $vs = @($v)
    $m = $c | Select-String -Pattern '^first core difference: vb (\d+)' | Select-Object -First 1
    if ($m) { $vs += [int]$m.Matches[0].Groups[1].Value }
    $dl = ($vs | ForEach-Object { "$($_ - 1),$_" }) -join ','
    $ve = ($vs | Measure-Object -Maximum).Maximum
    "  drill: re-running both to vb $($ve + 2) with ACNB_STATE_DUMP=$dl"
    $da = Join-Path $Out "${ta}_dump"; $db = Join-Path $Out "${tb}_dump"
    foreach ($d in @($da, $db)) { if (Test-Path $d) { Remove-Item -Recurse -Force $d } }
    $r = Invoke-StateRun $sa $Replay "${ta}_d" ($ve + 2) $kind $iop @{ ACNB_STATE_DUMP = $dl; ACNB_STATE_DUMP_DIR = $da }
    $r[0..($r.Count - 2)]
    $r = Invoke-StateRun $sb $Replay "${tb}_d" ($ve + 2) $kind $iop @{ ACNB_STATE_DUMP = $dl; ACNB_STATE_DUMP_DIR = $db }
    $r[0..($r.Count - 2)]
    foreach ($x in $vs) { & python $stcmp --dump $da $db $Fields $x | ForEach-Object { "  $_" } }
    "statecheck FAIL: $Replay first difference vb $v (areas $($fd[1]))$(if ($vs.Count -gt 1) { ", first core difference vb $($vs[1])" }); dumps $da | $db"
}

# ---------------------------------------------------------------- goldens (ST tier)
# the golden set: replay, exit / segment, window (SP readbacks: sbx_ene_t4 test room)
$goldenSet = @(
    @{ Replay = 'battle1'; Exit = 10950 },
    @{ Replay = 'garage6'; Exit = 3514 },
    @{ Replay = 'auto_asm1'; Segment = 1 },
    @{ Replay = 'sbx_ene_t4'; Exit = 2210; Window = 1 },
    @{ Replay = 'auto_arenaret1'; Segment = 1 })   # Arena match (lost) + ARENA RESULT + back to the menu
if (-not $GoldenDir) { $GoldenDir = Join-Path $root 'port\statehash\golden' }
function Invoke-Git { & git.exe -C $root @args }

function Get-FieldsHash {   # sha256 of the field list without comments / blank lines / extra blanks
    $f = Join-Path $root 'port\statehash\fields.txt'
    if ($env:ACNB_STATE_FIELDS) { $f = $env:ACNB_STATE_FIELDS }
    $t = foreach ($l in [IO.File]::ReadAllLines($f)) { $x = (($l -replace '#.*$', '').Trim()) -replace '\s+', ' '; if ($x) { $x } }
    $sha = [Security.Cryptography.SHA256]::Create()
    ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($t -join "`n")))) -replace '-', '').ToLower()
}
function Write-GzLines([string]$path, [string[]]$lines) {
    $fs = [IO.File]::Create($path)
    try {
        $gz = New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionLevel]::Optimal)
        $w = New-Object IO.StreamWriter($gz, (New-Object Text.UTF8Encoding $false))
        $w.NewLine = "`n"
        foreach ($l in $lines) { $w.WriteLine($l) }
        $w.Dispose()
    } finally { $fs.Dispose() }
}
function Read-GzLines([string]$path) {
    $fs = [IO.File]::OpenRead($path)
    try {
        $gz = New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionMode]::Decompress)
        $r = New-Object IO.StreamReader($gz)
        $t = $r.ReadToEnd(); $r.Dispose()
    } finally { $fs.Dispose() }
    @($t -split "`n" | Where-Object { $_ -ne '' })
}

# returns output lines, the last one = 0 (ok) / 1 (fail)
function Invoke-GoldenMake([hashtable]$g) {
    $rep = $g.Replay; $rf = Join-Path $b "$rep.txt"
    if (-not (Test-Path $rf)) { "statecheck make FAIL: no replay $rf"; return 1 }
    $seg = [int]$g.Segment; $ex = [int]$g.Exit
    if ($seg -gt 0 -and $ex -le 0) { $ex = (Resolve-ReplaySegment $rf $seg).Exit }
    if ($ex -le 0) { $ex = $exits[$rep] }
    if (-not $ex) { "statecheck make FAIL: ${rep}: no default exit, give -Exit or -Segment"; return 1 }
    $name = if ($seg -gt 0) { "${rep}_seg$seg" } else { $rep }
    $kind = Get-ReplayCardKind $rf $rep 6>$null
    $iop = Get-IopPin $rf
    $script:Window = [bool]$g.Window
    $r = Invoke-StateRun $Build $rep "stg_${name}_$Build" $ex $kind $iop @{}
    $r[0..($r.Count - 2)]
    $st = $r[-1]
    $lines = if (Test-Path $st) { @(Get-Content $st | Where-Object { $_ -like 'state vb *' }) } else { @() }
    if (-not $lines.Count) { "statecheck make FAIL: $name no state lines ($st)"; return 1 }
    if (@($r | Where-Object { "$_" -match 'statehash: ERROR|TIMEOUT' }).Count) { "statecheck make FAIL: $name run error"; return 1 }
    $last = ($lines[-1] -split ' ')[2]
    $head = Invoke-Git rev-parse HEAD; $dirty = @(Invoke-Git status --porcelain --untracked-files=no).Count
    $hdr = @("# golden logical state lines (port\tools\statecheck.ps1 -Make; one 'state vb' line per vblank)",
             "# replay: $rep", "# replay_sha256: $((Get-FileHash -Algorithm SHA256 $rf).Hash.ToLower())", "# exit: $ex", "# segment: $seg",
             "# window: $([int][bool]$g.Window)", "# build: $Build", "# card: $kind", "# iop_cpi0: $iop", "# full: $(if ($NoFull) { 0 } else { 1 })",
             "# commit: $head$(if ($dirty) { ' (dirty)' })", "# fields_sha256: $(Get-FieldsHash)",
             "# pins: sprun -State (Stage 0 source port)",
             "# lines: $($lines.Count) (last vb $last)")
    New-Item -ItemType Directory -Force $GoldenDir | Out-Null
    $gf = Join-Path $GoldenDir "$name.st.gz"
    Write-GzLines $gf ($hdr + $lines)
    "statecheck make ${name}: $($lines.Count) lines (exit $ex, $Build) -> $gf ($([math]::Round((Get-Item $gf).Length / 1KB)) KB)"
    0
}

function Invoke-GoldenVerify([string]$gf) {
    $name = [IO.Path]::GetFileName($gf) -replace '\.st\.gz$', ''
    $all = Read-GzLines $gf
    $h = @{}
    foreach ($l in $all) { if ($l -match '^# (\w+): (.*)$') { $h[$Matches[1]] = $Matches[2].Trim() } }
    $gold = @($all | Where-Object { $_ -like 'state vb *' })
    $rep = $h['replay']; $rf = Join-Path $b "$rep.txt"
    if (-not (Test-Path $rf)) { "statecheck verify ${name}: FAIL - no replay $rf"; return 1 }
    $sha = (Get-FileHash -Algorithm SHA256 $rf).Hash.ToLower()
    if ($sha -ne $h['replay_sha256']) { "statecheck verify ${name}: FAIL - replay file changed (sha256 $sha, golden $($h['replay_sha256'])): re-Make"; return 1 }
    $fh = Get-FieldsHash
    if ($fh -ne $h['fields_sha256']) {
        $mk = "statecheck -Make -Replay $rep$(if ([int]$h['segment']) { " -Segment $($h['segment'])" })"
        "statecheck verify ${name}: FAIL - field list changed (port\statehash\fields.txt $($fh.Substring(0, 12)), golden $("$($h['fields_sha256'])xxxxxxxxxxxx".Substring(0, 12)) made at $($h['commit'])): re-Make ($mk); no run"
        return 1
    }
    $script:Window = $h['window'] -eq '1'
    $script:NoFull = $h['full'] -eq '0'
    $ex = [int]$h['exit']
    "statecheck verify $name ($ex vb, $Build vs the golden of $($h['build']) at $($h['commit']))"
    $r = Invoke-StateRun $Build $rep "stv_${name}_$Build" $ex $h['card'] $h['iop_cpi0'] @{}
    $r[0..($r.Count - 2)]
    $st = $r[-1]
    if (-not (Test-Path $st)) { "statecheck verify ${name}: FAIL - no state lines"; return 1 }
    $gt = Join-Path $Out "stv_${name}_golden.st.txt"
    [IO.File]::WriteAllLines($gt, [string[]]$gold)
    $c = @(& python $stcmp $gt $st $N)
    $code = $LASTEXITCODE
    $c | ForEach-Object { "  $_" }
    $nrun = @(Get-Content $st | Where-Object { $_ -like 'state vb *' }).Count
    if ($code -eq 0 -and $nrun -eq $gold.Count) { "statecheck verify ${name}: PASS - $($gold.Count) lines identical"; return 0 }
    $fd = Get-FirstDiff $c
    $why = if ($fd) { "first difference vb $($fd[0]) (areas $($fd[1]))" } elseif ($code -eq 0) { "line count golden $($gold.Count), run $nrun" } else { 'no lines to compare' }
    "statecheck verify ${name}: FAIL - $why; golden $gt | run $st (drill: statecheck -Replay $rep -Self / -Other EXE)"
    1
}

function Invoke-Golden {
    $names = @($Replay -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $fail = 0; $n = 0
    if ($Make) {
        $set = $goldenSet
        if ($names.Count) {
            $set = foreach ($nm in $names) {
                $d = $goldenSet | Where-Object { $_.Replay -eq $nm } | Select-Object -First 1
                $x = @{ Replay = $nm }
                if ($d) { foreach ($k in $d.Keys) { $x[$k] = $d[$k] } }
                if ($Exit -gt 0) { $x.Exit = $Exit; $x.Segment = 0 }
                if ($Segment -gt 0) { $x.Segment = $Segment; $x.Exit = 0 }
                if ($Window) { $x.Window = 1 }
                $x
            }
        }
        foreach ($g in @($set)) { $n++; $o = @(Invoke-GoldenMake $g); if ($o.Count -gt 1) { $o[0..($o.Count - 2)] }; if ($o[-1] -ne 0) { $fail++ } }
        $sz = (Get-ChildItem (Join-Path $GoldenDir '*.st.gz') | Measure-Object Length -Sum).Sum
        "goldens: $GoldenDir, total $([math]::Round($sz / 1MB, 2)) MB"
    } else {
        $files = @(Get-ChildItem (Join-Path $GoldenDir '*.st.gz') -ErrorAction SilentlyContinue | Sort-Object Name | ForEach-Object { $_.FullName })
        if ($names.Count) {
            $files = foreach ($nm in $names) {
                $f = Join-Path $GoldenDir "$(if ($Segment -gt 0) { "${nm}_seg$Segment" } else { $nm }).st.gz"
                if (-not (Test-Path $f) -and $Segment -le 0) {
                    $alt = @(Get-ChildItem (Join-Path $GoldenDir "${nm}_seg*.st.gz") -ErrorAction SilentlyContinue)
                    if ($alt.Count) { $f = $alt[0].FullName }
                }
                $f
            }
        }
        if (-not @($files).Count) { "statecheck verify: FAIL - no golden files in $GoldenDir (make them with -Make)"; $fail++ }
        foreach ($f in @($files)) {
            $n++
            if (-not (Test-Path $f)) { "statecheck verify: FAIL - no golden $f (make it with -Make)"; $fail++; continue }
            $o = @(Invoke-GoldenVerify $f); if ($o.Count -gt 1) { $o[0..($o.Count - 2)] }; if ($o[-1] -ne 0) { $fail++ }
        }
    }
    "statecheck $(if ($Make) { 'make' } else { 'verify' }) $(if ($fail) { "FAIL: $fail of $n" } else { "PASS: $n" })"
}

if ($Make -or $Verify) { Invoke-Golden }
elseif ($Replay) { Invoke-Compare }
else { throw 'statecheck: give -Replay R (-Self / -Other EXE) or -Make / -Verify' }
