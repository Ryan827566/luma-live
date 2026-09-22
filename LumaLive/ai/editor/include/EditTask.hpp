#pragma once
#include <cstdint>
#include <string>
namespace luma::contracts { struct EditTask { std::string value; std::int64_t sequence{0}; }; }
