// eve-hot-path
#include "network/multiplayer/PeerRoom.h"

#include <algorithm>
#include <deque>
#include <limits>

namespace eve::network::multiplayer {
namespace {
constexpr size_t maxPayload = 4096;
constexpr size_t maxQueue   = 256;
constexpr size_t headerSize = 38;
enum class Op : uint8_t { Join = 1, Welcome, Roster, Ready, Start, Data, Leave, Close, Reject };

Result<void> fail(DiagnosticCode code, const char* message) {
    return Result<void>::failure(Diagnostic::error(code, message, "network.multiplayer"));
}
void put(std::vector<std::byte>& out, uint64_t value, size_t count) {
    for (size_t i = 0; i < count; ++i) out.push_back(static_cast<std::byte>((value >> (i * 8)) & 255));
}
uint64_t get(std::span<const std::byte> bytes, size_t offset, size_t count) {
    uint64_t result = 0;
    for (size_t i = 0; i < count; ++i) result |= uint64_t(std::to_integer<uint8_t>(bytes[offset + i])) << (i * 8);
    return result;
}
struct Envelope {
    Op                     op;
    uint64_t               room   = 0;
    uint64_t               nonce  = 0;
    uint32_t               player = 0;
    uint64_t               tick   = 0;
    std::vector<std::byte> payload;
};
std::vector<std::byte> encode(const Envelope& e) {
    std::vector<std::byte> bytes;
    bytes.reserve(headerSize + e.payload.size());
    for (char c : std::string_view("EVMP")) bytes.push_back(static_cast<std::byte>(c));
    put(bytes, 1, 1);
    put(bytes, static_cast<uint8_t>(e.op), 1);
    put(bytes, e.room, 8);
    put(bytes, e.nonce, 8);
    put(bytes, e.player, 4);
    put(bytes, e.tick, 8);
    put(bytes, e.payload.size(), 4);
    bytes.insert(bytes.end(), e.payload.begin(), e.payload.end());
    return bytes;
}
Result<Envelope> decode(std::span<const std::byte> bytes) {
    auto error = [](DiagnosticCode code) {
        return Result<Envelope>::failure(Diagnostic::error(code, "Invalid EVMP packet", "network.multiplayer"));
    };
    if (bytes.size() < headerSize || bytes.size() > headerSize + maxPayload || get(bytes, 0, 4) != 0x504d5645)
        return error(DiagnosticCode::ParseError);
    if (get(bytes, 4, 1) != 1) return error(DiagnosticCode::UnknownVersion);
    const auto op = get(bytes, 5, 1);
    if (op < 1 || op > 9 || get(bytes, 34, 4) != bytes.size() - headerSize) return error(DiagnosticCode::ParseError);
    Envelope e{static_cast<Op>(op), get(bytes, 6, 8),
               get(bytes, 14, 8),   static_cast<uint32_t>(get(bytes, 22, 4)),
               get(bytes, 26, 8),   {}};
    e.payload.assign(bytes.begin() + headerSize, bytes.end());
    return Result<Envelope>::success(std::move(e));
}
}  // namespace

struct PeerRoom::Impl {
    struct Member {
        RoomMember value;
        PeerId     peer;
    };
    RoomState              state      = RoomState::Idle;
    bool                   host       = false;
    uint64_t               room       = 0;
    uint64_t               nonce      = 0;
    uint32_t               local      = 0;
    uint32_t               nextPlayer = 2;
    uint8_t                capacity   = 0;
    PeerId                 hostPeer;
    std::vector<Member>    members;
    std::deque<RoomEvent>  events;
    std::deque<RoomPacket> packets;

    bool space(size_t outgoing, size_t incoming = 1) const {
        return packets.size() + outgoing <= maxQueue && events.size() + incoming <= maxQueue;
    }
    size_t remotes() const { return members.empty() ? 0 : members.size() - 1; }
    void   queue(PeerId peer, const Envelope& e) { packets.push_back({peer, encode(e)}); }
    void   broadcast(const Envelope& e) {
        for (const auto& member : members)
            if (member.peer.value) queue(member.peer, e);
    }
    void event(RoomEventKind kind, uint32_t sender = 0, uint64_t tick = 0, const std::vector<std::byte>& payload = {}) {
        events.push_back({kind, {room, sender}, tick, payload});
    }
    std::vector<std::byte> roster() const {
        std::vector<std::byte> bytes;
        put(bytes, members.size(), 1);
        for (const auto& member : members) {
            put(bytes, member.value.id.value, 4);
            put(bytes, member.value.ready ? 1 : 0, 1);
        }
        return bytes;
    }
    void publishRoster() {
        broadcast({Op::Roster, room, 0, 0, 0, roster()});
        event(RoomEventKind::RosterChanged);
    }
    void finish() {
        event(RoomEventKind::Closed);
        state = RoomState::Closed;
        members.clear();
        local = 0;
    }
};

PeerRoom::PeerRoom() : impl_(std::make_unique<Impl>()) {}
PeerRoom::~PeerRoom() = default;
RoomState               PeerRoom::state() const { return impl_->state; }
PlayerId                PeerRoom::localPlayer() const { return {impl_->room, impl_->local}; }
bool                    PeerRoom::isHost() const { return impl_->host; }
size_t                  PeerRoom::pendingPackets() const { return impl_->packets.size(); }
std::vector<RoomMember> PeerRoom::members() const {
    std::vector<RoomMember> result;
    for (const auto& member : impl_->members) result.push_back(member.value);
    return result;
}
std::optional<RoomEvent> PeerRoom::pollEvent() {
    if (impl_->events.empty()) return std::nullopt;
    auto event = std::move(impl_->events.front());
    impl_->events.pop_front();
    return event;
}
std::optional<RoomPacket> PeerRoom::pollPacket() {
    if (impl_->packets.empty()) return std::nullopt;
    auto packet = std::move(impl_->packets.front());
    impl_->packets.pop_front();
    return packet;
}
Result<void> PeerRoom::host(uint64_t roomNonce, uint8_t capacity) {
    auto& s = *impl_;
    if (s.state != RoomState::Idle || !roomNonce || capacity < 2 || capacity > 16)
        return fail(DiagnosticCode::InvalidArgument, "Host requires idle room, unique nonce and capacity 2..16");
    s.host     = true;
    s.room     = roomNonce;
    s.capacity = capacity;
    s.local    = 1;
    s.members.push_back({{{roomNonce, 1}, false}, {}});
    s.state = RoomState::Lobby;
    s.event(RoomEventKind::Joined, 1);
    return Result<void>::success();
}
Result<void> PeerRoom::join(PeerId hostPeer, uint64_t requestNonce) {
    auto& s = *impl_;
    if (s.state != RoomState::Idle || !hostPeer.value || !requestNonce)
        return fail(DiagnosticCode::InvalidArgument, "Join requires idle room, host peer and unique request nonce");
    s.hostPeer = hostPeer;
    s.nonce    = requestNonce;
    s.queue(hostPeer, {Op::Join, 0, requestNonce, 0, 0, {}});
    s.state = RoomState::Joining;
    return Result<void>::success();
}

Result<void> PeerRoom::receive(PeerId peer, std::span<const std::byte> bytes) {
    auto decoded = decode(bytes);
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    const auto& e = decoded.value();
    auto&       s = *impl_;
    if (!peer.value || s.state == RoomState::Idle || s.state == RoomState::Closed)
        return fail(DiagnosticCode::PreconditionViolation, "Room cannot receive in current state");
    if (e.op != Op::Data && e.tick != 0)
        return fail(DiagnosticCode::ParseError, "Control packet has a simulation tick");
    if (e.op != Op::Join && e.op != Op::Welcome && e.op != Op::Reject && e.nonce != 0)
        return fail(DiagnosticCode::ParseError, "Unexpected request nonce");
    if (e.op != Op::Data && e.op != Op::Welcome && e.op != Op::Roster && e.op != Op::Ready && e.op != Op::Reject &&
        !e.payload.empty())
        return fail(DiagnosticCode::ParseError, "Unexpected control payload");
    if (e.op == Op::Join) {
        if (!s.host || (s.state != RoomState::Lobby && s.state != RoomState::Playing) || e.room || e.player || !e.nonce)
            return fail(DiagnosticCode::PreconditionViolation, "Host lobby required for join");
        if (std::any_of(s.members.begin(), s.members.end(), [peer](const auto& m) { return m.peer == peer; }))
            return fail(DiagnosticCode::AlreadyExists, "Peer already joined");
        if (s.state == RoomState::Playing || s.members.size() >= s.capacity ||
            s.nextPlayer == std::numeric_limits<uint32_t>::max()) {
            if (!s.space(1, 0)) return fail(DiagnosticCode::Conflict, "Drain packets before rejecting join");
            const auto reason =
                s.state == RoomState::Playing ? RoomRejectReason::AlreadyPlaying : RoomRejectReason::Full;
            s.queue(peer, {Op::Reject, 0, e.nonce, 0, 0, {static_cast<std::byte>(reason)}});
            return Result<void>::success();
        }
        if (!s.space(s.remotes() + 2)) return fail(DiagnosticCode::Conflict, "Drain room queues before join");
        const uint32_t id = s.nextPlayer++;
        s.members.push_back({{{s.room, id}, false}, peer});
        s.queue(peer, {Op::Welcome, s.room, e.nonce, id, 0, s.roster()});
        s.publishRoster();
        return Result<void>::success();
    }
    if (e.op == Op::Reject && !s.host) {
        if (s.state != RoomState::Joining || peer != s.hostPeer || e.room || e.player || e.nonce != s.nonce ||
            e.payload.size() != 1 || get(e.payload, 0, 1) < 1 || get(e.payload, 0, 1) > 2)
            return fail(DiagnosticCode::ParseError, "Invalid join rejection");
        if (!s.space(0)) return fail(DiagnosticCode::Conflict, "Drain events before rejection");
        s.packets.clear();
        s.state = RoomState::Closed;
        s.event(RoomEventKind::Rejected);
        s.events.back().rejection = static_cast<RoomRejectReason>(get(e.payload, 0, 1));
        return Result<void>::success();
    }
    if (!s.host && (e.op == Op::Welcome || e.op == Op::Roster)) {
        if (peer != s.hostPeer || !e.room ||
            (e.op == Op::Welcome
                 ? (s.state != RoomState::Joining || e.nonce != s.nonce || e.player < 2)
                 : (e.room != s.room || e.player || (s.state != RoomState::Lobby && s.state != RoomState::Playing))))
            return fail(DiagnosticCode::StaleHandle, "Unsolicited or stale roster");
        if (e.payload.empty()) return fail(DiagnosticCode::ParseError, "Missing roster");
        const auto count = get(e.payload, 0, 1);
        if (count < 1 || count > 16 || e.payload.size() != 1 + count * 5)
            return fail(DiagnosticCode::ParseError, "Invalid roster size");
        const uint32_t            local = e.op == Op::Welcome ? e.player : s.local;
        std::vector<Impl::Member> candidate;
        uint32_t                  previous      = 0;
        bool                      containsLocal = false;
        for (size_t i = 0; i < count; ++i) {
            const auto id    = static_cast<uint32_t>(get(e.payload, 1 + i * 5, 4));
            const auto ready = get(e.payload, 5 + i * 5, 1);
            if (id <= previous || (i == 0 && id != 1) || ready > 1)
                return fail(DiagnosticCode::ParseError, "Invalid roster identity or readiness");
            previous = id;
            containsLocal |= id == local;
            candidate.push_back({{{e.room, id}, ready == 1}, {}});
        }
        if (!containsLocal) return fail(DiagnosticCode::StaleHandle, "Local player missing from roster");
        if (!s.space(0)) return fail(DiagnosticCode::Conflict, "Drain room events before roster");
        s.members = std::move(candidate);
        s.room    = e.room;
        s.local   = local;
        if (e.op == Op::Welcome) s.state = RoomState::Lobby;
        s.event(e.op == Op::Welcome ? RoomEventKind::Joined : RoomEventKind::RosterChanged, local);
        return Result<void>::success();
    }
    if (e.room != s.room || !s.room) return fail(DiagnosticCode::StaleHandle, "Packet belongs to another room");
    auto member = std::find_if(s.members.begin(), s.members.end(), [peer](const auto& m) { return m.peer == peer; });
    if (s.host ? (member == s.members.end() || e.player != member->value.id.value) : (peer != s.hostPeer))
        return fail(DiagnosticCode::PreconditionViolation, "Unauthorised peer or forged player identity");
    if (e.op == Op::Ready && s.host) {
        if (s.state != RoomState::Lobby || e.payload.size() != 1 || get(e.payload, 0, 1) > 1)
            return fail(DiagnosticCode::PreconditionViolation, "Invalid lobby ready request");
        if (!s.space(s.remotes())) return fail(DiagnosticCode::Conflict, "Drain room queues before readiness");
        member->value.ready = get(e.payload, 0, 1) == 1;
        s.publishRoster();
    } else if (e.op == Op::Start && !s.host) {
        if (s.state != RoomState::Lobby || e.player != 1 || s.members.size() < 2 ||
            !std::all_of(s.members.begin(), s.members.end(), [](const auto& m) { return m.value.ready; }))
            return fail(DiagnosticCode::PreconditionViolation, "Invalid start transition");
        if (!s.space(0)) return fail(DiagnosticCode::Conflict, "Drain events before start");
        s.state = RoomState::Playing;
        s.event(RoomEventKind::Started, 1);
    } else if (e.op == Op::Data) {
        if (s.state != RoomState::Playing || !std::any_of(s.members.begin(), s.members.end(),
                                                          [&e](const auto& m) { return m.value.id.value == e.player; }))
            return fail(DiagnosticCode::PreconditionViolation, "Message requires an active player");
        if (!s.space(s.host ? s.remotes() : 0)) return fail(DiagnosticCode::Conflict, "Drain queues before message");
        if (s.host) s.broadcast(e);
        s.event(RoomEventKind::Message, e.player, e.tick, e.payload);
    } else if (e.op == Op::Leave && s.host) {
        return disconnected(peer);
    } else if (e.op == Op::Close && !s.host && e.player == 1) {
        if (!s.space(0)) return fail(DiagnosticCode::Conflict, "Drain events before close");
        s.packets.clear();
        s.finish();
    } else {
        return fail(DiagnosticCode::PreconditionViolation, "Opcode not permitted for this role");
    }
    return Result<void>::success();
}

Result<void> PeerRoom::setReady(bool ready) {
    auto& s = *impl_;
    if (s.state != RoomState::Lobby) return fail(DiagnosticCode::PreconditionViolation, "Readiness requires lobby");
    if (!s.space(s.host ? s.remotes() : 1, s.host ? 1 : 0))
        return fail(DiagnosticCode::Conflict, "Drain queues before readiness");
    if (s.host) {
        s.members.front().value.ready = ready;
        s.publishRoster();
    } else {
        s.queue(s.hostPeer, {Op::Ready, s.room, 0, s.local, 0, {static_cast<std::byte>(ready ? 1 : 0)}});
    }
    return Result<void>::success();
}
Result<void> PeerRoom::start() {
    auto& s = *impl_;
    if (!s.host || s.state != RoomState::Lobby || s.members.size() < 2 ||
        !std::all_of(s.members.begin(), s.members.end(), [](const auto& m) { return m.value.ready; }))
        return fail(DiagnosticCode::PreconditionViolation, "Only host can start a ready multiplayer lobby");
    if (!s.space(s.remotes())) return fail(DiagnosticCode::Conflict, "Drain queues before start");
    s.broadcast({Op::Start, s.room, 0, 1, 0, {}});
    s.state = RoomState::Playing;
    s.event(RoomEventKind::Started, 1);
    return Result<void>::success();
}
Result<void> PeerRoom::send(uint64_t tick, std::span<const std::byte> payload) {
    auto& s = *impl_;
    if (s.state != RoomState::Playing || payload.size() > maxPayload)
        return fail(DiagnosticCode::InvalidArgument, "Message requires playing room and at most 4096 bytes");
    if (!s.space(s.host ? s.remotes() : 1, s.host ? 1 : 0))
        return fail(DiagnosticCode::Conflict, "Drain queues before sending");
    Envelope e{Op::Data, s.room, 0, s.local, tick, {payload.begin(), payload.end()}};
    if (s.host) {
        s.broadcast(e);
        s.event(RoomEventKind::Message, s.local, tick, e.payload);
    } else
        s.queue(s.hostPeer, e);
    return Result<void>::success();
}
Result<void> PeerRoom::disconnected(PeerId peer) {
    auto& s = *impl_;
    if (s.state == RoomState::Idle || s.state == RoomState::Closed)
        return fail(DiagnosticCode::PreconditionViolation, "Room is not connected");
    if (!s.host) {
        if (peer != s.hostPeer) return fail(DiagnosticCode::NotFound, "Not the host connection");
        if (!s.space(0)) return fail(DiagnosticCode::Conflict, "Drain events before disconnect");
        s.packets.clear();
        s.finish();
    } else {
        const auto it =
            std::find_if(s.members.begin(), s.members.end(), [peer](const auto& m) { return m.peer == peer; });
        if (!peer.value || it == s.members.end()) return fail(DiagnosticCode::NotFound, "Unknown peer");
        if (!s.space(s.remotes() - 1)) return fail(DiagnosticCode::Conflict, "Drain queues before disconnect");
        s.members.erase(it);
        std::erase_if(s.packets, [peer](const auto& p) { return p.peer == peer; });
        s.publishRoster();
    }
    return Result<void>::success();
}
Result<void> PeerRoom::close() {
    auto& s = *impl_;
    if (s.state == RoomState::Closed) return Result<void>::success();
    if (!s.space(s.host ? s.remotes() : 1)) return fail(DiagnosticCode::Conflict, "Drain queues before close");
    s.packets.clear();
    if (s.host)
        s.broadcast({Op::Close, s.room, 0, 1, 0, {}});
    else if (s.state == RoomState::Lobby || s.state == RoomState::Playing)
        s.queue(s.hostPeer, {Op::Leave, s.room, 0, s.local, 0, {}});
    s.finish();
    return Result<void>::success();
}
}  // namespace eve::network::multiplayer
