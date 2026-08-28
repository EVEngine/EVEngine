#pragma once

#include "common/Capability.h"
#include "common/Module.h"
#include "common/ServiceInterfaces.h"
#include "NetTypes.h"

#include <memory>
#include <mutex>
#include <atomic>
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

    /** @brief Creates an owning TCP socket handle for C++ callers. */
    std::unique_ptr<TcpSocket> makeTcp();
    /** @brief Creates an owning UDP socket handle for C++ callers. */
    std::unique_ptr<UdpSocket> makeUdp();
    /** @brief Creates an owning HTTP request handle for C++ callers. */
    std::unique_ptr<HttpRequest> makeHttp(std::string method, std::string url);
    /** @brief Creates an owning channel handle, or null for a null socket. */
    std::unique_ptr<Channel> makeChannel(TcpSocket* socket);
    /** @brief Creates an owning session handle. */
    std::unique_ptr<Session> makeSession();
    /** @brief Creates an owning streaming writer handle. */
    std::unique_ptr<NetWriter> makeWriter();
    /** @brief Creates an owning streaming reader handle. */
    std::unique_ptr<NetReader> makeReader(std::string bytes);
    /** @brief Creates an owning UDP link handle, or null for a null socket. */
    std::unique_ptr<UdpLink> makeUdpLink(UdpSocket* socket);
    /** @brief Creates an owning RPC handle, or null for a null link. */
    std::unique_ptr<NetRpc> makeRpc(UdpLink* link);
    /** @brief Creates an owning UDP host handle. */
    std::unique_ptr<NetHost> makeHost();

    /** @brief Script adapter: creates a caller-owned TCP socket. */
    TcpSocket*   newTcp();
    /** @brief Script adapter: creates a caller-owned UDP socket. */
    UdpSocket*   newUdp();
    /** @brief Script adapter: creates a caller-owned HTTP request. */
    HttpRequest* newHttp(std::string method, std::string url);
    /** @brief Script adapter: creates a caller-owned channel. */
    Channel*     newChannel(TcpSocket* socket);
    /** @brief Script adapter: creates a caller-owned session. */
    Session*     newSession();
    /** @brief Script adapter: creates a caller-owned writer. */
    NetWriter*   newWriter();
    /** @brief Script adapter: creates a caller-owned reader. */
    NetReader*   newReader(std::string bytes);
    /** @brief Script adapter: creates a caller-owned UDP link. */
    UdpLink*     newUdpLink(UdpSocket* socket);
    /** @brief Script adapter: creates a caller-owned RPC client. */
    NetRpc*      newRpc(UdpLink* link);
    /** @brief Script adapter: creates a caller-owned UDP host. */
    NetHost*     newHost();

    /** @brief Default socket/HTTP timeout in milliseconds. */
    void setTimeout(int ms);
    int  getTimeout() const;
    /** @brief Whether TLS peer certificates are verified (default true). */
    void setVerifySsl(bool verify);
    bool getVerifySsl() const;

    /** @brief Return a thread-safe copied telemetry snapshot for diagnostics tools. */
    NetTelemetrySnapshot telemetrySnapshot() const;
    /** @brief Reset cumulative telemetry counters without affecting live sockets. */
    void resetTelemetry();

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
    /** @brief Record bytes successfully handed to a transport. */
    void recordSent(size_t bytes);
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
    std::atomic<uint64_t> telemetryRevision_{1};
    std::atomic<uint64_t> sentBytes_{0}, receivedBytes_{0}, completions_{0};
    std::atomic<uint64_t> errors_{0}, connections_{0};
};

}  // namespace eve::network
