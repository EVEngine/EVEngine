#include "network/Network.h"
#include "network/NetWorker.h"
#include "network/TcpSocket.h"
#include "network/UdpSocket.h"
#include "network/HttpRequest.h"
#include "network/Channel.h"
#include "network/Session.h"
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
#include <functional>

namespace eve::network {

Module_IMPL(Network, new Network());

Network::Network() {
    worker_ = std::make_unique<NetWorker>(this);
    worker_->start();
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

Channel* Network::newChannel(TcpSocket* socket) {
    if (!socket) return nullptr;
    auto* ch = new Channel(socket);
    bindChannel(socket, ch);
    return ch;
}

Session* Network::newSession() {
    return new Session();
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
        try {
            char buf[64 * 1024];
            Poco::Net::SocketAddress sender;
            int n = sock->datagram()->receiveFrom(buf, sizeof(buf), sender);
            if (n > 0) {
                NetCompletion c;
                c.type   = NetEvType::Data;
                c.kind   = NetKind::Udp;
                c.handle = sock;
                c.bytes  = std::make_shared<std::vector<char>>(buf, buf + n);
                c.peer   = sender.toString();
                post(std::move(c));
            }
        } catch (const Poco::Exception&) {
        } catch (...) {
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
    for (auto& c : batch) emitCompletion(c);
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
}

void Network::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Network::getName);
    cls.addFunc("newTcp", &Network::newTcp);
    cls.addFunc("newUdp", &Network::newUdp);
    cls.addFunc("newHttp", &Network::newHttp);
    cls.addFunc("newChannel", &Network::newChannel);
    cls.addFunc("newSession", &Network::newSession);
    cls.addFunc("pump", &Network::pump);
    cls.addFunc("setTimeout", &Network::setTimeout);
    cls.addFunc("setVerifySsl", &Network::setVerifySsl);
}

}  // namespace eve::network
