#!/usr/bin/env bash
set -e

BASE_URL="${1:-http://127.0.0.1:8080}"

echo "== GET / =="
curl --noproxy '*' -i "${BASE_URL}/"

echo
echo "== GET /hello =="
curl --noproxy '*' -i "${BASE_URL}/hello"

echo
echo "== GET /notfound.html =="
curl --noproxy '*' -i "${BASE_URL}/notfound.html"

echo
echo "== POST /hello should be 405 =="
curl --noproxy '*' -i -X POST "${BASE_URL}/hello"

echo
echo "== GET /../CMakeLists.txt should not escape www root =="
curl --noproxy '*' -i "${BASE_URL}/%2e%2e/CMakeLists.txt"

echo
echo "simple tests finished"
