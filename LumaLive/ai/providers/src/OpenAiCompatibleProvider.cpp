#include "OpenAiCompatibleProvider.hpp"
#include <algorithm>
#include <sstream>
namespace luma::ai::providers {
namespace {
std::string JsonEscape(const std::string& s){std::string o;for(char c:s){switch(c){case '\\':o+="\\\\";break;case '"':o+="\\\"";break;case '\n':o+="\\n";break;case '\r':o+="\\r";break;case '\t':o+="\\t";break;default:o+=c;}}return o;}
std::string ExtractJsonString(const std::string& body,const std::string& key){auto p=body.find("\"" + key + "\"");if(p==std::string::npos)return {};p=body.find(':',p);if(p==std::string::npos)return {};p=body.find('"',p);if(p==std::string::npos)return {};std::string o;bool esc=false;for(++p;p<body.size();++p){char c=body[p];if(esc){o+=c;esc=false;continue;}if(c=='\\'){esc=true;continue;}if(c=='"')break;o+=c;}return o;}
core::ProviderConfig Make(std::string name,std::string endpoint,std::string env,std::string model){core::ProviderConfig c;c.name=std::move(name);c.endpoint=std::move(endpoint);c.api_key_env=std::move(env);if(!model.empty())c.models.push_back(std::move(model));c.capabilities={core::Capability::Chat};c.priority=100;return c;}
}
OpenAiCompatibleProvider::OpenAiCompatibleProvider(core::ProviderConfig c,std::shared_ptr<IAiHttpClient> h):config_(std::move(c)),client_(std::move(h)){}
core::AiResponse OpenAiCompatibleProvider::Execute(const core::AiRequest& r){core::AiResponse o;o.request_id=r.request_id;o.provider=config_.name;o.model=r.model.empty()&&!config_.models.empty()?config_.models.front():r.model;if(!config_.enabled){o.error="provider is disabled";return o;}if(!client_){o.error="HTTP client is not configured";return o;}if(r.input.empty()){o.error="input is empty";return o;}std::string url=config_.endpoint;if(!url.empty()&&url.back()!='/')url+='/';url+="chat/completions";std::string body="{\"model\":\""+JsonEscape(o.model)+"\",\"messages\":[{\"role\":\"user\",\"content\":\""+JsonEscape(r.input)+"\"}]}";auto h=client_->Post(url,{},body);if(h.status<200||h.status>=300){o.error=h.error.empty()?"HTTP "+std::to_string(h.status):h.error;return o;}o.success=true;o.text=ExtractJsonString(h.body,"content");if(o.text.empty())o.text=h.body;return o;}
core::ProviderConfig MakeOpenAiConfig(std::string e,std::string k,std::string m){return Make("openai",std::move(e),std::move(k),std::move(m));}
core::ProviderConfig MakeDeepSeekConfig(std::string e,std::string k,std::string m){return Make("deepseek",std::move(e),std::move(k),std::move(m));}
}