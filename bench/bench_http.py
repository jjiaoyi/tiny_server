#!/usr/bin/env python3
import argparse
import socket
import statistics
import threading
import time


def parse_args():
    parser = argparse.ArgumentParser(description="TinyWebServer simple keep-alive benchmark")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--path", default="/hello")
    parser.add_argument("--connections", type=int, default=50)
    parser.add_argument("--duration", type=int, default=10)
    parser.add_argument("--timeout", type=float, default=3.0)
    return parser.parse_args()


def read_response(sock):
    data = bytearray()
    header_end = -1

    while header_end < 0:
        chunk = sock.recv(4096)
        if not chunk:
            raise ConnectionError("connection closed before headers")
        data.extend(chunk)
        header_end = data.find(b"\r\n\r\n")

    header = bytes(data[:header_end]).decode("iso-8859-1")
    content_length = 0
    status_ok = header.startswith("HTTP/1.1 200")

    for line in header.split("\r\n")[1:]:
        if line.lower().startswith("content-length:"):
            content_length = int(line.split(":", 1)[1].strip())
            break

    body_start = header_end + 4
    remaining = content_length - (len(data) - body_start)
    while remaining > 0:
        chunk = sock.recv(min(4096, remaining))
        if not chunk:
            raise ConnectionError("connection closed before body")
        remaining -= len(chunk)

    return status_ok


def worker(args, stop_at, latencies, counters, lock):
    request = (
        f"GET {args.path} HTTP/1.1\r\n"
        f"Host: {args.host}:{args.port}\r\n"
        "Connection: keep-alive\r\n"
        "\r\n"
    ).encode("ascii")

    local_ok = 0
    local_fail = 0
    local_latencies = []

    while time.perf_counter() < stop_at:
        try:
            with socket.create_connection((args.host, args.port), timeout=args.timeout) as sock:
                sock.settimeout(args.timeout)
                while time.perf_counter() < stop_at:
                    begin = time.perf_counter()
                    sock.sendall(request)
                    ok = read_response(sock)
                    elapsed_ms = (time.perf_counter() - begin) * 1000.0

                    if ok:
                        local_ok += 1
                        local_latencies.append(elapsed_ms)
                    else:
                        local_fail += 1
        except OSError:
            local_fail += 1

    with lock:
        counters["ok"] += local_ok
        counters["fail"] += local_fail
        latencies.extend(local_latencies)


def percentile(sorted_values, percent):
    if not sorted_values:
        return 0.0
    index = int(round((percent / 100.0) * (len(sorted_values) - 1)))
    return sorted_values[index]


def main():
    args = parse_args()
    if args.connections <= 0 or args.duration <= 0:
        raise SystemExit("connections and duration must be positive")

    stop_at = time.perf_counter() + args.duration
    threads = []
    latencies = []
    counters = {"ok": 0, "fail": 0}
    lock = threading.Lock()

    started_at = time.perf_counter()
    for _ in range(args.connections):
        thread = threading.Thread(target=worker, args=(args, stop_at, latencies, counters, lock))
        thread.start()
        threads.append(thread)

    for thread in threads:
        thread.join()

    elapsed = time.perf_counter() - started_at
    total = counters["ok"] + counters["fail"]
    sorted_latencies = sorted(latencies)
    avg_latency = statistics.mean(sorted_latencies) if sorted_latencies else 0.0

    print("TinyWebServer benchmark")
    print(f"url: http://{args.host}:{args.port}{args.path}")
    print(f"connections: {args.connections}")
    print(f"duration: {args.duration}s")
    print(f"elapsed: {elapsed:.2f}s")
    print(f"total requests: {total}")
    print(f"successful requests: {counters['ok']}")
    print(f"failed requests: {counters['fail']}")
    print(f"requests/sec: {counters['ok'] / elapsed:.2f}")
    print(f"latency avg: {avg_latency:.2f} ms")
    print(f"latency p50: {percentile(sorted_latencies, 50):.2f} ms")
    print(f"latency p90: {percentile(sorted_latencies, 90):.2f} ms")
    print(f"latency p99: {percentile(sorted_latencies, 99):.2f} ms")


if __name__ == "__main__":
    main()

