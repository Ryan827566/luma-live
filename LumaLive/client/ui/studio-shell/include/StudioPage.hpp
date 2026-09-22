#pragma once
#include <string>
#include <vector>
#include <windows.h>

namespace luma::client::ui::studio {

enum class StudioPage {
    Main, Compact, Scenes, Inspector, Media, Audio, WebRtc, Live,
    Recordings, Diagnostics, Copilot, Devices, Settings, Setup, Alerts
};

struct StudioPageContext {
    std::wstring project{L"E-Sports Final Live Broadcast"};
    std::wstring notice{L"Ready"};
    std::wstring selected_scene{L"Scene 1 - Host Intro"};
    std::wstring selected_asset{L"Intro_Video_4K_60.mp4"};
    std::wstring selected_peer{L"John Doe"};
    bool live{true};
    bool recording{true};
    bool broadcast_paused{false};
    bool backup_retrying{true};
    bool copilot_auto{true};
    bool snap_grid{true};
    bool safe_areas{true};
    bool center_lock{false};
    bool oauth_locked{true};
    bool audio_muted[4]{false,false,false,false};
    bool audio_solo[4]{false,false,false,false};
    int audio_gain[4]{72,58,64,48};
    int bitrate{6200};
    int dropped_frames{13};
    int rtt_ms{12};
    int jitter_ms{4};
    int replay_seconds{0};
    int setup_step{0};
    int alert_count{4};
    int selected_recording{0};
    int selected_device{0};
};

class IStudioPage {
public:
    virtual ~IStudioPage() = default;
    virtual StudioPage id() const noexcept = 0;
    virtual const wchar_t* title() const noexcept = 0;
    virtual const wchar_t* subtitle() const noexcept = 0;
    virtual void paint(HDC dc, const RECT& area, StudioPageContext& context) const = 0;
    virtual bool click(int x, int y, const RECT& area, StudioPageContext& context) const = 0;
};

}
