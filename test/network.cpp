#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "event/Event.h"
#include "network/Network.h"
#include "network/TcpSocket.h"
#include "network/UdpSocket.h"
#include "network/HttpRequest.h"
#include "network/Channel.h"
#include "network/Session.h"
#include "network/NetStream.h"
#include "network/UdpLink.h"
#include "network/NetRpc.h"
#include "network/NetHost.h"
#include "network/NetWorker.h"
#include "data/ByteData.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <string>

using eve::event::Event;
using eve::event::Message;
using eve::event::Variant;

TEST_CASE("event.VariantMessage") {
    auto* ev = eve::event::Event::create();
    std::vector<Variant> args;
    args.push_back(Variant::makeInt(200));
    args.push_back(Variant::makeString("ok"));
    args.push_back(Variant::makePtr(nullptr));
    ev->push(std::make_unique<Message>("httpresp", args));
    auto m = ev->pollOwned();
    REQUIRE(m.get() != nullptr);
    CHECK(m->name == "httpresp");
    REQUIRE(m->args.size() == 3);
    CHECK(static_cast<int>(m->args[0].type) == static_cast<int>(Variant::Type::Int));
    CHECK(m->args[0].i == 200);
    CHECK(m->args[1].s == "ok");
}

TEST_CASE("network.NetWorkerQueue") {
    eve::network::NetWorker worker(nullptr);
    eve::network::NetCompletion c;
    c.type   = eve::network::NetEvType::Err;
    c.reason = "timeout";
    worker.post(std::move(c));
    std::vector<eve::network::NetCompletion> out;
    worker.drain(out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].reason == "timeout");
}

TEST_CASE("network.NetWorker.stopFromJobDoesNotDeadlock") {
    eve::network::NetWorker worker(nullptr);
    std::atomic<bool> stopped{false};
    worker.start();
    worker.submit([&] {
        worker.stop();
        stopped = true;
    });
    for (int i = 0; i < 200 && !stopped; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(stopped.load());
    worker.stop();
}

TEST_CASE("network.Channel.fragmentedAndCoalescedFrames") {
    auto* net = eve::network::Network::create();
    auto* ev = eve::event::Event::create();
    eve::network::TcpSocket socket(net);
    eve::network::Channel channel(&socket);

    channel.feed({0, 0});
    channel.feed({0, 2, 'a'});
    CHECK(!ev->pollOwned());
    channel.feed({'b', 0, 0, 0, 1, 'c'});

    auto first = ev->pollOwned();
    auto second = ev->pollOwned();
    REQUIRE(static_cast<bool>(first));
    REQUIRE(static_cast<bool>(second));
    CHECK(first->name == "chmsg");
    CHECK(second->name == "chmsg");
    REQUIRE(first->args.size() == 2);
    REQUIRE(second->args.size() == 2);
    auto* firstData = static_cast<eve::data::ByteData*>(first->args[1].p);
    auto* secondData = static_cast<eve::data::ByteData*>(second->args[1].p);
    REQUIRE(firstData != nullptr);
    REQUIRE(secondData != nullptr);
    CHECK(firstData->getSize() == 2);
    CHECK(secondData->getSize() == 1);
}

TEST_CASE("network.Channel.rejectsOversizedFrameHeader") {
    auto* net = eve::network::Network::create();
    auto* ev = eve::event::Event::create();
    while (ev->pollOwned()) {}
    eve::network::TcpSocket socket(net);
    eve::network::Channel channel(&socket);
    channel.feed({0x01, 0x00, 0x00, 0x01});
    net->pump();

    bool sawClose = false;
    bool sawError = false;
    while (auto message = ev->pollOwned()) {
        if (message->name == "chclose") sawClose = true;
        if (message->name == "neterr") sawError = true;
    }
    CHECK(sawClose);
    CHECK(sawError);
}

TEST_CASE("network.TcpEcho") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    REQUIRE(net != nullptr);
    REQUIRE(ev != nullptr);

    const uint16_t port = 39281;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool clientOk = false;
    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);

    eve::data::ByteData payload("ping", 4);
    REQUIRE(client->send(&payload));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata") {
                got = true;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.UdpSendTo") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    auto* a = net->newUdp();
    auto* b = net->newUdp();
    REQUIRE(a->bind(39301));
    REQUIRE(b->bind(39302));
    eve::data::ByteData payload("udp!", 4);
    REQUIRE(a->sendTo(&payload, "127.0.0.1", 39302));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 1 && m->args[0].p == b) {
                got = true;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    a->close();
    b->close();
    delete a;
    delete b;
}

TEST_CASE("network.HttpGetLocal") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39311;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* req = net->newHttp("GET", "http://127.0.0.1:" + std::to_string(port) + "/");
    REQUIRE(req->submit());

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {}
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    const char* httpResp =
        "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
    eve::data::ByteData respData(httpResp, std::strlen(httpResp));
    REQUIRE(peer->send(&respData));

    bool got = false;
    int status = 0;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "httpresp" && m->args.size() >= 2) {
                got = true;
                status = static_cast<int>(m->args[1].i);
                REQUIRE(m->args.size() >= 3);
                CHECK(m->args[2].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    CHECK(status == 200);

    peer->close();
    server->close();
    delete peer;
    delete server;
    delete req;
}

TEST_CASE("network.ChannelFrames") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39321;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {}
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chSend = net->newChannel(client);
    auto* chRecv = net->newChannel(peer);
    REQUIRE(chSend->sendMsgString("ab"));
    REQUIRE(chSend->sendMsgString("cd"));

    int msgs = 0;
    for (int i = 0; i < 400 && msgs < 2; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "chmsg") {
                ++msgs;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[1].p)
                CHECK(m->args[1].ownsPointer());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(msgs == 2);

    auto* sess = net->newSession();
    sess->add("main", chRecv);
    CHECK(sess->get("main") == chRecv);
    sess->closeAll();

    delete sess;
    delete chSend;
    delete chRecv;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Network.timeoutAndVerifySsl") {
    auto* net = eve::network::Network::create();
    CHECK(net->getTimeout() == 10000);
    CHECK(net->getVerifySsl() == true);
    net->setTimeout(5000);
    net->setVerifySsl(false);
    CHECK(net->getTimeout() == 5000);
    CHECK(net->getVerifySsl() == false);
}

TEST_CASE("network.Tcp.connectedAndLocalPort") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39291;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));
    CHECK(server->getLocalPort() == port);
    CHECK(!server->isConnected());

    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool clientOk = false;
    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);
    CHECK(client->isConnected());
    CHECK(peer->isConnected());
    CHECK(client->getLocalPort() != 0);
    CHECK(peer->getLocalPort() != 0);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Session.addGetRemoveCloseAll") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39331;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {}
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chClient = net->newChannel(client);
    auto* chServer = net->newChannel(peer);
    auto* sess = net->newSession();

    sess->add("client", chClient);
    sess->add("server", chServer);
    CHECK(sess->get("client") == chClient);
    CHECK(sess->get("server") == chServer);
    CHECK(sess->get("missing") == nullptr);

    sess->remove("client");
    CHECK(sess->get("client") == nullptr);
    CHECK(sess->get("server") == chServer);

    sess->closeAll();
    CHECK(sess->get("server") == nullptr);

    delete sess;
    delete chClient;
    delete chServer;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Http.setHeaderAndBody") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39341;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* req = net->newHttp("POST", "http://127.0.0.1:" + std::to_string(port) + "/test");
    req->setHeader("X-Test-Header", "custom-value");
    req->setBodyString("post-body");
    REQUIRE(req->submit());

    std::string received;
    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {}
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    for (int i = 0; i < 400 && received.find("\r\n\r\n") == std::string::npos; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer && m->args[1].p) {
                auto* data = static_cast<eve::data::ByteData*>(m->args[1].p);
                received.append(static_cast<char*>(data->getData()), data->getSize());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(received.find("X-Test-Header: custom-value") != std::string::npos);
    CHECK(received.find("post-body") != std::string::npos);

    const char* httpResp =
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    eve::data::ByteData respData(httpResp, std::strlen(httpResp));
    REQUIRE(peer->send(&respData));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "httpresp") {
                got = true;
                REQUIRE(m->args.size() >= 3);
                CHECK(m->args[2].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);

    peer->close();
    server->close();
    delete peer;
    delete server;
    delete req;
}

TEST_CASE("network.Http.httpsReturnsTlsWithoutSslBackend") {
    auto* net = eve::network::Network::create();
    REQUIRE(net != nullptr);
    net->setVerifySsl(false);
    auto* http = net->newHttp("GET", "https://example.com/");
    REQUIRE(http != nullptr);
    http->setVerifySsl(true);  // request-level override is stored / resolved in submit()
    REQUIRE(http->submit());

    auto* ev = eve::event::Event::create();
    REQUIRE(ev != nullptr);
    bool gotTls = false;
    for (int i = 0; i < 200 && !gotTls; ++i) {
        net->pump();
        while (auto msg = ev->pollOwned()) {
            if (msg->name == "neterr" && msg->args.size() >= 2 && msg->args[1].s == "tls")
                gotTls = true;
        }
        if (!gotTls)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(gotTls);
    delete http;
}

TEST_CASE("network.NetWriterReaderRoundtrip") {
    eve::network::NetWriter w;
    w.writeU8(0xAB);
    w.writeI8(-3);
    w.writeU16(0xBEEF);
    w.writeI16(-1234);
    w.writeU32(0xDEADBEEFu);
    w.writeI32(-70000);
    w.writeU64(0x0123456789ABCDEFull);
    w.writeI64(-1234567890123ll);
    w.writeF32(1.5f);
    w.writeF64(-2.25);
    w.writeBool(true);
    w.writeBool(false);
    w.writeString("hello");
    w.writeString(std::string("a\0b", 3));
    const char raw[] = {0x00, 0x01, '\xFE', '\xFF'};
    w.writeBytes(raw, sizeof(raw));

    REQUIRE(w.size() > 0);
    eve::network::NetReader r(w.data(), w.size());
    REQUIRE(r.ok());
    CHECK(r.u8() == 0xAB);
    CHECK(r.i8() == -3);
    CHECK(r.u16() == 0xBEEF);
    CHECK(r.i16() == -1234);
    CHECK(r.u32() == 0xDEADBEEFu);
    CHECK(r.i32() == -70000);
    CHECK(r.u64() == 0x0123456789ABCDEFull);
    CHECK(r.i64() == -1234567890123ll);
    CHECK(r.f32() == 1.5f);
    CHECK(r.f64() == -2.25);
    CHECK(r.b() == true);
    CHECK(r.b() == false);
    CHECK(r.str() == "hello");
    CHECK(r.str() == std::string("a\0b", 3));
    auto b = r.bytes(sizeof(raw));
    REQUIRE(b.size() == sizeof(raw));
    CHECK(b[0] == 0x00);
    CHECK(b[1] == 0x01);
    CHECK(b[2] == '\xFE');
    CHECK(b[3] == '\xFF');
    CHECK(r.remaining() == 0);
    CHECK(r.ok());
    CHECK(r.pos() == w.size());
}

TEST_CASE("network.NetReaderOverrunIsSticky") {
    eve::network::NetWriter w;
    w.writeU32(7);
    eve::network::NetReader r(w.data(), w.size());
    REQUIRE(r.ok());
    CHECK(r.u32() == 7);
    CHECK(r.u16() == 0);  // overrun
    CHECK(!r.ok());
    CHECK(r.u8() == 0);   // safe default after failure
    CHECK(r.remaining() == 0);
    CHECK(r.str().empty());

    eve::network::NetReader bad(nullptr, 4);
    CHECK(!bad.ok());

    eve::network::NetWriter w2;
    w2.writeString("abc");
    eve::network::NetReader r2(w2.data(), w2.size());
    CHECK(r2.str() == "abc");
    CHECK(r2.u32() == 0);  // overrun after payload
    CHECK(!r2.ok());
}

TEST_CASE("network.TcpLargeSendFlushesFully") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39351;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool clientOk = false;
    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);

    const size_t total = 2 * 1024 * 1024;  // 2 MiB, larger than any single send buffer
    std::string payload(total, 'x');
    for (size_t i = 0; i < total; ++i) payload[i] = static_cast<char>('a' + (i % 26));
    size_t got = 0;
    bool mismatch = false;

    for (size_t off = 0; off < total; off += 64 * 1024) {
        // Respect the 1 MiB send-buffer cap: wait for the worker to drain.
        for (int stall = 0; stall < 2000; ++stall) {
            if (client->pendingSendBytes() <= 128 * 1024) break;
            net->pump();
            while (auto m = ev->pollOwned()) {
                if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer &&
                    m->args[1].p) {
                    auto* d = static_cast<eve::data::ByteData*>(m->args[1].p);
                    const char* p = static_cast<const char*>(d->getData());
                    for (size_t j = 0; j < d->getSize(); ++j) {
                        if (p[j] != payload[got + j]) mismatch = true;
                    }
                    got += d->getSize();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        eve::data::ByteData chunk(payload.data() + off, 64 * 1024);
        REQUIRE(client->send(&chunk));
    }

    for (int i = 0; i < 2000 && got < total && !mismatch; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer &&
                m->args[1].p) {
                auto* d = static_cast<eve::data::ByteData*>(m->args[1].p);
                const char* p = static_cast<const char*>(d->getData());
                for (size_t j = 0; j < d->getSize(); ++j) {
                    if (p[j] != payload[got + j]) mismatch = true;
                }
                got += d->getSize();
            }
        }
        if (got < total) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!mismatch);
    CHECK(got == total);
    CHECK(client->pendingSendBytes() == 0);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.ChannelLargeMessageReassembles") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::event::Event::create();
    const uint16_t port = 39361;
    auto* server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {}
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chSend = net->newChannel(client);
    auto* chRecv = net->newChannel(peer);
    REQUIRE(chRecv != nullptr);

    const size_t bigSize = 512 * 1024;
    std::string big(bigSize, 'z');
    REQUIRE(chSend->sendMsgString(big));

    std::string got;
    for (int i = 0; i < 4000 && got.size() < bigSize; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "chmsg" && m->args.size() >= 2 && m->args[1].p) {
                auto* d = static_cast<eve::data::ByteData*>(m->args[1].p);
                got.assign(static_cast<const char*>(d->getData()), d->getSize());
            }
        }
        if (got.size() < bigSize) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got == big);

    delete chSend;
    delete chRecv;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.UdpReliableUnderLoss") {
    auto* net = eve::network::Network::create();
    auto* a = net->newUdp();
    auto* b = net->newUdp();
    REQUIRE(a->bind(39670));
    REQUIRE(b->bind(39671));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39671));
    REQUIRE(linkB->setRemote("127.0.0.1", 39670));
    linkA->setLossRate(0.25f);
    linkB->setLossRate(0.25f);

    std::vector<std::string> received;
    linkB->setMessageHandler(
        [&received](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) {
            received.emplace_back(d, n);
        });

    const int count = 40;
    for (int i = 0; i < count; ++i) {
        std::string msg = "m" + std::to_string(i) +
                          std::string(64 - std::to_string(i).size(), 'x');
        linkA->sendString(eve::network::UdpLink::MsgType::Reliable, 0, msg);
    }
    for (int i = 0; i < 2400 && received.size() < static_cast<size_t>(count); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 600 && linkA->pendingReliable() > 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(received.size() == static_cast<size_t>(count));
    CHECK(linkA->pendingReliable() == 0);
    bool inOrder = received.size() == static_cast<size_t>(count);
    for (int i = 0; i < count && inOrder; ++i) {
        std::string expect = "m" + std::to_string(i) +
                             std::string(64 - std::to_string(i).size(), 'x');
        if (received[static_cast<size_t>(i)] != expect) inOrder = false;
    }
    CHECK(inOrder);
}

TEST_CASE("network.UdpFragmentReassembly") {
    auto* net = eve::network::Network::create();
    auto* a = net->newUdp();
    auto* b = net->newUdp();
    REQUIRE(a->bind(39672));
    REQUIRE(b->bind(39673));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39673));
    REQUIRE(linkB->setRemote("127.0.0.1", 39672));
    linkA->setLossRate(0.1f);
    linkB->setLossRate(0.1f);

    std::string big(48 * 1024, 'q');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + (i % 26));
    std::string got;
    linkB->setMessageHandler(
        [&got](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) {
            got.assign(d, n);
        });

    linkA->sendString(eve::network::UdpLink::MsgType::Reliable, 0, big);
    for (int i = 0; i < 2400 && got.size() < big.size(); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 600 && linkA->pendingReliable() > 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got == big);
    CHECK(linkA->pendingReliable() == 0);
}

TEST_CASE("network.UdpUnreliableOrdered") {
    auto* net = eve::network::Network::create();
    auto* a = net->newUdp();
    auto* b = net->newUdp();
    REQUIRE(a->bind(39674));
    REQUIRE(b->bind(39675));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39675));
    REQUIRE(linkB->setRemote("127.0.0.1", 39674));

    std::vector<std::string> received;
    linkB->setMessageHandler(
        [&received](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) {
            received.emplace_back(d, n);
        });
    const int count = 12;
    for (int i = 0; i < count; ++i) {
        linkA->sendString(eve::network::UdpLink::MsgType::UnreliableOrdered, 1,
                          "o" + std::to_string(i));
    }
    for (int i = 0; i < 400 && received.size() < static_cast<size_t>(count); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(received.size() == static_cast<size_t>(count));
    bool inOrder = received.size() == static_cast<size_t>(count);
    for (int i = 0; i < count && inOrder; ++i) {
        if (received[static_cast<size_t>(i)] != "o" + std::to_string(i)) inOrder = false;
    }
    CHECK(inOrder);
}

TEST_CASE("network.UdpRpcRoundtrip") {
    auto* net = eve::network::Network::create();
    auto* a = net->newUdp();
    auto* b = net->newUdp();
    REQUIRE(a->bind(39676));
    REQUIRE(b->bind(39677));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39677));
    REQUIRE(linkB->setRemote("127.0.0.1", 39676));
    auto* rpcA = net->newRpc(linkA);
    auto* rpcB = net->newRpc(linkB);
    REQUIRE(rpcA != nullptr);
    REQUIRE(rpcB != nullptr);

    std::string gotB, gotA;
    rpcB->registerHandler(7, [&gotB](eve::network::NetReader& r) { gotB = r.str(); });
    rpcA->registerHandler(8, [&gotA](eve::network::NetReader& r) { gotA = r.str(); });

    eve::network::NetWriter w;
    w.writeString("hello-from-a");
    rpcA->call(7, w.data(), w.size(), true);
    for (int i = 0; i < 600 && (gotB.empty() || gotA.empty()); ++i) {
        net->pump();
        if (!gotB.empty() && gotA.empty()) {
            eve::network::NetWriter w2;
            w2.writeString("reply-from-b");
            rpcB->call(8, w2.data(), w2.size(), true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(gotB == "hello-from-a");
    CHECK(gotA == "reply-from-b");
}

TEST_CASE("network.NetHostPeers") {
    auto* net = eve::network::Network::create();
    auto* host = net->newHost();
    REQUIRE(host->start(39680));
    host->setTimeoutMs(500);

    std::string hostGot, clientGot;
    int connectedPeer = -1;
    int disconnectedPeer = -1;
    host->setMessageHandler(
        [&hostGot](uint32_t, eve::network::UdpLink::MsgType, uint8_t, const char* d,
                   size_t n) { hostGot.assign(d, n); });
    host->setPeerConnectedHandler([&connectedPeer](uint32_t id) {
        connectedPeer = static_cast<int>(id);
    });
    host->setPeerDisconnectedHandler([&disconnectedPeer](uint32_t id) {
        disconnectedPeer = static_cast<int>(id);
    });

    auto* csock = net->newUdp();
    REQUIRE(csock->connect("127.0.0.1", 39680));
    auto* clink = net->newUdpLink(csock);
    REQUIRE(clink->setRemote("127.0.0.1", 39680));
    clink->setTimeoutMs(500);
    clink->setMessageHandler(
        [&clientGot](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) {
            clientGot.assign(d, n);
        });

    clink->sendString(eve::network::UdpLink::MsgType::Reliable, 0, "ping");
    for (int i = 0; i < 600 && (hostGot.empty() || connectedPeer < 0); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(hostGot == "ping");
    CHECK(connectedPeer == 1);
    CHECK(host->peerCount() == 1);

    host->sendStringTo(1, eve::network::UdpLink::MsgType::Reliable, 0, "pong");
    for (int i = 0; i < 600 && clientGot.empty(); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(clientGot == "pong");

    clink->setLossRate(1.0f);
    for (int i = 0; i < 600 && disconnectedPeer < 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(disconnectedPeer == 1);
    CHECK(host->peerCount() == 0);
}
