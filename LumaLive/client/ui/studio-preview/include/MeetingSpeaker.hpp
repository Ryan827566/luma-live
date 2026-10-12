#pragma once
#include <chrono>
#include <map>
#include <set>
#include <string>

namespace luma::client::ui::preview {
// Called on the UI thread with only current, unmuted members' fresh levels.
class MeetingSpeaker {
public:
    using Clock = std::chrono::steady_clock;
    const std::string& Update(const std::set<std::string>& eligible,
                              const std::map<std::string, float>& levels,
                              Clock::time_point now) {
        if (!eligible.count(active_)) active_.clear();
        std::string candidate;
        float peak = 0.04f;
        for (const auto& [id, level] : levels) {
            if (eligible.count(id) && level > peak) { candidate = id; peak = level; }
        }
        // Silence retains the last eligible speaker; mute/leave invalidates it.
        // Continuing speech must not renew the hold and starve a new speaker.
        if (!candidate.empty() && candidate != active_ &&
            (active_.empty() || now - changed_ >= std::chrono::milliseconds(1500))) {
            active_ = candidate;
            changed_ = now;
        }
        return active_;
    }
    void Reset() { active_.clear(); changed_ = {}; }
private:
    std::string active_;
    Clock::time_point changed_{};
};
}
