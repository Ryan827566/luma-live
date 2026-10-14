#include "StudioPreview.hpp"
#include <filesystem>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    wchar_t path[32768]{};
    const auto length=GetEnvironmentVariableW(L"LUMALIVE_MEETING_RENDER_PATH",path,32768);
    if(!length||length>=32768||!std::filesystem::path(path).is_absolute())return 2;
    return luma::client::ui::preview::RunMeetingPreview(instance,show);
}
