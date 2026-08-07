#include "infrastructure/http/SimpleWebHttpHost.hpp"

#include <server_http.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace tggate::infrastructure::http {
namespace {

using Server = SimpleWeb::Server<SimpleWeb::HTTP>;

bool is_loopback(const std::string_view address) {
    return address == "127.0.0.1" || address == "::1";
}

SimpleWeb::StatusCode status_code(const int status) {
    switch (status) {
    case 200: return SimpleWeb::StatusCode::success_ok;
    case 202: return SimpleWeb::StatusCode::success_accepted;
    case 400: return SimpleWeb::StatusCode::client_error_bad_request;
    case 401: return SimpleWeb::StatusCode::client_error_unauthorized;
    case 403: return SimpleWeb::StatusCode::client_error_forbidden;
    case 404: return SimpleWeb::StatusCode::client_error_not_found;
    case 405: return SimpleWeb::StatusCode::client_error_method_not_allowed;
    case 406: return SimpleWeb::StatusCode::client_error_not_acceptable;
    case 413: return SimpleWeb::StatusCode::client_error_payload_too_large;
    case 415: return SimpleWeb::StatusCode::client_error_unsupported_media_type;
    case 429: return SimpleWeb::StatusCode::client_error_too_many_requests;
    default: return SimpleWeb::StatusCode::server_error_internal_server_error;
    }
}

application::http::HttpRequest copy_request(const Server::Request& request) {
    application::http::HttpRequest result{.method = request.method, .path = request.path};
    for (const auto& [key, value] : request.header) result.headers.emplace(key, value);
    result.body = request.content.string();
    return result;
}

void write_response(const std::shared_ptr<Server::Response>& response, const application::http::HttpResponse& value) {
    SimpleWeb::CaseInsensitiveMultimap headers;
    for (const auto& [key, header_value] : value.headers) headers.emplace(key, header_value);
    response->write(status_code(value.status), value.body, headers);
}

} // namespace

class SimpleWebHttpHost::Impl final {
public:
    std::unique_ptr<Server> server;
    std::thread thread;
    std::mutex mutex;
    std::condition_variable started;
    bool startup_finished = false;
    bool startup_ok = false;
    std::string startup_error;
    std::atomic_size_t active_requests = 0;
    std::size_t maximum_connections = 1;
};

SimpleWebHttpHost::SimpleWebHttpHost() : impl_(std::make_unique<Impl>()) {}
SimpleWebHttpHost::~SimpleWebHttpHost() { stop(); }

bool SimpleWebHttpHost::start(
    const application::http::HttpHostConfig& config,
    application::http::RequestHandler handler,
    std::string& error) {
    if (!config.enabled) {
        error = "The MCP host is disabled by runtime configuration";
        return false;
    }
    if (!is_loopback(config.bind_address) || config.port == 0 || config.max_request_body_bytes == 0 || !handler) {
        error = "HTTP host requires a loopback address, port, request limit, and handler";
        return false;
    }
    stop();
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->startup_finished = false;
        impl_->startup_ok = false;
        impl_->startup_error.clear();
        impl_->server = std::make_unique<Server>();
        impl_->server->config.address = config.bind_address;
        impl_->server->config.port = config.port;
        impl_->server->config.thread_pool_size = 1;
        impl_->server->config.timeout_request = 5;
        impl_->server->config.timeout_content = 15;
        impl_->server->config.max_request_streambuf_size = config.max_request_body_bytes;
        impl_->maximum_connections = config.maximum_connections;
        impl_->active_requests = 0;
        impl_->server->default_resource["GET"] = [](const std::shared_ptr<Server::Response>& response, const std::shared_ptr<Server::Request>&) {
            write_response(response, {.status = 404});
        };
        const auto request_handler = std::make_shared<application::http::RequestHandler>(std::move(handler));
        const auto dispatch = [this, request_handler](
                                                   const std::shared_ptr<Server::Response>& response,
                                                   const std::shared_ptr<Server::Request>& request) {
            if (impl_->active_requests.fetch_add(1, std::memory_order_acq_rel) >= impl_->maximum_connections) {
                impl_->active_requests.fetch_sub(1, std::memory_order_acq_rel);
                write_response(response, {.status = 429});
                return;
            }
            try {
                write_response(response, (*request_handler)(copy_request(*request)));
            } catch (...) {
                write_response(response, {.status = 500});
            }
            impl_->active_requests.fetch_sub(1, std::memory_order_acq_rel);
        };
        impl_->server->default_resource["POST"] = dispatch;
        impl_->server->default_resource["OPTIONS"] = dispatch;
    }
    impl_->thread = std::thread([this] {
        try {
            impl_->server->start([this](const unsigned short) {
                std::scoped_lock lock(impl_->mutex);
                impl_->startup_ok = true;
                impl_->startup_finished = true;
                impl_->started.notify_all();
            });
        } catch (const std::exception& exception) {
            std::scoped_lock lock(impl_->mutex);
            impl_->startup_error = exception.what();
            impl_->startup_finished = true;
            impl_->started.notify_all();
        }
    });
    std::unique_lock lock(impl_->mutex);
    if (!impl_->started.wait_for(lock, std::chrono::seconds(5), [this] { return impl_->startup_finished; })) {
        error = "HTTP host did not start within 5 seconds";
        lock.unlock();
        stop();
        return false;
    }
    if (!impl_->startup_ok) {
        error = impl_->startup_error.empty() ? "HTTP host failed to start" : impl_->startup_error;
        lock.unlock();
        stop();
        return false;
    }
    return true;
}

void SimpleWebHttpHost::stop() noexcept {
    {
        std::scoped_lock lock(impl_->mutex);
        if (impl_->server) impl_->server->stop();
    }
    if (impl_->thread.joinable()) impl_->thread.join();
    std::scoped_lock lock(impl_->mutex);
    impl_->server.reset();
}

} // namespace tggate::infrastructure::http
