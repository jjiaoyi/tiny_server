#pragma once

#include "net/EpollPoller.h"
#include "thread/ThreadPool.h"

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

class TcpServer {
public:
    TcpServer(int port, std::string wwwRoot, size_t threadCount);
    ~TcpServer();

    bool start();
    void stop();

private:
    void acceptConnections();
    void handleClient(int clientFd);
    void closeClient(int clientFd);
    bool sendAll(int clientFd, const std::string& data);

    int port_;
    std::string wwwRoot_;
    int listenFd_{-1};
    EpollPoller poller_;
    ThreadPool threadPool_;
    std::atomic<bool> running_{false};

    std::mutex buffersMutex_;
    std::unordered_map<int, std::string> clientBuffers_;
};

