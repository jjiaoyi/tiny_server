#pragma once

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Logger {
public:
    static Logger& instance();
    ~Logger();

    void init(const std::string& logFile);
    void shutdown();
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& level, const std::string& message);
    void backendLoop();
    void flushBuffer(const std::string& buffer);
    std::string nowTime() const;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::ofstream file_;
    std::thread backendThread_;
    std::string currentBuffer_;
    std::string nextBuffer_;
    std::vector<std::string> buffers_;
    bool running_{false};
};
