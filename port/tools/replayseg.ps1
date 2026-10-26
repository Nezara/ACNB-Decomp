# Replay helpers for sprun.ps1 / statecheck.ps1 (dot-sourced).
# Segment marks: lines "seg N begin V" / "seg N end V" in build\<replay>.txt, written by F2 / F3 while recording
# (runtime/video.c). Replay format v2 (P4b): line 1 "#acnb-replay 2", pad 1 lines "vblank buttons lx ly rx ry", pad 2
# lines "p2 vblank connected buttons lx ly rx ry"; replays without marks are unchanged.

function Get-ReplaySegments([string]$File) {
    $s = @{}
    foreach ($m in (Select-String -Path $File -Pattern '^seg (\d+) (begin|end) (\d+)')) {
        $n = [int]$m.Matches[0].Groups[1].Value
        if (-not $s.ContainsKey($n)) { $s[$n] = [pscustomobject]@{ N = $n; Begin = 0; End = 0 } }
        if ($m.Matches[0].Groups[2].Value -eq 'begin') { $s[$n].Begin = [int]$m.Matches[0].Groups[3].Value }
        else { $s[$n].End = [int]$m.Matches[0].Groups[3].Value }
    }
    $s.Values | Sort-Object N
}

# Sandbox replays: line 1 "#acnb-replay 3" + an "#acnb-sbx"
# header -> @{ Script; Card } ($null = a plain v2 replay). The exes refuse one with another card (exit 2).
function Get-ReplaySandbox([string]$File) {
    if (-not (Test-Path $File)) { return $null }
    $h = @(Get-Content $File -TotalCount 3)
    if (-not $h.Count -or $h[0] -ne '#acnb-replay 3') { return $null }
    $l = $h | Where-Object { $_ -like '#acnb-sbx *' } | Select-Object -First 1
    $s = if ($l -match ' script=(\S+)') { $Matches[1] } else { '?' }
    $c = if ($l -match ' card=(\S+)') { $Matches[1] } else { '-' }
    [pscustomobject]@{ Script = $s; Card = $c }
}

# Card kind per replay: a sandbox replay's header card (logs "SANDBOX replay (script X)"); else battle* = empty,
# blade* = blade, everything else garage.
function Get-ReplayCardKind([string]$File, [string]$Replay) {
    $x = Get-ReplaySandbox $File
    if ($x) {
        Write-Host "SANDBOX replay (script $($x.Script), card $($x.Card))"
        if ($x.Card -ne '-') { return $x.Card }
    }
    if ($Replay -like 'battle*') { 'empty' } elseif ($Replay -like 'blade*') { 'blade' } else { 'garage' }
}

# Card per replay (Get-ReplayCardKind): empty = a fresh empty card, else a copy of build\<kind>_card_orig.
function Initialize-ReplayCard([string]$Replay, [string]$b) {
    $card = Join-Path $b 'rcheck_card'
    if (Test-Path $card) { Remove-Item -Recurse -Force $card }
    $kind = Get-ReplayCardKind (Join-Path $b "$Replay.txt") $Replay
    if ($kind -eq 'empty') { New-Item -ItemType Directory $card | Out-Null }
    else { Copy-Item -Recurse (Join-Path $b "$($kind)_card_orig") $card }
    $card
}

# -Segment N -> @{ Exit; From } (exit = end + 60, as the runtime's --segment); -ListSegments prints the marks.
function Resolve-ReplaySegment([string]$File, [int]$Segment) {
    $s = @(Get-ReplaySegments $File | Where-Object { $_.N -eq $Segment })
    if (-not $s.Count -or -not $s[0].Begin) { throw "replay $File has no segment $Segment" }
    if (-not $s[0].End) { throw "replay $File segment $Segment has no end mark (F3)" }
    @{ Exit = $s[0].End + 60; From = $s[0].Begin }
}

function Show-ReplaySegments([string]$File) {
    $s = @(Get-ReplaySegments $File)
    if (-not $s.Count) { "$(Split-Path -Leaf $File): no segment marks"; return }
    foreach ($x in $s) { "$(Split-Path -Leaf $File) segment $($x.N): vblank $($x.Begin)..$($x.End)" }
}
