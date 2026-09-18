#pragma once
#include <cstdint>
#include <string>
namespace luma::shared::contracts {
enum class ErrorCode : std::int32_t { None=0, NotImplemented=1, InvalidArgument=2, PermissionDenied=3, InvalidState=4, Internal=5 };
struct Error { ErrorCode code{ErrorCode::None}; std::string message; };
class Result {
public:
    static Result Ok() { return {}; }
    static Result Failure(ErrorCode c, std::string m={}) { return Result{c,std::move(m)}; }
    bool IsOk() const noexcept { return code_==ErrorCode::None; }
    ErrorCode Code() const noexcept { return code_; }
    const std::string& Message() const noexcept { return message_; }
private:
    Result()=default; Result(ErrorCode c,std::string m):code_(c),message_(std::move(m)){}
    ErrorCode code_{}; std::string message_;
};
}
