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
#include "network/UdpSocket.h"
#include "zeroerr/unittest.h"

#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <chrono>
#include <thread>

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
