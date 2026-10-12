#include "MeetingSpeaker.hpp"
#include <iostream>
#include <stdexcept>
using namespace luma::client::ui::preview;
using namespace std::chrono_literals;
static void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
 try {
    MeetingSpeaker speaker;
    const auto start = MeetingSpeaker::Clock::time_point{};
    std::set<std::string> members{"alex", "sam"};
    Check(speaker.Update(members, {{"alex", .02f}}, start).empty(), "noise selected");
    Check(speaker.Update(members, {{"alex", .4f}}, start) == "alex", "initial speech");
    Check(speaker.Update(members, {{"sam", .8f}}, start+100ms) == "alex", "hold lost");
    Check(speaker.Update(members, {{"alex", .6f}}, start+1500ms) == "alex", "continuing speech");
    Check(speaker.Update(members, {{"sam", .8f}}, start+1533ms) == "sam", "continuing speaker renewed hold");
    Check(speaker.Update(members, {}, start+1600ms) == "sam", "silence should retain eligible speaker");
    members.erase("sam");
    Check(speaker.Update(members, {{"sam", 1.f}, {"alex", .5f}}, start+1650ms) == "alex", "muted/left speaker retained");
    members.clear();
    Check(speaker.Update(members, {{"alex", 1.f}}, start+1700ms).empty(), "empty meeting retained speaker");
    members.insert("sam");
    Check(speaker.Update(members, {{"sam", .5f}}, start+1750ms) == "sam", "rejoin suppressed by old hold");
    speaker.Reset();
    Check(speaker.Update(members, {}, start+1800ms).empty(), "reset retained speaker");
    std::cout << "PASS: speaker threshold, hold, silence, mute/leave, rejoin and reset\n";
    return 0;
 } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
