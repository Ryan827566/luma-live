#pragma once
#include "SignalingWire.hpp"
#include <cstdint>
#include <cstring>
#include <vector>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif
namespace luma::contracts::wire {
inline void put32(std::vector<std::uint8_t>& b,std::uint32_t v){v=htonl(v);auto*p=reinterpret_cast<std::uint8_t*>(&v);b.insert(b.end(),p,p+4);}
inline void put64(std::vector<std::uint8_t>& b,std::uint64_t v){put32(b,static_cast<std::uint32_t>(v>>32));put32(b,static_cast<std::uint32_t>(v));}
inline void putString(std::vector<std::uint8_t>&b,const std::string&s){put32(b,static_cast<std::uint32_t>(s.size()));b.insert(b.end(),s.begin(),s.end());}
inline bool get32(const std::vector<std::uint8_t>&b,std::size_t&o,std::uint32_t&v){if(o+4>b.size())return false;std::memcpy(&v,b.data()+o,4);o+=4;v=ntohl(v);return true;}
inline bool get64(const std::vector<std::uint8_t>&b,std::size_t&o,std::uint64_t&v){std::uint32_t a,c;if(!get32(b,o,a)||!get32(b,o,c))return false;v=(static_cast<std::uint64_t>(a)<<32)|c;return true;}
inline bool getString(const std::vector<std::uint8_t>&b,std::size_t&o,std::string&s){std::uint32_t n;if(!get32(b,o,n)||n>16*1024*1024||o+n>b.size())return false;s.assign(reinterpret_cast<const char*>(b.data()+o),n);o+=n;return true;}
inline std::vector<std::uint8_t> encode(const SignalingMessage&m){std::vector<std::uint8_t>b;b.push_back(static_cast<std::uint8_t>(m.type));put64(b,static_cast<std::uint64_t>(m.sequence));putString(b,m.room_id);putString(b,m.peer_id);putString(b,m.target_peer_id);putString(b,m.sdp);putString(b,m.candidate);putString(b,m.candidate_mid);putString(b,m.value);return b;}
inline bool decode(const std::vector<std::uint8_t>&b,SignalingMessage&m){std::size_t o=0;if(b.size()<9)return false;m.type=static_cast<SignalingMessageType>(b[o++]);std::uint64_t seq;if(!get64(b,o,seq))return false;m.sequence=static_cast<std::int64_t>(seq);return getString(b,o,m.room_id)&&getString(b,o,m.peer_id)&&getString(b,o,m.target_peer_id)&&getString(b,o,m.sdp)&&getString(b,o,m.candidate)&&getString(b,o,m.candidate_mid)&&getString(b,o,m.value)&&o==b.size();}
}
