#pragma once

#include <string>

namespace FileUtil {

bool readFile(const std::string& path, std::string& content);
std::string buildFilePath(const std::string& rootDir, const std::string& requestPath);
std::string getMimeType(const std::string& path);

} // namespace FileUtil

