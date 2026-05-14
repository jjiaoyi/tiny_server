# TinyWebServer

一个用于个人简历和面试讲解的轻量级 C++ Web 服务器。项目使用 C++17 编写，不依赖第三方网络库，核心网络能力基于 Linux Socket API、非阻塞 socket、epoll 和线程池实现。

## 项目简介

TinyWebServer 支持基础 HTTP/1.1 请求解析和静态文件访问：

- `GET /` 返回 `www/index.html`
- `GET /xxx` 返回 `www` 目录下对应静态文件
- `GET /hello` 返回 JSON：`{"message":"hello from tiny web server"}`
- 文件不存在返回 `404`
- 不支持的方法返回 `405`
- 请求格式错误返回 `400`

## 架构说明

项目分为五个核心模块：

- `net`：封装 TCP 服务端、epoll 和 socket 工具函数。
- `http`：负责 HTTP 请求解析和 HTTP 响应构造。
- `thread`：固定大小线程池，用于处理客户端请求。
- `log`：简单线程安全日志，输出到控制台和 `logs/server.log`。
- `util`：文件读取、路径拼接、MIME 类型判断。

主线程只负责监听 epoll 事件和接收新连接；客户端请求处理交给线程池，避免一个连接一个线程造成资源浪费。

## 一次 HTTP 请求的完整处理流程

1. 浏览器或 curl 与服务器建立 TCP 连接。
2. `listen fd` 在 epoll 中触发可读事件。
3. 主线程调用 `accept` 取出新连接，并设置 client fd 为非阻塞。
4. 主线程把 client fd 注册到 epoll，关注 `EPOLLIN | EPOLLONESHOT`。
5. client fd 可读后，主线程把处理任务放入线程池。
6. worker 线程调用 `recv` 读取 HTTP 请求数据。
7. `HttpParser` 解析请求行、请求头和空行。
8. 根据 method 和 path 构造响应：
   - `/hello` 返回 JSON
   - `/` 或静态文件路径读取 `www` 目录文件
   - 异常情况返回对应错误状态码
9. worker 线程调用 `send` 发送 HTTP 响应。
10. 当前版本采用短连接，响应发送完成后关闭 client fd。

## Socket 主流程说明

服务端启动时的 socket 流程：

1. `socket(AF_INET, SOCK_STREAM, 0)` 创建 TCP socket。
2. `setsockopt(SO_REUSEADDR)` 允许端口快速复用。
3. `bind` 绑定本机 IP 和端口。
4. `listen` 开始监听连接。
5. `accept` 从已完成连接队列中取出客户端连接。
6. `recv` 读取请求。
7. `send` 写回响应。
8. `close` 关闭连接。

## epoll 工作流程说明

1. `epoll_create1` 创建 epoll 实例。
2. `epoll_ctl ADD` 注册 listen fd，关注 `EPOLLIN`。
3. `epoll_wait` 阻塞等待事件。
4. listen fd 可读时，循环 `accept` 直到返回 `EAGAIN`。
5. 新 client fd 设置为非阻塞，并注册 `EPOLLIN | EPOLLONESHOT`。
6. client fd 可读时，把任务交给线程池。
7. 使用 `EPOLLONESHOT` 后，同一个 client fd 在 worker 处理期间不会被重复派发给其他线程。

## 线程池工作流程说明

线程池初始化时创建固定数量 worker 线程。主线程收到 client fd 可读事件后，把 `handleClient(fd)` 封装成任务放入队列，并通过条件变量唤醒 worker。worker 线程取出任务、读取请求、解析 HTTP、构造响应并发送。

这样可以避免每个连接都创建新线程，减少线程创建销毁开销，也更容易控制并发资源。

## HTTP 请求解析和响应构造说明

`HttpParser` 只解析本项目需要的 HTTP 子集：

- 请求行：`method path version`
- 请求头：按 `Header-Name: value` 解析
- 空行：通过 `\r\n\r\n` 判断 header 结束

`HttpResponse` 负责生成标准响应字符串：

```text
HTTP/1.1 200 OK
Content-Type: text/html; charset=utf-8
Content-Length: ...
Connection: close

响应体
```

## 编译运行方式

```bash
mkdir build
cd build
cmake ..
make
./tiny_webserver 8080 ../www
```

另开一个终端测试：

```bash
curl -i http://127.0.0.1:8080/
curl -i http://127.0.0.1:8080/hello
curl -i http://127.0.0.1:8080/notfound.html
```

也可以执行：

```bash
bash tests/simple_test.sh
```

## 常见面试追问与回答

### 为什么使用非阻塞 socket？

非阻塞 socket 可以避免某个连接没有数据时阻塞线程。配合 epoll，服务器只在 fd 就绪时处理 I/O，提高并发连接处理能力。

### epoll 相比 select 有什么优势？

`select` 需要每次传入 fd 集合，并且有 fd 数量限制；epoll 把 fd 注册到内核事件表，通过 `epoll_wait` 返回就绪事件，更适合大量连接场景。

### 为什么使用 EPOLLONESHOT？

同一个 client fd 可读时，如果多个线程同时处理，可能出现重复读取、数据错乱或提前关闭的问题。`EPOLLONESHOT` 保证事件触发一次后自动失效，worker 处理完成后关闭连接或重新注册事件。

### 为什么使用线程池？

线程池避免每个连接创建一个线程，减少线程创建销毁成本，并限制并发线程数量，使服务端资源更可控。

### 这个项目是 Reactor 模型吗？

可以看作一个简化版 Reactor。主线程负责事件监听和分发，worker 线程负责具体 I/O 读取、HTTP 解析和响应处理。严格高性能 Reactor 通常会进一步拆分 I/O 线程和业务线程。

### 当前 HTTP 支持完整吗？

不完整。项目只支持面试演示需要的 HTTP/1.1 子集：GET、请求行、基础 header、静态文件和简单 JSON 接口。暂不支持 HTTPS、长连接、分块传输、请求体和 CGI。

### 如何继续优化？

可以增加长连接支持、定时器关闭空闲连接、更完整的 HTTP 解析、统一错误页、异步日志、配置文件、压测和更细粒度的连接状态管理。

