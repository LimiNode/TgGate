#include "ui/desktop/PendingActionsViewModel.hpp"

namespace tggate::ui::desktop {

PendingActionsViewModel::PendingActionsViewModel(domain::approval::ApprovalService& approvals) : approvals_(approvals) {}

std::vector<PendingActionRow> PendingActionsViewModel::rows() const {
    std::vector<PendingActionRow> result;
    for (const auto& action : approvals_.pending_actions()) {
        result.push_back({
            .id = action.id,
            .client_id = action.client_id,
            .account_id = action.invocation.account_id,
            .tool_name = action.invocation.tool_name,
            .recipient = action.invocation.chat_id ? std::to_string(*action.invocation.chat_id) : "—",
            .text = action.arguments.value("text", ""),
        });
    }
    return result;
}

bool PendingActionsViewModel::approve(const std::string_view action_id) {
    return approvals_.approve(action_id).has_value();
}

bool PendingActionsViewModel::deny(const std::string_view action_id) {
    return approvals_.deny(action_id).has_value();
}

void PendingActionsViewModel::lockdown() {
    approvals_.lockdown();
}

} // namespace tggate::ui::desktop
