#pragma once
#include "common/Export.h"


#include "network/UdpLink.h"

#include <simplesquirrel/object.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace eve::network {

class NetReader;

/**
 * Lightweight RPC envelope over a UdpLink (or any link exposing the same
 * message callback). Wire format: u16 msgId + payload bytes, sent on channel 0.
 *
 * C++ handlers receive NetReader&; script handlers receive the raw payload as
 * a Squirrel string (build a NetReader with eve.Network().newReader(bytes)).
 */
/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM NetRpc {
public:
    /** @brief Net rpc. */
    explicit NetRpc(UdpLink* link);
    /** @brief Net rpc. */
    ~NetRpc();

    using Handler = std::function<void(NetReader&)>;

    /** @brief Call. */
    void call(uint16_t msgId, const void* data, size_t n, bool reliable = true);
    /** @brief Call string. */
    bool callString(uint16_t msgId, const std::string& payload, bool reliable = true);

    /** @brief Registers handler. */
    void registerHandler(uint16_t msgId, Handler h);
    /** Script-facing: stores a closure invoked with the payload string. */
    /** @brief Registers script. */
    void registerScript(uint16_t msgId, ssq::Object fn);

private:
    void onLinkMessage(UdpLink::MsgType type, uint8_t channel,
                       const char* data, size_t n);

    UdpLink* link_ = nullptr;
    std::map<uint16_t, Handler> handlers_;
    std::map<uint16_t, ssq::Object> scriptHandlers_;
};

}  // namespace eve::network
