#pragma once
#include <string>
#include <unordered_map>
namespace luma::ai::providers {
struct HttpResponse { int status{0}; std::string body; std::string error; };
class IAiHttpClient {
public:
 virtual ~IAiHttpClient()=default;
 virtual HttpResponse Post(const std::string& url,const std::unordered_map<std::string,std::string>& headers,const std::string& body)=0;
};
}