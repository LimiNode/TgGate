#pragma once

#include "domain/policy/Policy.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tggate::application {

struct AuditEntry {
    std::chrono::system_clock::time_point timestamp;
    std::string client_id;
    std::string account_id;
    std::string tool_name;
    std::optional<std::int64_t> chat_id;
    domain::policy::PolicyEffect decision;
    std::string result_status;
    std::optional<std::string> action_id;
    // Deliberately no message text or arguments.
};

class AuditService final {
public:
    void record(AuditEntry entry);
    [[nodiscard]] std::vector<AuditEntry> entries() const;

private:
    mutable std::mutex mutex_;
    std::vector<AuditEntry> entries_;
};

} // namespace tggate::application
