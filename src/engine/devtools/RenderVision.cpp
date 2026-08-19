#include "devtools/RenderVision.hpp"
#include "devtools/Immortal.hpp"

#include "common/RenderCapture.h"

#include <Poco/Base64Encoder.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/SocketAddress.h>
#include <Poco/Timespan.h>

#include <cstdlib>
#include <iterator>
#include <sstream>

namespace eve::dev {

namespace {

const char* kSystemPrompt =
    "You are a rendering debug assistant integrated with the EVEngine game engine. "
    "You receive a screenshot of the current rendered frame plus a block of engine "
    "render parameters. Respond in concise English prose aimed at another LLM agent. "
    "Cover: (1) what is visibly rendered (scene content, objects, colors, lighting, "
    "UI overlays, any artifacts or anomalies); (2) how the provided render parameters "
    "relate to the visible result (resolution, whether a 3D scene is active, readback "
    "state, render-pipeline event counts); (3) any anomalies (black screen, missing "
    "geometry, wrong colors, flicker) and the parameter likely responsible. Be specific "
    "and reference the parameter names verbatim.";

// Splits "http://host[:port][/basepath]" into host, port and base path.
void splitBaseUrl(const std::string& url, std::string& host, unsigned& port,
                  std::string& basePath) {
    host     = "127.0.0.1";
    port     = 80;
    basePath = "";
    std::string rest = url;
    const auto scheme = rest.find("://");
    if (scheme != std::string::npos) rest = rest.substr(scheme + 3);
    const auto slash = rest.find('/');
    std::string authority = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    if (slash != std::string::npos) basePath = rest.substr(slash);
    // Strip trailing slash from basePath.
    while (basePath.size() > 1 && basePath.back() == '/') basePath.pop_back();
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) {
        host = authority.substr(0, colon);
        try {
            port = static_cast<unsigned>(std::stoi(authority.substr(colon + 1)));
        } catch (...) {
            port = 80;
        }
    } else {
        host = authority;
    }
    if (host.empty()) host = "127.0.0.1";
}

std::string base64Encode(const void* data, size_t size) {
    std::ostringstream oss;
    {
        Poco::Base64Encoder enc(oss);
        enc.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        enc.close();
    }
    return oss.str();
}

}  // namespace

RenderVision& RenderVision::instance() {
    // Process-immortal singleton; see devtools/Immortal.hpp.
    return Immortal<RenderVision>::get();
}

void RenderVision::ensureEnvLocked() {
    if (envLoaded_) return;
    envLoaded_ = true;
    if (const char* v = std::getenv("EVE_VISION_BASE_URL")) baseUrl_ = v;
    if (const char* v = std::getenv("EVE_VISION_API_KEY")) apiKey_ = v;
    if (const char* v = std::getenv("EVE_VISION_MODEL")) model_ = v;
    if (const char* v = std::getenv("EVE_VISION_PATH")) path_ = v;
    if (const char* v = std::getenv("EVE_VISION_TIMEOUT_MS")) {
        try {
            timeoutMs_ = std::stoi(v);
        } catch (...) {
        }
    }
}

void RenderVision::setBaseUrl(std::string url) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    baseUrl_ = std::move(url);
}
void RenderVision::setApiKey(std::string key) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    apiKey_ = std::move(key);
}
void RenderVision::setModel(std::string model) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    model_ = std::move(model);
}
void RenderVision::setPath(std::string path) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    if (path.empty()) path = "/chat/completions";
    if (path.front() != '/') path.insert(path.begin(), '/');
    path_ = std::move(path);
}
void RenderVision::setTimeoutMs(int ms) {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    if (ms > 0) timeoutMs_ = ms;
}

bool RenderVision::configured() {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    return !baseUrl_.empty() && !model_.empty();
}

std::string RenderVision::configJson() {
    std::lock_guard<std::mutex> lock(mu_);
    ensureEnvLocked();
    Poco::JSON::Object::Ptr o = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    o->set("baseUrl", baseUrl_);
    o->set("model", model_);
    o->set("path", path_);
    o->set("timeoutMs", timeoutMs_);
    o->set("apiKeySet", !apiKey_.empty());
    std::ostringstream oss;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(o), oss, 0, 0);
    return oss.str();
}

std::string RenderVision::latest() const {
    std::lock_guard<std::mutex> lock(mu_);
    return latest_;
}

std::string RenderVision::lastError() const {
    std::lock_guard<std::mutex> lock(mu_);
    return lastError_;
}

void RenderVision::notifyPending(const std::string& reason, const std::string& source, int line) {
    std::lock_guard<std::mutex> lock(mu_);
    pendingReason_ = reason.empty() ? "breakpoint" : reason;
    pendingLoc_    = source + ":" + std::to_string(line);
    pending_.store(true);
}

bool RenderVision::pending() const { return pending_.load(); }

std::string RenderVision::pendingReason() const {
    std::lock_guard<std::mutex> lock(mu_);
    return pendingReason_;
}

std::string RenderVision::describe(eve::IRenderCapture* cap, const std::string& renderDataJson,
                                  bool fresh, const std::string& reason) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureEnvLocked();
        if (!fresh && !latest_.empty()) return latest_;
    }
    if (!cap) return "error: Graphics module not available for vision describe";
    return doDescribe(cap, renderDataJson, reason);
}

void RenderVision::pollPending(eve::IRenderCapture* cap, const std::string& renderDataJson) {
    if (!pending_.load() || !cap) return;
    std::string reason;
    {
        std::lock_guard<std::mutex> lock(mu_);
        reason = pendingReason_;
    }
    // clear first so a failed dump does not loop; doDescribe caches result/error.
    pending_.store(false);
    doDescribe(cap, renderDataJson, reason);
}

std::string RenderVision::doDescribe(eve::IRenderCapture* cap, const std::string& renderDataJson,
                                     const std::string& reason) {
    std::string baseUrl, apiKey, model, path;
    int         timeoutMs = timeoutMs_;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ensureEnvLocked();
        baseUrl   = baseUrl_;
        apiKey    = apiKey_;
        model     = model_;
        path      = path_;
        timeoutMs = timeoutMs_;
    }
    if (baseUrl.empty() || model.empty()) {
        const std::string err = "error: RenderVision not configured (set EVE_VISION_BASE_URL/MODEL or call eve_render_vision_config)";
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    }

    // --- Capture frame to PNG bytes in memory (via the render-capture interface) ---
    const std::string dataUrl = cap->capturePngDataUrl();
    if (dataUrl.empty()) {
        const std::string err = "error: frame readback returned no image";
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    }

    // --- Build OpenAI-compatible chat/completions body ---
    std::string context = reason.empty() ? "Engine render parameters:" : "Engine render parameters (dump reason: " + reason + "):";
    if (!renderDataJson.empty()) context += "\n" + renderDataJson;

    Poco::JSON::Object::Ptr root  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    root->set("model", model);
    Poco::JSON::Array::Ptr messages = Poco::JSON::Array::Ptr(new Poco::JSON::Array());

    Poco::JSON::Object::Ptr sys = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    sys->set("role", "system");
    sys->set("content", kSystemPrompt);
    messages->add(sys);

    Poco::JSON::Object::Ptr user = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    user->set("role", "user");
    Poco::JSON::Array::Ptr uc = Poco::JSON::Array::Ptr(new Poco::JSON::Array());

    Poco::JSON::Object::Ptr textPart = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    textPart->set("type", "text");
    textPart->set("text", context);
    uc->add(textPart);

    Poco::JSON::Object::Ptr imgPart = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    imgPart->set("type", "image_url");
    Poco::JSON::Object::Ptr iu = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    iu->set("url", dataUrl);
    imgPart->set("image_url", iu);
    uc->add(imgPart);

    user->set("content", uc);
    messages->add(user);
    root->set("messages", messages);
    root->set("max_tokens", 1024);

    std::ostringstream bodyStream;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(root), bodyStream, 0, 0);
    const std::string body = bodyStream.str();

    // --- HTTP POST ---
    std::string host;
    unsigned    port = 80;
    std::string basePath;
    splitBaseUrl(baseUrl, host, port, basePath);
    std::string endpoint = basePath + path;
    if (endpoint.empty()) endpoint = "/chat/completions";

    std::string responseBody;
    try {
        Poco::Net::HTTPClientSession session(host, port);
        session.setTimeout(Poco::Timespan(timeoutMs / 1000, (timeoutMs % 1000) * 1000));
        session.setKeepAlive(false);
        Poco::Net::HTTPRequest req(Poco::Net::HTTPRequest::HTTP_POST, endpoint,
                                   Poco::Net::HTTPMessage::HTTP_1_1);
        req.setContentType("application/json");
        req.setChunkedTransferEncoding(false);
        req.setContentLength(static_cast<int>(body.size()));
        if (!apiKey.empty()) req.set("Authorization", "Bearer " + apiKey);
        std::ostream& os = session.sendRequest(req);
        os << body;
        os.flush();
        Poco::Net::HTTPResponse res;
        std::istream&          is = session.receiveResponse(res);
        responseBody.assign(std::istreambuf_iterator<char>(is), std::istreambuf_iterator<char>());
        if (res.getStatus() != Poco::Net::HTTPResponse::HTTP_OK) {
            std::string snippet = responseBody.size() > 300 ? responseBody.substr(0, 300) : responseBody;
            const std::string err = "error: vision HTTP " + std::to_string(res.getStatus()) + ": " + snippet;
            std::lock_guard<std::mutex> lock(mu_);
            lastError_ = err;
            return err;
        }
    } catch (const Poco::Exception& e) {
        const std::string err = std::string("error: vision request failed: ") + e.displayText();
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    } catch (const std::exception& e) {
        const std::string err = std::string("error: vision request failed: ") + e.what();
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    }

    // --- Parse response ---
    try {
        Poco::JSON::Parser  parser;
        auto                var = parser.parse(responseBody);
        auto                obj = var.extract<Poco::JSON::Object::Ptr>();
        auto                choices = obj ? obj->getArray("choices") : nullptr;
        if (!choices || choices->size() == 0) {
            const std::string err = "error: vision response had no choices";
            std::lock_guard<std::mutex> lock(mu_);
            lastError_ = err;
            return err;
        }
        auto c0 = choices->getObject(0);
        auto msg = c0 ? c0->getObject("message") : nullptr;
        std::string content = msg ? msg->optValue<std::string>("content", "") : "";
        if (content.empty()) {
            const std::string err = "error: vision response had empty content";
            std::lock_guard<std::mutex> lock(mu_);
            lastError_ = err;
            return err;
        }
        std::lock_guard<std::mutex> lock(mu_);
        latest_ = content;
        lastError_.clear();
        return content;
    } catch (const Poco::Exception& e) {
        const std::string err = std::string("error: vision response parse failed: ") + e.displayText();
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    } catch (const std::exception& e) {
        const std::string err = std::string("error: vision response parse failed: ") + e.what();
        std::lock_guard<std::mutex> lock(mu_);
        lastError_ = err;
        return err;
    }
}

}  // namespace eve::dev
