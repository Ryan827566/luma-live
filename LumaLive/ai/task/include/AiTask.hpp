#pragma once
#include "AiTypes.hpp"
#include <cstdint>
#include <string>
namespace luma::ai::task { enum class Status{Queued,Running,Succeeded,Failed,Cancelled}; struct AiTask{std::string id;core::AiRequest request;Status status{Status::Queued};core::AiResponse response;std::int64_t created_at_ms{0};}; }
