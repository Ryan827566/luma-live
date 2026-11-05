#pragma once
#include "AiTypes.hpp"
#include <memory>
namespace luma::ai::providers {
class IAiProvider { public: virtual ~IAiProvider()=default; virtual const core::ProviderConfig& Config() const=0; virtual core::AiResponse Execute(const core::AiRequest&)=0; };
using AiProviderPtr=std::shared_ptr<IAiProvider>;
}
