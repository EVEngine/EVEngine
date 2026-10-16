#include <Poco/InflatingStream.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include "common/Capability.h"
#include "common/FrameCaptureImage.h"
#include "devtools/FrameCapturePreview.hpp"
#include "devtools/McpFrameCapture.hpp"
#include "devtools/McpToolFilter.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
eve::FrameCaptureCapabilities caps(size_t budget = 256u * 1024u * 1024u) {
    eve::FrameCaptureCapabilities value;
    value.backend     = "test";
    value.maxBytes    = budget;
    value.attachments = {eve::FrameAttachment::Depth, eve::FrameAttachment::Velocity, eve::FrameAttachment::Final};
    return value;
}
eve::FrameCaptureSnapshot pixels(eve::FrameAttachment attachment = eve::FrameAttachment::Depth) {
    eve::FrameCaptureSnapshot snapshot;
    eve::FrameCaptureBuffer   b;
    b.attachment = attachment;
    b.width = b.height = 1;
    b.bytes = std::make_shared<const std::vector<uint8_t>>(std::initializer_list<uint8_t>{128, 64, 255, 255});
    snapshot.buffers.push_back(std::move(b));
    return snapshot;
}
}  // namespace
TEST_CASE("mcp.frameCapture.atomicPublicationAndLeases") {
    eve::FrameCaptureStore store;
    store.configure(caps());
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Depth};
    auto queued         = store.request(request);
    ASSERT(queued.ok());
    const auto token = queued.value();
    auto       begun = store.begin(token, 42, 4);
    ASSERT(begun.ok());
    auto pending = store.snapshot(token);
    ASSERT(pending.ok());
    ASSERT(pending.value().phase == eve::FrameCapturePhase::Recording);
    ASSERT(pending.value().buffers.empty());
    auto submitted = store.submitted(token);
    ASSERT(submitted.ok());
    auto invalid = store.complete(token, {});
    ASSERT(!invalid.ok());
    auto complete = store.complete(token, pixels());
    ASSERT(complete.ok());
    store.retire(token);
    auto ready = store.snapshot(token);
    ASSERT(ready.ok());
    ASSERT(ready.value().frameId == 42);
    ASSERT(ready.value().phase == eve::FrameCapturePhase::Ready);
    auto released = store.release(token);
    ASSERT(released.ok());
    ASSERT(ready.value().buffers[0].bytes->at(1) == 64);
}
TEST_CASE("mcp.frameCapture.timeoutBudgetCancellationAndGeneration") {
    auto                   now = std::chrono::steady_clock::time_point{};
    eve::FrameCaptureStore store([&] { return now; });
    store.configure(caps(16));
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Final};
    request.requestKey  = "same";
    auto queued         = store.request(request);
    ASSERT(queued.ok());
    auto retry = store.request(request);
    ASSERT(retry.ok());
    ASSERT(retry.value() == queued.value());
    auto tooLarge = store.begin(queued.value(), 1, 9);
    ASSERT(!tooLarge.ok());
    now += std::chrono::seconds(6);
    auto expired = store.snapshot(queued.value());
    ASSERT(expired.ok());
    ASSERT(expired.value().phase == eve::FrameCapturePhase::Expired);
    store.invalidate();
    auto stale = store.snapshot(queued.value());
    ASSERT(!stale.ok());
    request.requestKey.clear();
    auto next = store.request(request);
    ASSERT(next.ok());
    auto begin = store.begin(next.value(), 2, 4);
    ASSERT(begin.ok());
    auto cancel = store.release(next.value());
    ASSERT(cancel.ok());
    auto submit = store.submitted(next.value());
    ASSERT(!submit.ok());
    store.retire(next.value());
}
TEST_CASE("mcp.frameCapture.rawDepthRetainsMotionAndPreviewIsPng") {
    auto        snapshot = pixels();
    const auto& b        = snapshot.buffers[0];
    auto        pixel    = eve::frameCapturePixel(b, 0, 0);
    ASSERT(pixel.ok());
    ASSERT(pixel.value().stored[1] == 64);
    ASSERT(std::abs(pixel.value().decoded[1] - (128.0 / 255.0 - 1.0)) < 1e-8);
    auto outside = eve::frameCapturePixel(b, 1, 0);
    ASSERT(!outside.ok());
    auto image = eve::dev::frameCapturePreview(b, {});
    ASSERT(image.ok());
    ASSERT(image.value().width == 1);
    ASSERT(image.value().png[0] == 137);
    ASSERT(image.value().png[1] == 80);
    const auto& png = image.value().png;
    std::string compressed;
    for (size_t offset = 8; offset + 12 <= png.size();) {
        const uint32_t length = (uint32_t(png[offset]) << 24) | (uint32_t(png[offset + 1]) << 16) |
                                (uint32_t(png[offset + 2]) << 8) | png[offset + 3];
        ASSERT(offset + length + 12 <= png.size());
        if (std::memcmp(png.data() + offset + 4, "IDAT", 4) == 0)
            compressed.append(reinterpret_cast<const char*>(png.data() + offset + 8), length);
        offset += size_t(length) + 12;
    }
    std::istringstream         stream(compressed);
    Poco::InflatingInputStream inflated(stream, Poco::InflatingStreamBuf::STREAM_ZLIB);
    std::array<uint8_t, 5>     scanline{};
    inflated.read(reinterpret_cast<char*>(scanline.data()), 5);
    ASSERT(inflated.gcount() == 5);
    ASSERT(scanline[0] == 0);
    ASSERT((scanline[1] == 128 && scanline[2] == 128 && scanline[3] == 128 && scanline[4] == 255));
    eve::dev::FramePreviewOptions invalid;
    invalid.view     = "invented";
    auto unsupported = eve::dev::frameCapturePreview(b, invalid);
    ASSERT(!unsupported.ok());
    ASSERT(b.bytes->at(1) == 64);
}
TEST_CASE("mcp.frameCapture.providerAbsentAndSchemaRoutes") {
    auto* previous = eve::cap::query<eve::IRenderFrameCapture>();
    if (previous) eve::cap::revoke<eve::IRenderFrameCapture>(previous);
    const auto response = eve::dev::callMcpFrameCaptureTool("eve_frame_capture_capabilities", nullptr);
    ASSERT(response.find("ProviderAbsent") != std::string::npos);
    Poco::JSON::Parser parser;
    auto               tools =
        parser.parse("[" + std::string(eve::dev::mcpFrameCaptureSchemas()) + "]").extract<Poco::JSON::Array::Ptr>();
    ASSERT(tools->size() == 7);
    ASSERT(eve::dev::mcpToolMutates("eve_frame_capture_export"));
    ASSERT(!eve::dev::mcpToolMutates("eve_frame_capture"));
    for (unsigned i = 0; i < tools->size(); ++i)
        ASSERT(eve::dev::isMcpFrameCaptureTool(tools->getObject(i)->getValue<std::string>("name")));
    if (previous) eve::cap::provide<eve::IRenderFrameCapture>(previous);
}

TEST_CASE("mcp.frameCapture.depthProjectionAndBgraSemantics") {
    eve::FrameCaptureBuffer depth;
    depth.attachment = eve::FrameAttachment::HwDepth;
    depth.format     = eve::FramePixelFormat::Depth32Float;
    depth.width = depth.height = 1;
    auto        bytes          = std::make_shared<std::vector<uint8_t>>(4);
    const float raw            = 0.25f;
    std::memcpy(bytes->data(), &raw, 4);
    depth.bytes = bytes;
    std::array<float, 16> inverse{};
    inverse[0] = inverse[5] = inverse[15] = 1;
    inverse[14]                           = -1;
    inverse[11]                           = -0.9f;
    auto perspective                      = eve::frameCaptureLinearDepth(depth, 0, 0, inverse);
    ASSERT(perspective.ok());
    ASSERT(std::abs(perspective.value() - 1.0 / 0.775) < 1e-6);
    inverse[11]  = 0.9f;
    inverse[15]  = 0.1f;
    auto reverse = eve::frameCaptureLinearDepth(depth, 0, 0, inverse);
    ASSERT(reverse.ok());
    ASSERT(std::abs(reverse.value() - 1.0 / 0.325) < 1e-6);
    inverse[11]       = 0;
    inverse[15]       = 1;
    inverse[10]       = -9;
    auto orthographic = eve::frameCaptureLinearDepth(depth, 0, 0, inverse);
    ASSERT(orthographic.ok());
    ASSERT(std::abs(orthographic.value() - 3.25) < 1e-6);
    auto bgra   = pixels(eve::FrameAttachment::Final).buffers[0];
    bgra.format = eve::FramePixelFormat::Bgra8;
    auto pixel  = eve::frameCapturePixel(bgra, 0, 0);
    ASSERT(pixel.ok());
    ASSERT((pixel.value().stored[0] == 255 && pixel.value().stored[2] == 128));
}
TEST_CASE("mcp.frameCapture.sharedPhysicalStorageAndRetainedLeaseBudget") {
    eve::FrameCaptureStore store;
    store.configure(caps(8));
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Depth, eve::FrameAttachment::Velocity};
    auto queued         = store.request(request);
    ASSERT(queued.ok());
    auto begin = store.begin(queued.value(), 7, 4);
    ASSERT(begin.ok());
    auto submit = store.submitted(queued.value());
    ASSERT(submit.ok());
    auto snapshot       = pixels();
    auto velocity       = snapshot.buffers[0];
    velocity.attachment = eve::FrameAttachment::Velocity;
    snapshot.buffers.push_back(velocity);
    auto done = store.complete(queued.value(), std::move(snapshot));
    ASSERT(done.ok());
    store.retire(queued.value());
    auto lease = store.snapshot(queued.value());
    ASSERT(lease.ok());
    auto released = store.release(queued.value());
    ASSERT(released.ok());
    auto next = store.request(request);
    ASSERT(next.ok());
    auto noRoom = store.begin(next.value(), 8, 4);
    ASSERT(!noRoom.ok());
}

TEST_CASE("mcp.frameCapture.protocolPreviewAndAtomicNativeExport") {
    eve::FrameCaptureStore store;
    store.configure(caps());
    struct Registration {
        eve::IRenderFrameCapture* previous = eve::cap::query<eve::IRenderFrameCapture>();
        eve::IRenderFrameCapture* current;
        explicit Registration(eve::IRenderFrameCapture* provider) : current(provider) {
            eve::cap::provide<eve::IRenderFrameCapture>(provider);
        }
        ~Registration() {
            eve::cap::revoke<eve::IRenderFrameCapture>(current);
            if (previous) eve::cap::provide<eve::IRenderFrameCapture>(previous);
        }
    } registration(&store);
    Poco::JSON::Parser parser;
    auto               call = [&](const char* name, Poco::JSON::Object::Ptr args) {
        return parser.parse(eve::dev::callMcpFrameCaptureTool(name, args)).extract<Poco::JSON::Object::Ptr>();
    };
    auto payload = [&](Poco::JSON::Object::Ptr response) {
        return parser.parse(response->getArray("content")->getObject(0)->getValue<std::string>("text"))
            .extract<Poco::JSON::Object::Ptr>();
    };
    Poco::JSON::Object::Ptr args    = new Poco::JSON::Object;
    Poco::JSON::Array::Ptr  buffers = new Poco::JSON::Array;
    buffers->add("gbuffer.depth");
    args->set("buffers", buffers);
    auto queued = payload(call("eve_frame_capture", args));
    REQUIRE(queued->getValue<std::string>("status") == "queued");
    const auto token = queued->getValue<std::string>("token");
    auto       begin = store.begin(token, 19, 4);
    REQUIRE(begin.ok());
    auto submitted = store.submitted(token);
    REQUIRE(submitted.ok());
    auto done = store.complete(token, pixels());
    REQUIRE(done.ok());
    store.retire(token);
    args = new Poco::JSON::Object;
    args->set("token", token);
    args->set("buffer", "gbuffer.depth");
    auto preview = payload(call("eve_frame_buffer_image", args));
    REQUIRE(preview->getValue<std::string>("status") == "pending");
    auto poll = [&](const std::string& job) {
        Poco::JSON::Object::Ptr query = new Poco::JSON::Object;
        query->set("token", job);
        Poco::JSON::Object::Ptr response;
        for (int attempt = 0; attempt < 100; ++attempt) {
            response = call("eve_frame_capture_status", query);
            if (response->getArray("content")->getObject(0)->getValue<std::string>("type") == "image") return response;
            if (payload(response)->getValue<std::string>("status") != "pending") return response;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return response;
    };
    auto image = poll(preview->getValue<std::string>("jobToken"));
    REQUIRE(image->getArray("content")->getObject(0)->getValue<std::string>("type") == "image");
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("eve-frame-capture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(directory / "depth.bin", ignored);
            std::filesystem::remove(directory / "depth.bin.json", ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{directory};
    args->set("encoding", "nativeBinary");
    args->set("path", (directory / "depth.bin").string());
    auto exportJob = payload(call("eve_frame_capture_export", args));
    auto exported  = payload(poll(exportJob->getValue<std::string>("jobToken")));
    REQUIRE(exported->getValue<std::string>("status") == "ready");
    std::ifstream     bytes(directory / "depth.bin", std::ios::binary);
    const std::string raw{std::istreambuf_iterator<char>(bytes), std::istreambuf_iterator<char>()};
    REQUIRE(raw.size() == 4);
    REQUIRE(static_cast<unsigned char>(raw[1]) == 64);
    bytes.close();
    std::ifstream     metadata(directory / "depth.bin.json");
    const std::string json{std::istreambuf_iterator<char>(metadata), std::istreambuf_iterator<char>()};
    REQUIRE(parser.parse(json).extract<Poco::JSON::Object::Ptr>()->getValue<std::string>("byteOrder") ==
            "little-endian");
    metadata.close();
    args->set("view", "rawChannels");
    auto collision = payload(call("eve_frame_capture_export", args));
    auto rejected  = payload(poll(collision->getValue<std::string>("jobToken")));
    REQUIRE(rejected->getValue<std::string>("status") == "failed");
    REQUIRE(std::filesystem::file_size(directory / "depth.bin") == 4);
    eve::dev::resetMcpFrameCapture();
}
