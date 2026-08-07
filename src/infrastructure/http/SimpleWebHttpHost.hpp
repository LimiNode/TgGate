#pragma once

#include "application/http/IHttpHost.hpp"

#include <memory>

namespace tggate::infrastructure::http {

class SimpleWebHttpHost final : public application::http::IHttpHost {
public:
    SimpleWebHttpHost();
    ~SimpleWebHttpHost() override;
    SimpleWebHttpHost(const SimpleWebHttpHost&) = delete;
    SimpleWebHttpHost& operator=(const SimpleWebHttpHost&) = delete;

    [[nodiscard]] bool start(
        const application::http::HttpHostConfig& config,
        application::http::RequestHandler handler,
        std::string& error) override;
    void stop() noexcept override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tggate::infrastructure::http
