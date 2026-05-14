#include "http/HttpParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>

ParseResult HttpParser::parse(const std::string& data, HttpRequest& request) {
    // HTTP 头部以空行结束。这里只解析请求行和 header，不处理请求体。
    size_t headerEnd = data.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        return ParseResult::Incomplete;
    }

    std::string headerPart = data.substr(0, headerEnd);
    std::istringstream stream(headerPart);

    std::string line;
    if (!std::getline(stream, line)) {
        return ParseResult::BadRequest;
    }

    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }

    // 请求行格式：GET /path HTTP/1.1
    std::istringstream requestLine(line);
    std::string extra;
    if (!(requestLine >> request.method >> request.path >> request.version) || (requestLine >> extra)) {
        return ParseResult::BadRequest;
    }

    if (request.method.empty() || request.path.empty() || request.version.empty()) {
        return ParseResult::BadRequest;
    }

    if (request.path[0] != '/' ||
        (request.version != "HTTP/1.0" && request.version != "HTTP/1.1")) {
        return ParseResult::BadRequest;
    }

    request.headers.clear();
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            break;
        }

        size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0) {
            return ParseResult::BadRequest;
        }

        std::string name = toLower(trim(line.substr(0, colon)));
        std::string value = trim(line.substr(colon + 1));
        if (name.empty()) {
            return ParseResult::BadRequest;
        }

        request.headers[name] = value;
    }

    return ParseResult::Complete;
}

std::string HttpParser::trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }

    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return s.substr(begin, end - begin);
}

std::string HttpParser::toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}
