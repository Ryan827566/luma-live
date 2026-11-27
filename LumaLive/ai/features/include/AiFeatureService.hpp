#pragma once
#include "IAiGateway.hpp"
#include <memory>
namespace luma::ai::features {
enum class Feature { Chat, Director, Host, Editor, Moderation, Subtitles, Vision, StudioCopilot, MeetingAssistant };
struct FeatureRequest { std::string request_id; std::string input; std::string model; std::string provider; };
class AiFeatureService {
public:
 explicit AiFeatureService(std::shared_ptr<gateway::IAiGateway> gateway):gateway_(std::move(gateway)){}
 core::AiResponse Execute(Feature, const FeatureRequest&);
private: std::shared_ptr<gateway::IAiGateway> gateway_;
};
}
