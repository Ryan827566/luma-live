#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
namespace luma::contracts::auth::crypto {
namespace detail {
inline std::uint32_t rotr(std::uint32_t x,std::uint32_t n){return(x>>n)|(x<<(32u-n));}
inline std::uint32_t load32(const std::uint8_t*p){return(std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|std::uint32_t(p[3]);}
inline void store32(std::uint8_t*p,std::uint32_t x){p[0]=std::uint8_t(x>>24);p[1]=std::uint8_t(x>>16);p[2]=std::uint8_t(x>>8);p[3]=std::uint8_t(x);}
inline constexpr std::array<std::uint32_t,64>K={0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,0xa2bfe8fau,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
}
inline std::array<std::uint8_t,32> sha256(std::span<const std::uint8_t>in){
 std::uint32_t h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
 auto bits=std::uint64_t(in.size())*8u; auto total=((in.size()+9u+63u)/64u)*64u; std::vector<std::uint8_t>m(total); std::copy(in.begin(),in.end(),m.begin());m[in.size()]=0x80;for(int i=0;i<8;++i)m[total-1-i]=std::uint8_t(bits>>(8*i));
 for(std::size_t o=0;o<total;o+=64){std::uint32_t w[64]{};for(int i=0;i<16;++i)w[i]=detail::load32(m.data()+o+4*i);for(int i=16;i<64;++i){auto s0=detail::rotr(w[i-15],7)^detail::rotr(w[i-15],18)^(w[i-15]>>3);auto s1=detail::rotr(w[i-2],17)^detail::rotr(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],x=h[7];for(int i=0;i<64;++i){auto S1=detail::rotr(e,6)^detail::rotr(e,11)^detail::rotr(e,25);auto ch=(e&f)^((~e)&g);auto t1=x+S1+ch+detail::K[i]+w[i];auto S0=detail::rotr(a,2)^detail::rotr(a,13)^detail::rotr(a,22);auto maj=(a&b)^(a&c)^(b&c);auto t2=S0+maj;x=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=x;}
 std::array<std::uint8_t,32>o{};for(int i=0;i<8;++i)detail::store32(o.data()+4*i,h[i]);return o;
}
inline std::array<std::uint8_t,32> hmac_sha256(std::span<const std::uint8_t>key,std::span<const std::uint8_t>msg){
 std::array<std::uint8_t,64>k{};if(key.size()>64){auto h=sha256(key);std::copy(h.begin(),h.end(),k.begin());}else std::copy(key.begin(),key.end(),k.begin());std::array<std::uint8_t,64>i{},o{};for(std::size_t n=0;n<64;++n){i[n]=std::uint8_t(k[n]^0x36);o[n]=std::uint8_t(k[n]^0x5c);}std::vector<std::uint8_t>x;i.size();x.insert(x.end(),i.begin(),i.end());x.insert(x.end(),msg.begin(),msg.end());auto ih=sha256(x);x.assign(o.begin(),o.end());x.insert(x.end(),ih.begin(),ih.end());return sha256(x);
}
inline std::array<std::uint8_t,32> pbkdf2_hmac_sha256(std::string_view pw,std::span<const std::uint8_t>salt,std::uint32_t it=120000){
 if(!it)throw std::invalid_argument("iterations must be positive");std::vector<std::uint8_t>m(salt.begin(),salt.end());m.resize(salt.size()+4);m[salt.size()+3]=1;auto*pp=reinterpret_cast<const std::uint8_t*>(pw.data());std::span<const std::uint8_t>p(pp,pw.size());auto u=hmac_sha256(p,m),t=u;for(std::uint32_t n=1;n<it;++n){u=hmac_sha256(p,u);for(std::size_t j=0;j<t.size();++j)t[j]^=u[j];}return t;
}
inline std::vector<std::uint8_t> random_bytes(std::size_t n){std::random_device rd;std::vector<std::uint8_t>o(n);for(auto&b:o)b=std::uint8_t(rd());return o;}
inline std::string hex(std::span<const std::uint8_t>b){static constexpr char d[]="0123456789abcdef";std::string o;o.reserve(b.size()*2);for(auto v:b){o.push_back(d[v>>4]);o.push_back(d[v&15]);}return o;}
inline std::vector<std::uint8_t> from_hex(std::string_view s){if(s.size()%2)throw std::invalid_argument("invalid hex length");auto n=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};std::vector<std::uint8_t>o(s.size()/2);for(std::size_t i=0;i<o.size();++i){int a=n(s[2*i]),b=n(s[2*i+1]);if(a<0||b<0)throw std::invalid_argument("invalid hex");o[i]=std::uint8_t((a<<4)|b);}return o;}
inline bool constant_time_equal(std::span<const std::uint8_t>a,std::span<const std::uint8_t>b){if(a.size()!=b.size())return false;std::uint8_t v=0;for(std::size_t i=0;i<a.size();++i)v|=std::uint8_t(a[i]^b[i]);return v==0;}
}
