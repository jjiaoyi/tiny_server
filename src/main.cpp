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

bool parsePositiveSize(const std::string& text, size_t& value) {
    try {
        size_t pos = 0;
        unsigned long parsed = std::stoul(text, &pos);
        if (pos != text.size() || parsed == 0) {
            return false;
        }

        value = static_cast<size_t>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseBoolFlag(const std::string& text, bool& value) {
    if (text == "1" || text == "true" || text == "on") {
        value = true;
        return true;
    }

    if (text == "0" || text == "false" || text == "off") {
        value = false;
        return true;
    }

    return false;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <port> <www_root> [thread_count] [log_file] [access_log:0|1]\n";
        return 1;
    }

    int port = 0;
    std::string wwwRoot = argv[2];
    std::string logFile = "logs/server.log";
    bool accessLogEnabled = true;

    if (!parsePort(argv[1], port)) {
        std::cerr << "Invalid port\n";
        return 1;
    }

    // 忽略 SIGPIPE：客户端提前断开时，send 不会导致进程被信号杀死。
    std::signal(SIGPIPE, SIG_IGN);

    size_t threadCount = std::thread::hardware_concurrency();
    if (threadCount == 0) {
        threadCount = 4;
    }

    if (argc >= 4 && !parsePositiveSize(argv[3], threadCount)) {
        std::cerr << "Invalid thread_count\n";
        return 1;
    }

    if (argc >= 5) {
        logFile = argv[4];
    }

    if (argc >= 6 && !parseBoolFlag(argv[5], accessLogEnabled)) {
        std::cerr << "Invalid access_log flag, use 0/1, true/false, or on/off\n";
        return 1;
    }

    Logger::instance().init(logFile);

    TcpServer server(port, wwwRoot, threadCount, accessLogEnabled);
    if (!server.start()) {
        Logger::instance().error("server start failed");
        return 1;
    }

    return 0;
}
