#include "application/AuditService.hpp"

namespace tggate::application {

void AuditService::record(AuditEntry entry) {
    std::scoped_lock lock(mutex_);
    entries_.push_back(std::move(entry));
}

std::vector<AuditEntry> AuditService::entries() const {
    std::scoped_lock lock(mutex_);
    return entries_;
}

} // namespace tggate::application
