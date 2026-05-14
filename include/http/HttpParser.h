#pragma once

#include "http/HttpRequest.h"

#include <string>

enum class ParseResult {
    Complete,
    Incomplete,
    BadRequest
};

class HttpParser {
public:
    static ParseResult parse(const std::string& data, HttpRequest& request);

private:
    static std::string trim(const std::string& s);
    static std::string toLower(const std::string& s);
};

