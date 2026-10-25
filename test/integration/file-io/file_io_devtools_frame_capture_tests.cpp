// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "platform-file",
  "module": "devtools"
}
*/
// clang-format on

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
