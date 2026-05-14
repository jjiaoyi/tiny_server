# TinyWebServer 使用、压测与实现检查

本文档记录当前服务器的功能范围、运行方式、压测方式和关键实现检查结论。目标是让项目“简历可写、面试可讲、代码能跑、压测能复现”，不把项目包装成工业级服务器。

## 当前支持的 HTTP 路径

| 请求 | 路径 | 行为 |
| --- | --- | --- |
| `GET` | `/` | 返回 `www/index.html` |
| `GET` | `/hello` | 返回 JSON：`{"message":"hello from tiny web server"}` |
| `GET` | `/xxx` | 返回 `www` 目录下对应静态文件 |
| `HEAD` | `/`、`/hello`、`/xxx` | 返回与 `GET` 相同的响应头，但不发送响应体 |
| 其他方法 | 任意路径 | 返回 `405 Method Not Allowed` |
| 合法方法但文件不存在 | `/notfound.html` 等 | 返回 `404 Not Found` |
| 请求格式错误 | 非法请求行、非法 header、请求头过大等 | 返回 `400 Bad Request` |

路径安全边界：

- 静态资源只能从 `www` 根目录下读取。
- URL 解码后会做路径规范化，阻止 `/%2e%2e/CMakeLists.txt` 这类目录穿越访问。

## Reactor 模型的事件流转过程

当前实现是“简化 Reactor + 线程池”，不是多 Reactor。

事件流转：

1. `TcpServer::start()` 创建 listen socket，并把 `listen fd` 注册到 epoll。
2. 主线程循环调用 `epoll_wait` 等待事件。
3. 如果 `listen fd` 可读，说明有新连接到来，主线程循环 `accept`，直到 `accept` 返回 `EAGAIN`。
4. 新的 `client fd` 被设置为非阻塞，并注册 `EPOLLIN | EPOLLONESHOT | EPOLLRDHUP`。
5. 如果 `client fd` 可读，主线程先把连接标记为 `processing=true`，再把 `handleClient(fd)` 投递到线程池。
6. worker 线程读取 HTTP 请求、解析请求、构造响应并发送。
7. 如果需要 keep-alive，worker 清空该连接的请求缓冲区，并通过 `epoll_ctl MOD` 重新注册 `EPOLLONESHOT`。
8. 如果请求不需要 keep-alive、发生错误或客户端关闭连接，则从 epoll 删除 fd 并关闭 fd。
9. 主线程定期扫描空闲连接，超过默认 30 秒无活动则关闭。

当前没有实现：

- main Reactor + sub Reactor 多事件循环。
- 连接写缓冲区。
- 基于 `EPOLLOUT` 的完整异步写。
- `EPOLLET` 边缘触发模式。

## 线程模型说明

当前线程模型：

| 线程 | 职责 |
| --- | --- |
| 主线程 | `epoll_wait`、`accept` 新连接、分发可读 client fd、清理空闲连接 |
| worker 线程池 | `recv` 请求、HTTP 解析、读取静态文件、构造响应、`send` 响应 |
| 日志后台线程 | 批量刷新日志缓冲到控制台和日志文件 |

设计取舍：

- 好处：结构简单，容易讲清楚，避免每个连接创建一个线程。
- 代价：worker 线程承担了 I/O 读写和业务处理；严格高性能 Reactor 通常会把 I/O 事件循环和业务线程进一步拆开。

## 如何编译

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build
```

也可以使用传统方式：

```bash
mkdir build
cd build
cmake ..
make
```

## 如何启动服务器

在项目根目录启动：

```bash
./build/tiny_webserver 8080 www
```

带完整参数启动：

```bash
./build/tiny_webserver 8080 www 4 logs/server.log 1
```

参数含义：

```text
./tiny_webserver <port> <www_root> [thread_count] [log_file] [access_log:0|1]
```

压测时建议关闭逐请求访问日志，减少日志输出对结果的影响：

```bash
./build/tiny_webserver 8080 www 4 logs/server.log 0
```

## 如何执行功能测试

服务器启动后，另开终端执行：

```bash
./tests/simple_test.sh
```

功能测试会覆盖：

- `GET /`
- `GET /hello`
- `HEAD /hello`
- `GET /notfound.html`
- `POST /hello` 返回 405
- `GET /%2e%2e/CMakeLists.txt` 目录穿越拦截

## 如何执行压测

先启动服务器，建议关闭访问日志：

```bash
./build/tiny_webserver 8080 www 4 logs/server.log 0
```

另开终端执行：

```bash
python3 bench/bench_http.py --host 127.0.0.1 --port 8080 --path /hello --connections 50 --duration 10
```

也可以使用保存结果的封装脚本：

```bash
./bench/run_benchmark.sh
```

可通过环境变量调整参数：

```bash
CONNECTIONS=100 DURATION=20 REQUEST_PATH=/ ./bench/run_benchmark.sh
```

补充多组并发连接阶梯压测：

```bash
./bench/run_concurrency_matrix.sh
```

默认依次压测 `50 100 200 500 1000` 个 keep-alive 连接。可以按机器能力调整：

```bash
CONNECTION_CASES="100 200 500 1000" DURATION=20 REQUEST_PATH=/hello ./bench/run_concurrency_matrix.sh
```

如果只想补跑 1000 连接这一组：

```bash
CONNECTION_CASES="1000" DURATION=20 REQUEST_PATH=/hello ./bench/run_concurrency_matrix.sh
```

## 如何查看压测结果

`bench/bench_http.py` 会直接在终端输出：

- 总请求数
- 成功请求数
- 失败请求数
- requests/sec
- 平均延迟
- P50/P90/P99 延迟

`bench/run_benchmark.sh` 会把真实压测输出保存到：

```text
bench/results/benchmark_YYYYmmdd_HHMMSS.txt
```

`bench/run_concurrency_matrix.sh` 会把多组并发连接压测输出保存到：

```text
bench/results/concurrency_matrix_YYYYmmdd_HHMMSS.txt
```

查看最近一次结果：

```bash
ls -lt bench/results/
cat bench/results/benchmark_*.txt
```

注意：仓库不在 README 中编写固定 QPS 或延迟数字。压测数据必须来自本机实际运行，并和机器配置、线程数、连接数、是否开启访问日志等条件一起记录。

## 关键实现检查

### 1. 非阻塞读写有没有正确处理 EAGAIN？

读方向：已处理。

- `accept` 返回 `EAGAIN/EWOULDBLOCK` 时，说明当前连接队列已经取空，退出 accept 循环。
- `recv` 返回 `EAGAIN/EWOULDBLOCK` 时，说明当前没有更多数据可读；如果请求还不完整，会重新注册 `EPOLLONESHOT` 等待下一次可读事件。

写方向：部分处理，但还不是完整 Reactor 写模型。

- `send` 返回 `EAGAIN/EWOULDBLOCK` 时，当前实现使用 `poll(POLLOUT)` 等待短时间可写，然后继续发送。
- 当前没有把未写完的数据放入连接级 output buffer，也没有注册 `EPOLLOUT` 到 epoll。

结论：非阻塞读处理合理；非阻塞写能处理短响应的部分写和 `EAGAIN`，但不是完整异步写。

### 2. HTTP 请求是否支持半包读取？

支持基础半包读取。

当前每个连接维护一个请求缓冲区。worker 每次 `recv` 后把数据追加到该缓冲区，直到发现 HTTP 头部结束标记：

```text
\r\n\r\n
```

如果没有读到完整 header，会重新注册 `EPOLLONESHOT`，等待下一次可读事件。

当前限制：

- 只解析请求头，不支持请求体。
- 不支持 HTTP pipeline 中一次读到多个请求后连续解析多个请求。

### 3. 写缓冲区没写完时是否注册 EPOLLOUT？

没有。

当前实现是：

- `sendAll()` 循环发送响应。
- 如果 `send` 写出部分数据，会继续发送剩余数据。
- 如果 `send` 返回 `EAGAIN`，使用 `poll(POLLOUT)` 等待可写。

这能保证当前小响应基本正确发送，但不是完整的 Reactor 写事件处理。

后续优化方向：

1. 给每个连接增加 `outputBuffer`。
2. `send` 没写完时，把剩余数据放入 `outputBuffer`。
3. 通过 `epoll_ctl MOD` 注册 `EPOLLOUT`。
4. fd 可写时继续发送。
5. 发送完成后取消 `EPOLLOUT`，保留 `EPOLLIN | EPOLLONESHOT`。

### 4. 连接关闭时 fd 有没有从 epoll 删除并 close？

有。

当前关闭路径统一调用 `closeClient(fd)`：

```text
poller_.delFd(fd)
close(fd)
connections_.erase(fd)
```

触发关闭的情况包括：

- 客户端关闭连接，`recv` 返回 0。
- epoll 返回错误或关闭事件。
- 请求不需要 keep-alive。
- 请求格式错误或文件不存在等错误响应后关闭。
- 空闲连接超时。
- 发送失败或超时。

### 5. 压测结果有没有真实保存，而不是 README 里编的数字？

现在支持真实保存。

- README 不写固定 QPS 或延迟数字。
- `bench/bench_http.py` 只负责运行并输出指标。
- `bench/run_benchmark.sh` 会把实际运行输出保存到 `bench/results/benchmark_YYYYmmdd_HHMMSS.txt`。

压测结果是否可信，需要同时记录：

- 机器 CPU、内存和系统版本。
- 编译方式。
- 启动参数。
- 压测连接数、时长、请求路径。
- 是否关闭访问日志。

## 当前不足与后续优化

当前不足：

- 不是多 Reactor；主线程负责所有 epoll 事件分发。
- worker 线程直接做 socket 读写和 HTTP 处理。
- 写方向没有连接级 output buffer，也没有注册 `EPOLLOUT`。
- 空闲连接清理是定期扫描，不是时间轮或小根堆。
- HTTP 只支持基础 GET/HEAD 和请求头解析，不支持请求体、chunked、pipeline。
- 静态文件通过普通文件读取，没有使用 `sendfile`。
- 连接表使用全局互斥锁，高并发下可能成为瓶颈。
- 压测脚本是轻量自测工具，不替代 `wrk`、`ab` 等专业压测工具。

后续可以优化：

- 增加连接级 input/output buffer。
- 用 `EPOLLOUT` 实现完整异步写。
- 抽象更轻量的 `Connection` 状态机，但避免过度设计。
- 使用小根堆或 `timerfd` 管理空闲连接。
- 支持请求体和更完整的 HTTP 错误页。
- 静态文件响应使用 `sendfile`。
- 增加 `wrk` 压测脚本，并记录本机真实结果。
- 将访问日志按级别或采样输出，避免压测时日志影响性能。
