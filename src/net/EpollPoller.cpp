#include "net/EpollPoller.h"

#include "log/Logger.h"

#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

EpollPoller::EpollPoller() {
    // epoll_create1 创建 epoll 实例，后续通过 epoll_ctl 注册 fd，通过 epoll_wait 等待事件。
    epollFd_ = epoll_create1(0);
    if (epollFd_ < 0) {
        Logger::instance().error("epoll_create1 failed: " + std::string(std::strerror(errno)));
    }
}

EpollPoller::~EpollPoller() {
    if (epollFd_ >= 0) {
        ::close(epollFd_);
    }
}

bool EpollPoller::addFd(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        Logger::instance().error("epoll add fd failed: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool EpollPoller::modFd(int fd, uint32_t events) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
        Logger::instance().error("epoll mod fd failed: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

bool EpollPoller::delFd(int fd) {
    if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        Logger::instance().warn("epoll del fd failed: " + std::string(std::strerror(errno)));
        return false;
    }

    return true;
}

std::vector<EpollEvent> EpollPoller::wait(int timeoutMs) {
    constexpr int kMaxEvents = 1024;
    std::vector<epoll_event> events(kMaxEvents);

    int n = epoll_wait(epollFd_, events.data(), kMaxEvents, timeoutMs);
    std::vector<EpollEvent> result;

    if (n < 0) {
        if (errno != EINTR) {
            Logger::instance().error("epoll_wait failed: " + std::string(std::strerror(errno)));
        }
        return result;
    }

    result.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        result.push_back({events[i].data.fd, events[i].events});
    }

    return result;
}

