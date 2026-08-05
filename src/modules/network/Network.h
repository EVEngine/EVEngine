#pragma once

#include "common/Module.h"
#include "NetTypes.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::network {

class TcpSocket;
class UdpSocket;
class HttpRequest;
class Channel;
class Session;
class NetWorker;

class Network : public Module {
public:
    Module_REG(Network);
    Network();
    ~Network() override;

    TcpSocket*   newTcp();
    UdpSocket*   newUdp();
    HttpRequest* newHttp(std::string method, std::string url);
    Channel*     newChannel(TcpSocket* socket);
    Session*     newSession();

    void setTimeout(int ms);
    int  getTimeout() const;
    void setVerifySsl(bool verify);
    bool getVerifySsl() const;

    void pump();

    void post(NetCompletion c);
    void drainForTest(std::vector<NetCompletion>& out);

    NetWorker* worker() const { return worker_.get(); }

    void watchTcp(TcpSocket* sock);
    void unwatchTcp(TcpSocket* sock);
    void watchUdp(UdpSocket* sock);
    void unwatchUdp(UdpSocket* sock);
    void pollSockets();  // called from NetWorker thread

    void bindChannel(TcpSocket* sock, Channel* ch);
    void unbindChannel(TcpSocket* sock);
    Channel* channelFor(TcpSocket* sock) const;

private:
    void emitCompletion(const NetCompletion& c);

    int  timeoutMs_ = 10000;
    bool verifySsl_ = true;
    std::unique_ptr<NetWorker> worker_;

    mutable std::mutex watchMu_;
    std::vector<TcpSocket*> watchedTcp_;
    std::vector<UdpSocket*> watchedUdp_;

    mutable std::mutex channelMu_;
    std::unordered_map<TcpSocket*, Channel*> channels_;
};

}  // namespace eve::network
