#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "event/Event.h"
#include "network/Network.h"
#include "network/TcpSocket.h"
#include "network/UdpSocket.h"
#include "network/HttpRequest.h"
#include "network/Channel.h"
#include "network/Session.h"
#include "data/ByteData.h"

#include <thread>
#include <chrono>
#include <cstring>

using eve::event::Event;
using eve::event::Message;
using eve::event::Variant;

TEST_CASE("event.VariantMessage") {
    auto* ev = eve::event::Event::create();
    std::vector<Variant> args;
    args.push_back(Variant::makeInt(200));
    args.push_back(Variant::makeString("ok"));
    args.push_back(Variant::makePtr(nullptr));
    ev->push(new Message("httpresp", args));
    Message* m = ev->poll();
    REQUIRE(m != nullptr);
    CHECK(m->name == "httpresp");
    REQUIRE(m->args.size() == 3);
    CHECK(static_cast<int>(m->args[0].type) == static_cast<int>(Variant::Type::Int));
    CHECK(m->args[0].i == 200);
    CHECK(m->args[1].s == "ok");
    delete m;
}

TEST_CASE("network.NetWorkerQueue") {
    auto* net = eve::network::Network::create();
    eve::network::NetCompletion c;
    c.type   = eve::network::NetEvType::Err;
    c.reason = "timeout";
    net->post(std::move(c));
    std::vector<eve::network::NetCompletion> out;
    net->drainForTest(out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].reason == "timeout");
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
        while (auto* m = ev->poll()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
            delete m;
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
        while (auto* m = ev->poll()) {
            if (m->name == "netdata") {
                got = true;
                if (m->args.size() >= 2 && m->args[1].p) {
                    delete static_cast<eve::data::ByteData*>(m->args[1].p);
                }
            }
            delete m;
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
        while (auto* m = ev->poll()) {
            if (m->name == "netdata" && m->args.size() >= 1 && m->args[0].p == b) {
                got = true;
                if (m->args.size() >= 2 && m->args[1].p)
                    delete static_cast<eve::data::ByteData*>(m->args[1].p);
            }
            delete m;
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
        while (auto* m = ev->poll()) { delete m; }
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
        while (auto* m = ev->poll()) {
            if (m->name == "httpresp" && m->args.size() >= 2) {
                got = true;
                status = static_cast<int>(m->args[1].i);
                if (m->args.size() >= 3 && m->args[2].p)
                    delete static_cast<eve::data::ByteData*>(m->args[2].p);
            }
            delete m;
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
        while (auto* m = ev->poll()) { delete m; }
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
        while (auto* m = ev->poll()) {
            if (m->name == "chmsg") {
                ++msgs;
                if (m->args.size() >= 2 && m->args[1].p)
                    delete static_cast<eve::data::ByteData*>(m->args[1].p);
            }
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[1].p)
                delete static_cast<eve::data::ByteData*>(m->args[1].p);
            delete m;
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
