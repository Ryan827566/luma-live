#include "AiFeatureService.hpp"
namespace luma::ai::features {
core::AiResponse AiFeatureService::Execute(Feature f,const FeatureRequest&q){
 core::AiRequest r;r.request_id=q.request_id;r.model=q.model;r.input=q.input;if(!q.provider.empty())r.metadata["provider"]=q.provider;
 switch(f){
  case Feature::Chat:r.capability=core::Capability::Chat;r.operation="chat";r.system_prompt="You are LumaLive AI Chat.";break;
  case Feature::Director:r.capability=core::Capability::Chat;r.operation="meeting_director";r.system_prompt="Analyze meeting state and return concise host guidance.";break;
  case Feature::Host:r.capability=core::Capability::Chat;r.operation="meeting_host";r.system_prompt="Act as the LumaLive meeting host.";break;
  case Feature::Editor:r.capability=core::Capability::Chat;r.operation="editor_analysis";r.system_prompt="Analyze transcript and media markers for editing.";break;
  case Feature::Moderation:r.capability=core::Capability::Moderation;r.operation="moderation";r.system_prompt="Classify content and explain the risk category.";break;
  case Feature::Subtitles:r.capability=core::Capability::Asr;r.operation="subtitles";r.system_prompt="Produce timestamped speech transcription.";break;
  case Feature::Vision:r.capability=core::Capability::Vision;r.operation="vision";r.system_prompt="Analyze the supplied visual context.";break;
  case Feature::StudioCopilot:r.capability=core::Capability::Chat;r.operation="studio_copilot";r.system_prompt="Help operate the LumaLive Studio project.";break;
  case Feature::MeetingAssistant:r.capability=core::Capability::Chat;r.operation="meeting_assistant";r.system_prompt="Summarize discussion and extract decisions and action items.";break;
 }
 if(!gateway_){r.request_id=q.request_id;core::AiResponse o;o.error="AI gateway is unavailable";return o;}
 return gateway_->Execute(std::move(r));
}
}
