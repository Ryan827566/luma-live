#pragma once
#include "../contracts/errors/Error.hpp"
#include "../contracts/events/Event.hpp"
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>
namespace luma::shared::common {
using EventHandler=std::function<void(const contracts::events::Event&)>;
class IEventBus {
public: virtual ~IEventBus()=default;
virtual contracts::Result Subscribe(const std::string&, EventHandler)=0;
virtual contracts::Result Unsubscribe(const std::string&)=0;
virtual contracts::Result Publish(const contracts::events::Event&)=0;
};
class EventBus final: public IEventBus {
public:
contracts::Result Subscribe(const std::string& t, EventHandler h) override { std::scoped_lock l(m_); handlers_[t].push_back(std::move(h)); return contracts::Result::Ok(); }
contracts::Result Unsubscribe(const std::string& t) override { std::scoped_lock l(m_); handlers_.erase(t); return contracts::Result::Ok(); }
contracts::Result Publish(const contracts::events::Event& e) override { std::vector<EventHandler> hs; {std::scoped_lock l(m_); auto it=handlers_.find(e.id); if(it!=handlers_.end()) hs=it->second;} for(auto& h:hs) if(h) h(e); return contracts::Result::Ok(); }
private: std::mutex m_; std::unordered_map<std::string,std::vector<EventHandler>> handlers_;
};
}
