#pragma once

#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "net/EpollPoller.h"
#include "thread/ThreadPool.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

class TcpServer {
public:
    TcpServer(int port, std::string wwwRoot, size_t threadCount, bool accessLogEnabled = true);
    ~TcpServer();

    bool start();
    void stop();

private:
    struct Connection {
        std::string buffer;
        std::string peer;
        std::chrono::steady_clock::time_point lastActive;
        bool processing{false};
    };

    void acceptConnections();
    void handleClient(int clientFd);
    void closeClient(int clientFd);
    void closeIdleConnections();
    bool markProcessing(int clientFd);
    bool finishProcessing(int clientFd, bool keepAlive, bool clearBuffer);
    bool appendClientData(int clientFd, const char* data, size_t length, bool& headerComplete,
                          bool& requestTooLarge);
    bool getClientBuffer(int clientFd, std::string& buffer);
    bool sendAll(int clientFd, const std::string& data);
    bool shouldKeepAlive(const HttpRequest& request, int statusCode) const;
    HttpResponse buildResponse(const HttpRequest& request, bool isHeadRequest) const;

    int port_;
    std::string wwwRoot_;
    bool accessLogEnabled_{true};
    int listenFd_{-1};
    EpollPoller poller_;
    ThreadPool threadPool_;
    std::atomic<bool> running_{false};

    std::mutex connectionsMutex_;
    std::unordered_map<int, Connection> connections_;
    std::chrono::seconds idleTimeout_{30};
};
