#include "MenuBar.h"

#include <imgui.h>
#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>
#include <ship/window/Window.h>
#include <ship/window/gui/Gui.h>

#include <SDL2/SDL.h>

#include <cstdlib>

namespace ssb64 {

namespace {

// Toggle a window-visibility cvar via the Gui's window registry. Looks up
// the GuiWindow by name so the same call path the ImGui windows use to
// persist their open state stays authoritative.
void ToggleWindow(const char* name) {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (auto win = gui->GetGuiWindow(name)) {
        win->ToggleVisibility();
    }
}

bool IsWindowVisible(const char* name) {
    auto gui = Ship::Context::GetInstance()->GetWindow()->GetGui();
    if (auto win = gui->GetGuiWindow(name)) {
        return win->IsVisible();
    }
    return false;
}

} // namespace

// "gOpenMenuBar" is libultraship's CVAR_MENU_BAR_OPEN cvar name. We bind to
// it explicitly (rather than via the macro) because cvars.cmake's
// add_compile_definitions only attaches to libultraship's translation units.
MenuBar::MenuBar() : Ship::GuiMenuBar("gOpenMenuBar", false) {
}

void MenuBar::DrawElement() {
    if (!ImGui::BeginMenuBar()) {
        return;
    }

    DrawFileMenu();
    DrawViewMenu();
    DrawHelpMenu();

    ImGui::EndMenuBar();

    if (mShowAboutPopup) {
        ImGui::OpenPopup("About SSB64");
        mShowAboutPopup = false;
    }
    if (ImGui::BeginPopupModal("About SSB64", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Super Smash Bros. 64 — PC Port");
        ImGui::Separator();
        ImGui::Text("Built from the ssb-decomp-re decompilation");
        ImGui::Text("Powered by libultraship + Torch");
        ImGui::Separator();
        const auto appData =
            Ship::Context::GetInstance()->GetAppDirectoryPath();
        ImGui::Text("App data: %s", appData.c_str());
        ImGui::Separator();
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void MenuBar::DrawFileMenu() {
    if (!ImGui::BeginMenu("File")) {
        return;
    }

    if (ImGui::MenuItem("Re-extract assets...")) {
        // Delete the resolved ssb64.o2r so the next launch re-runs the
        // first-run flow. We don't touch f3d.o2r — that's shader-only and
        // ROM-independent.
        const auto path =
            Ship::Context::GetPathRelativeToAppDirectory("ssb64.o2r");
        std::error_code ec;
        std::filesystem::remove(path, ec);
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_INFORMATION, "Re-extract scheduled",
            "ssb64.o2r will be regenerated from your ROM the next time the "
            "game launches.",
            nullptr);
    }

    ImGui::Separator();

    if (ImGui::MenuItem("Quit", "Alt+F4")) {
        SDL_Event q{};
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
    }

    ImGui::EndMenu();
}

void MenuBar::DrawViewMenu() {
    if (!ImGui::BeginMenu("View")) {
        return;
    }

    struct Toggle {
        const char* label;
        const char* window;
    };
    static constexpr Toggle kToggles[] = {
        {"Stats", "Stats"},
        {"Console", "Console"},
        {"Controller Configuration", "Input Editor"},
        {"GFX Debugger", "GfxDebuggerWindow"},
    };

    for (const auto& t : kToggles) {
        bool visible = IsWindowVisible(t.window);
        if (ImGui::MenuItem(t.label, nullptr, visible)) {
            ToggleWindow(t.window);
        }
    }

    ImGui::EndMenu();
}

void MenuBar::DrawHelpMenu() {
    if (!ImGui::BeginMenu("Help")) {
        return;
    }
    if (ImGui::MenuItem("About...")) {
        mShowAboutPopup = true;
    }
    ImGui::EndMenu();
}

} // namespace ssb64
