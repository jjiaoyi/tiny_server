#!/usr/bin/env bash
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8080}"
REQUEST_PATH="${REQUEST_PATH:-/hello}"
CONNECTIONS="${CONNECTIONS:-50}"
DURATION="${DURATION:-10}"
RESULT_DIR="${RESULT_DIR:-bench/results}"

mkdir -p "${RESULT_DIR}"

timestamp="$(date '+%Y%m%d_%H%M%S')"
result_file="${RESULT_DIR}/benchmark_${timestamp}.txt"

{
    echo "TinyWebServer benchmark run"
    echo "time: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "host: ${HOST}"
    echo "port: ${PORT}"
    echo "path: ${REQUEST_PATH}"
    echo "connections: ${CONNECTIONS}"
    echo "duration: ${DURATION}s"
    echo "system: $(uname -a)"
    echo
    python3 bench/bench_http.py \
        --host "${HOST}" \
        --port "${PORT}" \
        --path "${REQUEST_PATH}" \
        --connections "${CONNECTIONS}" \
        --duration "${DURATION}"
} | tee "${result_file}"

echo
echo "saved result: ${result_file}"

