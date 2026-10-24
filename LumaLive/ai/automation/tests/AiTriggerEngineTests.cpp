#include <cassert>
#include "AiTriggerEngine.hpp"
using namespace luma::ai::automation;
int main() {
    AiTriggerEngine engine;
    WorkflowDefinition meeting;
    meeting.id = "meeting-follow-up";
    meeting.trigger = {TriggerType::Event, "meeting_ended", {{"room", "room-1"}}};
    WorkflowDefinition all_meetings;
    all_meetings.id = "meeting-summary";
    all_meetings.trigger = {TriggerType::Event, "meeting_ended", {}};
    WorkflowDefinition live;
    live.id = "live-alert";
    live.trigger = {TriggerType::Event, "live_started", {}};
    const AutomationEvent event{"meeting_ended", {{"room", "room-1"}, {"host", "alice"}}};
    const auto matches = engine.Match(event, {meeting, all_meetings, live});
    assert(matches.size() == 2 && matches[0] == "meeting-follow-up" && matches[1] == "meeting-summary");
    const AutomationEvent other_room{"meeting_ended", {{"room", "room-2"}}};
    const auto other_matches = engine.Match(other_room, {meeting, all_meetings, live});
    assert(other_matches.size() == 1 && other_matches[0] == "meeting-summary");
    assert(engine.Match({"", {}}, {meeting}).empty());
    WorkflowDefinition manual;
    manual.id = "manual";
    manual.trigger = {TriggerType::Manual, "meeting_ended", {}};
    assert(engine.Match(event, {manual}).empty());
    return 0;
}
