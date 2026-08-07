#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace tggate::application::http {

struct HttpRequest final {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse final {
    int status = 500;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpHostConfig final {
    bool enabled = false;
    std::string bind_address = "127.0.0.1";
    unsigned short port = 0;
    std::size_t max_request_body_bytes = 1024 * 1024;
    std::size_t maximum_connections = 16;
};

using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

class IHttpHost {
public:
    virtual ~IHttpHost() = default;
    [[nodiscard]] virtual bool start(const HttpHostConfig& config, RequestHandler handler, std::string& error) = 0;
    virtual void stop() noexcept = 0;
};

} // namespace tggate::application::http
