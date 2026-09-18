#pragma once
#include <cstdint>
#include <string>
namespace luma::runtime { enum class ExecutionStatus { Accepted, Running, Completed, Failed, Cancelled }; struct TaskState { std::uint64_t id{}; ExecutionStatus status{ExecutionStatus::Accepted}; std::string message; }; }
