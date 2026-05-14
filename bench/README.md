# Benchmark

本目录提供可复现的本地压测脚本，不在仓库中预置或伪造任何性能数据。

## 使用方式

先编译并启动服务器：

```bash
cmake -S . -B build
cmake --build build
./build/tiny_webserver 8080 www 4 logs/server.log
```

压测时建议关闭逐请求访问日志：

```bash
./build/tiny_webserver 8080 www 4 logs/server.log 0
```

## Linux 跑

如果在 Linux 或虚拟机里直接压测，可以用项目自带的 Python/socket 脚本：

```bash
python3 bench/bench_http.py --host 127.0.0.1 --port 8080 --path /hello --connections 50 --duration 10
```

也可以压测静态首页：

```bash
python3 bench/bench_http.py --host 127.0.0.1 --port 8080 --path / --connections 50 --duration 10
```

单组压测并保存结果：

```bash
./bench/run_benchmark.sh
```

补充多组并发连接阶梯压测：

```bash
./bench/run_concurrency_matrix.sh
```

默认会依次压测 `50 100 200 500 1000` 个 keep-alive 连接。可以按机器能力调整连接数、路径和时长：

```bash
CONNECTION_CASES="100 200 500 1000" DURATION=20 REQUEST_PATH=/ ./bench/run_concurrency_matrix.sh
```

也可以用 Linux 版 `bombardier` 脚本，输出 CSV 和原始日志：

```bash
./bench/run_linux_bombardier_matrix.sh
```

默认访问 `http://127.0.0.1:8080/`，默认并发组为 `10 50 100 200 500 1000`，每组持续 30 秒。可以按需覆盖：

```bash
URL="http://10.241.34.106:8080/" \
CONNECTION_CASES="50 100 200 500 1000" \
DURATION_SECONDS=30 \
./bench/run_linux_bombardier_matrix.sh
```

Linux 脚本会在 `bench/results/` 下保存一份 `.csv` 汇总和一份 `.txt` 原始日志。

## Windows 跑

如果服务器跑在虚拟机里，可以在 Windows 本机用 `bombardier` 压测虚拟机地址：

```powershell
bench\run_windows_bombardier_matrix.ps1
```

脚本默认访问 `http://10.241.34.106:8080/`，默认并发组为 `10, 50, 100, 200, 500, 1000`，每组持续 30 秒。可以按需覆盖：

```powershell
bench\run_windows_bombardier_matrix.ps1 `
  -Url "http://10.241.34.106:8080/hello" `
  -Connections 50,100,200,500 `
  -DurationSeconds 20
```

Windows 脚本会在 `bench/results/` 下保存一份 `.csv` 汇总和一份 `.txt` 原始日志。


## 输出指标

脚本会输出：

- 总请求数
- 成功请求数
- 失败请求数
- requests/sec
- 平均延迟
- P50/P90/P99 延迟

## 记录模板

请在自己的机器上运行后再填写结果，不要直接复制他人的数字。

| 并发连接数 |   总请求数 |  成功请求数 | 失败请求数 |     QPS |    平均延迟 |     P50 |      P90 |      P99 |
| ----: | -----: | -----: | ----: | ------: | ------: | ------: | -------: | -------: |
|   100 | 174421 | 174421 |     0 | 8714.90 | 11.41ms |  9.91ms |  21.37ms |  36.52ms |
|   200 | 153040 | 153040 |     0 | 7640.96 | 25.57ms | 20.68ms |  50.56ms |  87.25ms |
|   500 | 154888 | 154888 |     0 | 7711.79 | 52.77ms | 36.93ms | 116.65ms | 221.91ms |
|  1000 | 152021 | 152021 |     0 | 7549.60 | 58.91ms | 41.00ms | 134.28ms | 273.66ms |
