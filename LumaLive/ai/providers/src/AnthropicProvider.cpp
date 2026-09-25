#include "AnthropicProvider.hpp"

#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

namespace luma::ai::providers {
namespace {

std::string Escape(const std::string& value) {
    std::string output;
    for (const char c : value) {
        switch (c) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output += c; break;
        }
    }
    return output;
}

std::string ExtractJsonString(const std::string& body, const std::string& key) {
    const auto key_pos = body.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return {};
    const auto colon_pos = body.find(':', key_pos);
    if (colon_pos == std::string::npos) return {};
    const auto quote_pos = body.find('"', colon_pos);
    if (quote_pos == std::string::npos) return {};

    std::string output;
    bool escaped = false;
    for (std::size_t i = quote_pos + 1; i < body.size(); ++i) {
        const char c = body[i];
        if (escaped) {
            switch (c) {
            case 'n': output += '\n'; break;
            case 'r': output += '\r'; break;
            case 't': output += '\t'; break;
            default: output += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') escaped = true;
        else if (c == '"') break;
        else output += c;
    }
    return output;
}

core::ProviderConfig Make(std::string endpoint, std::string apiKeyEnv, std::string model) {
    core::ProviderConfig config;
    config.name = "anthropic";
    config.endpoint = std::move(endpoint);
    config.api_key_env = std::move(apiKeyEnv);
    if (!model.empty()) config.models.push_back(std::move(model));
    config.capabilities = {core::Capability::Chat};
    config.priority = 100;
    return config;
}

} // namespace

AnthropicProvider::AnthropicProvider(
    core::ProviderConfig config, std::shared_ptr<IAiHttpClient> client)
    : config_(std::move(config)), client_(std::move(client)) {}

core::AiResponse AnthropicProvider::Execute(const core::AiRequest& request) {
    core::AiResponse response;
    response.request_id = request.request_id;
    response.provider = config_.name;
    response.model = request.model.empty() && !config_.models.empty()
        ? config_.models.front() : request.model;

    if (!config_.enabled) {
        response.error = "provider is disabled";
        return response;
    }
    if (!client_) {
        response.error = "HTTP client is not configured";
        return response;
    }
    if (request.input.empty()) {
        response.error = "input is empty";
        return response;
    }
    if (response.model.empty()) {
        response.error = "model is not configured";
        return response;
    }
    if (config_.endpoint.empty()) {
        response.error = "provider endpoint is empty";
        return response;
    }

    std::unordered_map<std::string, std::string> headers{
        {"anthropic-version", "2023-06-01"},
        {"Content-Type", "application/json"}
    };
    if (!config_.api_key_env.empty()) {
        const char* api_key = std::getenv(config_.api_key_env.c_str());
        if (!api_key || *api_key == '\0') {
            response.error =
                "API key environment variable is missing: " + config_.api_key_env;
            return response;
        }
        headers["x-api-key"] = api_key;
    }

    std::string url = config_.endpoint;
    if (!url.empty() && url.back() != '/') url += '/';
    url += "v1/messages";

    std::string body = "{\"model\":\"" + Escape(response.model) +
                       "\",\"max_tokens\":1024";
    if (!request.system_prompt.empty()) {
        body += ",\"system\":\"" + Escape(request.system_prompt) + "\"";
    }
    body += ",\"messages\":[{\"role\":\"user\",\"content\":\"" +
            Escape(request.input) + "\"}]}";

    const auto http = client_->Post(url, headers, body);
    if (http.status < 200 || http.status >= 300) {
        response.error = http.error.empty()
            ? "HTTP " + std::to_string(http.status) : http.error;
        return response;
    }

    response.text = ExtractJsonString(http.body, "text");
    if (response.text.empty()) {
        response.error = "provider response did not contain text content";
        return response;
    }
    response.success = true;
    return response;
}

core::ProviderConfig MakeAnthropicConfig(
    std::string endpoint, std::string apiKeyEnv, std::string model) {
    return Make(std::move(endpoint), std::move(apiKeyEnv), std::move(model));
}

} // namespace luma::ai::providers
