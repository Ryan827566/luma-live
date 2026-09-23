#include "luma/client/application/studio/StudioApplication.hpp"
#include "luma/client/ui/scene_editor/SceneEditorView.hpp"
#include "StudioShell.hpp"
#include "StudioPreview.hpp"

#ifdef _WIN32
#include <windows.h>
#include <string>

int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR, int n) {
    const std::wstring commandLine = GetCommandLineW();

    // The media workspace is the default. Keep the existing editor and design
    // page catalogue accessible during incremental integration.
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

    if (commandLine.find(L"--workspace-pages") != std::wstring::npos)
        return luma::client::ui::studio::RunStudioShell(h, n);
    return luma::client::ui::preview::RunStudioPreview(h, n);
}
#else
int main() { return 0; }
#endif
