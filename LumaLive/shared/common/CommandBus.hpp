#pragma once
#include "../contracts/errors/Error.hpp"
#include "../contracts/commands/Command.hpp"
#include <functional>
#include <mutex>
#include <unordered_map>
namespace luma::shared::common {
using CommandHandler=std::function<contracts::Result(const contracts::commands::Command&)>;
class ICommandBus {
public: virtual ~ICommandBus()=default;
virtual contracts::Result Register(const std::string&,CommandHandler)=0;
virtual contracts::Result Unregister(const std::string&)=0;
virtual contracts::Result Dispatch(const contracts::commands::Command&)=0;
};
class CommandBus final: public ICommandBus {
public:
contracts::Result Register(const std::string& n,CommandHandler h) override {std::scoped_lock l(m_); handlers_[n]=std::move(h); return contracts::Result::Ok();}
contracts::Result Unregister(const std::string& n) override {std::scoped_lock l(m_); handlers_.erase(n); return contracts::Result::Ok();}
contracts::Result Dispatch(const contracts::commands::Command& c) override {CommandHandler h; {std::scoped_lock l(m_); auto it=handlers_.find(c.id); if(it==handlers_.end()) return contracts::Result::Failure(contracts::ErrorCode::NotImplemented); h=it->second;} return h(c);}
private: std::mutex m_; std::unordered_map<std::string,CommandHandler> handlers_;
};
}
