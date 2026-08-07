#pragma once

#include "domain/approval/ApprovalService.hpp"
#include "ui/desktop/DesktopUiPort.hpp"

#include <string>
#include <vector>

namespace tggate::ui::desktop {

// This is the UI-facing model. ImGuiX views render it; they do not call TDLib.
class PendingActionsViewModel final {
public:
    explicit PendingActionsViewModel(domain::approval::ApprovalService& approvals);

    [[nodiscard]] std::vector<PendingActionRow> rows() const;
    [[nodiscard]] bool approve(std::string_view action_id);
    [[nodiscard]] bool deny(std::string_view action_id);
    void lockdown();

private:
    domain::approval::ApprovalService& approvals_;
};

} // namespace tggate::ui::desktop
