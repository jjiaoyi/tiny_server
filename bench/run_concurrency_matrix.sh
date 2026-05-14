#!/usr/bin/env bash
set -euo pipefail

HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-8080}"
REQUEST_PATH="${REQUEST_PATH:-/hello}"
DURATION="${DURATION:-10}"
CONNECTION_CASES="${CONNECTION_CASES:-50 100 200 500 1000}"
RESULT_DIR="${RESULT_DIR:-bench/results}"
TIMEOUT="${TIMEOUT:-3.0}"
PAUSE_SECONDS="${PAUSE_SECONDS:-1}"

mkdir -p "${RESULT_DIR}"

timestamp="$(date '+%Y%m%d_%H%M%S')"
result_file="${RESULT_DIR}/concurrency_matrix_${timestamp}.txt"

{
    echo "TinyWebServer concurrency benchmark matrix"
    echo "time: $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "host: ${HOST}"
    echo "port: ${PORT}"
    echo "path: ${REQUEST_PATH}"
    echo "duration per case: ${DURATION}s"
    echo "connection cases: ${CONNECTION_CASES}"
    echo "timeout: ${TIMEOUT}s"
    echo "system: $(uname -a)"

    for connections in ${CONNECTION_CASES}; do
        echo
        echo "============================================================"
        echo "connections: ${connections}"
        echo "============================================================"
        python3 bench/bench_http.py \
            --host "${HOST}" \
            --port "${PORT}" \
            --path "${REQUEST_PATH}" \
            --connections "${connections}" \
            --duration "${DURATION}" \
            --timeout "${TIMEOUT}"

        if [[ "${PAUSE_SECONDS}" != "0" ]]; then
            sleep "${PAUSE_SECONDS}"
        fi
    done
} | tee "${result_file}"

echo
echo "saved result: ${result_file}"
