#pragma once
#include <cstdint>
#include <string>

namespace luma::contracts {

enum class SignalingMessageType : std::uint8_t { JoinRoom=1, LeaveRoom=2, Offer=3, Answer=4, IceCandidate=5, PeerJoined=6, PeerLeft=7, Error=8, Ping=9, Pong=10, RoomJoined=11, CallInvite=12, CallAccept=13, CallReject=14, CallCancel=15, CallHangup=16, CallBusy=17, CallMediaState=18, CallReconnect=19, MeetingCreate=20, MeetingJoin=21, MeetingJoined=22, MeetingLeave=23, MeetingKick=24, MeetingEnd=25, MeetingEnded=26, MeetingMemberJoined=27, MeetingMemberLeft=28, MeetingMediaState=29, MeetingOffer=30, MeetingAnswer=31, MeetingIceCandidate=32, MeetingRemoved=33 };

struct SignalingMessage {
    SignalingMessageType type{SignalingMessageType::Error};
    std::string room_id;
    std::string peer_id;
    std::string target_peer_id;
    std::string sdp;
    std::string candidate;
    std::string candidate_mid;
    std::string value;
    std::int64_t sequence{0};
};

} // namespace luma::contracts

