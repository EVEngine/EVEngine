#include "network/multiplayer/PeerRoomBindings.h"
#include "common/SquirrelBinding.h"
#include "network/Network.h"
#include "network/multiplayer/UdpRoom.h"

#include <functional>
#include <string>

namespace eve::network::multiplayer {
namespace {
Result<void> invalid() {
    return Result<void>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "Invalid room port, capacity, nonce or tick", "network.multiplayer"));
}
std::string stateName(RoomState state) {
    switch (state) {
        case RoomState::Idle: return "idle";
        case RoomState::Joining: return "joining";
        case RoomState::Lobby: return "lobby";
        case RoomState::Playing: return "playing";
        case RoomState::Closed: return "closed";
    }
    return "closed";
}
std::string eventName(RoomEventKind kind) {
    switch (kind) {
        case RoomEventKind::Joined: return "joined";
        case RoomEventKind::RosterChanged: return "roster";
        case RoomEventKind::Started: return "started";
        case RoomEventKind::Message: return "message";
        case RoomEventKind::Closed: return "closed";
        case RoomEventKind::Rejected: return "rejected";
    }
    return "closed";
}
}  // namespace
void exposePeerRoomBindings(ssq::Table& table) {
    const auto vm = table.getHandle();
    auto cls = table.addClass<UdpRoom>("PeerRoom",
                                       std::function<UdpRoom*()>([] { return new UdpRoom(*Network::create()); }), true);
    cls.addFunc("host", [vm](UdpRoom* self, int64_t port, int64_t nonce, int64_t capacity) {
        if (port < 1 || port > 65535 || nonce < 1 || capacity < 2 || capacity > 16)
            return script::projectResult(vm, invalid());
        return script::projectResult(
            vm, self->host(static_cast<uint16_t>(port), static_cast<uint64_t>(nonce), static_cast<uint8_t>(capacity)));
    });
    cls.addFunc("join", [vm](UdpRoom* self, std::string address, int64_t port, int64_t nonce) {
        if (port < 1 || port > 65535 || nonce < 1) return script::projectResult(vm, invalid());
        return script::projectResult(vm,
                                     self->join(address, static_cast<uint16_t>(port), static_cast<uint64_t>(nonce)));
    });
    cls.addFunc("pump", [vm](UdpRoom* self) { return script::projectResult(vm, self->pump()); });
    cls.addFunc("setReady",
                [vm](UdpRoom* self, bool ready) { return script::projectResult(vm, self->room().setReady(ready)); });
    cls.addFunc("start", [vm](UdpRoom* self) { return script::projectResult(vm, self->room().start()); });
    cls.addFunc("close", [vm](UdpRoom* self) { return script::projectResult(vm, self->room().close()); });
    cls.addFunc("send", [vm](UdpRoom* self, int64_t tick, std::string payload) {
        if (tick < 0) return script::projectResult(vm, invalid());
        const auto bytes = std::as_bytes(std::span(payload.data(), payload.size()));
        return script::projectResult(vm, self->room().send(static_cast<uint64_t>(tick), bytes));
    });
    cls.addFunc("state", [](UdpRoom* self) { return stateName(self->room().state()); });
    cls.addFunc("isHost", [](UdpRoom* self) { return self->room().isHost(); });
    cls.addFunc("pendingSends", [](UdpRoom* self) { return static_cast<int64_t>(self->pendingSends()); });
    cls.addFunc("localPlayer", [vm](UdpRoom* self) {
        const auto id = self->room().localPlayer();
        ssq::Table result(vm);
        // Room identity is decimal text to preserve all native uint64_t values in Squirrel.
        result.set("room", std::to_string(id.room));
        result.set("player", static_cast<int64_t>(id.value));
        return result;
    });
    cls.addFunc("members", [vm](UdpRoom* self) {
        std::vector<ssq::Table> values;
        for (const auto& member : self->room().members()) {
            ssq::Table value(vm);
            value.set("room", std::to_string(member.id.room));
            value.set("player", static_cast<int64_t>(member.id.value));
            value.set("ready", member.ready);
            values.push_back(value);
        }
        return ssq::Array(vm, values);
    });
    cls.addFunc("pollEvent", [vm](UdpRoom* self) {
        ssq::Table result(vm);
        auto       event = self->room().pollEvent();
        result.set("hasEvent", event.has_value());
        if (event) {
            result.set("kind", eventName(event->kind));
            result.set("room", std::to_string(event->sender.room));
            result.set("player", static_cast<int64_t>(event->sender.value));
            result.set("tick", std::to_string(event->tick));
            result.set("rejection", static_cast<int64_t>(event->rejection));
            std::string payload;
            if (!event->payload.empty())
                payload.assign(reinterpret_cast<const char*>(event->payload.data()), event->payload.size());
            result.set("payload", payload);
        }
        return result;
    });
}
}  // namespace eve::network::multiplayer
