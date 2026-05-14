#!/usr/bin/env bash
set -euo pipefail

URL="${URL:-http://127.0.0.1:8080/}"
CONNECTION_CASES="${CONNECTION_CASES:-10 50 100 200 500 1000}"
DURATION_SECONDS="${DURATION_SECONDS:-30}"
RESULT_DIR="${RESULT_DIR:-bench/results}"
PAUSE_SECONDS="${PAUSE_SECONDS:-2}"

microsecond_unit="$(printf '\302\265s')"
latency_unit_pattern="ns|us|${microsecond_unit}|ms|s"

latency_to_ms() {
    local value="$1"
    local number unit

    if [[ -z "${value}" ]]; then
        printf ''
        return
    fi

    if [[ ! "${value}" =~ ^([0-9]+([.][0-9]+)?)(ns|us|${microsecond_unit}|ms|s)$ ]]; then
        printf '%s' "${value}"
        return
    fi

    number="${BASH_REMATCH[1]}"
    unit="${BASH_REMATCH[3]}"

    awk -v n="${number}" -v u="${unit}" -v micro="${microsecond_unit}" 'BEGIN {
        if (u == "ns") {
            printf "%.3f", n / 1000000.0
        } else if (u == "us" || u == micro) {
            printf "%.3f", n / 1000.0
        } else if (u == "ms") {
            printf "%.3f", n
        } else if (u == "s") {
            printf "%.3f", n * 1000.0
        } else {
            printf "%s", n
        }
    }'
}

regex_value() {
    local text="$1"
    local pattern="$2"

    printf '%s\n' "${text}" | perl -0777 -ne "print \$1 if /${pattern}/i"
}

csv_escape() {
    local value="${1:-}"
    value="${value//\"/\"\"}"
    printf '"%s"' "${value}"
}

if ! command -v bombardier >/dev/null 2>&1; then
    echo "bombardier was not found."
    echo "Install it first, for example:"
    echo "  go install github.com/codesenberg/bombardier@latest"
    echo "or download it from https://github.com/codesenberg/bombardier/releases"
    exit 1
fi

mkdir -p "${RESULT_DIR}"

timestamp="$(date '+%Y%m%d_%H%M%S')"
csv_file="${RESULT_DIR}/linux_bombardier_matrix_${timestamp}.csv"
raw_file="${RESULT_DIR}/linux_bombardier_matrix_${timestamp}.txt"

printf '%s\n' 'time,url,duration_seconds,connections,requests_per_sec,latency_avg_ms,latency_max_ms,p50_ms,p90_ms,p95_ms,p99_ms,2xx,non_2xx,throughput' >"${csv_file}"

{
    echo "TinyWebServer Linux bombardier benchmark matrix"
    echo "time: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "url: ${URL}"
    echo "duration per case: ${DURATION_SECONDS}s"
    echo "connection cases: ${CONNECTION_CASES}"
    echo "system: $(uname -a)"
    echo
} >"${raw_file}"

for connections in ${CONNECTION_CASES}; do
    echo
    echo "============================================================"
    echo "connections: ${connections}"
    echo "============================================================"

    output="$(bombardier -c "${connections}" -d "${DURATION_SECONDS}s" -l "${URL}" 2>&1)"

    {
        echo
        echo "============================================================"
        echo "connections: ${connections}"
        echo "============================================================"
        printf '%s\n' "${output}"
    } >>"${raw_file}"

    printf '%s\n' "${output}"

    rps="$(regex_value "${output}" 'Reqs/sec\s+([0-9]+(?:\.[0-9]+)?)')"
    latency_avg="$(latency_to_ms "$(regex_value "${output}" "Latency\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    latency_max="$(latency_to_ms "$(regex_value "${output}" "Latency\s+[0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern})\s+[0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern})\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    p50="$(latency_to_ms "$(regex_value "${output}" "50%\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    p90="$(latency_to_ms "$(regex_value "${output}" "90%\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    p95="$(latency_to_ms "$(regex_value "${output}" "95%\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    p99="$(latency_to_ms "$(regex_value "${output}" "99%\s+([0-9]+(?:\.[0-9]+)?(?:${latency_unit_pattern}))")")"
    req_1xx="$(regex_value "${output}" '1xx\s*-\s*([0-9]+)')"
    req_2xx="$(regex_value "${output}" '2xx\s*-\s*([0-9]+)')"
    req_3xx="$(regex_value "${output}" '3xx\s*-\s*([0-9]+)')"
    req_4xx="$(regex_value "${output}" '4xx\s*-\s*([0-9]+)')"
    req_5xx="$(regex_value "${output}" '5xx\s*-\s*([0-9]+)')"
    req_others="$(regex_value "${output}" 'others\s*-\s*([0-9]+)')"
    throughput="$(regex_value "${output}" 'Throughput:\s*([^\r\n]+)')"

    non_2xx="$(( ${req_1xx:-0} + ${req_3xx:-0} + ${req_4xx:-0} + ${req_5xx:-0} + ${req_others:-0} ))"

    {
        csv_escape "$(date '+%Y-%m-%d %H:%M:%S')"; printf ','
        csv_escape "${URL}"; printf ','
        csv_escape "${DURATION_SECONDS}"; printf ','
        csv_escape "${connections}"; printf ','
        csv_escape "${rps}"; printf ','
        csv_escape "${latency_avg}"; printf ','
        csv_escape "${latency_max}"; printf ','
        csv_escape "${p50}"; printf ','
        csv_escape "${p90}"; printf ','
        csv_escape "${p95}"; printf ','
        csv_escape "${p99}"; printf ','
        csv_escape "${req_2xx}"; printf ','
        csv_escape "${non_2xx}"; printf ','
        csv_escape "${throughput}"
        printf '\n'
    } >>"${csv_file}"

    if [[ "${PAUSE_SECONDS}" -gt 0 ]]; then
        sleep "${PAUSE_SECONDS}"
    fi
done

echo
echo "CSV saved: ${csv_file}"
echo "Raw log saved: ${raw_file}"
