#include "luma/client/application/studio/StudioApplication.hpp"
#include "luma/client/ui/scene_editor/SceneEditorView.hpp"
#include "luma/client/ui/studio-shell/StudioShell.hpp"

#ifdef _WIN32
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int n) {
    const std::wstring commandLine = GetCommandLineW();

    // The new Studio Shell is the default visual client. Existing Scene Editor
    // remains available for the implementation/migration phase.
    if (commandLine.find(L"--scene-editor") != std::wstring::npos) {
        luma::client::application::studio::StudioApplication app;
        luma::client::ui::scene_editor::SceneEditorView view(app);
        if (!view.Create(h, n)) return 1;
        MSG m{};
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        return static_cast<int>(m.wParam);
    }

    return luma::client::ui::studio::RunStudioShell(h, n);
}
#else
int main() { return 0; }
#endif
