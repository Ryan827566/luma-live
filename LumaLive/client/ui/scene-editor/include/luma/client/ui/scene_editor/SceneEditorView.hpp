#pragma once
#include "luma/client/application/studio/StudioApplication.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
namespace luma::client::ui::scene_editor {
#ifdef _WIN32
class SceneEditorView final {
    HWND hwnd_{nullptr};
    application::studio::StudioApplication& app_;
    client::domain::studio::StudioLayerId selected_;
    POINT dragStart_{};
    bool dragging_{false};
public:
    explicit SceneEditorView(application::studio::StudioApplication& app):app_(app){}
    bool Create(HINSTANCE instance, int showCommand);
    HWND Window() const noexcept { return hwnd_; }
    static LRESULT CALLBACK WndProc(HWND,UINT,WPARAM,LPARAM);
private:
    LRESULT Handle(UINT,WPARAM,LPARAM);
    void Paint(HDC);
    void HitTestAndSelect(POINT);
};
#endif
}
