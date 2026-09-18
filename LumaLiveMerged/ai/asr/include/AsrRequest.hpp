#pragma once
#include <cstdint>
#include <string>
namespace luma::contracts { struct AsrRequest { std::string value; std::int64_t sequence{0}; }; }
