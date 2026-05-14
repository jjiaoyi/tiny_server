# 从零掌握 TinyWebServer 中的 Linux 网络编程全流程

本文档面向“没怎么学过 Linux 网络编程，但想完整讲清楚本项目”的读者。目标不是把所有网络协议细节一次学完，而是围绕 TinyWebServer 这个项目，把一个 C++ Web 服务器从启动、监听、接收连接、读取请求、解析 HTTP、返回响应、长连接复用到关闭连接的全过程讲透。

对应项目入口：

- `src/main.cpp`
- `src/net/SocketUtil.cpp`
- `src/net/EpollPoller.cpp`
- `src/net/TcpServer.cpp`
- `src/http/HttpParser.cpp`
- `src/http/HttpResponse.cpp`
- `src/thread/ThreadPool.cpp`

## 1. 你最终要掌握什么

学完本文档后，你应该能回答这些问题：

1. 浏览器访问 `http://127.0.0.1:8080/hello` 时，服务器内部发生了什么？
2. `socket`、`bind`、`listen`、`accept` 分别做什么？
3. 为什么网络连接在 Linux 里也是一个 `fd`？
4. 阻塞 I/O 和非阻塞 I/O 有什么区别？
5. 为什么需要 epoll？
6. `epoll_wait` 返回“可读”到底是什么意思？
7. 为什么使用 `EPOLLONESHOT`？
8. 线程池在本项目中解决了什么问题？
9. HTTP 请求是如何从字节流变成 `method/path/version/header` 的？
10. 静态文件路径为什么要做目录穿越防护？

## 2. 从一次浏览器请求开始

假设你已经启动服务器：

```bash
cd build
./tiny_webserver 8080 ../www
```

然后执行：

```bash
curl -i http://127.0.0.1:8080/hello
```

你看到的响应大概是：

```http
HTTP/1.1 200 OK
Connection: keep-alive
Content-Length: 40
Content-Type: application/json; charset=utf-8
Keep-Alive: timeout=30
Server: TinyWebServer

{"message":"hello from tiny web server"}
```

这背后的完整链路是：

1. `curl` 创建客户端 socket。
2. 客户端向 `127.0.0.1:8080` 发起 TCP 连接。
3. 服务器的 `listen fd` 收到新连接事件。
4. 服务器调用 `accept` 得到一个新的 `client fd`。
5. 服务器把 `client fd` 设置成非阻塞。
6. 服务器把 `client fd` 加入 epoll，监听可读事件。
7. 客户端发送 HTTP 请求文本。
8. epoll 告诉服务器：这个 `client fd` 可读。
9. 主线程把 `client fd` 交给线程池。
10. worker 线程调用 `recv` 读取请求字节。
11. `HttpParser` 解析请求行和请求头。
12. 发现 path 是 `/hello`，构造 JSON 响应。
13. worker 线程调用 `send` 写回响应。
14. 如果客户端允许 keep-alive，服务器清空连接缓冲区并重新注册 epoll 事件。
15. 如果客户端关闭连接、请求出错或空闲超时，服务器关闭 `client fd`。

当前项目支持 HTTP/1.1 keep-alive。也就是说，同一个 TCP 连接可以连续处理多个请求；如果连接空闲超过默认 30 秒，服务器会主动关闭它。

## 3. Linux 中“一切皆文件”和 fd

Linux 下，普通文件、socket、管道、终端设备都可以用文件描述符表示。

文件描述符就是一个非负整数，例如：

- `0`：标准输入
- `1`：标准输出
- `2`：标准错误
- `3` 及以后：进程打开的新文件、socket 等

在本项目中：

- `listenFd_` 是监听 socket 对应的 fd。
- `clientFd` 是某个客户端连接对应的 fd。
- epoll 内部也有自己的 fd，即 `epollFd_`。

你可以把 fd 理解成“进程访问内核对象的句柄”。用户态程序不会直接操作网卡，而是通过 fd 调用系统调用，让内核帮你完成网络收发。

## 4. TCP 服务端的标准流程

一个 TCP 服务端的最小流程通常是：

```cpp
int listenFd = socket(AF_INET, SOCK_STREAM, 0);
bind(listenFd, ...);
listen(listenFd, SOMAXCONN);

while (true) {
    int clientFd = accept(listenFd, ...);
    recv(clientFd, ...);
    send(clientFd, ...);
    close(clientFd);
}
```

本项目也是这个流程，只是加上了：

- 非阻塞 socket
- epoll
- 线程池
- HTTP 解析
- 静态文件读取
- 异步日志

对应代码在 `src/net/SocketUtil.cpp` 和 `src/net/TcpServer.cpp`。

## 5. socket：创建网络通信端点

项目代码：

```cpp
int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
```

含义：

- `AF_INET`：使用 IPv4。
- `SOCK_STREAM`：使用 TCP。
- `0`：协议默认，配合 `SOCK_STREAM` 时就是 TCP。

如果成功，返回一个 fd；失败返回 `-1`，错误原因放在 `errno` 中。

常见失败原因：

- 系统 fd 数量耗尽。
- 进程资源限制过低。
- 参数传错。

本项目里创建失败会写日志：

```cpp
Logger::instance().error("socket failed: " + std::string(std::strerror(errno)));
```

## 6. setsockopt：端口快速复用

项目代码：

```cpp
int opt = 1;
setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

作用：允许服务器重启时更快重新绑定同一个端口。

为什么需要？

TCP 连接关闭后可能进入 `TIME_WAIT` 状态。如果没有设置 `SO_REUSEADDR`，你刚关掉服务器又马上重启，可能遇到：

```text
bind failed: Address already in use
```

注意：`SO_REUSEADDR` 不是万能的。它不代表两个服务可以随便同时监听同一个完全相同的 IP 和端口。

## 7. bind：绑定 IP 和端口

项目代码：

```cpp
sockaddr_in addr{};
addr.sin_family = AF_INET;
addr.sin_addr.s_addr = htonl(INADDR_ANY);
addr.sin_port = htons(static_cast<uint16_t>(port));

bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
```

`bind` 的作用：告诉内核，这个 socket 要监听哪个本地地址和端口。

几个关键点：

- `INADDR_ANY`：监听本机所有网卡地址。
- `port`：例如 `8080`。
- `htons`：host to network short，把主机字节序转成网络字节序。
- `htonl`：host to network long。

为什么需要字节序转换？

不同 CPU 对多字节整数的存储顺序可能不同。网络协议统一使用大端字节序，也叫网络字节序。

常见失败原因：

- 端口已经被占用。
- 没有权限绑定低端口，例如普通用户绑定 `80`。
- IP 地址不属于本机。

## 8. listen：进入监听状态

项目代码：

```cpp
listen(listenFd, SOMAXCONN);
```

`listen` 的作用：把 socket 从普通 socket 变成监听 socket。

调用 `listen` 后，内核会为这个监听 socket 维护连接队列。客户端完成 TCP 三次握手后，会进入已完成连接队列，等待服务端 `accept` 取走。

`SOMAXCONN` 是系统允许的较大 backlog 值。它不等于最大并发连接数，只影响连接队列长度。

## 9. accept：取出客户端连接

项目代码在 `TcpServer::acceptConnections()`：

```cpp
int clientFd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
```

`accept` 的作用：从监听 socket 的已完成连接队列中取出一个客户端连接。

非常重要：`accept` 返回的是一个新的 fd。

- `listenFd`：继续负责监听新连接。
- `clientFd`：负责和某一个客户端通信。

所以一个服务端通常只有一个 `listen fd`，但会有很多个 `client fd`。

本项目使用非阻塞监听 socket，所以 `acceptConnections()` 里要循环调用 `accept`：

```cpp
while (true) {
    int clientFd = accept(...);
    if (clientFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        ...
    }
}
```

为什么要循环？

epoll 告诉你 listen fd 可读，意思是“至少有一个连接可以 accept”。但可能一瞬间来了多个连接，所以要一直 accept，直到返回 `EAGAIN`，表示当前队列已经取空。

## 10. close：关闭 fd

项目中通过：

```cpp
::close(fd);
```

关闭 socket。

关闭 `clientFd` 的效果：

- 释放该连接在进程和内核中的资源。
- 通知对端连接关闭。
- fd 数字未来可能被内核复用。

fd 复用是一个重要概念。比如 fd `6` 关闭后，下一个新连接可能又拿到 fd `6`。所以代码中不要在关闭 fd 后继续使用旧 fd，也不要让多个线程同时操作同一个 fd。

## 11. 阻塞 I/O 是什么

默认 socket 是阻塞的。

阻塞 `accept`：

- 如果没有新连接，线程会卡在 `accept`。

阻塞 `recv`：

- 如果客户端没发数据，线程会卡在 `recv`。

阻塞 `send`：

- 如果发送缓冲区满，线程可能卡在 `send`。

最简单的服务器可以用阻塞 I/O，但并发能力差。因为一个线程如果卡在某个连接上，就不能及时处理其他连接。

## 12. 非阻塞 socket 是什么

项目代码：

```cpp
int flags = fcntl(fd, F_GETFL, 0);
fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

设置 `O_NONBLOCK` 后：

- 没有新连接时，`accept` 不阻塞，返回 `-1`，`errno=EAGAIN`。
- 没有数据可读时，`recv` 不阻塞，返回 `-1`，`errno=EAGAIN`。
- 不能立刻写入时，`send` 不阻塞，返回 `-1`，`errno=EAGAIN`。

非阻塞 I/O 的核心思路：

> 系统调用能做就做，不能做就立刻返回，让程序去处理别的事。

但这也带来一个问题：如果我们不停循环调用非阻塞 `recv`，没有数据时会一直返回 `EAGAIN`，造成 CPU 空转。

所以需要 I/O 多路复用。

## 13. I/O 多路复用是什么

I/O 多路复用解决的问题：

> 一个线程如何同时等待多个 fd 的 I/O 事件？

常见方案：

- `select`
- `poll`
- `epoll`

本项目使用 `epoll`。

你可以把 epoll 理解成：

1. 先把你关心的 fd 注册给 epoll。
2. 然后调用 `epoll_wait` 睡眠等待。
3. 有 fd 就绪时，`epoll_wait` 返回就绪事件列表。
4. 程序只处理这些真正就绪的 fd。

## 14. epoll 三个核心 API

### 14.1 epoll_create1

项目代码：

```cpp
epollFd_ = epoll_create1(0);
```

创建一个 epoll 实例，返回 epoll 自己的 fd。

### 14.2 epoll_ctl

项目代码：

```cpp
epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
```

作用：

- `EPOLL_CTL_ADD`：新增监听某个 fd。
- `EPOLL_CTL_MOD`：修改某个 fd 的监听事件。
- `EPOLL_CTL_DEL`：从 epoll 中删除某个 fd。

### 14.3 epoll_wait

项目代码：

```cpp
int n = epoll_wait(epollFd_, events.data(), kMaxEvents, timeoutMs);
```

作用：等待事件发生。

返回值：

- `n > 0`：返回 n 个就绪事件。
- `n == 0`：超时。
- `n < 0`：出错，如果 `errno == EINTR` 通常表示被信号打断，可以继续。

## 15. epoll 事件是什么意思

本项目主要使用：

- `EPOLLIN`：可读。
- `EPOLLERR`：错误。
- `EPOLLHUP`：挂起。
- `EPOLLRDHUP`：对端关闭或半关闭。
- `EPOLLONESHOT`：事件只触发一次，触发后需要重新注册。

`EPOLLIN` 对 listen fd 和 client fd 含义不同：

### listen fd 的 EPOLLIN

表示有新连接可以 `accept`。

### client fd 的 EPOLLIN

表示有数据可以 `recv`，或者对端关闭导致读返回 `0`。

所以主循环里要区分：

```cpp
if (event.fd == listenFd_) {
    acceptConnections();
} else {
    threadPool_.enqueue([this, clientFd]() { handleClient(clientFd); });
}
```

## 16. 为什么用 EPOLLONESHOT

如果不用 `EPOLLONESHOT`，可能出现这种情况：

1. client fd 可读。
2. epoll 返回事件。
3. 主线程把 fd 交给 worker A。
4. worker A 还没处理完。
5. epoll 又返回同一个 fd 的可读事件。
6. 主线程又把同一个 fd 交给 worker B。
7. A 和 B 同时 `recv/send/close` 同一个 fd。

这会导致：

- 请求数据被多个线程抢读。
- 响应混乱。
- 一个线程关闭 fd，另一个线程继续使用已关闭 fd。
- fd 被内核复用后，误操作新的连接。

本项目注册 client fd 时使用：

```cpp
EPOLLIN | EPOLLONESHOT | EPOLLRDHUP
```

含义：

> 这个 fd 的事件触发一次后自动失效，直到程序重新 `epoll_ctl MOD`。

本项目的策略是：

- 如果请求没收完整，就重新注册 EPOLLONESHOT。
- 如果请求处理完，就发送响应并关闭连接。

对应代码：

```cpp
if (!headerComplete && requestData.find("\r\n\r\n") == std::string::npos) {
    poller_.modFd(clientFd, EPOLLIN | EPOLLONESHOT | EPOLLRDHUP);
    return;
}
```

## 17. 主线程和 worker 线程怎么分工

本项目可以看作一个简化版 Reactor。

主线程负责：

- 创建 listen socket。
- 创建 epoll。
- 等待 `epoll_wait`。
- accept 新连接。
- 把 client fd 注册到 epoll。
- 把可读 client fd 派发给线程池。

worker 线程负责：

- `recv` 读取 HTTP 请求。
- 调用 `HttpParser` 解析请求。
- 读取静态文件或构造动态 JSON。
- 调用 `send` 发送响应。
- 关闭 client fd。

这种分工的好处：

- 主线程不做耗时业务逻辑，能继续响应新连接。
- worker 线程数量固定，避免无限创建线程。
- 代码结构清晰，适合面试讲解。

## 18. 线程池为什么必要

最直观的做法是：

```cpp
while (true) {
    int clientFd = accept(...);
    std::thread([clientFd]() {
        handleClient(clientFd);
    }).detach();
}
```

问题：

- 每个连接创建一个线程，开销大。
- 高并发时线程数量失控。
- 线程上下文切换增加。
- 系统资源很快耗尽。

线程池的思路：

1. 启动时创建固定数量线程。
2. 主线程只把任务放进队列。
3. worker 线程从队列取任务执行。
4. 没有任务时 worker 通过条件变量睡眠。

对应代码在 `src/thread/ThreadPool.cpp`。

关键成员：

```cpp
std::vector<std::thread> workers_;
std::queue<std::function<void()>> tasks_;
std::mutex mutex_;
std::condition_variable cv_;
bool stop_{false};
```

工作流程：

1. 构造函数创建 worker 线程。
2. `enqueue` 把任务推入队列。
3. `cv_.notify_one()` 唤醒一个 worker。
4. worker 在 `workerLoop` 中取任务。
5. 析构时设置 `stop_ = true` 并唤醒所有线程。

## 19. recv：读取请求

项目代码：

```cpp
ssize_t n = ::recv(clientFd, buffer, sizeof(buffer), 0);
```

返回值含义：

- `n > 0`：读取到 n 字节。
- `n == 0`：对端正常关闭连接。
- `n < 0` 且 `errno == EAGAIN`：当前没有更多数据。
- `n < 0` 且 `errno == EINTR`：被信号打断，可以继续。
- 其他错误：关闭连接。

TCP 是字节流协议，这点非常重要。

TCP 不保留应用层消息边界。客户端一次 `send` 的 HTTP 请求，服务端可能：

- 一次 `recv` 全部读到。
- 分多次 `recv` 才读完整。
- 一次 `recv` 读到多个请求的一部分。

本项目采用简化 HTTP 解析，只处理请求头。判断请求头完整的标志是：

```cpp
"\r\n\r\n"
```

也就是 HTTP header 后面的空行。

## 20. 请求缓冲区为什么需要

因为一次 `recv` 不一定能读完整 HTTP 请求，所以项目为每个 client fd 保存一个字符串缓冲区：

```cpp
std::unordered_map<int, std::string> clientBuffers_;
```

读到数据后追加：

```cpp
requestData.append(buffer, static_cast<size_t>(n));
```

如果还没有读到 `\r\n\r\n`，说明请求头还不完整，就重新注册 `EPOLLONESHOT`，等待下一次可读。

项目还设置了请求大小上限：

```cpp
constexpr size_t kMaxRequestSize = 8192;
```

超过上限返回 `400 Bad Request`，避免恶意客户端无限发送 header 导致内存膨胀。

## 21. send：发送响应

项目代码：

```cpp
ssize_t n = ::send(clientFd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
```

为什么要循环 `send`？

因为 `send` 不保证一次把所有数据写完。它可能只写出一部分，所以要维护 `sent`：

```cpp
size_t sent = 0;
while (sent < data.size()) {
    ssize_t n = send(...);
    if (n > 0) {
        sent += n;
    }
}
```

为什么使用 `MSG_NOSIGNAL`？

如果客户端提前断开，服务端继续 `send` 可能触发 `SIGPIPE`。默认情况下，`SIGPIPE` 可能杀死进程。`MSG_NOSIGNAL` 可以避免这次发送触发信号。

项目中也在 `main.cpp` 里忽略了 `SIGPIPE`：

```cpp
std::signal(SIGPIPE, SIG_IGN);
```

这是双保险。

## 22. HTTP 请求长什么样

一个 GET 请求大概是：

```http
GET /hello HTTP/1.1
Host: 127.0.0.1:8080
User-Agent: curl/8.0
Accept: */*

```

注意：真实 HTTP 使用 `\r\n` 换行，不是单独的 `\n`。

结构：

1. 请求行：`method path version`
2. 请求头：`Header-Name: value`
3. 空行：表示 header 结束
4. 请求体：GET 通常没有请求体，本项目不解析请求体

本项目至少解析：

- method
- path
- version
- headers

对应结构：

```cpp
struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;
};
```

## 23. HTTP 解析流程

对应代码在 `src/http/HttpParser.cpp`。

核心步骤：

### 23.1 找 header 结束位置

```cpp
size_t headerEnd = data.find("\r\n\r\n");
if (headerEnd == std::string::npos) {
    return ParseResult::Incomplete;
}
```

没找到空行，说明请求还没读完整。

### 23.2 解析请求行

```cpp
std::istringstream requestLine(line);
requestLine >> request.method >> request.path >> request.version;
```

合法请求行例子：

```http
GET /hello HTTP/1.1
```

非法请求行例子：

```http
GET
GET /hello
GET /hello HTTP/1.1 extra
```

### 23.3 校验 path 和 HTTP version

```cpp
request.path[0] == '/'
request.version == "HTTP/1.0" || request.version == "HTTP/1.1"
```

### 23.4 解析 header

每一行找冒号：

```cpp
size_t colon = line.find(':');
```

然后拆成：

- header name
- header value

header name 会转成小写，方便大小写无关查询。

## 24. HTTP 响应长什么样

一个响应大概是：

```http
HTTP/1.1 200 OK
Connection: keep-alive
Content-Length: 40
Content-Type: application/json; charset=utf-8
Keep-Alive: timeout=30
Server: TinyWebServer

{"message":"hello from tiny web server"}
```

结构：

1. 状态行：`HTTP/1.1 statusCode reason`
2. 响应头
3. 空行
4. 响应体

对应代码在 `src/http/HttpResponse.cpp`。

## 25. Content-Length 为什么重要

HTTP 响应中：

```http
Content-Length: 40
```

告诉客户端响应体有多少字节。

如果没有 `Content-Length`，客户端可能不知道响应体在哪里结束。短连接可以靠连接关闭判断结束，但长连接不会在每个响应后关闭连接，所以 `Content-Length` 对 keep-alive 尤其重要。

本项目设置 body 时自动写入：

```cpp
headers_["Content-Length"] = std::to_string(body_.size());
```

## 26. 静态文件服务

访问：

```bash
curl -i http://127.0.0.1:8080/
```

项目会把 `/` 映射到：

```text
www/index.html
```

访问：

```bash
curl -i http://127.0.0.1:8080/a.css
```

会尝试读取：

```text
www/a.css
```

对应代码：

```cpp
std::string filePath = FileUtil::buildFilePath(wwwRoot_, request.path);
FileUtil::readFile(filePath, content);
```

如果读取成功，返回 `200 OK`。

如果文件不存在，返回 `404 Not Found`。

## 27. 目录穿越是什么

假设没有防护，攻击者可能请求：

```text
/../CMakeLists.txt
```

如果服务器直接拼接路径：

```text
www/../CMakeLists.txt
```

这个路径会跳出 `www` 目录，访问项目根目录下的文件。

更隐蔽的形式：

```text
/%2e%2e/CMakeLists.txt
```

其中 `%2e` 是 URL 编码后的 `.`。

本项目的防护步骤：

1. URL 解码。
2. 拒绝控制字符和反斜杠。
3. 去掉开头的 `/`。
4. 用 `std::filesystem` 规范化路径。
5. 确认最终目标路径仍然在 `wwwRoot` 内。

如果不在根目录内，返回空路径，最终响应 `404`。

## 28. 动态接口 `/hello`

项目中写死了一个简单动态接口：

```cpp
else if (request.path == "/hello") {
    response = HttpResponse::json("{\"message\":\"hello from tiny web server\"}");
}
```

返回：

```json
{"message":"hello from tiny web server"}
```

这个接口的意义：

- 展示服务器不只能返回静态文件。
- 面试时可以引出路由、Controller、业务逻辑等概念。
- 后续可以扩展成 `/time`、`/status`、`/api/user` 等。

## 29. 错误状态码

本项目支持：

### 400 Bad Request

请求格式错误，例如：

- 请求行字段不完整。
- HTTP 版本非法。
- header 格式错误。
- 请求头超过大小限制。

### 404 Not Found

静态文件不存在，或者路径不允许访问。

### 405 Method Not Allowed

只支持 GET 和 HEAD。如果是 POST、PUT、DELETE 等，返回 405。

同时会设置：

```http
Allow: GET, HEAD
```

## 30. 日志模块

对应代码在 `src/log/Logger.cpp`。

日志记录：

- 服务器启动。
- 客户端连接。
- 请求路径。
- 响应状态码。
- 错误信息。

日志同时输出到：

- 控制台
- `logs/server.log`

当前日志是异步日志：业务线程调用 `Logger::info/warn/error` 时，只把日志追加到内存缓冲区；后台日志线程定时取出缓冲区，统一写到控制台和日志文件。

这样做的好处是：

- worker 线程不会频繁阻塞在磁盘 I/O 上。
- 多条日志可以批量写入，减少刷盘次数。
- 前台线程只需要短时间持有互斥锁，日志开销更稳定。

## 31. 项目运行时的完整调用链

以 `/hello` 为例：

```text
main()
  -> Logger::init()
  -> TcpServer server(...)
  -> server.start()
       -> SocketUtil::createListenSocket()
            -> socket()
            -> setsockopt()
            -> bind()
            -> listen()
            -> setNonBlocking()
       -> poller_.addFd(listenFd, EPOLLIN)
       -> while running
            -> poller_.wait()
            -> listen fd readable
                 -> acceptConnections()
                      -> accept()
                      -> setNonBlocking(clientFd)
                      -> poller_.addFd(clientFd, EPOLLIN | EPOLLONESHOT)
            -> client fd readable
                 -> threadPool_.enqueue(handleClient)
                      -> recv()
                      -> HttpParser::parse()
                      -> request.path == "/hello"
                      -> HttpResponse::json()
                      -> sendAll()
                      -> closeClient()
```

## 32. 如何调试这个项目

### 32.1 编译

```bash
cmake -S . -B build
cmake --build build
```

项目开启了：

```text
-Wall -Wextra -Wpedantic
```

这些编译选项会帮助你发现常见问题。

### 32.2 启动

```bash
./build/tiny_webserver 8080 www
```

或者：

```bash
cd build
./tiny_webserver 8080 ../www
```

### 32.3 curl 测试

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
curl --noproxy '*' -i http://127.0.0.1:8080/hello
curl --noproxy '*' -i http://127.0.0.1:8080/notfound.html
curl --noproxy '*' -i -X POST http://127.0.0.1:8080/hello
curl --noproxy '*' -i http://127.0.0.1:8080/%2e%2e/CMakeLists.txt
```

### 32.4 查看端口是否监听

```bash
ss -lntp | grep 8080
```

你可能看到类似：

```text
LISTEN 0 4096 0.0.0.0:8080 0.0.0.0:* users:(("tiny_webserver",pid=...,fd=...))
```

### 32.5 查看日志

```bash
tail -f logs/server.log
```

### 32.6 使用 strace 观察系统调用

```bash
strace -f -e trace=network,epoll_ctl,epoll_wait,read,write,close ./build/tiny_webserver 8080 www
```

然后另开终端 curl，你可以看到 `socket`、`bind`、`listen`、`accept`、`epoll_wait`、`recvfrom`、`sendto` 等系统调用。

## 33. 常见 bug 和排查思路

### 33.1 bind failed: Address already in use

原因：

- 端口已经被其他进程占用。
- 旧服务还没退出。

排查：

```bash
ss -lntp | grep 8080
```

换端口：

```bash
./build/tiny_webserver 9090 www
```

### 33.2 curl 连接失败

检查：

1. 服务是否启动。
2. 端口是否正确。
3. 是否在同一台机器访问。
4. 是否被代理环境变量影响。

建议本机测试时使用：

```bash
curl --noproxy '*' -i http://127.0.0.1:8080/
```

### 33.3 访问 `/` 返回 404

检查启动参数：

```bash
./build/tiny_webserver 8080 www
```

如果你在 `build` 目录下启动，则应该是：

```bash
./tiny_webserver 8080 ../www
```

### 33.4 程序卡住

可能原因：

- 阻塞调用没有处理好。
- fd 没有设置非阻塞。
- epoll 事件没有重新注册。
- worker 线程全部被耗时任务占住。

本项目已经给 listen fd 和 client fd 设置了非阻塞。

### 33.5 同一个 fd 日志里反复出现

这是正常现象。Linux 会复用已经关闭的 fd 数字。比如上一个连接 fd 是 `6`，关闭后，下一个连接可能仍然是 `6`。

## 34. 面试时怎么讲这个项目

可以按这个顺序讲：

1. 项目目标：用 C++17 实现一个轻量级 HTTP 服务器。
2. 网络层：基于 Linux Socket API，完成 `socket/bind/listen/accept/recv/send/close`。
3. I/O 模型：listen fd 和 client fd 都设置为非阻塞，主线程使用 epoll 监听事件。
4. 并发模型：主线程负责事件分发，线程池负责处理客户端请求。
5. 安全点：使用 `EPOLLONESHOT` 避免同一个 client fd 被多个线程同时处理。
6. HTTP 层：解析请求行和 header，支持 GET、静态文件、JSON 接口。
7. 工程化：CMake 构建、日志模块、测试脚本、README 文档。
8. 边界：目前不支持 HTTPS、请求体和完整 HTTP/1.1，但结构上可扩展。

## 35. 高频面试追问

### 35.1 TCP 和 HTTP 是什么关系？

TCP 是传输层协议，负责可靠字节流传输。HTTP 是应用层协议，定义请求和响应的文本格式。本项目先用 TCP 收发字节，再按 HTTP 格式解析这些字节。

### 35.2 为什么说 TCP 是字节流？

TCP 不保留消息边界。应用层一次发送的数据，接收方可能分多次收到，也可能一次收到多个应用层消息。因此 HTTP 解析必须自己判断请求是否完整。

### 35.3 epoll 的可读事件代表已经读到数据了吗？

不是。epoll 只告诉你 fd 当前“可以读”，真正读取数据还要调用 `recv`。如果对端关闭，fd 也可能表现为可读，此时 `recv` 返回 `0`。

### 35.4 为什么非阻塞 I/O 通常要配合 epoll？

非阻塞 I/O 遇到没有数据会立即返回 `EAGAIN`。如果没有 epoll，程序只能轮询所有 fd，浪费 CPU。epoll 可以让线程睡眠，等 fd 就绪后再处理。

### 35.5 为什么不用一个线程处理所有请求？

一个线程可以处理 I/O 事件，但如果解析、读取文件、业务逻辑耗时，会阻塞后续事件处理。线程池可以把处理逻辑分摊到多个 worker。

### 35.6 为什么不用每个连接一个线程？

线程创建和销毁有成本。大量连接会导致线程数量过多、内存消耗大、上下文切换频繁。线程池可以限制并发线程数。

### 35.7 EPOLLONESHOT 的缺点是什么？

每次事件触发后，如果连接还要继续监听，必须手动 `epoll_ctl MOD` 重新注册。代码复杂度略高，但能避免多线程同时处理同一个 fd。

### 35.8 当前项目是否支持长连接？

支持基础 HTTP keep-alive。HTTP/1.1 默认保持连接，除非请求头中带 `Connection: close`；HTTP/1.0 只有显式带 `Connection: keep-alive` 才保持连接。worker 处理完请求后，如果需要保持连接，会清空当前请求缓冲区并通过 `epoll_ctl MOD` 重新注册 `EPOLLONESHOT`。主线程还会定期关闭超过默认 30 秒没有活动的空闲连接。

### 35.9 如何支持 POST 请求体？

需要解析 `Content-Length`，在 header 结束后继续读取指定长度的 body。还要限制 body 大小，避免内存攻击。

### 35.10 如何支持更高并发？

可以考虑：

- 使用边缘触发 `EPOLLET`。
- 拆分 I/O 线程和业务线程。
- 使用连接状态机。
- 使用更精细的时间堆或时间轮管理空闲连接。
- 优化异步日志缓冲和刷盘策略。
- 使用零拷贝 `sendfile` 发送静态文件。
- 减少大锁粒度。

## 36. 建议学习路线

如果你想真正吃透这个项目，可以按下面顺序练习：

1. 手写一个阻塞版 echo server，只用 `socket/bind/listen/accept/recv/send`。
2. 把 echo server 改成非阻塞。
3. 加入 epoll，只处理单线程事件循环。
4. 加入线程池，把 client fd 交给 worker。
5. 把 echo 协议换成 HTTP 请求解析。
6. 加入静态文件读取。
7. 加入日志和错误处理。
8. 增加边界测试，例如 400、404、405、目录穿越。

TinyWebServer 当前代码正好是第 4 到第 8 步的综合版本。

## 37. 本项目的简化点

为了适合初学者和面试讲解，本项目刻意没有做这些复杂功能：

- HTTPS/TLS。
- chunked transfer encoding。
- 请求体解析。
- CGI/FastCGI。
- 完整 MIME 类型表。
- 高性能缓存。
- 多 Reactor。
- 高精度定时器管理。
- 压测级优化。

这不是缺点，而是边界清晰。面试时要主动说明：

> 当前项目优先展示 Linux 网络编程主流程、epoll、线程池和基础 HTTP 处理。复杂 HTTP 特性和高性能优化可以作为后续扩展。

## 38. 你应该能画出的架构图

```text
                 +-------------------+
                 |      main()       |
                 +---------+---------+
                           |
                           v
                 +-------------------+
                 |    TcpServer      |
                 | socket/bind/listen|
                 +---------+---------+
                           |
                           v
                 +-------------------+
                 |   EpollPoller     |
                 | epoll_wait events |
                 +----+----------+---+
                      |          |
        listen fd 可读|          |client fd 可读
                      v          v
              +-----------+   +----------------+
              |  accept   |   |  ThreadPool    |
              +-----+-----+   +-------+--------+
                    |                 |
                    v                 v
             注册 client fd      worker handleClient
                                      |
                                      v
                              recv HTTP request
                                      |
                                      v
                              HttpParser parse
                                      |
                                      v
                         static file / JSON / error
                                      |
                                      v
                              send HTTP response
                                      |
                                      v
                                  close fd
```

## 39. 最后用一句话总结

TinyWebServer 的核心就是：

> 主线程用非阻塞 socket 和 epoll 管理大量连接事件，用 `EPOLLONESHOT` 保证同一连接不会被多个线程同时处理，再把具体 HTTP 解析、静态文件读取和响应发送交给线程池完成。

如果你能把这句话展开讲 5 到 10 分钟，并能从源码指出每一步在哪里实现，就基本掌握了本项目蕴含的 Linux 网络编程全流程。
