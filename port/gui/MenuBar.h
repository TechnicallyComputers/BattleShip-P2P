#pragma once

#include <ship/window/gui/GuiMenuBar.h>
#include <string>

namespace ssb64 {

// Top-of-screen ImGui menu bar. Toggle visibility with F1 (LUS default).
//
// Layout mirrors Ship of Harkinian's bar:
//   File ─ Open ROM... / Re-extract assets / Quit
//   View ─ Stats / Console / Input Editor / GFX Debugger
//   Help ─ About
class MenuBar : public Ship::GuiMenuBar {
  public:
    MenuBar();
    ~MenuBar() override = default;

  protected:
    void InitElement() override {}
    void DrawElement() override;
    void UpdateElement() override {}

  private:
    void DrawFileMenu();
    void DrawViewMenu();
    void DrawHelpMenu();

    bool mShowAboutPopup = false;
};

} // namespace ssb64
