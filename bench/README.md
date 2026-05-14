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

在 WSL 或 Linux 上用 `bombardier` 压测虚拟机里的服务器：

```bash
./bench/run_linux_bombardier_matrix.sh
```

默认访问 `http://10.241.34.106:8080/`，默认并发组为 `10 50 100 200 500 1000`，每组持续 30 秒。可以按需覆盖：

```bash
URL="http://10.241.34.106:8080/" \
CONNECTION_CASES="50 100 200 500 1000" \
DURATION_SECONDS=30 \
./bench/run_linux_bombardier_matrix.sh
```

Linux 脚本会在 `bench/results/bombardier_<时间>/` 下保存 `summary.csv` 汇总和每个并发组的 `raw_c*.txt` 原始日志。

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

已有压测记录：

- `docs/benchmark_bombardier_20260514.md`

| 并发连接数 |     QPS | 平均延迟 | P50 | P90 | P95 | P99 | 2xx 响应数 | 其他响应数 | 吞吐量 |
| ----: | ------: | ------: | ------: | ------: | ------: | ------: | -----: | ----: | ------: |
|    50 |         |         |         |         |         |         |        |       |         |
|   100 |         |         |         |         |         |         |        |       |         |
|   200 |         |         |         |         |         |         |        |       |         |
|   500 |         |         |         |         |         |         |        |       |         |
|  1000 |         |         |         |         |         |         |        |       |         |
