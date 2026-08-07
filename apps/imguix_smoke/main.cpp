#include <imgui.h>
#include <imguix/core.hpp>
#include <imguix/themes/DarkCharcoalTheme.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

class SmokeController final : public ImGuiX::Controller {
public:
    SmokeController(ImGuiX::WindowInterface& window, const int frame_limit)
        : Controller(window), frame_limit_(frame_limit) {}

    void onInit() override {
        ImGuiX::Themes::registerDarkCharcoalTheme(themeManager());
        setTheme("dark-charcoal");
    }

    void drawContent() override {}

    void drawUi() override {
        ImGui::Begin("TgGate ImGuiX smoke test");
        ImGui::TextUnformatted("ImGuiX + ImGui-SFML + SFML are live.");
        ImGui::Text("Rendered frame: %d", frame_count_ + 1);
        ImGui::Separator();
        ImGui::TextUnformatted("Policy: default deny");
        ImGui::TextUnformatted("Approval: prepare -> approve -> execute");
        ImGui::End();

        ++frame_count_;
        if (frame_count_ == 1) {
            std::cout << "TgGate ImGuiX smoke window rendered" << std::endl;
        }
        if (frame_count_ >= frame_limit_) {
            window().close();
        }
    }

private:
    int frame_limit_ = 5;
    int frame_count_ = 0;
};

class SmokeWindow final : public ImGuiX::WindowInstance {
public:
    SmokeWindow(const int id, ImGuiX::ApplicationContext& application, std::string name, const int frame_limit)
        : WindowInstance(id, application, std::move(name)), frame_limit_(frame_limit) {}

    void onInit() override {
        createController<SmokeController>(frame_limit_);
        create(720, 360);
    }

private:
    int frame_limit_;
};

int parse_frame_limit(const int argc, char* argv[]) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--frames") {
            const auto value = std::atoi(argv[index + 1]);
            return value > 0 ? value : 5;
        }
    }
    return 5;
}

} // namespace

int main(const int argc, char* argv[]) {
    ImGuiX::Application application;
    application.createWindow<SmokeWindow>("TgGate ImGuiX smoke", parse_frame_limit(argc, argv));
    application.run();
    return 0;
}
