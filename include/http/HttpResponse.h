#pragma once

#include <map>
#include <string>

class HttpResponse {
public:
    HttpResponse(int statusCode, std::string reason);

    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body, const std::string& contentType);
    std::string toString(bool includeBody = true) const;

    int statusCode() const;

    static HttpResponse text(int code, const std::string& reason, const std::string& body);
    static HttpResponse json(const std::string& body);

private:
    int statusCode_;
    std::string reason_;
    std::map<std::string, std::string> headers_;
    std::string body_;
};
