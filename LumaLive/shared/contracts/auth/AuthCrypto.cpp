#include "contracts/auth/AuthCrypto.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#include <bcrypt.h>
#pragma comment(lib,"bcrypt.lib")
#else
#include <cerrno>
#include <sys/random.h>
#endif

namespace luma::contracts::auth::crypto {

std::vector<std::uint8_t> random_bytes(std::size_t n){
    std::vector<std::uint8_t> out(n);
    std::size_t offset=0;
    while(offset<n){
        const std::size_t chunk=std::min<std::size_t>(n-offset,1u<<20);
#ifdef _WIN32
        const auto status=::BCryptGenRandom(
            nullptr,
            reinterpret_cast<PUCHAR>(out.data()+offset),
            static_cast<ULONG>(chunk),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if(status!=0)throw std::runtime_error("OS secure random generation failed");
        offset+=chunk;
#else
        const auto count=::getrandom(out.data()+offset,chunk,0);
        if(count<0){
            if(errno==EINTR)continue;
            throw std::runtime_error("OS secure random generation failed");
        }
        if(count==0)throw std::runtime_error("OS secure random generation returned no bytes");
        offset+=static_cast<std::size_t>(count);
#endif
    }
    return out;
}

}
