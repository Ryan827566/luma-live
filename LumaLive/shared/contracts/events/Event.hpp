#pragma once
#include <cstdint>
#include <string>
namespace luma::shared::contracts::events {
struct Event { std::string id; std::int64_t timestampMs{}; virtual ~Event()=default; };
}
