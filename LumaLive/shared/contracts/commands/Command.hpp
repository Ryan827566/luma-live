#pragma once
#include <string>
namespace luma::shared::contracts::commands {
struct Command { std::string id; virtual ~Command()=default; };
}
