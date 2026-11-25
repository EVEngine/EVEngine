#include "network/multiplayer/PeerRoom.h"
#include "zeroerr/unittest.h"

#include <array>

namespace {
using namespace eve::network::multiplayer;
void drain(PeerRoom& room) {
    while (room.pollEvent()) {
    }
}
struct RoomFixture {
    PeerRoom host;
    PeerRoom a;
    PeerRoom b;

    void relay() {
        for (int i = 0; i < 4; ++i) {
            while (auto p = a.pollPacket()) REQUIRE(host.receive({10}, p->bytes).ok());
            while (auto p = b.pollPacket()) REQUIRE(host.receive({20}, p->bytes).ok());
            while (auto p = host.pollPacket()) {
                REQUIRE((p->peer.value == 10 ? a : b).receive({1}, p->bytes).ok());
            }
        }
    }
    void lobby() {
        REQUIRE(host.host(123456, 3).ok());
        REQUIRE(a.join({1}, 100).ok());
        REQUIRE(b.join({1}, 200).ok());
        relay();
    }
    void playing() {
        lobby();
        REQUIRE(host.setReady(true).ok());
        REQUIRE(a.setReady(true).ok());
        REQUIRE(b.setReady(true).ok());
        relay();
        REQUIRE(host.start().ok());
        relay();
        drain(host);
        drain(a);
        drain(b);
    }
};
}  // namespace

TEST_CASE("network.Multiplayer.hostAndTwoPlayers") {
    RoomFixture f;
    f.playing();
    CHECK(f.host.state() == RoomState::Playing);
    CHECK(f.a.state() == RoomState::Playing);
    CHECK(f.b.state() == RoomState::Playing);
    CHECK(f.a.members().size() == 3);
    CHECK(f.a.localPlayer().value == 2);
    CHECK(f.b.localPlayer().value == 3);
    const std::array payload{std::byte{0}, std::byte{255}, std::byte{42}};
    REQUIRE(f.a.send(17, payload).ok());
    f.relay();
    for (auto* room : {&f.host, &f.a, &f.b}) {
        auto event = room->pollEvent();
        REQUIRE(event.has_value());
        CHECK(event->kind == RoomEventKind::Message);
        CHECK(event->sender == f.a.localPlayer());
        CHECK(event->tick == 17);
        CHECK(event->payload == std::vector<std::byte>(payload.begin(), payload.end()));
        CHECK(!room->pollEvent().has_value());
    }
    REQUIRE(f.host.send(18, {}).ok());
    f.relay();
    REQUIRE(f.b.pollEvent()->sender.value == 1);
}

TEST_CASE("network.Multiplayer.readinessAndAuthority") {
    RoomFixture f;
    f.lobby();
    CHECK(!f.host.start().ok());
    CHECK(!f.a.start().ok());
    CHECK(!f.a.send(0, {}).ok());
    REQUIRE(f.host.setReady(true).ok());
    REQUIRE(f.a.setReady(true).ok());
    f.relay();
    CHECK(!f.host.start().ok());
    REQUIRE(f.b.setReady(true).ok());
    f.relay();
    REQUIRE(f.host.start().ok());
    f.relay();
    CHECK(!f.a.setReady(false).ok());
}

TEST_CASE("network.Multiplayer.rejectsMalformedStaleAndSpoofedPacketsAtomically") {
    RoomFixture f;
    f.playing();
    REQUIRE(f.a.send(1, {}).ok());
    auto packet = f.a.pollPacket();
    REQUIRE(packet.has_value());
    for (size_t length = 0; length < packet->bytes.size(); ++length)
        CHECK(!f.host.receive({10}, std::span(packet->bytes).first(length)).ok());
    auto invalid = packet->bytes;
    invalid[4]   = std::byte{2};
    CHECK(!f.host.receive({10}, invalid).ok());
    invalid = packet->bytes;
    invalid[6] ^= std::byte{1};
    CHECK(!f.host.receive({10}, invalid).ok());
    invalid     = packet->bytes;
    invalid[22] = std::byte{3};
    CHECK(!f.host.receive({10}, invalid).ok());
    CHECK(!f.host.receive({999}, packet->bytes).ok());
    invalid = packet->bytes;
    invalid.push_back(std::byte{0});
    CHECK(!f.host.receive({10}, invalid).ok());
    CHECK(!f.host.pollEvent().has_value());
    CHECK(!f.host.pollPacket().has_value());
    CHECK(f.host.members().size() == 3);
    REQUIRE(f.host.receive({10}, packet->bytes).ok());
    CHECK(f.host.pollEvent()->sender.value == 2);
}

TEST_CASE("network.Multiplayer.leaveAndHostLoss") {
    RoomFixture f;
    f.playing();
    const auto previous = f.a.localPlayer();
    REQUIRE(f.a.close().ok());
    f.relay();
    CHECK(f.host.members().size() == 2);
    CHECK(f.b.members().size() == 2);
    CHECK(f.a.state() == RoomState::Closed);
    CHECK(f.a.localPlayer().value == 0);
    CHECK(!f.a.join({1}, 333).ok());
    CHECK(previous.value == 2);
    REQUIRE(f.b.disconnected({1}).ok());
    CHECK(f.b.state() == RoomState::Closed);
    CHECK(f.b.members().empty());
    bool closed = false;
    while (auto event = f.b.pollEvent()) closed |= event->kind == RoomEventKind::Closed;
    CHECK(closed);
}

TEST_CASE("network.Multiplayer.capacityAndWelcomeCorrelation") {
    PeerRoom host;
    PeerRoom a;
    PeerRoom b;
    REQUIRE(host.host(44, 2).ok());
    REQUIRE(a.join({1}, 11).ok());
    REQUIRE(b.join({1}, 22).ok());
    auto join = a.pollPacket();
    REQUIRE(host.receive({10}, join->bytes).ok());
    auto welcome = host.pollPacket();
    CHECK(!b.receive({1}, welcome->bytes).ok());
    CHECK(b.state() == RoomState::Joining);
    REQUIRE(a.receive({1}, welcome->bytes).ok());
    CHECK(!host.receive({10}, join->bytes).ok());
    auto excess = b.pollPacket();
    REQUIRE(host.receive({20}, excess->bytes).ok());
    CHECK(host.members().size() == 2);
    auto roster = host.pollPacket();
    REQUIRE(roster.has_value());
    auto rejection = host.pollPacket();
    REQUIRE(rejection.has_value());
    REQUIRE(b.receive({1}, rejection->bytes).ok());
    CHECK(b.state() == RoomState::Closed);
    CHECK(b.pollEvent()->rejection == RoomRejectReason::Full);
    REQUIRE(host.disconnected({10}).ok());
    REQUIRE(host.receive({20}, excess->bytes).ok());
    CHECK(host.members()[1].id.value == 3);
}

TEST_CASE("network.Multiplayer.backpressureAndPayloadLimits") {
    RoomFixture f;
    f.playing();
    std::vector<std::byte> tooLarge(4097);
    CHECK(!f.host.send(0, tooLarge).ok());
    for (uint64_t i = 0; i < 128; ++i) REQUIRE(f.host.send(i, {}).ok());
    CHECK(!f.host.send(129, {}).ok());
    size_t packets = 0;
    while (f.host.pollPacket()) ++packets;
    CHECK(packets == 256);
    size_t events = 0;
    while (f.host.pollEvent()) ++events;
    CHECK(events == 128);
    REQUIRE(f.host.send(130, {}).ok());
}

TEST_CASE("network.Multiplayer.hostCloseReachesAllClients") {
    RoomFixture f;
    f.playing();
    REQUIRE(f.host.close().ok());
    f.relay();
    CHECK(f.a.state() == RoomState::Closed);
    CHECK(f.b.state() == RoomState::Closed);
    REQUIRE(f.host.close().ok());
}

TEST_CASE("network.Multiplayer.inboxBackpressureCanRetryWithoutPartialDelivery") {
    RoomFixture f;
    f.playing();
    for (uint64_t i = 0; i < 256; ++i) {
        REQUIRE(f.host.send(i, {}).ok());
        while (auto packet = f.host.pollPacket()) {
            if (packet->peer.value == 10) REQUIRE(f.a.receive({1}, packet->bytes).ok());
        }
        drain(f.host);
    }
    REQUIRE(f.host.send(256, {}).ok());
    auto packet = f.host.pollPacket();
    REQUIRE(packet.has_value());
    CHECK(!f.a.receive({1}, packet->bytes).ok());
    auto first = f.a.pollEvent();
    REQUIRE(first.has_value());
    CHECK(first->tick == 0);
    REQUIRE(f.a.receive({1}, packet->bytes).ok());
    uint64_t expected = 1;
    while (auto event = f.a.pollEvent()) CHECK(event->tick == expected++);
    CHECK(expected == 257);
}

TEST_CASE("network.Multiplayer.rejectJoinAfterStart") {
    RoomFixture f;
    f.playing();
    PeerRoom late;
    REQUIRE(late.join({1}, 987).ok());
    auto request = late.pollPacket();
    REQUIRE(f.host.receive({30}, request->bytes).ok());
    auto rejection = f.host.pollPacket();
    REQUIRE(late.receive({1}, rejection->bytes).ok());
    CHECK(late.state() == RoomState::Closed);
    CHECK(late.pollEvent()->rejection == RoomRejectReason::AlreadyPlaying);
    CHECK(f.host.members().size() == 3);
}
