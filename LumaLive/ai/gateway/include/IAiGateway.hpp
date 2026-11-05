#pragma once
#include "AiTypes.hpp"
#include "ProviderRegistry.hpp"
#include <memory>
#include <vector>
namespace luma::ai::gateway { class IAiGateway{public:virtual~IAiGateway()=default;virtual bool Start()=0;virtual void Stop()=0;virtual core::AiResponse Execute(core::AiRequest)=0;virtual bool RegisterProvider(providers::AiProviderPtr)=0;virtual std::vector<core::ProviderConfig>Providers()const=0;}; std::shared_ptr<IAiGateway>CreateGateway(); }
