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
inline std::uint32_t rotr(std::uint32_t x,std::uint32_t n){return (x>>n)|(x<<(32u-n));}
inline std::uint32_t load32(const std::uint8_t* p){return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|std::uint32_t(p[3]);}
inline void store32(std::uint8_t* p,std::uint32_t x){p[0]=std::uint8_t(x>>24);p[1]=std::uint8_t(x>>16);p[2]=std::uint8_t(x>>8);p[3]=std::uint8_t(x);}
inline constexpr std::array<std::uint32_t,64> K={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
}

inline std::array<std::uint8_t,32> sha256(std::span<const std::uint8_t> input){
    std::uint32_t h[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    const auto bit_len=std::uint64_t(input.size())*8u;
    const auto total=((input.size()+9u+63u)/64u)*64u;
    std::vector<std::uint8_t> msg(total,0);
    std::copy(input.begin(),input.end(),msg.begin());
    msg[input.size()]=0x80;
    for(int i=0;i<8;++i) msg[total-1-i]=std::uint8_t(bit_len>>(8*i));
    for(std::size_t off=0;off<total;off+=64){
        std::uint32_t w[64]{};
        for(int i=0;i<16;++i) w[i]=detail::load32(msg.data()+off+4*i);
        for(int i=16;i<64;++i){
            const auto s0=detail::rotr(w[i-15],7)^detail::rotr(w[i-15],18)^(w[i-15]>>3);
            const auto s1=detail::rotr(w[i-2],17)^detail::rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        auto a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for(int i=0;i<64;++i){
            const auto S1=detail::rotr(e,6)^detail::rotr(e,11)^detail::rotr(e,25);
            const auto ch=(e&f)^((~e)&g);
            const auto t1=hh+S1+ch+detail::K[i]+w[i];
            const auto S0=detail::rotr(a,2)^detail::rotr(a,13)^detail::rotr(a,22);
            const auto maj=(a&b)^(a&c)^(b&c);
            const auto t2=S0+maj;
            hh=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=hh;
    }
    std::array<std::uint8_t,32> out{};
    for(int i=0;i<8;++i) detail::store32(out.data()+4*i,h[i]);
    return out;
}


inline std::array<std::uint8_t,20> sha1(std::span<const std::uint8_t> input){
    auto rol=[](std::uint32_t x,std::uint32_t n){return (x<<n)|(x>>(32u-n));};
    const auto bit_len=std::uint64_t(input.size())*8u;
    const auto total=((input.size()+9u+63u)/64u)*64u;
    std::vector<std::uint8_t> msg(total,0);
    std::copy(input.begin(),input.end(),msg.begin());
    msg[input.size()]=0x80;
    for(int i=0;i<8;++i) msg[total-1-i]=std::uint8_t(bit_len>>(8*i));
    std::uint32_t h0=0x67452301u,h1=0xefcdab89u,h2=0x98badcfeu,h3=0x10325476u,h4=0xc3d2e1f0u;
    for(std::size_t off=0;off<total;off+=64){
        std::uint32_t w[80]{};
        for(int i=0;i<16;++i) w[i]=detail::load32(msg.data()+off+4*i);
        for(int i=16;i<80;++i) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        std::uint32_t a=h0,b=h1,cc=h2,d=h3,e=h4;
        for(int i=0;i<80;++i){
            std::uint32_t f=0,k=0;
            if(i<20){f=(b&cc)|((~b)&d);k=0x5a827999u;}
            else if(i<40){f=b^cc^d;k=0x6ed9eba1u;}
            else if(i<60){f=(b&cc)|(b&d)|(cc&d);k=0x8f1bbcdcu;}
            else {f=b^cc^d;k=0xca62c1d6u;}
            const auto temp=rol(a,5)+f+e+k+w[i];
            e=d;d=cc;cc=rol(b,30);b=a;a=temp;
        }
        h0+=a;h1+=b;h2+=cc;h3+=d;h4+=e;
    }
    std::array<std::uint8_t,20> out{};
    detail::store32(out.data()+0,h0);detail::store32(out.data()+4,h1);detail::store32(out.data()+8,h2);
    detail::store32(out.data()+12,h3);detail::store32(out.data()+16,h4);
    return out;
}

inline std::array<std::uint8_t,20> hmac_sha1(std::span<const std::uint8_t> key,std::span<const std::uint8_t> message){
    std::array<std::uint8_t,64> k0{};
    if(key.size()>64){const auto kh=sha1(key);std::copy(kh.begin(),kh.end(),k0.begin());}
    else std::copy(key.begin(),key.end(),k0.begin());
    std::array<std::uint8_t,64> ipad{},opad{};
    for(std::size_t i=0;i<64;++i){ipad[i]=std::uint8_t(k0[i]^0x36u);opad[i]=std::uint8_t(k0[i]^0x5cu);}
    std::vector<std::uint8_t> inner;
    inner.reserve(64+message.size());
    inner.insert(inner.end(),ipad.begin(),ipad.end());
    inner.insert(inner.end(),message.begin(),message.end());
    const auto ih=sha1(inner);
    std::vector<std::uint8_t> outer;
    outer.reserve(64+ih.size());
    outer.insert(outer.end(),opad.begin(),opad.end());
    outer.insert(outer.end(),ih.begin(),ih.end());
    return sha1(outer);
}

inline std::array<std::uint8_t,32> hmac_sha256(std::span<const std::uint8_t> key,std::span<const std::uint8_t> message){
    std::array<std::uint8_t,64> k0{};
    if(key.size()>64){const auto kh=sha256(key);std::copy(kh.begin(),kh.end(),k0.begin());}
    else std::copy(key.begin(),key.end(),k0.begin());
    std::array<std::uint8_t,64> ipad{},opad{};
    for(std::size_t i=0;i<64;++i){ipad[i]=std::uint8_t(k0[i]^0x36u);opad[i]=std::uint8_t(k0[i]^0x5cu);}
    std::vector<std::uint8_t> inner;
    inner.reserve(64+message.size());
    inner.insert(inner.end(),ipad.begin(),ipad.end());
    inner.insert(inner.end(),message.begin(),message.end());
    const auto ih=sha256(inner);
    std::vector<std::uint8_t> outer;
    outer.reserve(64+ih.size());
    outer.insert(outer.end(),opad.begin(),opad.end());
    outer.insert(outer.end(),ih.begin(),ih.end());
    return sha256(outer);
}

inline std::array<std::uint8_t,32> pbkdf2_hmac_sha256(std::string_view password,std::span<const std::uint8_t> salt,std::uint32_t iterations=120000){
    if(iterations==0) throw std::invalid_argument("iterations must be positive");
    std::vector<std::uint8_t> msg(salt.begin(),salt.end());
    msg.resize(salt.size()+4);
    msg[salt.size()+0]=0;msg[salt.size()+1]=0;msg[salt.size()+2]=0;msg[salt.size()+3]=1;
    const auto* pw=reinterpret_cast<const std::uint8_t*>(password.data());
    const std::span<const std::uint8_t> pwspan(pw,password.size());
    auto u=hmac_sha256(pwspan,msg);
    auto t=u;
    for(std::uint32_t i=1;i<iterations;++i){u=hmac_sha256(pwspan,u);for(std::size_t j=0;j<t.size();++j)t[j]^=u[j];}
    return t;
}


inline std::string base32_encode(std::span<const std::uint8_t> bytes){
    static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    out.reserve((bytes.size()*8+4)/5);
    std::uint32_t buffer=0;int bits=0;
    for(const auto b:bytes){
        buffer=(buffer<<8)|b;bits+=8;
        while(bits>=5){bits-=5;out.push_back(alphabet[(buffer>>bits)&31u]);}
    }
    if(bits>0)out.push_back(alphabet[(buffer<<(5-bits))&31u]);
    return out;
}

inline std::vector<std::uint8_t> base32_decode(std::string_view text){
    auto value=[](char c)->int{
        if(c>='A'&&c<='Z')return c-'A';
        if(c>='a'&&c<='z')return c-'a';
        if(c>='2'&&c<='7')return c-'2'+26;
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buffer=0;int bits=0;
    for(char c:text){
        if(c==' '||c=='-'||c=='=')continue;
        const int v=value(c);if(v<0)throw std::invalid_argument("invalid base32");
        buffer=(buffer<<5)|std::uint32_t(v);bits+=5;
        if(bits>=8){bits-=8;out.push_back(std::uint8_t((buffer>>bits)&0xffu));}
    }
    return out;
}

inline std::string uri_component(std::string_view value){
    static constexpr char digits[]="0123456789ABCDEF";
    std::string out;
    for(unsigned char c:value){
        if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out.push_back(char(c));
        else {out.push_back('%');out.push_back(digits[c>>4]);out.push_back(digits[c&0xf]);}
    }
    return out;
}

inline std::string make_totp_code(std::string_view secret_base32,std::int64_t epoch_seconds,std::uint32_t period=30,std::uint32_t digits=6){
    if(period==0||digits<6||digits>8)throw std::invalid_argument("invalid TOTP parameters");
    const auto secret=base32_decode(secret_base32);
    const auto counter=std::uint64_t(epoch_seconds/std::int64_t(period));
    std::array<std::uint8_t,8> message{};
    for(int i=0;i<8;++i)message[7-i]=std::uint8_t(counter>>(8*i));
    const auto digest=hmac_sha1(secret,message);
    const auto offset=digest[19]&0x0fu;
    const std::uint32_t binary=(std::uint32_t(digest[offset])&0x7fu)<<24 |
        std::uint32_t(digest[offset+1])<<16 | std::uint32_t(digest[offset+2])<<8 | std::uint32_t(digest[offset+3]);
    std::uint32_t mod=1;for(std::uint32_t i=0;i<digits;++i)mod*=10u;
    std::ostringstream out;out<<std::setw(int(digits))<<std::setfill('0')<<(binary%mod);
    return out.str();
}

inline bool verify_totp(std::string_view secret_base32,std::string_view code,std::int64_t epoch_seconds,std::int32_t window=1){
    if(code.size()!=6&&code.size()!=7&&code.size()!=8)return false;
    for(char c:code)if(c<'0'||c>'9')return false;
    for(std::int32_t delta=-window;delta<=window;++delta){
        const auto t=epoch_seconds+std::int64_t(delta)*30;
        if(make_totp_code(secret_base32,t,30,static_cast<std::uint32_t>(code.size()))==code)return true;
    }
    return false;
}

inline std::string make_otpauth_uri(std::string_view secret_base32,std::string_view account,std::string_view issuer="LumaLive"){
    return "otpauth://totp/"+uri_component(issuer)+":"+uri_component(account)+
        "?secret="+std::string(secret_base32)+"&issuer="+uri_component(issuer)+
        "&algorithm=SHA1&digits=6&period=30";
}
inline std::vector<std::uint8_t> random_bytes(std::size_t n){
    std::random_device rd;std::vector<std::uint8_t> out(n);for(auto&b:out)b=std::uint8_t(rd());return out;
}
inline std::string hex(std::span<const std::uint8_t> bytes){
    static constexpr char digits[]="0123456789abcdef";std::string out;out.reserve(bytes.size()*2);
    for(auto b:bytes){out.push_back(digits[b>>4]);out.push_back(digits[b&0x0f]);}return out;
}
inline std::vector<std::uint8_t> from_hex(std::string_view s){
    if(s.size()%2)throw std::invalid_argument("invalid hex length");
    auto nib=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};
    std::vector<std::uint8_t> out(s.size()/2);
    for(std::size_t i=0;i<out.size();++i){int hi=nib(s[2*i]),lo=nib(s[2*i+1]);if(hi<0||lo<0)throw std::invalid_argument("invalid hex");out[i]=std::uint8_t((hi<<4)|lo);}return out;
}
inline bool constant_time_equal(std::span<const std::uint8_t> a,std::span<const std::uint8_t> b){
    if(a.size()!=b.size()) return false;
    std::uint8_t v=0;
    for(std::size_t i=0;i<a.size();++i) v|=std::uint8_t(a[i]^b[i]);
    return v==0;
}
}
