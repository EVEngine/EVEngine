#include "network/Network.h"
#include "network/NetWorker.h"
#include "network/TcpSocket.h"
#include "network/UdpSocket.h"
#include "network/HttpRequest.h"
#include "network/Channel.h"
#include "network/Session.h"
#include "network/NetStream.h"
#include "network/UdpLink.h"
#include "network/NetHost.h"
#include "network/NetRpc.h"
#include "data/ByteData.h"
#include "event/Event.h"

#include <Poco/Net/StreamSocket.h>
#include <Poco/Net/ServerSocket.h>
#include <Poco/Net/DatagramSocket.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Net/NetException.h>
#include <Poco/Exception.h>

#include <simplesquirrel/simplesquirrel.hpp>
#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>

namespace eve::network {

namespace {

void callScript1(const ssq::Object& obj, const std::string& s) {
    if (obj.isEmpty()) return;
    ssq::Function f = obj.toFunction();
    if (f.isEmpty()) return;
    HSQUIRRELVM raw = f.getHandle();
    SQInteger top = sq_gettop(raw);
    sq_pushobject(raw, f.getRaw());
    sq_pushroottable(raw);
    ssq::detail::pushValue(raw, s);
    sq_call(raw, 2, SQFalse, SQTrue);
    sq_settop(raw, top);
}

void callScript2(const ssq::Object& obj, int64_t a, const std::string& s) {
    if (obj.isEmpty()) return;
    ssq::Function f = obj.toFunction();
    if (f.isEmpty()) return;
    HSQUIRRELVM raw = f.getHandle();
    SQInteger top = sq_gettop(raw);
    sq_pushobject(raw, f.getRaw());
    sq_pushroottable(raw);
    ssq::detail::pushValue(raw, a);
    ssq::detail::pushValue(raw, s);
    sq_call(raw, 3, SQFalse, SQTrue);
    sq_settop(raw, top);
}

void callScript3(const ssq::Object& obj, int64_t a, int64_t b, const std::string& s) {
    if (obj.isEmpty()) return;
    ssq::Function f = obj.toFunction();
    if (f.isEmpty()) return;
    HSQUIRRELVM raw = f.getHandle();
    SQInteger top = sq_gettop(raw);
    sq_pushobject(raw, f.getRaw());
    sq_pushroottable(raw);
    ssq::detail::pushValue(raw, a);
    ssq::detail::pushValue(raw, b);
    ssq::detail::pushValue(raw, s);
    sq_call(raw, 4, SQFalse, SQTrue);
    sq_settop(raw, top);
}

}  // namespace

Module_IMPL(Network, new Network());

Network::Network() {
    worker_ = std::make_unique<NetWorker>(this);
    worker_->start();
    eve::cap::provide<eve::service::INetwork>(this);
}

Network::~Network() {
    if (worker_) worker_->stop();
    worker_.reset();
}

TcpSocket* Network::newTcp() {
    return new TcpSocket(this);
}

UdpSocket* Network::newUdp() {
    return new UdpSocket(this);
}

HttpRequest* Network::newHttp(std::string method, std::string url) {
    return new HttpRequest(this, std::move(method), std::move(url));
}

bool Network::httpRequest(const std::string& method, const std::string& url,
                          const std::string& body, int timeoutMs, int& status,
                          std::string& responseBody) {
    HttpRequest* req = newHttp(method, url);
    if (!req) return false;
    if (!body.empty()) req->setBodyString(body);
    req->setTimeout(timeoutMs > 0 ? timeoutMs : 10000);
    if (!req->submit()) return false;

    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 1);
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<NetCompletion> out;
        drainForTest(out);
        for (const auto& c : out) {
            if (c.handle != req) continue;
            if (c.type == NetEvType::HttpResp) {
                status = c.status;
                if (c.bytes) responseBody.assign(c.bytes->begin(), c.bytes->end());
                return true;
            }
            if (c.type == NetEvType::Err) return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

Channel* Network::newChannel(TcpSocket* socket) {
    if (!socket) return nullptr;
    auto* ch = new Channel(socket);
    bindChannel(socket, ch);
    return ch;
}

Session* Network::newSession() {
    return new Session();
}

NetWriter* Network::newWriter() {
    return new NetWriter();
}

NetReader* Network::newReader(std::string bytes) {
    auto* r = new NetReader();
    r->setBytes(bytes);
    return r;
}

UdpLink* Network::newUdpLink(UdpSocket* socket) {
    if (!socket) return nullptr;
    auto* link = new UdpLink(this, socket);
    bindUdpLink(socket, link);
    return link;
}

NetRpc* Network::newRpc(UdpLink* link) {
    return link ? new NetRpc(link) : nullptr;
}

NetHost* Network::newHost() {
    return new NetHost(this);
}

void Network::bindUdpLink(UdpSocket* sock, UdpLink* link) {
    if (sock) udpLinks_[sock] = link;
}

void Network::unbindUdpLink(UdpSocket* sock) {
    if (sock) udpLinks_.erase(sock);
}

void Network::bindUdpHost(UdpSocket* sock, NetHost* host) {
    if (sock) udpHosts_[sock] = host;
}

void Network::unbindUdpHost(UdpSocket* sock) {
    if (sock) udpHosts_.erase(sock);
}

void Network::setTimeout(int ms) {
    timeoutMs_ = ms;
}

int Network::getTimeout() const {
    return timeoutMs_;
}

void Network::setVerifySsl(bool verify) {
    verifySsl_ = verify;
}

bool Network::getVerifySsl() const {
    return verifySsl_;
}

void Network::post(NetCompletion c) {
    if (worker_) worker_->post(std::move(c));
}

void Network::drainForTest(std::vector<NetCompletion>& out) {
    if (worker_) worker_->drain(out);
}

void Network::watchTcp(TcpSocket* sock) {
    std::lock_guard<std::mutex> lock(watchMu_);
    if (std::find(watchedTcp_.begin(), watchedTcp_.end(), sock) == watchedTcp_.end())
        watchedTcp_.push_back(sock);
}

void Network::unwatchTcp(TcpSocket* sock) {
    std::lock_guard<std::mutex> lock(watchMu_);
    watchedTcp_.erase(std::remove(watchedTcp_.begin(), watchedTcp_.end(), sock), watchedTcp_.end());
}

void Network::watchUdp(UdpSocket* sock) {
    std::lock_guard<std::mutex> lock(watchMu_);
    if (std::find(watchedUdp_.begin(), watchedUdp_.end(), sock) == watchedUdp_.end())
        watchedUdp_.push_back(sock);
}

void Network::unwatchUdp(UdpSocket* sock) {
    std::lock_guard<std::mutex> lock(watchMu_);
    watchedUdp_.erase(std::remove(watchedUdp_.begin(), watchedUdp_.end(), sock), watchedUdp_.end());
}

void Network::bindChannel(TcpSocket* sock, Channel* ch) {
    std::lock_guard<std::mutex> lock(channelMu_);
    channels_[sock] = ch;
}

void Network::unbindChannel(TcpSocket* sock) {
    std::lock_guard<std::mutex> lock(channelMu_);
    channels_.erase(sock);
}

Channel* Network::channelFor(TcpSocket* sock) const {
    std::lock_guard<std::mutex> lock(channelMu_);
    auto it = channels_.find(sock);
    return it == channels_.end() ? nullptr : it->second;
}

void Network::pollSockets() {
    std::vector<TcpSocket*> tcpCopy;
    std::vector<UdpSocket*> udpCopy;
    {
        std::lock_guard<std::mutex> lock(watchMu_);
        tcpCopy = watchedTcp_;
        udpCopy = watchedUdp_;
    }
    for (TcpSocket* sock : tcpCopy) {
        if (!sock) continue;
        if (sock->isListening() && sock->server()) {
            try {
                Poco::Net::StreamSocket ss = sock->server()->acceptConnection();
                ss.setBlocking(false);
                auto peer = std::make_unique<TcpSocket>(this);
                auto stream = std::make_unique<Poco::Net::StreamSocket>(ss);
                peer->setConnectedSocket(std::move(stream));
                TcpSocket* raw = peer.get();
                sock->pushAccepted(std::move(peer));
                watchTcp(raw);
                NetCompletion c;
                c.type   = NetEvType::Conn;
                c.kind   = NetKind::Tcp;
                c.handle = raw;
                c.reason = "ok";
                c.peer   = raw->getPeer();
                post(std::move(c));
            } catch (...) {
            }
        }
        if (sock->isConnected() && sock->stream()) {
            sock->flushSend();
            try {
                char buf[64 * 1024];
                int n = sock->stream()->receiveBytes(buf, sizeof(buf));
                if (n > 0) {
                    NetCompletion c;
                    c.type   = NetEvType::Data;
                    c.kind   = NetKind::Tcp;
                    c.handle = sock;
                    c.bytes  = std::make_shared<std::vector<char>>(buf, buf + n);
                    c.peer   = sock->getPeer();
                    post(std::move(c));
                } else if (n == 0) {
                    NetCompletion c;
                    c.type   = NetEvType::Err;
                    c.kind   = NetKind::Tcp;
                    c.handle = sock;
                    c.reason = "closed";
                    post(std::move(c));
                    unwatchTcp(sock);
                }
            } catch (const Poco::Exception&) {
            } catch (...) {
            }
        }
    }
    for (UdpSocket* sock : udpCopy) {
        if (!sock || !sock->datagram()) continue;
        for (int i = 0; i < 64; ++i) {
            try {
                char buf[64 * 1024];
                Poco::Net::SocketAddress sender;
                int n = sock->datagram()->receiveFrom(buf, sizeof(buf), sender);
                if (n <= 0) break;  // would-block
                NetCompletion c;
                c.type   = NetEvType::Data;
                c.kind   = NetKind::Udp;
                c.handle = sock;
                c.bytes  = std::make_shared<std::vector<char>>(buf, buf + n);
                c.peer   = sender.toString();
                post(std::move(c));
            } catch (const Poco::Exception&) {
                break;
            } catch (...) {
                break;
            }
        }
    }
}

void Network::emitCompletion(const NetCompletion& c) {
    auto* ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
    if (!ev) return;

    using eve::event::Variant;
    using eve::event::Message;

    std::vector<Variant> args;
    switch (c.type) {
    case NetEvType::Conn:
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makeString(c.reason.empty() ? "ok" : c.reason));
        ev->push(new Message("netconn", args));
        break;
    case NetEvType::Data: {
        Channel* ch = channelFor(static_cast<TcpSocket*>(c.handle));
        if (ch && c.bytes) {
            ch->feed(*c.bytes);
            return;
        }
        eve::data::ByteData* bd = nullptr;
        if (c.bytes && !c.bytes->empty())
            bd = new eve::data::ByteData(c.bytes->data(), c.bytes->size());
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makePtr(bd));
        args.push_back(Variant::makeString(c.peer));
        ev->push(new Message("netdata", args));
        break;
    }
    case NetEvType::Err: {
        Channel* ch = channelFor(static_cast<TcpSocket*>(c.handle));
        if (ch) {
            args.push_back(Variant::makePtr(ch));
            args.push_back(Variant::makeString(c.reason));
            ev->push(new Message("chclose", args));
        }
        args.clear();
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makeString(c.reason));
        ev->push(new Message("neterr", args));
        break;
    }
    case NetEvType::HttpResp: {
        eve::data::ByteData* bd = nullptr;
        if (c.bytes && !c.bytes->empty())
            bd = new eve::data::ByteData(c.bytes->data(), c.bytes->size());
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makeInt(c.status));
        args.push_back(Variant::makePtr(bd));
        ev->push(new Message("httpresp", args));
        break;
    }
    case NetEvType::ChMsg: {
        eve::data::ByteData* bd = nullptr;
        if (c.bytes && !c.bytes->empty())
            bd = new eve::data::ByteData(c.bytes->data(), c.bytes->size());
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makePtr(bd));
        ev->push(new Message("chmsg", args));
        break;
    }
    case NetEvType::ChClose:
        args.push_back(Variant::makePtr(c.handle));
        args.push_back(Variant::makeString(c.reason));
        ev->push(new Message("chclose", args));
        break;
    }
}

void Network::pump() {
    std::vector<NetCompletion> batch;
    if (worker_) worker_->drain(batch);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
    for (auto& c : batch) {
        if (c.type == NetEvType::Data && c.kind == NetKind::Udp) {
            auto* sock = static_cast<UdpSocket*>(c.handle);
            auto hit = udpHosts_.find(sock);
            if (hit != udpHosts_.end()) {
                if (c.bytes) hit->second->onDatagram(*c.bytes, c.peer);
                continue;
            }
            auto lit = udpLinks_.find(sock);
            if (lit != udpLinks_.end()) {
                if (c.bytes) lit->second->onDatagram(*c.bytes, c.peer);
                continue;
            }
        }
        emitCompletion(c);
    }
    for (auto& kv : udpHosts_) kv.second->pump(now);
    for (auto& kv : udpLinks_) kv.second->pump(now);
}

void Network::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Network::create, false);
    expose(cls);

    auto tcp = table.addClass<TcpSocket>(
        "TcpSocket", std::function<TcpSocket*()>([]() { return new TcpSocket(Network::create()); }), true);
    tcp.addFunc("connect", &TcpSocket::connect);
    tcp.addFunc("listen", &TcpSocket::listen);
    tcp.addFunc("accept", &TcpSocket::accept);
    tcp.addFunc("send", &TcpSocket::send);
    tcp.addFunc("sendString", &TcpSocket::sendString);
    tcp.addFunc("close", &TcpSocket::close);
    tcp.addFunc("isConnected", &TcpSocket::isConnected);
    tcp.addFunc("getPeer", &TcpSocket::getPeer);

    auto udp = table.addClass<UdpSocket>(
        "UdpSocket", std::function<UdpSocket*()>([]() { return new UdpSocket(Network::create()); }), true);
    udp.addFunc("bind", &UdpSocket::bind);
    udp.addFunc("connect", &UdpSocket::connect);
    udp.addFunc("sendTo", &UdpSocket::sendTo);
    udp.addFunc("sendToString", &UdpSocket::sendToString);
    udp.addFunc("send", &UdpSocket::send);
    udp.addFunc("sendString", &UdpSocket::sendString);
    udp.addFunc("close", &UdpSocket::close);

    auto http = table.addClass<HttpRequest>(
        "HttpRequest",
        std::function<HttpRequest*()>(
            []() { return new HttpRequest(Network::create(), "GET", "http://127.0.0.1/"); }),
        true);
    http.addFunc("setHeader", &HttpRequest::setHeader);
    http.addFunc("setBody", &HttpRequest::setBody);
    http.addFunc("setBodyString", &HttpRequest::setBodyString);
    http.addFunc("setTimeout", &HttpRequest::setTimeout);
    http.addFunc("setVerifySsl", &HttpRequest::setVerifySsl);
    http.addFunc("submit", &HttpRequest::submit);

    auto ch = table.addClass<Channel>(
        "Channel", std::function<Channel*()>([]() { return new Channel(nullptr); }), true);
    ch.addFunc("sendMsg", &Channel::sendMsg);
    ch.addFunc("sendMsgString", &Channel::sendMsgString);
    ch.addFunc("getSocket", &Channel::getSocket);

    auto sess = table.addClass<Session>(
        "Session", std::function<Session*()>([]() { return new Session(); }), true);
    sess.addFunc("add", &Session::add);
    sess.addFunc("get", &Session::get);
    sess.addFunc("remove", &Session::remove);
    sess.addFunc("closeAll", &Session::closeAll);

    auto writer = table.addClass<NetWriter>(
        "NetWriter", std::function<NetWriter*()>([]() { return new NetWriter(); }), true);
    writer.addFunc("writeU8", [](NetWriter* w, int64_t v) { w->writeU8(static_cast<uint8_t>(v)); });
    writer.addFunc("writeI8", [](NetWriter* w, int64_t v) { w->writeI8(static_cast<int8_t>(v)); });
    writer.addFunc("writeU16", [](NetWriter* w, int64_t v) { w->writeU16(static_cast<uint16_t>(v)); });
    writer.addFunc("writeI16", [](NetWriter* w, int64_t v) { w->writeI16(static_cast<int16_t>(v)); });
    writer.addFunc("writeU32", [](NetWriter* w, int64_t v) { w->writeU32(static_cast<uint32_t>(v)); });
    writer.addFunc("writeI32", [](NetWriter* w, int64_t v) { w->writeI32(static_cast<int32_t>(v)); });
    writer.addFunc("writeU64", [](NetWriter* w, int64_t v) { w->writeU64(static_cast<uint64_t>(v)); });
    writer.addFunc("writeI64", [](NetWriter* w, int64_t v) { w->writeI64(static_cast<int64_t>(v)); });
    writer.addFunc("writeF32", &NetWriter::writeF32);
    writer.addFunc("writeF64", &NetWriter::writeF64);
    writer.addFunc("writeBool", &NetWriter::writeBool);
    writer.addFunc("writeString", &NetWriter::writeString);
    writer.addFunc("writeBytes", [](NetWriter* w, eve::data::ByteData* d) {
        if (d) w->writeBytes(d->getData(), d->getSize());
    });
    writer.addFunc("toString", [](NetWriter* w) { return w->toString(); });
    writer.addFunc("size", [](NetWriter* w) { return static_cast<int64_t>(w->size()); });

    auto reader = table.addClass<NetReader>(
        "NetReader", std::function<NetReader*()>([]() { return new NetReader(); }), true);
    reader.addFunc("init", [](NetReader* r, eve::data::ByteData* d) {
        return d ? r->init(d->getData(), d->getSize()) : false;
    });
    reader.addFunc("initString", [](NetReader* r, const std::string& s) { return r->setBytes(s); });
    reader.addFunc("u8", [](NetReader* r) { return static_cast<int64_t>(r->u8()); });
    reader.addFunc("i8", [](NetReader* r) { return static_cast<int64_t>(r->i8()); });
    reader.addFunc("u16", [](NetReader* r) { return static_cast<int64_t>(r->u16()); });
    reader.addFunc("i16", [](NetReader* r) { return static_cast<int64_t>(r->i16()); });
    reader.addFunc("u32", [](NetReader* r) { return static_cast<int64_t>(r->u32()); });
    reader.addFunc("i32", [](NetReader* r) { return static_cast<int64_t>(r->i32()); });
    reader.addFunc("u64", [](NetReader* r) { return static_cast<int64_t>(r->u64()); });
    reader.addFunc("i64", [](NetReader* r) { return static_cast<int64_t>(r->i64()); });
    reader.addFunc("f32", &NetReader::f32);
    reader.addFunc("f64", &NetReader::f64);
    reader.addFunc("bool", &NetReader::b);
    reader.addFunc("str", [](NetReader* r) { return r->str(); });
    reader.addFunc("bytes", [](NetReader* r, int64_t n) {
        auto v = r->bytes(static_cast<size_t>(n));
        return std::string(v.data(), v.size());
    });
    reader.addFunc("remaining", [](NetReader* r) { return static_cast<int64_t>(r->remaining()); });
    reader.addFunc("pos", [](NetReader* r) { return static_cast<int64_t>(r->pos()); });
    reader.addFunc("ok", [](NetReader* r) { return r->ok(); });

    auto link = table.addClass<UdpLink>(
        "UdpLink",
        std::function<UdpLink*()>([]() { return new UdpLink(nullptr, nullptr); }), true);
    link.addFunc("setRemote", &UdpLink::setRemote);
    link.addFunc("setRemoteString", &UdpLink::setRemoteString);
    link.addFunc("sendReliable", [](UdpLink* l, int64_t ch, const std::string& s) {
        l->sendString(UdpLink::MsgType::Reliable, static_cast<uint8_t>(ch), s);
    });
    link.addFunc("sendUnreliable", [](UdpLink* l, int64_t ch, const std::string& s) {
        l->sendString(UdpLink::MsgType::Unreliable, static_cast<uint8_t>(ch), s);
    });
    link.addFunc("sendOrdered", [](UdpLink* l, int64_t ch, const std::string& s) {
        l->sendString(UdpLink::MsgType::UnreliableOrdered, static_cast<uint8_t>(ch), s);
    });
    link.addFunc("onMessage", [](UdpLink* l, ssq::Object fn) {
        if (!l) return;
        l->setMessageHandler(
            [fn](UdpLink::MsgType, uint8_t ch, const char* d, size_t n) {
                callScript2(fn, ch, std::string(d, n));
            });
    });
    link.addFunc("setLossRate", &UdpLink::setLossRate);
    link.addFunc("setTimeoutMs", &UdpLink::setTimeoutMs);
    link.addFunc("isAlive", [](UdpLink* l) { return l && l->isAlive(); });
    link.addFunc("peer", &UdpLink::peer);
    link.addFunc("peerId", [](UdpLink* l) { return static_cast<int64_t>(l->peerId()); });
    link.addFunc("pendingReliable",
                 [](UdpLink* l) { return static_cast<int64_t>(l->pendingReliable()); });
    link.addFunc("pendingFragments",
                 [](UdpLink* l) { return static_cast<int64_t>(l->pendingFragments()); });

    auto rpc = table.addClass<NetRpc>(
        "NetRpc", std::function<NetRpc*()>([]() { return new NetRpc(nullptr); }), true);
    rpc.addFunc("callRpc", [](NetRpc* r, int64_t msgId, const std::string& payload,
                              bool reliable) {
        r->callString(static_cast<uint16_t>(msgId), payload, reliable);
    });
    rpc.addFunc("registerRpc", [](NetRpc* r, int64_t msgId, ssq::Object fn) {
        r->registerScript(static_cast<uint16_t>(msgId), fn);
    });

    auto host = table.addClass<NetHost>(
        "NetHost", std::function<NetHost*()>([]() { return new NetHost(nullptr); }), true);
    host.addFunc("start", &NetHost::start);
    host.addFunc("onMessage", [](NetHost* h, ssq::Object fn) {
        if (!h) return;
        h->setMessageHandler(
            [fn](uint32_t peerId, UdpLink::MsgType, uint8_t ch, const char* d, size_t n) {
                callScript3(fn, peerId, ch, std::string(d, n));
            });
    });
    host.addFunc("onPeerConnected", [](NetHost* h, ssq::Object fn) {
        if (!h) return;
        h->setPeerConnectedHandler([fn](uint32_t id) { callScript2(fn, id, ""); });
    });
    host.addFunc("onPeerDisconnected", [](NetHost* h, ssq::Object fn) {
        if (!h) return;
        h->setPeerDisconnectedHandler([fn](uint32_t id) { callScript2(fn, id, ""); });
    });
    host.addFunc("sendReliable", [](NetHost* h, int64_t peerId, int64_t ch,
                                    const std::string& s) {
        h->sendStringTo(static_cast<uint32_t>(peerId), UdpLink::MsgType::Reliable,
                        static_cast<uint8_t>(ch), s);
    });
    host.addFunc("sendUnreliable", [](NetHost* h, int64_t peerId, int64_t ch,
                                      const std::string& s) {
        h->sendStringTo(static_cast<uint32_t>(peerId), UdpLink::MsgType::Unreliable,
                        static_cast<uint8_t>(ch), s);
    });
    host.addFunc("sendOrdered", [](NetHost* h, int64_t peerId, int64_t ch,
                                   const std::string& s) {
        h->sendStringTo(static_cast<uint32_t>(peerId), UdpLink::MsgType::UnreliableOrdered,
                        static_cast<uint8_t>(ch), s);
    });
    host.addFunc("link", &NetHost::linkByPeerId);
    host.addFunc("peerCount", [](NetHost* h) { return static_cast<int64_t>(h->peerCount()); });
    host.addFunc("setLossRate", &NetHost::setLossRate);
    host.addFunc("setTimeoutMs", &NetHost::setTimeoutMs);
}

void Network::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Network::getName);
    cls.addFunc("newTcp", &Network::newTcp);
    cls.addFunc("newUdp", &Network::newUdp);
    cls.addFunc("newHttp", &Network::newHttp);
    cls.addFunc("newChannel", &Network::newChannel);
    cls.addFunc("newSession", &Network::newSession);
    cls.addFunc("newWriter", &Network::newWriter);
    cls.addFunc("newReader", &Network::newReader);
    cls.addFunc("newUdpLink", &Network::newUdpLink);
    cls.addFunc("newRpc", &Network::newRpc);
    cls.addFunc("newHost", &Network::newHost);
    cls.addFunc("pump", &Network::pump);
    cls.addFunc("setTimeout", &Network::setTimeout);
    cls.addFunc("setVerifySsl", &Network::setVerifySsl);
}

}  // namespace eve::network
