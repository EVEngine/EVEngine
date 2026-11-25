// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "concurrency",
  "module": "network"
}
*/
// clang-format on

#include "network/Network.h"
#include "network/UdpLink.h"
#include "network/UdpSocket.h"
#include "network/multiplayer/UdpRoom.h"
#include "zeroerr/unittest.h"

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <chrono>
#include <thread>

TEST_CASE("network.Udp.destroyLinkThenPump") {
    eve::network::Network network;
    auto                  socket = network.makeUdp();
    REQUIRE(socket->bind(0));
    {
        auto link = network.makeUdpLink(socket.get());
        REQUIRE(link != nullptr);
    }
    // Regression: Network used to retain the destroyed UdpLink in its pump map.
    network.pump();
    socket.reset();
    network.pump();
}

TEST_CASE("network.Multiplayer.udpHostTwoClientsAndTeardown") {
    using namespace eve::network::multiplayer;
    eve::network::Network network;
    {
        Poco::Net::DatagramSocket reservation(Poco::Net::SocketAddress("127.0.0.1", 0));
        const auto                port = reservation.address().port();
        reservation.close();
        UdpRoom host(network), a(network), b(network);
        REQUIRE(host.host(port, 3333, 3).ok());
        REQUIRE(a.join("127.0.0.1", port, 1111).ok());
        REQUIRE(b.join("127.0.0.1", port, 2222).ok());
        const auto pumpUntil = [&](const auto& predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!predicate() && std::chrono::steady_clock::now() < deadline) {
                REQUIRE(host.pump().ok());
                REQUIRE(a.pump().ok());
                REQUIRE(b.pump().ok());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            REQUIRE(predicate());
        };
        pumpUntil([&] { return a.room().members().size() == 3 && b.room().members().size() == 3; });
        REQUIRE(host.room().setReady(true).ok());
        REQUIRE(a.room().setReady(true).ok());
        REQUIRE(b.room().setReady(true).ok());
        pumpUntil([&] {
            auto members = host.room().members();
            return members.size() == 3 && members[0].ready && members[1].ready && members[2].ready;
        });
        REQUIRE(host.room().start().ok());
        pumpUntil([&] { return a.room().state() == RoomState::Playing && b.room().state() == RoomState::Playing; });
        while (b.room().pollEvent()) {
        }
        const std::string payload("hello\0peer", 10);
        REQUIRE(a.room().send(42, std::as_bytes(std::span(payload.data(), payload.size()))).ok());
        bool received = false;
        pumpUntil([&] {
            while (auto event = b.room().pollEvent()) {
                if (event->kind == RoomEventKind::Message) {
                    CHECK(event->sender == a.room().localPlayer());
                    CHECK(event->tick == 42);
                    CHECK(event->payload.size() == payload.size());
                    received = true;
                }
            }
            return received;
        });
        REQUIRE(host.room().close().ok());
        pumpUntil([&] { return a.room().state() == RoomState::Closed && b.room().state() == RoomState::Closed; });
    }
    network.pump();
    CHECK(network.telemetrySnapshot().watchedUdp == 0);
}

TEST_CASE("network.Udp.closeAndDestroyWithPendingSends") {
    // Destruction of the module joins the worker after every socket has retired.
    // With ASan this also catches jobs that dereference a destroyed socket.
    eve::network::Network network;
    for (int i = 0; i < 256; ++i) {
        auto socket = network.makeUdp();
        REQUIRE(socket->bind(0));
        REQUIRE(socket->sendToString("pending", "127.0.0.1", 9));
        socket->close();
    }
    CHECK(network.telemetrySnapshot().watchedUdp == 0);
}

TEST_CASE("network.Udp.closeAndDestroyWithPendingConnectedSends") {
    eve::network::Network network;
    for (int i = 0; i < 256; ++i) {
        auto socket = network.makeUdp();
        REQUIRE(socket->connect("127.0.0.1", 9));
        REQUIRE(socket->sendString("pending"));
        socket->close();
    }
    CHECK(network.telemetrySnapshot().watchedUdp == 0);
}

TEST_CASE("network.Udp.closeAfterReceiveRetiresPolling") {
    eve::network::Network     network;
    Poco::Net::DatagramSocket sender;
    for (int i = 0; i < 64; ++i) {
        auto socket = network.makeUdp();
        REQUIRE(socket->bind(0));
        const auto port = socket->datagram()->address().port();
        REQUIRE(sender.sendTo("x", 1, Poco::Net::SocketAddress("127.0.0.1", port)) == 1);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (network.telemetrySnapshot().receivedBytes < static_cast<uint64_t>(i + 1) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        REQUIRE(network.telemetrySnapshot().receivedBytes == static_cast<uint64_t>(i + 1));
        socket->close();
    }
    CHECK(network.telemetrySnapshot().watchedUdp == 0);
}
