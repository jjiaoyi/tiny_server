#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1:8080}"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "${TMP_DIR}"' EXIT

request() {
    local name="$1"
    local method="$2"
    local url="$3"
    local body_file="${TMP_DIR}/${name}.body"
    local header_file="${TMP_DIR}/${name}.headers"

    if [[ "${method}" == "HEAD" ]]; then
        : > "${body_file}"
        curl --noproxy '*' -sS --head -D "${header_file}" -o /dev/null \
            "${url}" -w '%{http_code}'
    else
        curl --noproxy '*' -sS -D "${header_file}" -o "${body_file}" \
            -X "${method}" "${url}" -w '%{http_code}'
    fi
}

assert_equals() {
    local actual="$1"
    local expected="$2"
    local message="$3"

    if [[ "${actual}" != "${expected}" ]]; then
        echo "FAIL: ${message}. expected=${expected}, actual=${actual}" >&2
        exit 1
    fi
}

assert_contains() {
    local file="$1"
    local expected="$2"
    local message="$3"

    if ! grep -q "${expected}" "${file}"; then
        echo "FAIL: ${message}. missing=${expected}" >&2
        echo "---- ${file} ----" >&2
        cat "${file}" >&2
        exit 1
    fi
}

echo "== GET / =="
status="$(request index GET "${BASE_URL}/")"
assert_equals "${status}" "200" "GET / status"
assert_contains "${TMP_DIR}/index.body" "TinyWebServer is running" "GET / body"

echo "== GET /hello =="
status="$(request hello GET "${BASE_URL}/hello")"
assert_equals "${status}" "200" "GET /hello status"
assert_contains "${TMP_DIR}/hello.body" '"message":"hello from tiny web server"' "GET /hello body"
assert_contains "${TMP_DIR}/hello.headers" "Connection: keep-alive" "GET /hello keep-alive header"

echo "== HEAD /hello =="
status="$(request head_hello HEAD "${BASE_URL}/hello")"
assert_equals "${status}" "200" "HEAD /hello status"
assert_contains "${TMP_DIR}/head_hello.headers" "Content-Length: 40" "HEAD /hello content length"
assert_equals "$(wc -c < "${TMP_DIR}/head_hello.body" | tr -d ' ')" "0" "HEAD /hello empty body"

echo "== GET /notfound.html =="
status="$(request notfound GET "${BASE_URL}/notfound.html")"
assert_equals "${status}" "404" "GET /notfound.html status"

echo "== POST /hello should be 405 =="
status="$(request post_hello POST "${BASE_URL}/hello")"
assert_equals "${status}" "405" "POST /hello status"
assert_contains "${TMP_DIR}/post_hello.headers" "Allow: GET, HEAD" "POST /hello allow header"

echo "== GET /../CMakeLists.txt should not escape www root =="
status="$(request traversal GET "${BASE_URL}/%2e%2e/CMakeLists.txt")"
assert_equals "${status}" "404" "directory traversal status"

echo "all simple tests passed"
