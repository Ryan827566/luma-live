#pragma once
#include <string>
#include <windows.h>

namespace luma::client::ui::studio {

enum class StudioPage { Main, Scenes, Media, Audio, WebRtc, Live, Recordings, Diagnostics, Copilot, Devices, Settings, Setup };

struct StudioPageContext {
    std::wstring project{L"E-Sports Final Live Broadcast"};
    std::wstring status{L"STREAM: ONLINE"};
    double cpu{24.1};
    double gpu{48.3};
    double memory{4.1};
    double render_ms{2.1};
    int bitrate{6200};
    int rtt_ms{15};
};

class IStudioPage {
public:
    virtual ~IStudioPage() = default;
    virtual StudioPage id() const noexcept = 0;
    virtual const wchar_t* title() const noexcept = 0;
    virtual const wchar_t* subtitle() const noexcept = 0;
    virtual void paint(HDC dc, const RECT& area, const StudioPageContext& context) const = 0;
};

}
