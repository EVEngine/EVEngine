#pragma once

#include "common/Export.h"

#include <atomic>
#include <mutex>
#include <string>

namespace eve::graphics {
class Graphics;
}

namespace eve::dev {

/**
 * @brief Vision-model bridge for rendered-frame review (desktop / devtools only).
 *
 * Captures the current frame (Graphics readback), encodes it as a PNG data URL,
 * and sends it together with engine render parameters to an OpenAI-compatible
 * vision model over plain HTTP (JSON body, no multipart). The model returns a
 * prose description of what is rendered and how the provided render parameters
 * relate to the visible result — useful for other LLM agents debugging visuals.
 *
 * Config is set via env vars or the `eve_render_vision_config` MCP tool:
 *   EVE_VISION_BASE_URL  e.g. http://127.0.0.1:11434/v1  (must be plain HTTP;
 *                        no TLS/OpenSSL is linked into the engine)
 *   EVE_VISION_API_KEY   optional bearer token
 *   EVE_VISION_MODEL     e.g. llava / gpt-4o-mini / qwen2-vl
 *   EVE_VISION_PATH      request path suffix, default /chat/completions
 *   EVE_VISION_TIMEOUT_MS
 *
 * Capture/describe must be called on the main (render/game) thread, since it
 * touches the Graphics readback. Breakpoint / error sites only record a pending
 * flag here; the actual dump happens later from McpServer::poll.
 */
class EVENGINE_API RenderVision {
public:
    static RenderVision& instance();

    RenderVision(const RenderVision&)            = delete;
    RenderVision& operator=(const RenderVision&) = delete;

    // --- Config (env-backed on first use) ---
    void setBaseUrl(std::string url);
    void setApiKey(std::string key);
    void setModel(std::string model);
    void setPath(std::string path);
    void setTimeoutMs(int ms);
    /** @brief JSON description of current config (API key masked). */
    std::string configJson();
    bool        configured();

    // --- Cache ---
    std::string latest() const;
    std::string lastError() const;

    // --- Pending trigger (recorded off-main; dumped on main thread) ---
    /** @brief Record that a breakpoint / critical site wants a vision dump. */
    void notifyPending(const std::string& reason, const std::string& source, int line);
    bool pending() const;
    std::string pendingReason() const;

    /**
     * @brief Main-thread capture + vision describe. Returns the description text, or
     * a string starting with "error: " on failure. When `fresh` is false and a
     * cached description exists, returns the cache.
     */
    std::string describe(graphics::Graphics* gfx, const std::string& renderDataJson,
                         bool fresh, const std::string& reason = {});

    /** @brief McpServer::poll hook: performs one pending dump when safe, clears flag. */
    void pollPending(graphics::Graphics* gfx, const std::string& renderDataJson);

private:
    RenderVision() = default;

    void ensureEnvLocked();
    std::string doDescribe(graphics::Graphics* gfx, const std::string& renderDataJson,
                           const std::string& reason);

    mutable std::mutex mu_;
    std::string baseUrl_ = "http://127.0.0.1:11434/v1";
    std::string apiKey_;
    std::string model_ = "llava";
    std::string path_  = "/chat/completions";
    int         timeoutMs_ = 20000;
    bool        envLoaded_ = false;

    std::string latest_;
    std::string lastError_;

    std::atomic<bool> pending_{false};
    std::string pendingReason_;
    std::string pendingLoc_;
};

}  // namespace eve::dev
