#pragma once

#include "common/Capability.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
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
class NetWriter;
class NetReader;
class UdpLink;
class NetHost;
class NetRpc;

/**
 * @brief Network module: TCP/UDP/HTTP factories, background worker, and
 * completion event plumbing. Script: `net <- eve.Network();`
 */
class Network : public Module, public eve::service::INetwork {
public:
    Module_REG(Network);
    Network();
    ~Network() override;

    /** @brief Creates a new TCP socket (client or server). */
    TcpSocket*   newTcp();
    /** @brief Creates a new UDP socket. */
    UdpSocket*   newUdp();
    /** @brief Creates a new HTTP request (method e.g. "GET"/"POST", full URL). */
    HttpRequest* newHttp(std::string method, std::string url);
    /** @brief Creates a length-prefixed Channel over a TCP socket. */
    Channel*     newChannel(TcpSocket* socket);
    /** @brief Creates a named-channel session container. */
    Session*     newSession();
    /** @brief Creates a streaming byte writer. */
    NetWriter*   newWriter();
    /** @brief Creates a streaming byte reader over a string buffer. */
    NetReader*   newReader(std::string bytes);
    /** @brief Creates a framed UDP link over a socket. */
    UdpLink*     newUdpLink(UdpSocket* socket);
    /** @brief Creates an RPC client over a UDP link. */
    NetRpc*      newRpc(UdpLink* link);
    /** @brief Creates a UDP host (peer discovery / broadcast). */
    NetHost*     newHost();

    /** @brief Default socket/HTTP timeout in milliseconds. */
    void setTimeout(int ms);
    int  getTimeout() const;
    /** @brief Whether TLS peer certificates are verified (default true). */
    void setVerifySsl(bool verify);
    bool getVerifySsl() const;

    /** @brief Drains worker completions and emits them as events; call per frame. */
    void pump();

    /** @brief Synchronous HTTP request (blocks up to timeoutMs on the worker). */
    bool httpRequest(const std::string& method, const std::string& url,
                     const std::string& body, int timeoutMs, int& status,
                     std::string& responseBody) override;

private:
    friend class Channel;
    friend class HttpRequest;
    friend class NetHost;
    friend class NetWorker;
    friend class TcpSocket;
    friend class UdpSocket;

    /** @brief Posts a completion to the worker queue (thread-safe). */
    void post(NetCompletion c);
    /** @brief Drains pending worker completions into out. */
    void drainCompletions(std::vector<NetCompletion>& out);
    /** @brief Returns the module-owned worker to internal network collaborators. */
    NetWorker* worker() const { return worker_.get(); }

    /** @brief Registers/unregisters sockets for worker polling. */
    void watchTcp(TcpSocket* sock);
    void unwatchTcp(TcpSocket* sock);
    void watchUdp(UdpSocket* sock);
    void unwatchUdp(UdpSocket* sock);
    /** @brief Polls watched sockets; called from the NetWorker thread. */
    void pollSockets();

    /** @brief Binds a Channel to its socket for reverse lookup. */
    void bindChannel(TcpSocket* sock, Channel* ch);
    void unbindChannel(TcpSocket* sock);
    Channel* channelFor(TcpSocket* sock) const;

    /** @brief Main-thread-only UDP collaborator registration. */
    void bindUdpLink(UdpSocket* sock, UdpLink* link);
    void unbindUdpLink(UdpSocket* sock);
    void bindUdpHost(UdpSocket* sock, NetHost* host);
    void unbindUdpHost(UdpSocket* sock);

    void emitCompletion(const NetCompletion& c);

    int  timeoutMs_ = 10000;
    bool verifySsl_ = true;
    std::unique_ptr<NetWorker> worker_;

    mutable std::mutex watchMu_;
    std::vector<TcpSocket*> watchedTcp_;
    std::vector<UdpSocket*> watchedUdp_;

    mutable std::mutex channelMu_;
    std::unordered_map<TcpSocket*, Channel*> channels_;

    std::unordered_map<UdpSocket*, UdpLink*> udpLinks_;
    std::unordered_map<UdpSocket*, NetHost*> udpHosts_;
};

}  // namespace eve::network
