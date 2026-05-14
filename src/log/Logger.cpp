#include "log/Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& logFile) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::path path(logFile);

    if (!path.parent_path().empty()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            std::cerr << "create log directory failed: " << ec.message() << std::endl;
        }
    }

    file_.open(logFile, std::ios::app);
    if (!file_.is_open()) {
        std::cerr << "open log file failed: " << logFile << std::endl;
    }
}

void Logger::info(const std::string& message) {
    log("INFO", message);
}

void Logger::warn(const std::string& message) {
    log("WARN", message);
}

void Logger::error(const std::string& message) {
    log("ERROR", message);
}

void Logger::log(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string line = "[" + nowTime() + "] [" + level + "] " + message;
    std::cout << line << std::endl;

    if (file_.is_open()) {
        file_ << line << std::endl;
    }
}

std::string Logger::nowTime() const {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm{};
    localtime_r(&time, &tm);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}
