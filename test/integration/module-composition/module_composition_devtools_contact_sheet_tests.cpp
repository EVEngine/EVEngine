// clang-format off
/* eve-test-policy:
{"schema":"evengine.test-policy","version":1,"module":"devtools"}
*/
// clang-format on
#include "common/Capability.h"
#include "common/FrameCapture.h"
#include "devtools/FrameContactSheet.hpp"
#include "devtools/McpFrameCapture.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <Poco/InflatingStream.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <cstring>
#include <sstream>
#include <thread>

TEST_CASE("mcp.contactSheet.compositionAndCancellation") {
    eve::FrameCaptureStore        store;
    eve::FrameCaptureCapabilities caps;
    caps.attachments = {eve::FrameAttachment::Final};
    store.configure(caps);
    struct Registration {
        eve::IRenderFrameCapture* previous = eve::cap::query<eve::IRenderFrameCapture>();
        eve::IRenderFrameCapture* current;
        explicit Registration(eve::IRenderFrameCapture* provider) : current(provider) {
            eve::cap::provide<eve::IRenderFrameCapture>(provider);
        }
        ~Registration() {
            eve::dev::resetMcpFrameCapture();
            eve::cap::revoke<eve::IRenderFrameCapture>(current);
            if (previous) eve::cap::provide<eve::IRenderFrameCapture>(previous);
        }
    } registration(&store);
    Poco::JSON::Array::Ptr tokens = new Poco::JSON::Array;
    for (unsigned i = 0; i < 4; ++i) {
        eve::FrameCaptureRequest request;
        request.attachments = {eve::FrameAttachment::Final};
        auto queued         = store.request(request);
        REQUIRE(queued.ok());
        const auto token = queued.value();
        tokens->add(token);
        auto begun = store.begin(token, i + 10, 4);
        REQUIRE(begun.ok());
        auto submitted = store.submitted(token);
        REQUIRE(submitted.ok());
        eve::FrameCaptureSnapshot snapshot;
        eve::FrameCaptureBuffer   buffer;
        buffer.width = buffer.height = 1;
        buffer.bytes                 = std::make_shared<const std::vector<uint8_t>>(4, 255);
        snapshot.buffers.push_back(buffer);
        auto complete = store.complete(token, std::move(snapshot));
        REQUIRE(complete.ok());
        store.retire(token);
    }
    Poco::JSON::Parser parser;
    auto               payload = [&](const std::string& response) {
        auto envelope = parser.parse(response).extract<Poco::JSON::Object::Ptr>();
        return parser.parse(envelope->getArray("content")->getObject(0)->getValue<std::string>("text"))
            .extract<Poco::JSON::Object::Ptr>();
    };
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object;
    args->set("tokens", tokens);
    args->set("cellWidth", 4);
    args->set("cellHeight", 4);
    auto start = payload(eve::dev::callMcpFrameCaptureTool("eve_frame_contact_sheet", args));
    REQUIRE(start->getValue<std::string>("status") == "pending");
    Poco::JSON::Object::Ptr poll = new Poco::JSON::Object;
    poll->set("token", start->getValue<std::string>("jobToken"));
    std::string response;
    for (unsigned attempt = 0; attempt < 200; ++attempt) {
        response = eve::dev::callMcpFrameCaptureTool("eve_frame_capture_status", poll);
        if (response.find("image/png") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(response.find("image/png") != std::string::npos);
    auto imageEnvelope = parser.parse(response).extract<Poco::JSON::Object::Ptr>();
    auto metadata      = parser.parse(imageEnvelope->getArray("content")->getObject(1)->getValue<std::string>("text"))
                        .extract<Poco::JSON::Object::Ptr>();
    ASSERT(!metadata->getValue<bool>("sameSimulationInstantVerified"));
    REQUIRE(metadata->getArray("cells")->size() == 4);
    auto lastCell = metadata->getArray("cells")->getObject(3);
    ASSERT(lastCell->getValue<unsigned>("frameId") == 13);
    ASSERT(lastCell->getValue<unsigned>("x") == 4);
    ASSERT(lastCell->getValue<unsigned>("y") == 4);
    Poco::JSON::Object::Ptr release = new Poco::JSON::Object;
    release->set("token", tokens->getElement<std::string>(3));
    const auto released = eve::dev::callMcpFrameCaptureTool("eve_frame_capture_release", release);
    ASSERT(released.find("released") != std::string::npos);
    ASSERT(eve::dev::callMcpFrameCaptureTool("eve_frame_capture_status", poll).find("CancelledJob") !=
           std::string::npos);
    ASSERT(eve::dev::callMcpFrameCaptureTool("eve_frame_contact_sheet", args).find("CaptureNotReady") !=
           std::string::npos);
    args->set("unknown", true);
    ASSERT(eve::dev::callMcpFrameCaptureTool("eve_frame_contact_sheet", args).find("InvalidArguments") !=
           std::string::npos);
    store.invalidate();
    ASSERT(eve::dev::callMcpFrameCaptureTool("eve_frame_capture_status", poll).find("StaleJob") != std::string::npos);
}

namespace {
eve::FrameCaptureBuffer solid(uint8_t r, uint8_t g, uint8_t b) {
    eve::FrameCaptureBuffer frame;
    frame.width = frame.height = 1;
    frame.bytes = std::make_shared<const std::vector<uint8_t>>(std::initializer_list<uint8_t>{r, g, b, 255});
    return frame;
}
std::vector<uint8_t> scanlines(const eve::dev::FramePreview& image) {
    std::string compressed;
    const auto& png = image.png;
    for (size_t offset = 8; offset + 12 <= png.size();) {
        const uint32_t size = (uint32_t(png[offset]) << 24) | (uint32_t(png[offset + 1]) << 16) |
                              (uint32_t(png[offset + 2]) << 8) | png[offset + 3];
        REQUIRE(size_t(size) + offset + 12 <= png.size());
        if (std::memcmp(png.data() + offset + 4, "IDAT", 4) == 0)
            compressed.append(reinterpret_cast<const char*>(png.data() + offset + 8), size);
        offset += size_t(size) + 12;
    }
    std::istringstream         input(compressed);
    Poco::InflatingInputStream inflate(input, Poco::InflatingStreamBuf::STREAM_ZLIB);
    std::vector<uint8_t>       out((size_t(image.width) * 4 + 1) * image.height);
    inflate.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    REQUIRE(inflate.gcount() == static_cast<std::streamsize>(out.size()));
    return out;
}
}  // namespace

TEST_CASE("devtools.contactSheet.rowMajorAndBgra") {
    std::vector<eve::FrameCaptureBuffer> frames{solid(255, 0, 0), solid(0, 255, 0), solid(255, 0, 0),
                                                solid(255, 255, 0)};
    frames[2].format = eve::FramePixelFormat::Bgra8;
    eve::dev::FrameContactSheetOptions options;
    options.cellWidth = options.cellHeight = 1;
    auto result                            = eve::dev::frameContactSheet(frames, options);
    REQUIRE(result.ok());
    ASSERT(result.value().width == 2);
    ASSERT(result.value().height == 2);
    const auto                 decoded = scanlines(result.value());
    const std::vector<uint8_t> expected{0, 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 0, 255, 255, 255, 255, 0, 255};
    ASSERT(decoded == expected);
    ASSERT((*frames[2].bytes)[0] == 255);
}

TEST_CASE("devtools.contactSheet.letterboxAndRejectInvalidStorage") {
    std::vector<eve::FrameCaptureBuffer> frames{solid(255, 0, 0)};
    eve::dev::FrameContactSheetOptions   options;
    options.columns   = 1;
    options.cellWidth = options.cellHeight = 3;
    auto result                            = eve::dev::frameContactSheet(frames, options);
    REQUIRE(result.ok());
    const auto decoded = scanlines(result.value());
    ASSERT(decoded[1] == 0);
    ASSERT(decoded[18] == 255);
    ASSERT(decoded[19] == 0);
    frames[0].width = 2;
    auto malformed  = eve::dev::frameContactSheet(frames, options);
    ASSERT(!malformed.ok());
    frames[0]        = solid(1, 2, 3);
    frames[0].format = eve::FramePixelFormat::Depth32Float;
    auto unsupported = eve::dev::frameContactSheet(frames, options);
    ASSERT(!unsupported.ok());
    options.columns = 0;
    auto zero       = eve::dev::frameContactSheet(frames, options);
    ASSERT(!zero.ok());
    options.columns   = 1;
    options.cellWidth = 2049;
    auto oversized    = eve::dev::frameContactSheet(frames, options);
    ASSERT(!oversized.ok());
}

TEST_CASE("devtools.contactSheet.retainsSixteenFramesWithinByteBudget") {
    eve::FrameCaptureStore        store;
    eve::FrameCaptureCapabilities caps;
    caps.attachments = {eve::FrameAttachment::Final};
    store.configure(caps);
    std::vector<std::string> tokens;
    for (unsigned i = 0; i < 16; ++i) {
        eve::FrameCaptureRequest request;
        request.attachments = {eve::FrameAttachment::Final};
        auto queued         = store.request(request);
        REQUIRE(queued.ok());
        tokens.push_back(queued.value());
        auto busy = store.request(request);
        ASSERT(!busy.ok());
        auto begun = store.begin(tokens.back(), i, 4);
        REQUIRE(begun.ok());
        auto submitted = store.submitted(tokens.back());
        REQUIRE(submitted.ok());
        eve::FrameCaptureSnapshot snapshot;
        snapshot.buffers.push_back(solid(1, 2, 3));
        auto completed = store.complete(tokens.back(), std::move(snapshot));
        REQUIRE(completed.ok());
        store.retire(tokens.back());
    }
    eve::FrameCaptureRequest request;
    request.attachments = {eve::FrameAttachment::Final};
    auto full           = store.request(request);
    ASSERT(!full.ok());
    auto released = store.release(tokens.front());
    REQUIRE(released.ok());
    auto next = store.request(request);
    ASSERT(next.ok());
}

TEST_CASE("devtools.contactSheet.aspectAndEncodingBudget") {
    auto                    pixels = std::make_shared<std::vector<uint8_t>>(4 * 2 * 4, 255);
    eve::FrameCaptureBuffer frame;
    frame.width  = 4;
    frame.height = 2;
    frame.bytes  = pixels;
    eve::dev::FrameContactSheetOptions options;
    options.columns   = 1;
    options.cellWidth = options.cellHeight = 2;
    std::vector<eve::FrameCaptureBuffer> frames{frame};
    auto                                 image = eve::dev::frameContactSheet(frames, options);
    REQUIRE(image.ok());
    const auto decoded = scanlines(image.value());
    ASSERT(decoded[1] == 255);
    ASSERT(decoded[10] == 0);
    pixels         = std::make_shared<std::vector<uint8_t>>(64 * 64 * 4);
    uint32_t state = 12345;
    for (auto& value : *pixels) {
        state = state * 1664525u + 1013904223u;
        value = static_cast<uint8_t>(state >> 24);
    }
    frames[0].width = frames[0].height = 64;
    frames[0].bytes                    = pixels;
    options.cellWidth = options.cellHeight = 64;
    options.maxBytes                       = 1024;
    auto tooLarge                          = eve::dev::frameContactSheet(frames, options);
    ASSERT(!tooLarge.ok());
    options.maxBytes = 32768;
    auto fits        = eve::dev::frameContactSheet(frames, options);
    ASSERT(fits.ok());
}
