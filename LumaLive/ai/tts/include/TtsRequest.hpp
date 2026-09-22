#pragma once
#include <cstdint>
#include <string>
namespace luma::contracts { struct TtsRequest { std::string value; std::int64_t sequence{0}; }; }
