#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::map<std::string, std::string> headers;

    std::string getHeader(const std::string& name) const {
        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        auto it = headers.find(key);
        return it == headers.end() ? "" : it->second;
    }
};
