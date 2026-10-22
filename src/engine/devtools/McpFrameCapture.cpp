#include "devtools/McpFrameCapture.hpp"
#include "common/Capability.h"
#include "common/FrameCaptureImage.h"
#include "devtools/FrameCapturePreview.hpp"
#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpServer.hpp"

#include <Poco/Base64Encoder.h>
#include <Poco/JSON/Array.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <sstream>

namespace eve::dev {
namespace {
using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;
Object::Ptr object() { return new Object; }
Array::Ptr  array() { return new Array; }
Object::Ptr envelope(std::string status) {
    auto o = object();
    o->set("schemaId", "eve.renderCapture");
    o->set("schemaVersion", 1);
    o->set("status", status);
    return o;
}
std::string reply(Object::Ptr o) { return textContentResult(mcpStringify(Poco::Dynamic::Var(o))); }
std::string error(std::string reason, bool retry = false) {
    auto o = envelope("failed");
    o->set("code", reason);
    o->set("message", reason);
    o->set("retryable", retry);
    return textContentResult(mcpStringify(Poco::Dynamic::Var(o)), !retry);
}
std::string resultError(const Status& status) {
    return error(status.diagnostics().empty() ? std::string(statusCodeName(status.code()))
                                              : status.diagnostics().front().message());
}
Object::Ptr bufferJson(const FrameCaptureBuffer& b) {
    auto o = object();
    o->set("id", std::string(frameAttachmentName(b.attachment)));
    o->set("width", b.width);
    o->set("height", b.height);
    o->set("producerFrameId", static_cast<Poco::UInt64>(b.producerFrameId));
    o->set("format", b.format == FramePixelFormat::Depth32Float ? "D32_SFLOAT"
                     : b.format == FramePixelFormat::Bgra8      ? "BGRA8"
                                                                : "RGBA8");
    o->set("encoding", b.encoding);
    o->set("colorSpace", b.colorSpace);
    o->set("status", b.bytes ? "available" : "unavailable");
    o->set("reason", b.unavailableReason);
    o->set("bytes", static_cast<Poco::UInt64>(b.bytes ? b.bytes->size() : 0));
    o->set("samples", 1);
    o->set("origin", "top-left");
    const bool shadow = b.attachment >= FrameAttachment::Shadow0;
    o->set("producerPass", b.attachment == FrameAttachment::Final ? "present" : shadow ? "csm" : "gbuffer");
    o->set("viewId", shadow ? "primary-directional-csm" : "main");
    o->set("storageGroup", b.attachment == FrameAttachment::Depth || b.attachment == FrameAttachment::Velocity
                               ? "gbuffer.depthColor"
                           : shadow ? "shadow.csm"
                                    : std::string(frameAttachmentName(b.attachment)));
    if (shadow)
        o->set("arrayLayer", static_cast<unsigned>(b.attachment) - static_cast<unsigned>(FrameAttachment::Shadow0));
    auto matrix = array();
    for (float value : b.producerMatrix)
        matrix->add(std::isfinite(value) ? Poco::Dynamic::Var(value) : Poco::Dynamic::Var("nonfinite"));
    o->set("producerMatrixColumnMajor", matrix);
    return o;
}
struct JobOutput {
    std::string response;
};
struct Job {
    uint64_t                              generation;
    std::string                           capture;
    std::string                           key;
    std::future<JobOutput>                future;
    std::string                           response;
    std::chrono::steady_clock::time_point deadline;
    std::shared_ptr<std::atomic<bool>>    cancelled;
};
std::map<std::string, Job> jobs;
uint64_t                   nextJob = 1;
struct ExportCleanup {
    std::vector<std::filesystem::path> paths;
    ~ExportCleanup() {
        for (const auto& path : paths) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};
void writeExportFile(const std::filesystem::path& path, const char* data, size_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data, static_cast<std::streamsize>(size));
    stream.close();
    if (!stream) throw std::runtime_error("ExportWriteFailed");
}
std::string base64(const std::vector<uint8_t>& bytes) {
    std::ostringstream  out;
    Poco::Base64Encoder encoder(out);
    encoder.rdbuf()->setLineLength(0);
    encoder.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    encoder.close();
    return out.str();
}
std::string pending(std::string token) {
    auto o = envelope("pending");
    o->set("jobToken", token);
    o->set("retryable", true);
    return reply(o);
}
std::string pollJob(const std::string& token, uint64_t generation) {
    auto it = jobs.find(token);
    if (it == jobs.end() || it->second.generation != generation) return error("StaleJob");
    auto& job = it->second;
    if (job.cancelled->load()) return error("CancelledJob");
    if (job.response.empty()) {
        if (job.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return pending(token);
        try {
            job.response = job.future.get().response;
        } catch (const std::exception& e) {
            job.response = error(std::string("WorkerFailed: ") + e.what());
        }
    }
    return job.response;
}
bool validArguments(Object::Ptr args, const std::set<std::string>& allowed) {
    if (!args) return true;
    for (const auto& [key, value] : *args) {
        if (!allowed.contains(key)) return false;
        if (key == "buffers") {
            if (value.type() != typeid(Array::Ptr)) return false;
        } else if (key == "timeoutMs" || key == "x" || key == "y" || key == "channel" || key == "maxWidth" ||
                   key == "maxHeight" || key == "maxBytes") {
            if (!value.isInteger() || value.convert<double>() < -2147483648.0 || value.convert<double>() > 2147483647.0)
                return false;
        } else if (key == "rangeMin" || key == "rangeMax" || key == "exposure") {
            if (!value.isNumeric()) return false;
        } else if (!value.isString())
            return false;
    }
    return true;
}
}  // namespace

bool isMcpFrameCaptureTool(std::string_view name) {
    return name == "eve_frame_capture_capabilities" || name == "eve_frame_capture" ||
           name == "eve_frame_capture_status" || name == "eve_frame_buffer_image" || name == "eve_frame_pixel" ||
           name == "eve_frame_capture_export" || name == "eve_frame_capture_release";
}

void resetMcpFrameCapture() {
    for (auto& [token, job] : jobs) {
        (void)token;
        job.cancelled->store(true);
    }
    if (auto* capture = cap::query<IRenderFrameCapture>()) capture->invalidate();
}

std::string callMcpFrameCaptureTool(std::string_view name, Object::Ptr args) {
    auto* service = cap::query<IRenderFrameCapture>();
    if (!service) return error("ProviderAbsent");
    const auto caps = service->capabilities();
    for (auto it = jobs.begin(); it != jobs.end();) {
        if (it->second.generation != caps.generation) it->second.cancelled->store(true);
        const bool done = !it->second.response.empty() ||
                          it->second.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        if (done &&
            (it->second.generation != caps.generation || it->second.deadline < std::chrono::steady_clock::now()))
            it = jobs.erase(it);
        else
            ++it;
    }
    if (name == "eve_frame_capture_capabilities") {
        if (!validArguments(args, {})) return error("InvalidArguments");
        auto o = envelope("ready");
        o->set("backend", caps.backend);
        o->set("providerGeneration", static_cast<Poco::UInt64>(caps.generation));
        o->set("maxBytes", static_cast<Poco::UInt64>(caps.maxBytes));
        auto buffers = array();
        for (auto a : caps.attachments) buffers->add(std::string(frameAttachmentName(a)));
        o->set("buffers", buffers);
        return reply(o);
    }
    if (name == "eve_frame_capture") {
        if (!validArguments(args, {"buffers", "timeoutMs", "onUnavailable", "requestKey"}) || !args ||
            !args->has("buffers"))
            return error("InvalidArguments");
        FrameCaptureRequest request;
        auto                buffers = args->getArray("buffers");
        if (buffers->size() > 11) return error("InvalidArguments");
        for (size_t i = 0; i < buffers->size(); ++i) {
            if (!buffers->get(static_cast<unsigned>(i)).isString()) return error("InvalidArguments");
            auto parsed = parseFrameAttachment(buffers->getElement<std::string>(static_cast<unsigned>(i)));
            if (!parsed) return resultError(parsed.status());
            request.attachments.push_back(parsed.value());
        }
        const auto unavailable = getArgString(args, "onUnavailable", "fail");
        if (unavailable != "fail" && unavailable != "report") return error("InvalidArguments");
        request.reportUnavailable = unavailable == "report";
        request.timeout           = std::chrono::milliseconds(getArgInt(args, "timeoutMs", 5000));
        request.requestKey        = getArgString(args, "requestKey");
        auto result               = service->request(std::move(request));
        if (!result) return resultError(result.status());
        auto state = service->snapshot(result.value());
        if (!state) return resultError(state.status());
        auto o = envelope(std::string(frameCapturePhaseName(state.value().phase)));
        o->set("token", result.value());
        return reply(o);
    }
    const std::set<std::string> statusArgs{"token"};
    const std::set<std::string> imageArgs{"token",    "buffer",   "view",     "channel",   "rangeMin",
                                          "rangeMax", "exposure", "maxWidth", "maxHeight", "maxBytes"};
    const std::set<std::string> pixelArgs{"token", "buffer", "x", "y"};
    const std::set<std::string> exportArgs{"token",    "buffer",   "encoding", "path",     "view",      "channel",
                                           "rangeMin", "rangeMax", "exposure", "maxWidth", "maxHeight", "maxBytes"};
    if (!validArguments(args, name == "eve_frame_buffer_image"     ? imageArgs
                              : name == "eve_frame_pixel"          ? pixelArgs
                              : name == "eve_frame_capture_export" ? exportArgs
                                                                   : statusArgs))
        return error("InvalidArguments");
    const auto token = getArgString(args, "token");
    if (name == "eve_frame_capture_status" && token.starts_with("job:")) return pollJob(token, caps.generation);
    if (name == "eve_frame_capture_release") {
        auto result = service->release(token);
        if (!result) return resultError(result.status());
        for (auto& [jobToken, job] : jobs) {
            (void)jobToken;
            if (job.capture == token) job.cancelled->store(true);
        }
        auto o = envelope(std::string(frameCapturePhaseName(result.value())));
        o->set("token", token);
        return reply(o);
    }
    auto result = service->snapshot(token);
    if (!result) return resultError(result.status());
    const auto snapshot = result.value();
    if (name == "eve_frame_capture_status") {
        auto o = envelope(std::string(frameCapturePhaseName(snapshot.phase)));
        o->set("token", token);
        o->set("frameId", static_cast<Poco::UInt64>(snapshot.frameId));
        o->set("providerGeneration", static_cast<Poco::UInt64>(snapshot.generation));
        o->set("backend", snapshot.backend);
        o->set("reason", snapshot.reason);
        o->set("depthConvention", snapshot.depthConvention);
        o->set("targetGeneration", static_cast<Poco::UInt64>(snapshot.targetGeneration));
        o->set("viewId", "main");
        auto inverseProjection = array();
        auto projection        = array();
        auto view              = array();
        for (size_t i = 0; i < 16; ++i) {
            auto finite = [](float f) {
                return std::isfinite(f) ? Poco::Dynamic::Var(f) : Poco::Dynamic::Var("nonfinite");
            };
            inverseProjection->add(finite(snapshot.inverseProjection[i]));
            projection->add(finite(snapshot.projectionMatrix[i]));
            view->add(finite(snapshot.viewMatrix[i]));
        }
        o->set("inverseProjectionColumnMajor", inverseProjection);
        o->set("projectionColumnMajor", projection);
        o->set("viewColumnMajor", view);
        auto buffers = array();
        for (const auto& b : snapshot.buffers) buffers->add(bufferJson(b));
        o->set("buffers", buffers);
        return reply(o);
    }
    if (snapshot.phase != FrameCapturePhase::Ready)
        return error("CaptureNotReady", snapshot.phase == FrameCapturePhase::Queued ||
                                            snapshot.phase == FrameCapturePhase::Recording ||
                                            snapshot.phase == FrameCapturePhase::ReadbackPending);
    auto parsed = parseFrameAttachment(getArgString(args, "buffer"));
    if (!parsed) return resultError(parsed.status());
    const auto it = std::find_if(snapshot.buffers.begin(), snapshot.buffers.end(),
                                 [&](const auto& b) { return b.attachment == parsed.value(); });
    if (it == snapshot.buffers.end() || !it->bytes) return error("AttachmentUnavailable");
    if (name == "eve_frame_pixel") {
        if (!args || !args->has("x") || !args->has("y")) return error("InvalidArguments");
        const int x = getArgInt(args, "x", -1), y = getArgInt(args, "y", -1);
        if (x < 0 || y < 0) return error("PixelOutOfBounds");
        auto pixel = frameCapturePixel(*it, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
        if (!pixel) return resultError(pixel.status());
        auto o = envelope("ready");
        o->set("token", token);
        o->set("buffer", std::string(frameAttachmentName(it->attachment)));
        o->set("frameId", static_cast<Poco::UInt64>(snapshot.frameId));
        o->set("x", x);
        o->set("y", y);
        auto channels = array();
        for (size_t c = 0; c < pixel.value().channels; ++c) {
            auto channel = object();
            channel->set("name", std::string(pixel.value().labels[c]));
            const double decoded = pixel.value().decoded[c];
            channel->set("stored", std::isfinite(pixel.value().stored[c]) ? Poco::Dynamic::Var(pixel.value().stored[c])
                                                                          : Poco::Dynamic::Var("nonfinite"));
            channel->set("decoded", std::isfinite(decoded) ? Poco::Dynamic::Var(decoded)
                                                           : Poco::Dynamic::Var(std::isnan(decoded) ? "NaN"
                                                                                : decoded > 0       ? "+Inf"
                                                                                                    : "-Inf"));
            channels->add(channel);
        }
        o->set("channels", channels);
        if (it->attachment == FrameAttachment::HwDepth) {
            auto depth = frameCaptureLinearDepth(*it, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                                                 snapshot.inverseProjection);
            if (depth)
                o->set("linearViewDepth", depth.value());
            else
                o->set("linearViewDepthStatus", "InvalidDepthProjection");
        }
        return reply(o);
    }
    FramePreviewOptions options;
    options.inverseProjection = snapshot.inverseProjection;
    options.view              = getArgString(args, "view", "auto");
    options.channel           = getArgInt(args, "channel", 0);
    options.maxWidth          = static_cast<uint32_t>(getArgInt(args, "maxWidth", 1024));
    options.maxHeight         = static_cast<uint32_t>(getArgInt(args, "maxHeight", 1024));
    options.rangeMin          = getArgFloat(args, "rangeMin", 0);
    options.rangeMax          = getArgFloat(args, "rangeMax", 1);
    options.exposure          = getArgFloat(args, "exposure", 0);
    const int maxBytes        = getArgInt(args, "maxBytes", 2 * 1024 * 1024);
    if (maxBytes < 1024 || maxBytes > 16 * 1024 * 1024) return error("InvalidImageBudget");
    options.maxBytes     = static_cast<size_t>(maxBytes);
    const auto encoding  = getArgString(args, "encoding", "previewPng");
    auto       path      = getArgString(args, "path");
    const bool exporting = name == "eve_frame_capture_export";
    if (exporting && (path.empty() || (encoding != "previewPng" && encoding != "nativeBinary")))
        return error("InvalidExport");
    if (exporting) {
        if (path.find('\0') != std::string::npos) return error("InvalidExportPath");
        try {
            auto       resolved = std::filesystem::path(path);
            const auto root     = McpServer::instance().gameRoot();
            if (resolved.is_relative() && !root.empty()) resolved = std::filesystem::path(root) / resolved;
            path = std::filesystem::absolute(resolved).lexically_normal().generic_string();
        } catch (const std::exception&) {
            return error("InvalidExportPath");
        }
    }
    const auto key = std::string(name) + ":" + mcpStringify(Poco::Dynamic::Var(args));
    for (const auto& [jobToken, job] : jobs)
        if (job.generation == caps.generation && job.key == key) return pollJob(jobToken, caps.generation);
    size_t running = 0;
    for (const auto& [jobId, job] : jobs) {
        (void)jobId;
        if (job.response.empty() && job.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            ++running;
    }
    if (running >= 2) return error("WorkerBudgetExceeded");
    // Completed previews are disposable projections; capture snapshots retain their own TTL.
    while (jobs.size() >= 2) {
        auto disposable = std::find_if(jobs.begin(), jobs.end(), [](const auto& pair) {
            return !pair.second.response.empty() ||
                   pair.second.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        });
        if (disposable == jobs.end()) return error("WorkerBudgetExceeded");
        jobs.erase(disposable);
    }
    const auto jobToken  = "job:" + std::to_string(caps.generation) + ":" + std::to_string(nextJob++);
    const auto buffer    = *it;
    auto       cancelled = std::make_shared<std::atomic<bool>>(false);
    auto       future    = std::async(
        std::launch::async,
        [buffer, options, token, snapshotFrame = snapshot.frameId, exporting, encoding, path, jobToken,
         cancelled]() -> JobOutput {
            try {
                FramePreview preview;
                if (!exporting || encoding == "previewPng") {
                    auto encoded = frameCapturePreview(buffer, options);
                    if (!encoded) return {resultError(encoded.status())};
                    preview = std::move(encoded.value());
                }
                auto o = envelope("ready");
                o->set("token", token);
                o->set("jobToken", jobToken);
                if (cancelled->load()) return {error("CancelledJob")};
                o->set("frameId", static_cast<Poco::UInt64>(snapshotFrame));
                o->set("buffer", bufferJson(buffer));
                if (!exporting) {
                    o->set("width", preview.width);
                    o->set("height", preview.height);
                    o->set("view", preview.view);
                    o->set("channel", options.channel);
                    o->set("rangeMin", options.rangeMin);
                    o->set("rangeMax", options.rangeMax);
                    o->set("exposure", options.exposure);
                    return {imageContentResult(base64(preview.png), "image/png", mcpStringify(Poco::Dynamic::Var(o)))};
                }
                const std::filesystem::path destination(path);
                if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
                auto temporary = destination;
                temporary += "." + std::to_string(snapshotFrame) + "." +
                             std::to_string(std::hash<std::string>{}(jobToken)) + ".tmp";
                ExportCleanup cleanup;
                cleanup.paths.push_back(temporary);
                const auto& bytes = encoding == "nativeBinary" ? *buffer.bytes : preview.png;
                writeExportFile(temporary, reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (cancelled->load()) return {error("CancelledJob")};
                if (encoding == "nativeBinary") {
                    if constexpr (std::endian::native != std::endian::little) return {error("UnsupportedByteOrder")};
                    o->set("byteOrder", "little-endian");
                    o->set("rowStride", static_cast<Poco::UInt64>(buffer.width) * 4);
                    o->set("path", path);
                    auto inverseProjection = array();
                    for (float value : options.inverseProjection)
                        inverseProjection->add(std::isfinite(value) ? Poco::Dynamic::Var(value)
                                                                             : Poco::Dynamic::Var("nonfinite"));
                    o->set("inverseProjectionColumnMajor", inverseProjection);
                    const std::filesystem::path metadataPath(path + ".json");
                    auto                        metadataTemporary = temporary;
                    metadataTemporary += ".json";
                    cleanup.paths.push_back(metadataTemporary);
                    const auto metadata = mcpStringify(Poco::Dynamic::Var(o));
                    writeExportFile(metadataTemporary, metadata.data(), metadata.size());
                    // A same-filesystem hard link publishes without replacing an existing destination.
                    std::filesystem::create_hard_link(metadataTemporary, metadataPath);
                    cleanup.paths.push_back(metadataPath);
                }
                if (cancelled->load()) return {error("CancelledJob")};
                std::filesystem::create_hard_link(temporary, destination);
                // The binary/PNG is the commit marker; metadata is already complete when it appears.
                if (encoding == "nativeBinary") cleanup.paths.pop_back();
                o->set("path", path);
                o->set("bytes", static_cast<Poco::UInt64>(bytes.size()));
                return {reply(o)};
            } catch (const std::exception& e) {
                return {error(std::string("ExportOrPreviewFailed: ") + e.what())};
            }
        });
    jobs.emplace(jobToken, Job{caps.generation,
                               token,
                               key,
                               std::move(future),
                               {},
                               std::chrono::steady_clock::now() + std::chrono::seconds(60),
                               std::move(cancelled)});
    return pending(jobToken);
}

std::string_view mcpFrameCaptureSchemas() {
    static const std::string schemas = [] {
        std::string                      out;
        const std::array<std::string, 7> names{
            "eve_frame_capture_capabilities", "eve_frame_capture", "eve_frame_capture_status",
            "eve_frame_buffer_image",         "eve_frame_pixel",   "eve_frame_capture_export",
            "eve_frame_capture_release"};
        for (const auto& name : names) {
            auto tool = object();
            tool->set("name", name);
            tool->set("description",
                      "Immutable same-frame render capture. Request queues the next normal frame; status polls "
                      "capture/job tokens. Image/export return jobToken; poll status until ready. Pixels use original "
                      "top-left coordinates. No implicit gameplay advance.");
            auto schema = object();
            schema->set("type", "object");
            schema->set("additionalProperties", false);
            auto props    = object();
            auto required = array();
            auto property = [&](const std::string& key, const std::string& type, bool need = false) {
                auto p = object();
                p->set("type", type);
                props->set(key, p);
                if (need) required->add(key);
            };
            if (name == "eve_frame_capture") {
                property("buffers", "array", true);
                auto items = object();
                items->set("type", "string");
                props->getObject("buffers")->set("items", items);
                property("timeoutMs", "integer");
                property("onUnavailable", "string");
                property("requestKey", "string");
            } else if (name != "eve_frame_capture_capabilities") {
                property("token", "string", true);
                if (name == "eve_frame_buffer_image" || name == "eve_frame_pixel" || name == "eve_frame_capture_export")
                    property("buffer", "string", true);
                if (name == "eve_frame_pixel") {
                    property("x", "integer", true);
                    property("y", "integer", true);
                }
                if (name == "eve_frame_buffer_image" || name == "eve_frame_capture_export") {
                    property("view", "string");
                    property("channel", "integer");
                    property("rangeMin", "number");
                    property("rangeMax", "number");
                    property("exposure", "number");
                    property("maxWidth", "integer");
                    property("maxHeight", "integer");
                    property("maxBytes", "integer");
                }
                if (name == "eve_frame_capture_export") {
                    property("encoding", "string", true);
                    property("path", "string", true);
                }
            }
            auto enumeration = [&](const std::string& key, std::initializer_list<const char*> values) {
                if (!props->has(key)) return;
                auto list = array();
                for (const auto* value : values) list->add(value);
                props->getObject(key)->set("enum", list);
            };
            auto bounds = [&](const std::string& key, int low, int high) {
                if (!props->has(key)) return;
                props->getObject(key)->set("minimum", low);
                props->getObject(key)->set("maximum", high);
            };
            enumeration("view",
                        {"auto", "rawChannels", "normalWorld", "velocity", "scalar", "linearDepth", "hdrExposure"});
            enumeration("encoding", {"previewPng", "nativeBinary"});
            enumeration("onUnavailable", {"fail", "report"});
            bounds("timeoutMs", 100, 30000);
            bounds("channel", 0, 3);
            bounds("maxWidth", 1, 2048);
            bounds("maxHeight", 1, 2048);
            bounds("maxBytes", 1024, 16777216);
            bounds("x", 0, 2147483647);
            bounds("y", 0, 2147483647);
            schema->set("properties", props);
            schema->set("required", required);
            tool->set("inputSchema", schema);
            if (!out.empty()) out += ',';
            out += mcpStringify(Poco::Dynamic::Var(tool));
        }
        return out;
    }();
    return schemas;
}
}  // namespace eve::dev
