#include "net/SocketUtil.h"

#include "log/Logger.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace SocketUtil {

bool setNonBlocking(int fd) {
    // Linux 下通过 fcntl 给 fd 增加 O_NONBLOCK 标志，使 read/accept 不会阻塞线程。
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return false;
    }

    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

int createListenSocket(int port) {
    // 1. socket：创建 TCP 套接字。
    int listenFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) {
        Logger::instance().error("socket failed: " + std::string(std::strerror(errno)));
        return -1;
    }

    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    // 2. bind：把套接字绑定到本机端口。
    if (::bind(listenFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        Logger::instance().error("bind failed: " + std::string(std::strerror(errno)));
        closeFd(listenFd);
        return -1;
    }

    // 3. listen：把主动 socket 转成监听 socket，开始接收连接队列。
    if (::listen(listenFd, SOMAXCONN) < 0) {
        Logger::instance().error("listen failed: " + std::string(std::strerror(errno)));
        closeFd(listenFd);
        return -1;
    }

    if (!setNonBlocking(listenFd)) {
        Logger::instance().error("set listen fd non-blocking failed");
        closeFd(listenFd);
        return -1;
    }

    return listenFd;
}

void closeFd(int fd) {
    if (fd >= 0) {
        ::close(fd);
    }
}

std::string getPeerAddress(int fd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return "unknown";
    }

    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

} // namespace SocketUtil

