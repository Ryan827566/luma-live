#pragma once
#include <array>
#include <string_view>
namespace luma::shared::phase { struct Phase { int number; std::string_view name; }; inline constexpr std::array<Phase,15> kPhases{{ {1,"Foundation"},{2,"Scene Editor"},{3,"Account/API/Data"},{4,"Live/Chat/Social"},{5,"WebRTC/Signaling"},{6,"SFU/Multi-person"},{7,"AI Platform"},{8,"AI Copilot"},{9,"AI Director"},{10,"AI Host/Moderation"},{11,"ASR/Subtitles/TTS"},{12,"Vision/Effects"},{13,"AI Editor/Content"},{14,"Analytics/Admin"},{15,"Performance/Security/Installer"} }}; }
