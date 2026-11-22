#pragma once
#include "common/Export.h"


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
class HttpRuntime;
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
 * @thread Construct, configure, pump and destroy on the composition/owner thread.
 * @reentrancy Callbacks may retire HTTP requests, but must not destroy this module
 * or recursively call pump() before the outer pump returns.
 */
class EVENGINE_API_PLATFORM Network : public Module, public eve::service::INetwork {
public:
    Module_REG(Network);
    /** @brief Network. */
    Network();
    /** @brief Cancels HTTP completions and joins background I/O before destruction.
     * @cost May wait for in-flight socket timeouts; OS DNS resolution can take longer.
     * @thread Destroy on the module owner thread, without concurrent API calls.
     */
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
    /** @brief Returns the timeout. */
    int  getTimeout() const;
    /** @brief Whether TLS peer certificates are verified (default true). */
    void setVerifySsl(bool verify);
    /** @brief Returns the verify ssl. */
    bool getVerifySsl() const;
    /** @brief Sets an owning UTF-8 CA file or hashed CA directory for future HTTPS requests.
     * Empty selects system/default trust. A custom source exclusively replaces it.
     * @thread Network owner thread; requests snapshot this policy on submission.
     * @cost Copies the path per policy change; jobs load certificates per HTTPS request.
     */
    void setCaLocation(std::string location);

    /** @brief Return a thread-safe copied telemetry snapshot for diagnostics tools. */
    NetTelemetrySnapshot telemetrySnapshot() const;
    /** @brief Reset cumulative telemetry counters without affecting live sockets. */
    void resetTelemetry();

    /** @brief Drains worker completions and emits them as events; call per frame. */
    void pump();

    /** @brief Synchronous HTTP(S) compatibility service; other completions remain queued.
     * @cost Blocks the owner thread for network I/O until completion or the requested deadline.
     * @note Does not dispatch events or invoke callbacks. Certificate verification defaults to true.
     */
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
    void recordCompletion(const NetCompletion& c);
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

    std::shared_ptr<HttpRuntime> http_;
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
