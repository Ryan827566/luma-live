#pragma once
#include <windows.h>
#include <memory>
namespace luma::client::ui::preview {
class AiPanel;
inline constexpr UINT RequestMeetingMedia = WM_APP + 33;
inline constexpr UINT OpenMeetingAssistant = WM_APP + 31;
inline constexpr UINT ToggleWorkspaceFullscreen = WM_APP + 32;
class HostedMeeting {
public:
    virtual ~HostedMeeting() = default;
    virtual HWND Handle() const = 0;
    virtual void SetDpi(UINT dpi) = 0;
    virtual bool CanClose() = 0;
    virtual bool Active() = 0;
    virtual AiPanel& Assistant() = 0;
};
std::unique_ptr<HostedMeeting> CreateHostedMeeting(HINSTANCE instance, HWND parent);
int RunMeetingPreview(HINSTANCE instance,int showCommand);
int RunStudioPreview(HINSTANCE instance,int showCommand);
}
