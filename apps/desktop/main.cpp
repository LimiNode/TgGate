#include "ui/desktop/DesktopController.hpp"
#include "ui/desktop/DesktopUiState.hpp"

#include <imguix/core.hpp>

#include <string>

namespace {

class DesktopWindow final : public ImGuiX::WindowInstance {
public:
    DesktopWindow(int id, ImGuiX::ApplicationContext& application, std::string name, tggate::ui::desktop::DesktopUiPort& state)
        : WindowInstance(id, application, std::move(name)), state_(state) {}

    void onInit() override {
        createController<tggate::ui::desktop::DesktopController>(state_);
        create(1280, 760);
    }

private:
    tggate::ui::desktop::DesktopUiPort& state_;
};

} // namespace

int main() {
    tggate::ui::desktop::DesktopUiState state;
    ImGuiX::Application application;
    application.createWindow<DesktopWindow>("TgGate", state);
    application.run();
}
