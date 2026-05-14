#include "util/FileUtil.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace FileUtil {
namespace {

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string urlDecode(const std::string& text, bool& ok) {
    ok = true;
    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '%') {
            result.push_back(text[i]);
            continue;
        }

        if (i + 2 >= text.size()) {
            ok = false;
            return "";
        }

        int high = hexValue(text[i + 1]);
        int low = hexValue(text[i + 2]);
        if (high < 0 || low < 0) {
            ok = false;
            return "";
        }

        result.push_back(static_cast<char>((high << 4) | low));
        i += 2;
    }

    return result;
}

bool hasUnsafeChar(const std::string& path) {
    for (unsigned char ch : path) {
        // NUL 和其他控制字符不应该出现在 URL 路径中，避免传给文件系统后产生歧义。
        if (ch == '\0' || ch < 32 || ch == 127) {
            return true;
        }
    }
    return false;
}

bool isSubPath(const std::filesystem::path& root, const std::filesystem::path& target) {
    auto rootIt = root.begin();
    auto targetIt = target.begin();

    for (; rootIt != root.end(); ++rootIt, ++targetIt) {
        if (targetIt == target.end() || *rootIt != *targetIt) {
            return false;
        }
    }

    return true;
}

} // namespace

bool readFile(const std::string& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    std::ostringstream oss;
    oss << file.rdbuf();
    content = oss.str();
    return true;
}

std::string buildFilePath(const std::string& rootDir, const std::string& requestPath) {
    if (requestPath.empty() || requestPath[0] != '/') {
        return "";
    }

    std::string path = requestPath;
    size_t queryPos = path.find('?');
    if (queryPos != std::string::npos) {
        path = path.substr(0, queryPos);
    }

    bool decodeOk = false;
    path = urlDecode(path, decodeOk);
    if (!decodeOk || hasUnsafeChar(path) || path.find('\\') != std::string::npos) {
        return "";
    }

    if (path == "/") {
        path = "/index.html";
    } else if (!path.empty() && path.back() == '/') {
        path += "index.html";
    }

    // 防止目录穿越：先去掉 URL 开头的 /，再用 filesystem 做路径规范化。
    while (!path.empty() && path.front() == '/') {
        path.erase(path.begin());
    }

    std::filesystem::path root = std::filesystem::absolute(rootDir).lexically_normal();
    std::filesystem::path target = (root / path).lexically_normal();

    if (!isSubPath(root, target)) {
        return "";
    }

    return target.string();
}

std::string getMimeType(const std::string& path) {
    static const std::unordered_map<std::string, std::string> mimeTypes = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".json", "application/json; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".txt", "text/plain; charset=utf-8"}
    };

    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = path.substr(dot);
    auto it = mimeTypes.find(ext);
    if (it != mimeTypes.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

} // namespace FileUtil
