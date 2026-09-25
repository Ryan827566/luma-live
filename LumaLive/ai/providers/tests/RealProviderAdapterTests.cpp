#include "OpenAiCompatibleProvider.hpp"
#include "AnthropicProvider.hpp"

#include <cassert>
#include <cstdlib>
#include <string>
#include <unordered_map>

using namespace luma::ai;

namespace {

#ifdef _WIN32
void SetEnv(const char* name, const char* value) { _putenv_s(name, value); }
void ClearEnv(const char* name) { _putenv_s(name, ""); }
#else
void SetEnv(const char* name, const char* value) { setenv(name, value, 1); }
void ClearEnv(const char* name) { unsetenv(name); }
#endif

class FakeHttp final : public providers::IAiHttpClient {
public:
    std::string url;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    providers::HttpResponse response{
        200,
        R"({"choices":[{"message":{"content":"ok"}}])",
        ""
    };

    providers::HttpResponse Post(
        const std::string& u,
        const std::unordered_map<std::string, std::string>& h,
        const std::string& b) override {
        url = u;
        headers = h;
        body = b;
        return response;
    }
};

} // namespace

int main() {
    auto http = std::make_shared<FakeHttp>();
    SetEnv("OPENAI_API_KEY_TEST", "openai-secret");

    const auto openai_config =
        providers::MakeOpenAiConfig(
            "https://api.openai.com/v1", "OPENAI_API_KEY_TEST", "gpt-test");
    providers::OpenAiCompatibleProvider openai(openai_config, http);

    core::AiRequest request;
    request.request_id = "1";
    request.input = "hello";
    request.system_prompt = "be concise";

    const auto openai_response = openai.Execute(request);
    assert(openai_response.success);
    assert(openai_response.text == "ok");
    assert(http->url == "https://api.openai.com/v1/chat/completions");
    assert(http->headers.at("Authorization") == "Bearer openai-secret");
    assert(http->headers.at("Content-Type") == "application/json");
    assert(http->body.find("\"system\"") != std::string::npos);

    SetEnv("ANTHROPIC_API_KEY_TEST", "anthropic-secret");
    http->response = {
        200, R"({"content":[{"type":"text","text":"hello from anthropic"}]})", ""
    };

    const auto anthropic_config =
        providers::MakeAnthropicConfig(
            "https://api.anthropic.com", "ANTHROPIC_API_KEY_TEST", "claude-test");
    providers::AnthropicProvider anthropic(anthropic_config, http);

    const auto anthropic_response = anthropic.Execute(request);
    assert(anthropic_response.success);
    assert(anthropic_response.text == "hello from anthropic");
    assert(http->url == "https://api.anthropic.com/v1/messages");
    assert(http->headers.at("x-api-key") == "anthropic-secret");
    assert(http->headers.at("anthropic-version") == "2023-06-01");
    assert(http->headers.at("Content-Type") == "application/json");
    assert(http->body.find("\"system\":\"be concise\"") != std::string::npos);

    ClearEnv("OPENAI_API_KEY_TEST");
    ClearEnv("ANTHROPIC_API_KEY_TEST");
    return 0;
}
