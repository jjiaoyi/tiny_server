#include "log/Logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
constexpr size_t kLogBufferSize = 4 * 1024 * 1024;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const std::string& logFile) {
    shutdown();

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

    currentBuffer_.clear();
    nextBuffer_.clear();
    currentBuffer_.reserve(kLogBufferSize);
    nextBuffer_.reserve(kLogBufferSize);
    buffers_.clear();
    running_ = true;
    backendThread_ = std::thread(&Logger::backendLoop, this);
}

void Logger::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ && !backendThread_.joinable()) {
            return;
        }
        running_ = false;
        if (!currentBuffer_.empty()) {
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_.clear();
            currentBuffer_.reserve(kLogBufferSize);
        }
    }

    cv_.notify_all();

    if (backendThread_.joinable()) {
        backendThread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
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
    std::string line = "[" + nowTime() + "] [" + level + "] " + message;
    line.push_back('\n');

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            std::cout << line;
            return;
        }

        if (currentBuffer_.size() + line.size() > kLogBufferSize) {
            buffers_.push_back(std::move(currentBuffer_));
            if (!nextBuffer_.empty()) {
                currentBuffer_ = std::move(nextBuffer_);
            } else {
                currentBuffer_.clear();
                currentBuffer_.reserve(kLogBufferSize);
            }
            cv_.notify_one();
        }

        currentBuffer_.append(line);
    }
}

void Logger::backendLoop() {
    std::vector<std::string> buffersToWrite;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::seconds(2), [this]() {
                return !running_ || !buffers_.empty();
            });

            if (!currentBuffer_.empty()) {
                buffers_.push_back(std::move(currentBuffer_));
                if (!nextBuffer_.empty()) {
                    currentBuffer_ = std::move(nextBuffer_);
                } else {
                    currentBuffer_.clear();
                    currentBuffer_.reserve(kLogBufferSize);
                }
            }

            buffersToWrite.swap(buffers_);

            if (!running_ && buffersToWrite.empty()) {
                break;
            }
        }

        for (const auto& buffer : buffersToWrite) {
            flushBuffer(buffer);
        }
        buffersToWrite.clear();

        if (file_.is_open()) {
            file_.flush();
        }
    }
}

void Logger::flushBuffer(const std::string& buffer) {
    std::cout << buffer;
    if (file_.is_open()) {
        file_ << buffer;
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
