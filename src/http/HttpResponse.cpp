#include "http/HttpResponse.h"

#include <sstream>

HttpResponse::HttpResponse(int statusCode, std::string reason)
    : statusCode_(statusCode), reason_(std::move(reason)) {
    headers_["Server"] = "TinyWebServer";
    headers_["Connection"] = "close";
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::setBody(const std::string& body, const std::string& contentType) {
    body_ = body;
    headers_["Content-Type"] = contentType;
    headers_["Content-Length"] = std::to_string(body_.size());
}

std::string HttpResponse::toString() const {
    // 响应格式：状态行 + 响应头 + 空行 + 响应体。
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode_ << " " << reason_ << "\r\n";

    for (const auto& header : headers_) {
        oss << header.first << ": " << header.second << "\r\n";
    }

    oss << "\r\n";
    oss << body_;
    return oss.str();
}

int HttpResponse::statusCode() const {
    return statusCode_;
}

HttpResponse HttpResponse::text(int code, const std::string& reason, const std::string& body) {
    HttpResponse response(code, reason);
    response.setBody(body, "text/plain; charset=utf-8");
    return response;
}

HttpResponse HttpResponse::json(const std::string& body) {
    HttpResponse response(200, "OK");
    response.setBody(body, "application/json; charset=utf-8");
    return response;
}

