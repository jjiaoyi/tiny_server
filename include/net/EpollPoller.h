#pragma once

#include <cstdint>
#include <vector>

struct EpollEvent {
    int fd;
    uint32_t events;
};

class EpollPoller {
public:
    EpollPoller();
    ~EpollPoller();

    bool addFd(int fd, uint32_t events);
    bool modFd(int fd, uint32_t events);
    bool delFd(int fd);
    std::vector<EpollEvent> wait(int timeoutMs);

private:
    int epollFd_{-1};
};

