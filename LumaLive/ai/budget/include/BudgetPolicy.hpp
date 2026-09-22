#pragma once
#include <cstdint>
#include <string>
namespace luma::contracts { struct BudgetPolicy { std::string value; std::int64_t sequence{0}; }; }
