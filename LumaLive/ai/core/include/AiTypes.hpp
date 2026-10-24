#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace luma::ai::core {
enum class Capability { Chat, Embedding, Asr, Tts, Vision, Translation, Moderation };
inline const char* ToString(Capability c) {
 switch(c){case Capability::Chat:return "chat";case Capability::Embedding:return "embedding";case Capability::Asr:return "asr";case Capability::Tts:return "tts";case Capability::Vision:return "vision";case Capability::Translation:return "translation";case Capability::Moderation:return "moderation";} return "unknown";
}
struct AiRequest { std::string request_id; Capability capability{Capability::Chat}; std::string operation; std::string model; std::string input; std::string system_prompt; std::unordered_map<std::string,std::string> metadata; };
struct AiUsage { std::int64_t input_tokens{0}, output_tokens{0}, total_tokens{0}, latency_ms{0}; };
struct AiResponse { bool success{false}; std::string request_id, provider, model, text, error; AiUsage usage{}; };
struct ProviderConfig { std::string name, endpoint, api_key_env; std::vector<std::string> models; std::vector<Capability> capabilities; bool enabled{true}; std::int32_t priority{100}; };
}
