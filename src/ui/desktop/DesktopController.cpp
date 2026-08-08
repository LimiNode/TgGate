#include "ui/desktop/DesktopController.hpp"

#include <imguix/themes/DarkCharcoalTheme.hpp>

#include <algorithm>
#include <iterator>

namespace tggate::ui::desktop {

DesktopController::DesktopController(ImGuiX::WindowInterface& window, DesktopUiPort& state)
    : Controller(window), state_(state) {}

void DesktopController::onInit() {
    ImGuiX::Themes::registerDarkCharcoalTheme(themeManager());
    setTheme("dark-charcoal");
}

void DesktopController::drawContent() {}

void DesktopController::drawUi() {
    constexpr const char* pages[] = {"Overview", "Accounts", "MCP Clients", "Permissions", "Pending Actions", "Audit", "Server", "Security"};
    ImGui::Begin("TgGate — Local Telegram Agent Gateway");
    ImGui::BeginChild("navigation", ImVec2(160.0f, 0.0f), true);
    for (int index = 0; index < static_cast<int>(std::size(pages)); ++index) {
        if (ImGui::Selectable(pages[index], active_page_ == index)) active_page_ = index;
    }
    ImGui::Separator();
    if (ImGui::Button("LOCKDOWN", ImVec2(-1.0f, 0.0f))) state_.lockdown();
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), true);

    if (active_page_ == 0) draw_overview();
    else if (active_page_ == 1) draw_accounts();
    else if (active_page_ == 4) draw_pending_actions();
    else if (active_page_ == 6) draw_server();
    else {
        ImGui::Text("%s", pages[active_page_]);
        ImGui::Separator();
        ImGui::TextUnformatted("This application section is wired through application services and will be filled by the next integration slice.");
    }
    ImGui::EndChild();
    ImGui::End();
}

void DesktopController::draw_overview() {
    ImGui::TextUnformatted("Secure local Telegram gateway");
    ImGui::Separator();
    ImGui::Text("MCP: %s", state_.server_status().c_str());
    ImGui::Text("Telegram: %s", state_.telegram_status().c_str());
    ImGui::Text("Configured MCP clients: %zu", state_.configured_client_count());
    ImGui::TextWrapped("Configuration: %s", state_.configuration_status().c_str());
    ImGui::Spacing();
    ImGui::TextUnformatted("Invariant: MCP never calls TDLib directly. Writes always require prepare → approve → execute.");
}

void DesktopController::draw_accounts() {
    ImGui::TextUnformatted("Telegram accounts");
    ImGui::Separator();
    const auto accounts = state_.accounts();
    if (accounts.empty()) {
        ImGui::TextUnformatted("No account definitions. Edit data/config/accounts.json.");
        return;
    }
    for (const auto& account : accounts) {
        ImGui::PushID(account.id.c_str());
        ImGui::Text("%s (%s)", account.display_name.c_str(), account.id.c_str());
        ImGui::Text("State: %s", account.authorization_status.c_str());
        if (!account.has_api_credentials) {
            ImGui::TextUnformatted("Set api_id in the local account configuration and enter the API hash below.");
        }
        ImGui::InputText("Phone number", phone_number_, sizeof(phone_number_));
        ImGui::InputText("API hash (memory only)", api_hash_, sizeof(api_hash_), ImGuiInputTextFlags_Password);
        if (ImGui::Button("Start authorization")) {
            state_.start_authorization(account.id, phone_number_, api_hash_);
            std::fill(std::begin(api_hash_), std::end(api_hash_), '\0');
        }
        if (account.authorization_status == "Waiting for authentication code") {
            ImGui::InputText("Authentication code", authentication_code_, sizeof(authentication_code_));
            ImGui::SameLine();
            if (ImGui::Button("Submit code")) {
                state_.submit_authentication_code(authentication_code_);
                std::fill(std::begin(authentication_code_), std::end(authentication_code_), '\0');
            }
        } else if (account.authorization_status == "Waiting for 2FA password") {
            ImGui::InputText("2FA password", authentication_password_, sizeof(authentication_password_), ImGuiInputTextFlags_Password);
            ImGui::SameLine();
            if (ImGui::Button("Submit 2FA")) {
                state_.submit_authentication_password(authentication_password_);
                std::fill(std::begin(authentication_password_), std::end(authentication_password_), '\0');
            }
        } else if (account.authorization_status == "Registration is required") {
            ImGui::TextUnformatted("Telegram requires registration. Complete it in an official Telegram client, then restart authorization.");
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    const auto error = state_.authorization_error();
    if (!error.empty()) ImGui::TextWrapped("Authorization: %s", error.c_str());
}

void DesktopController::draw_pending_actions() {
    ImGui::TextUnformatted("Pending local approvals");
    ImGui::Separator();
    const auto actions = state_.pending_actions();
    if (actions.empty()) {
        ImGui::TextUnformatted("No actions await approval.");
        return;
    }
    for (const auto& action : actions) {
        ImGui::PushID(action.id.c_str());
        ImGui::Text("Client: %s", action.client_id.c_str());
        ImGui::Text("Account: %s   Recipient: %s", action.account_id.c_str(), action.recipient.c_str());
        ImGui::TextWrapped("%s", action.text.c_str());
        if (ImGui::Button("Approve once")) state_.approve(action.id);
        ImGui::SameLine();
        if (ImGui::Button("Deny")) state_.deny(action.id);
        ImGui::Separator();
        ImGui::PopID();
    }
}

void DesktopController::draw_server() {
    ImGui::TextUnformatted("Local MCP HTTP host");
    ImGui::Separator();
    ImGui::TextWrapped("%s", state_.server_status().c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("Each MCP profile has its own DPAPI-protected bearer token. Copy it only into the matching trusted local MCP client.");
    ImGui::TextUnformatted("Treat every bearer token as a password: copying places it in the Windows clipboard.");
    const auto clients = state_.mcp_client_credentials();
    if (clients.empty()) {
        ImGui::TextUnformatted("No MCP client profiles are configured.");
        return;
    }
    for (const auto& client : clients) {
        ImGui::PushID(client.id.c_str());
        ImGui::Text("%s (%s) — %s", client.display_name.c_str(), client.id.c_str(), client.enabled ? "enabled" : "disabled");
        if (ImGui::Button("Copy bearer token")) {
            auto token = state_.mcp_bearer_token_for_export(client.id);
            if (token) {
                ImGui::SetClipboardText(token->c_str());
                std::fill(token->begin(), token->end(), '\0');
                token->clear();
                bearer_token_copied_client_ = client.id;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Regenerate token")) pending_token_rotation_client_ = client.id;
        if (pending_token_rotation_client_ == client.id) {
            ImGui::TextWrapped("This immediately invalidates this client's current token. Copy the new token after rotation.");
            if (ImGui::Button("Confirm rotation")) {
                if (state_.regenerate_mcp_bearer_token(client.id)) bearer_token_copied_client_.clear();
                pending_token_rotation_client_.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) pending_token_rotation_client_.clear();
        }
        if (bearer_token_copied_client_ == client.id) ImGui::TextUnformatted("Bearer token copied to the clipboard.");
        ImGui::Separator();
        ImGui::PopID();
    }
}

} // namespace tggate::ui::desktop
