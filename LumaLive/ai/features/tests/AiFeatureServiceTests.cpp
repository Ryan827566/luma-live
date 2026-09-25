#include "AiFeatureService.hpp"
#include "ProviderRegistry.hpp"
#include <cassert>
int main(){using namespace luma::ai;auto g=gateway::CreateGateway();g->Start();core::ProviderConfig c;c.name="feature-test";c.capabilities={core::Capability::Chat,core::Capability::Asr,core::Capability::Vision,core::Capability::Moderation};g->RegisterProvider(std::make_shared<providers::DeterministicProvider>(c));features::AiFeatureService s(g);features::FeatureRequest q;q.request_id="f1";q.input="meeting transcript";auto a=s.Execute(features::Feature::MeetingAssistant,q);auto b=s.Execute(features::Feature::Vision,q);assert(a.success&&b.success);assert(a.text.find("meeting transcript")!=std::string::npos);return 0;}
