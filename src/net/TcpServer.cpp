#include "net/TcpServer.h"

#include "http/HttpParser.h"
#include "http/HttpResponse.h"
#include "log/Logger.h"
#include "net/SocketUtil.h"
#include "util/FileUtil.h"

#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr size_t kMaxRequestSize = 8192;

std::string makeClientLogPrefix(int fd) {
    return "client fd=" + std::to_string(fd) + " ";
}
}

TcpServer::TcpServer(int port, std::string wwwRoot, size_t threadCount)
    : port_(port), wwwRoot_(std::move(wwwRoot)), threadPool_(threadCount) {
}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    listenFd_ = SocketUtil::createListenSocket(port_);
    if (listenFd_ < 0) {
        return false;
    }

    // listen fd 只需要关注可读事件：有新连接进入 accept 队列。
    if (!poller_.addFd(listenFd_, EPOLLIN)) {
        return false;
    }

    running_ = true;
    Logger::instance().info("server started, listen on port " + std::to_string(port_) +
                            ", www root: " + wwwRoot_);

    while (running_) {
        auto events = poller_.wait(1000);

        for (const auto& event : events) {
            if (event.fd == listenFd_) {
                acceptConnections();
                continue;
            }

            if (event.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                Logger::instance().warn(makeClientLogPrefix(event.fd) + "epoll error/hup");
                closeClient(event.fd);
                continue;
            }

            if (event.events & EPOLLIN) {
                int clientFd = event.fd;
                // EPOLLONESHOT 保证同一个 client fd 的读事件只触发一次。
                // 处理任务交给线程池，避免主线程被业务处理阻塞。
                threadPool_.enqueue([this, clientFd]() { handleClient(clientFd); });
            }
        }
    }

    return true;
}

void TcpServer::stop() {
    running_ = false;

    if (listenFd_ >= 0) {
        SocketUtil::closeFd(listenFd_);
        listenFd_ = -1;
    }
}

void TcpServer::acceptConnections() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);

        // accept 从内核已完成连接队列中取出一个客户端连接。
        int clientFd = ::accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno != EINTR) {
                Logger::instance().error("accept failed: " + std::string(std::strerror(errno)));
            }
            continue;
        }

        if (!SocketUtil::setNonBlocking(clientFd)) {
            Logger::instance().error("set client fd non-blocking failed");
            SocketUtil::closeFd(clientFd);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(buffersMutex_);
            clientBuffers_[clientFd] = "";
        }

        // client fd 注册 EPOLLIN 和 EPOLLONESHOT：
        // 可读时交给一个 worker；worker 没处理完前不会再次派发到其他线程。
        if (!poller_.addFd(clientFd, EPOLLIN | EPOLLONESHOT | EPOLLRDHUP)) {
            closeClient(clientFd);
            continue;
        }

        Logger::instance().info("client connected: fd=" + std::to_string(clientFd) +
                                ", peer=" + SocketUtil::getPeerAddress(clientFd));
    }
}

void TcpServer::handleClient(int clientFd) {
    char buffer[4096];
    bool headerComplete = false;

    while (true) {
        // recv 从非阻塞 socket 读取数据。读到 EAGAIN 表示当前没有更多数据。
        ssize_t n = ::recv(clientFd, buffer, sizeof(buffer), 0);

        if (n > 0) {
            bool requestTooLarge = false;
            {
                std::lock_guard<std::mutex> lock(buffersMutex_);
                std::string& requestData = clientBuffers_[clientFd];
                requestData.append(buffer, static_cast<size_t>(n));

                requestTooLarge = requestData.size() > kMaxRequestSize;
                headerComplete = requestData.find("\r\n\r\n") != std::string::npos;
            }

            if (requestTooLarge) {
                HttpResponse response = HttpResponse::text(400, "Bad Request", "Bad Request\n");
                sendAll(clientFd, response.toString());
                Logger::instance().warn(makeClientLogPrefix(clientFd) + "request too large, status=400");
                closeClient(clientFd);
                return;
            }

            if (headerComplete) {
                break;
            }

            continue;
        }

        if (n == 0) {
            Logger::instance().info(makeClientLogPrefix(clientFd) + "closed by peer");
            closeClient(clientFd);
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        Logger::instance().error(makeClientLogPrefix(clientFd) + "recv failed: " +
                                 std::string(std::strerror(errno)));
        closeClient(clientFd);
        return;
    }

    std::string requestData;
    {
        std::lock_guard<std::mutex> lock(buffersMutex_);
        requestData = clientBuffers_[clientFd];
    }

    if (!headerComplete && requestData.find("\r\n\r\n") == std::string::npos) {
        // 请求还没收完整，重新注册 EPOLLONESHOT，等待下一次可读事件。
        poller_.modFd(clientFd, EPOLLIN | EPOLLONESHOT | EPOLLRDHUP);
        return;
    }

    HttpRequest request;
    ParseResult parseResult = HttpParser::parse(requestData, request);

    HttpResponse response(200, "OK");

    if (parseResult == ParseResult::BadRequest || parseResult == ParseResult::Incomplete) {
        response = HttpResponse::text(400, "Bad Request", "Bad Request\n");
    } else if (request.method != "GET") {
        response = HttpResponse::text(405, "Method Not Allowed", "Method Not Allowed\n");
        response.setHeader("Allow", "GET");
    } else if (request.path == "/hello") {
        response = HttpResponse::json("{\"message\":\"hello from tiny web server\"}");
    } else {
        std::string filePath = FileUtil::buildFilePath(wwwRoot_, request.path);
        std::string content;

        if (filePath.empty() || !FileUtil::readFile(filePath, content)) {
            response = HttpResponse::text(404, "Not Found", "404 Not Found\n");
        } else {
            response = HttpResponse(200, "OK");
            response.setBody(content, FileUtil::getMimeType(filePath));
        }
    }

    std::string pathForLog = request.path.empty() ? "-" : request.path;
    Logger::instance().info(makeClientLogPrefix(clientFd) + "path=" + pathForLog +
                            ", status=" + std::to_string(response.statusCode()));

    sendAll(clientFd, response.toString());
    closeClient(clientFd);
}

void TcpServer::closeClient(int clientFd) {
    poller_.delFd(clientFd);
    SocketUtil::closeFd(clientFd);

    std::lock_guard<std::mutex> lock(buffersMutex_);
    clientBuffers_.erase(clientFd);
}

bool TcpServer::sendAll(int clientFd, const std::string& data) {
    size_t sent = 0;

    while (sent < data.size()) {
        // send 可能一次只写出部分数据，所以需要循环直到全部发送完成。
        ssize_t n = ::send(clientFd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);

        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd pfd{};
            pfd.fd = clientFd;
            pfd.events = POLLOUT;

            int ret = ::poll(&pfd, 1, 3000);
            if (ret <= 0) {
                Logger::instance().warn(makeClientLogPrefix(clientFd) + "send timeout");
                return false;
            }
            continue;
        }

        if (n < 0 && errno == EINTR) {
            continue;
        }

        Logger::instance().error(makeClientLogPrefix(clientFd) + "send failed: " +
                                 std::string(std::strerror(errno)));
        return false;
    }

    return true;
}
