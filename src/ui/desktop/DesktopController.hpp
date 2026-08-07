#pragma once

#include "ui/desktop/DesktopUiPort.hpp"

#include <imguix/core.hpp>

namespace tggate::ui::desktop {

class DesktopController final : public ImGuiX::Controller {
public:
    DesktopController(ImGuiX::WindowInterface& window, DesktopUiPort& state);

    void onInit() override;
    void drawContent() override;
    void drawUi() override;

private:
    void draw_overview();
    void draw_accounts();
    void draw_pending_actions();
    void draw_server();

    DesktopUiPort& state_;
    int active_page_ = 0;
    char phone_number_[32]{};
    char api_hash_[128]{};
    char authentication_code_[32]{};
    char authentication_password_[128]{};
    std::string bearer_token_copied_client_;
    std::string pending_token_rotation_client_;
};

} // namespace tggate::ui::desktop
