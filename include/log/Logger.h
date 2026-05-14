#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger {
public:
    static Logger& instance();

    void init(const std::string& logFile);
    void info(const std::string& message);
    void warn(const std::string& message);
    void error(const std::string& message);

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void log(const std::string& level, const std::string& message);
    std::string nowTime() const;

    std::mutex mutex_;
    std::ofstream file_;
};

