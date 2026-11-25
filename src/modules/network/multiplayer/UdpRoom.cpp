// eve-hot-path
#include "network/multiplayer/UdpRoom.h"
#include "network/NetHost.h"
#include "network/Network.h"
#include "network/UdpLink.h"
#include "network/UdpSocket.h"

#include <deque>
#include <string>

namespace eve::network::multiplayer {
namespace {
Result<void> error(DiagnosticCode code, const char* message) {
    return Result<void>::failure(Diagnostic::error(code, message, "network.multiplayer.udp"));
}
constexpr uint8_t roomChannel  = 7;
constexpr size_t  pendingLimit = 256;
}  // namespace
struct UdpRoom::Impl {
    explicit Impl(Network& owner) : network(owner) {}
    Network& network;
    PeerRoom room;
    // Destruction is reversed: link before socket, both before borrowed network.
    std::unique_ptr<NetHost>   host;
    std::unique_ptr<UdpSocket> socket;
    std::unique_ptr<UdpLink>   link;
    struct Incoming {
        PeerId                 peer;
        bool                   disconnected = false;
        std::vector<std::byte> bytes;
    };
    std::deque<Incoming>      incoming;
    std::optional<RoomPacket> outgoing;
    bool                      overflow       = false;
    bool                      closedObserved = false;

    void message(PeerId peer, UdpLink::MsgType type, uint8_t channel, const char* data, size_t size) {
        if (channel != roomChannel || type != UdpLink::MsgType::Reliable) return;
        if (incoming.size() >= pendingLimit) {
            overflow = true;
            return;
        }
        // Keep malformed messages bounded; PeerRoom reports the parse error.
        if (size > 4134) size = 0;
        const auto* begin = reinterpret_cast<const std::byte*>(data);
        Incoming    item{peer, false, {}};
        if (size) item.bytes.assign(begin, begin + size);
        incoming.push_back(std::move(item));
    }
    void lost(PeerId peer) {
        if (incoming.size() >= pendingLimit) {
            overflow = true;
            return;
        }
        incoming.push_back({peer, true, {}});
    }
    Result<void> flush() {
        if (room.state() == RoomState::Closed && !closedObserved) {
            outgoing.reset();
            closedObserved = true;
        }
        for (size_t i = 0; i < pendingLimit; ++i) {
            if (!outgoing) outgoing = room.pollPacket();
            if (!outgoing) break;
            const auto& packet      = *outgoing;
            UdpLink*    destination = host ? host->linkByPeerId(packet.peer.value) : link.get();
            if (!destination) {
                outgoing.reset();
                return error(DiagnosticCode::NotFound, "Room transport peer is gone");
            }
            if (destination->pendingReliable() >= 64)
                return error(DiagnosticCode::Conflict, "Reliable UDP room send window is full");
            destination->send(UdpLink::MsgType::Reliable, roomChannel, packet.bytes.data(), packet.bytes.size());
            outgoing.reset();
        }
        return Result<void>::success();
    }
};
UdpRoom::UdpRoom(Network& network) : impl_(std::make_unique<Impl>(network)) {}
UdpRoom::~UdpRoom() = default;
size_t UdpRoom::pendingSends() const {
    const auto& s = *impl_;
    return s.room.pendingPackets() + (s.outgoing ? 1 : 0) +
           (s.host ? s.host->pendingReliable() : (s.link ? s.link->pendingReliable() : 0));
}
PeerRoom&    UdpRoom::room() { return impl_->room; }
Result<void> UdpRoom::host(uint16_t port, uint64_t nonce, uint8_t capacity) {
    auto& s = *impl_;
    if (s.room.state() != RoomState::Idle || !port || !nonce || capacity < 2 || capacity > 16)
        return error(DiagnosticCode::InvalidArgument, "Invalid host configuration");
    auto candidate = s.network.makeHost();
    if (!candidate->start(port)) return error(DiagnosticCode::Failed, "Cannot bind room UDP port");
    candidate->setMessageHandler([&s](uint32_t peer, UdpLink::MsgType type, uint8_t ch, const char* p, size_t n) {
        s.message({peer}, type, ch, p, n);
    });
    candidate->setPeerDisconnectedHandler([&s](uint32_t peer) { s.lost({peer}); });
    auto result = s.room.host(nonce, capacity);
    if (!result.ok()) return result;
    s.host = std::move(candidate);
    return Result<void>::success();
}
Result<void> UdpRoom::join(std::string_view address, uint16_t port, uint64_t nonce) {
    auto& s = *impl_;
    if (s.room.state() != RoomState::Idle || address.empty() || address.find('\0') != std::string_view::npos || !port ||
        !nonce)
        return error(DiagnosticCode::InvalidArgument, "Invalid join configuration");
    auto socket = s.network.makeUdp();
    if (!socket->connect(std::string(address), port)) return error(DiagnosticCode::Failed, "Cannot connect UDP socket");
    auto link = s.network.makeUdpLink(socket.get());
    if (!link || !link->setRemote(std::string(address), port))
        return error(DiagnosticCode::Failed, "Cannot address host UDP link");
    link->setMessageHandler(
        [&s](UdpLink::MsgType type, uint8_t ch, const char* p, size_t n) { s.message({1}, type, ch, p, n); });
    link->setDisconnectHandler([&s](const std::string&) { s.lost({1}); });
    auto result = s.room.join({1}, nonce);
    if (!result.ok()) return result;
    s.socket = std::move(socket);
    s.link   = std::move(link);
    return Result<void>::success();
}
Result<void> UdpRoom::pump() {
    auto& s = *impl_;
    s.network.pump();
    if (s.room.state() == RoomState::Closed) {
        // ACK processing still runs in Network. Late application messages cannot
        // reopen the room or prevent its queued terminal notification from flushing.
        s.incoming.clear();
        return s.flush();
    }
    if (s.overflow) return error(DiagnosticCode::Failed, "Incoming UDP room queue overflow; recreate adapter");
    while (!s.incoming.empty()) {
        const auto& item   = s.incoming.front();
        auto        result = item.disconnected ? s.room.disconnected(item.peer) : s.room.receive(item.peer, item.bytes);
        if (!result.ok()) {
            // Only backpressure is retryable. Rejected packets cannot block the following valid packet.
            if (result.status().code() != StatusCode::Conflict) {
                s.incoming.pop_front();
            } else {
                // Free outbound capacity even if admission is blocked; otherwise a full
                // room outbox and a pending incoming message would deadlock each other.
                auto flushed = s.flush();
                if (!flushed.ok()) return flushed;
            }
            return result;
        }
        if (item.disconnected && s.outgoing && s.outgoing->peer == item.peer) s.outgoing.reset();
        s.incoming.pop_front();
        if (s.room.state() == RoomState::Closed) {
            s.incoming.clear();
            break;
        }
    }
    return s.flush();
}
}  // namespace eve::network::multiplayer
