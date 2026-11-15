#pragma once

#include <string>
#include <vector>

struct HttpResponse {
    long status = 0;
    std::string body;
};

// method: "GET" 或 "POST"；POST 时自动加 form-urlencoded Content-Type
HttpResponse HttpRequest(const std::string& url,
                         const std::string& method,
                         const std::vector<std::string>& extraHeaders,
                         const std::string& postBody = "");