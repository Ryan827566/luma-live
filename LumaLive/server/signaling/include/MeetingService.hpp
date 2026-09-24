#pragma once
#include "runtime-contracts/SignalingWire.hpp"
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
namespace luma::server::signaling {
// Connection IDs are transport handles, not authenticated account identities.
class MeetingService {
public:
    using Connection = std::uint64_t;
    struct Delivery { Connection connection; contracts::SignalingMessage message; };
    std::vector<Delivery> Handle(Connection connection,const contracts::SignalingMessage& request);
    std::vector<Delivery> Disconnect(Connection connection);
    bool Contains(Connection connection) const;
    static bool IsMeetingMessage(contracts::SignalingMessageType type);
private:
    struct Member { Connection connection; std::string video{"video:off"},audio{"audio:off"}; };
    struct Meeting { std::string host; std::int64_t epoch; std::map<std::string,Member> members; };
    struct Binding { std::string meeting,member; };
    std::vector<Delivery> LeaveLocked(Connection connection);
    mutable std::mutex mutex_;
    std::map<std::string,Meeting> meetings_;
    std::map<Connection,Binding> bindings_;
    std::int64_t nextEpoch_{0};
};
}
