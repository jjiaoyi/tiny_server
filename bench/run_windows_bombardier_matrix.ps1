param(
    [string]$Url = "http://10.241.34.106:8080/",
    [int[]]$Connections = @(10, 50, 100, 200, 500, 1000),
    [int]$DurationSeconds = 30,
    [string]$ResultDir = "bench/results",
    [int]$PauseSeconds = 2
)

$ErrorActionPreference = "Stop"
$LatencyUnitPattern = "ns|us|$([char]0x00B5)s|ms|s"

function Convert-LatencyToMs {
    param([string]$Value)

    if ([string]::IsNullOrWhiteSpace($Value)) {
        return ""
    }

    $match = [regex]::Match($Value.Trim(), "^([0-9]+(?:\.[0-9]+)?)($LatencyUnitPattern)$", [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if (-not $match.Success) {
        return $Value
    }

    $number = [double]$match.Groups[1].Value
    $unit = $match.Groups[2].Value.ToLowerInvariant()

    switch ($unit) {
        "ns" { return [math]::Round($number / 1000000.0, 3) }
        "us" { return [math]::Round($number / 1000.0, 3) }
        "$([char]0x00B5)s" { return [math]::Round($number / 1000.0, 3) }
        "ms" { return [math]::Round($number, 3) }
        "s"  { return [math]::Round($number * 1000.0, 3) }
        default { return $Value }
    }
}

function Get-RegexValue {
    param(
        [string]$Text,
        [string]$Pattern,
        [int]$Group = 1
    )

    $match = [regex]::Match($Text, $Pattern, [System.Text.RegularExpressions.RegexOptions]::IgnoreCase)
    if ($match.Success) {
        return $match.Groups[$Group].Value
    }

    return ""
}

if (-not (Get-Command bombardier -ErrorAction SilentlyContinue)) {
    Write-Host "bombardier was not found."
    Write-Host "Install it first, for example:"
    Write-Host "  scoop install bombardier"
    Write-Host "or download it from https://github.com/codesenberg/bombardier/releases"
    exit 1
}

New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

$timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$csvFile = Join-Path $ResultDir "windows_bombardier_matrix_$timestamp.csv"
$rawFile = Join-Path $ResultDir "windows_bombardier_matrix_$timestamp.txt"

"time,url,duration_seconds,connections,requests_per_sec,latency_avg_ms,latency_max_ms,p50_ms,p90_ms,p95_ms,p99_ms,2xx,non_2xx,throughput" |
    Set-Content -Encoding UTF8 $csvFile

@(
    "TinyWebServer Windows bombardier benchmark matrix"
    "time: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss zzz')"
    "url: $Url"
    "duration per case: ${DurationSeconds}s"
    "connection cases: $($Connections -join ' ')"
    "machine: $env:COMPUTERNAME"
    ""
) | Set-Content -Encoding UTF8 $rawFile

foreach ($connection in $Connections) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "connections: $connection"
    Write-Host "============================================================"

    $output = & bombardier -c $connection -d "${DurationSeconds}s" -l $Url 2>&1
    $text = $output -join [Environment]::NewLine

    @(
        ""
        "============================================================"
        "connections: $connection"
        "============================================================"
        $text
    ) | Add-Content -Encoding UTF8 $rawFile

    Write-Host $text

    $rps = Get-RegexValue $text 'Reqs/sec\s+([0-9]+(?:\.[0-9]+)?)'
    $latencyAvg = Convert-LatencyToMs (Get-RegexValue $text "Latency\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $latencyMax = Convert-LatencyToMs (Get-RegexValue $text "Latency\s+[0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern)\s+[0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern)\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $p50 = Convert-LatencyToMs (Get-RegexValue $text "50%\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $p90 = Convert-LatencyToMs (Get-RegexValue $text "90%\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $p95 = Convert-LatencyToMs (Get-RegexValue $text "95%\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $p99 = Convert-LatencyToMs (Get-RegexValue $text "99%\s+([0-9]+(?:\.[0-9]+)?(?:$LatencyUnitPattern))")
    $req2xx = Get-RegexValue $text '2xx\s*-\s*([0-9]+)'
    $req1xx = Get-RegexValue $text '1xx\s*-\s*([0-9]+)'
    $req3xx = Get-RegexValue $text '3xx\s*-\s*([0-9]+)'
    $req4xx = Get-RegexValue $text '4xx\s*-\s*([0-9]+)'
    $req5xx = Get-RegexValue $text '5xx\s*-\s*([0-9]+)'
    $others = Get-RegexValue $text 'others\s*-\s*([0-9]+)'
    $throughput = Get-RegexValue $text 'Throughput:\s*([^\r\n]+)'

    $non2xxValues = @($req1xx, $req3xx, $req4xx, $req5xx, $others) | ForEach-Object {
        if ($_ -eq "") { 0 } else { [int64]$_ }
    }
    $non2xx = ($non2xxValues | Measure-Object -Sum).Sum

    $line = @(
        (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
        $Url
        $DurationSeconds
        $connection
        $rps
        $latencyAvg
        $latencyMax
        $p50
        $p90
        $p95
        $p99
        $req2xx
        $non2xx
        $throughput.Trim()
    ) | ForEach-Object {
        '"' + ($_ -replace '"', '""') + '"'
    }

    Add-Content -Encoding UTF8 $csvFile ($line -join ",")

    if ($PauseSeconds -gt 0) {
        Start-Sleep -Seconds $PauseSeconds
    }
}

Write-Host ""
Write-Host "CSV saved: $csvFile"
Write-Host "Raw log saved: $rawFile"
