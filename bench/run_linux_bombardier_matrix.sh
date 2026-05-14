#!/usr/bin/env bash

set -u

URL="${URL:-http://10.241.34.106:8080/}"
CONNECTION_CASES="${CONNECTION_CASES:-10 50 100 200 500 1000}"
DURATION_SECONDS="${DURATION_SECONDS:-30}"

if ! command -v bombardier >/dev/null 2>&1; then
    echo "bombardier was not found."
    echo "Install it first, for example:"
    echo "  go install github.com/codesenberg/bombardier@latest"
    exit 1
fi

TIME_TAG="$(date +%Y%m%d_%H%M%S)"
OUT_DIR="bench/results/bombardier_${TIME_TAG}"
mkdir -p "$OUT_DIR"

SUMMARY="$OUT_DIR/summary.csv"

echo "url,connections,duration_seconds,reqs_per_sec,latency_avg,latency_p50,latency_p90,latency_p95,latency_p99,http_2xx,others,throughput" > "$SUMMARY"

echo "TinyWebServer bombardier benchmark matrix"
echo "time: $(date '+%Y-%m-%d %H:%M:%S')"
echo "url: $URL"
echo "duration per case: ${DURATION_SECONDS}s"
echo "connection cases: $CONNECTION_CASES"
echo "output dir: $OUT_DIR"
echo

for CONN in $CONNECTION_CASES; do
    RAW_FILE="$OUT_DIR/raw_c${CONN}.txt"

    echo "============================================================"
    echo "connections: $CONN"
    echo "============================================================"

    bombardier -c "$CONN" -d "${DURATION_SECONDS}s" -l "$URL" 2>&1 | tee "$RAW_FILE"

    REQS_PER_SEC="$(awk '/Reqs\/sec/ {print $2; exit}' "$RAW_FILE")"
    LAT_AVG="$(awk '/^[[:space:]]*Latency[[:space:]]+/ {print $2; exit}' "$RAW_FILE")"
    LAT_P50="$(awk '$1=="50%" {print $2; exit}' "$RAW_FILE")"
    LAT_P90="$(awk '$1=="90%" {print $2; exit}' "$RAW_FILE")"
    LAT_P95="$(awk '$1=="95%" {print $2; exit}' "$RAW_FILE")"
    LAT_P99="$(awk '$1=="99%" {print $2; exit}' "$RAW_FILE")"
    HTTP_2XX="$(grep -E '2xx -' "$RAW_FILE" | sed -E 's/.*2xx - ([0-9]+).*/\1/' | head -n 1)"
    OTHERS="$(grep -E 'others -' "$RAW_FILE" | sed -E 's/.*others - ([0-9]+).*/\1/' | head -n 1)"
    THROUGHPUT="$(awk '/Throughput:/ {print $2; exit}' "$RAW_FILE")"

    echo "${URL},${CONN},${DURATION_SECONDS},${REQS_PER_SEC},${LAT_AVG},${LAT_P50},${LAT_P90},${LAT_P95},${LAT_P99},${HTTP_2XX},${OTHERS},${THROUGHPUT}" >> "$SUMMARY"

    echo
done

echo "============================================================"
echo "summary saved to: $SUMMARY"
echo "raw outputs saved to: $OUT_DIR"
echo "============================================================"
cat "$SUMMARY"
