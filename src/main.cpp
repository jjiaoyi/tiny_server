#include "log/Logger.h"
#include "net/TcpServer.h"

#include <csignal>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

namespace {

bool parsePort(const std::string& text, int& port) {
    try {
        size_t pos = 0;
        int value = std::stoi(text, &pos);
        if (pos != text.size() || value <= 0 || value > 65535) {
            return false;
        }

        port = value;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <port> <www_root>\n";
        return 1;
    }

    int port = 0;
    std::string wwwRoot = argv[2];

    if (!parsePort(argv[1], port)) {
        std::cerr << "Invalid port\n";
        return 1;
    }

    // 忽略 SIGPIPE：客户端提前断开时，send 不会导致进程被信号杀死。
    std::signal(SIGPIPE, SIG_IGN);

    Logger::instance().init("../logs/server.log");

    size_t threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) {
        threadCount = 4;
    }

    TcpServer server(port, wwwRoot, threadCount);
    if (!server.start()) {
        Logger::instance().error("server start failed");
        return 1;
    }

    return 0;
}
