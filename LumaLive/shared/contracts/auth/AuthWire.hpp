#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace luma::contracts::auth::wire {
enum class Type:unsigned char{RegisterBegin=1,RegisterChallenge=2,RegisterFinish=3,RegisterOk=4,LoginBegin=5,LoginChallenge=6,LoginProof=7,LoginOk=8,Logout=9,LogoutOk=10,ValidateSession=11,Error=12,Ping=13,Pong=14};
struct Packet{Type type{};std::vector<std::string>fields;};
inline std::string encode(const Packet&p){std::string o=std::to_string((unsigned)p.type);o.push_back('|');for(std::size_t i=0;i<p.fields.size();++i){if(i)o.push_back('|');o+=p.fields[i];}o.push_back('\n');return o;}
inline Packet decode_line(std::string_view s){std::vector<std::string>v;std::string c;for(char x:s){if(x=='\r'||x=='\n')continue;if(x=='|'){v.push_back(std::move(c));c.clear();}else c.push_back(x);}v.push_back(std::move(c));if(v.empty()||v[0].empty())throw std::invalid_argument("invalid packet");auto n=std::stoul(v[0]);if(n>255)throw std::invalid_argument("invalid packet type");Packet p{static_cast<Type>(n),{}};v.erase(v.begin());p.fields=std::move(v);return p;}
inline bool valid_field(std::string_view s){for(char c:s)if(c=='|'||c=='\r'||c=='\n'||c=='\t')return false;return true;}
}
