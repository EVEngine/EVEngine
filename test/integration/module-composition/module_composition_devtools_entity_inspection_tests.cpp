// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"devtools"}
#include <Poco/JSON/Parser.h>
#include <chrono>
#include <thread>
#include "common/Capability.h"
#include "common/EntityInspection.h"
#include "devtools/McpEntityInspection.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("mcp.entityInspection.providerAndEncodingContract") {
    using namespace eve;
    struct Provider final : IEntityInspection {
        FrameCaptureStore  store;
        EntityViewSnapshot value;
        Provider() {
            FrameCaptureCapabilities caps;
            caps.attachments = {FrameAttachment::Final};
            store.configure(caps);
        }
        Result<std::string> request(EntityViewRequest request) override {
            value.request = request;
            FrameCaptureRequest input;
            input.attachments = {FrameAttachment::Final};
            return store.request(input);
        }
        Result<EntityViewSnapshot> snapshot(std::string_view token) override {
            auto state = store.snapshot(token);
            if (!state.ok()) return Result<EntityViewSnapshot>::failure(state.status());
            auto result    = value;
            result.capture = std::move(state.value());
            return Result<EntityViewSnapshot>::success(std::move(result));
        }
        Result<FrameCapturePhase> release(std::string_view token) override { return store.release(token); }
    } provider;
    struct Registration {
        IEntityInspection* previous = cap::query<IEntityInspection>();
        ~Registration() { cap::provide<IEntityInspection>(previous); }
    } registration;
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object;
    args->set("domain", "renderable3d");
    args->set("mode", "geometry");
    args->set("entityId", 7);
    args->set("generation", 2);
    args->set("cellSize", 64);
    cap::provide<IEntityInspection>(nullptr);
    ASSERT(dev::callMcpEntityInspectionTool("eve_entity_views", args).find("unavailable") != std::string::npos);
    cap::provide<IEntityInspection>(&provider);
    args->set("unexpected", 1);
    ASSERT(dev::callMcpEntityInspectionTool("eve_entity_views", args).find("Unknown argument") != std::string::npos);
    args->remove("unexpected");
    Poco::JSON::Parser parser;
    auto               response =
        parser.parse(dev::callMcpEntityInspectionTool("eve_entity_views", args)).extract<Poco::JSON::Object::Ptr>();
    auto body = parser.parse(response->getArray("content")->getObject(0)->getValue<std::string>("text"))
                    .extract<Poco::JSON::Object::Ptr>();
    REQUIRE(body->getValue<std::string>("status") == "queued");
    const auto              token = body->getValue<std::string>("token");
    Poco::JSON::Object::Ptr poll  = new Poco::JSON::Object;
    poll->set("token", token);
    auto begin = provider.store.begin(token, 42, 16);
    REQUIRE(begin.ok());
    auto submitted = provider.store.submitted(token);
    REQUIRE(submitted.ok());
    FrameCaptureSnapshot snapshot;
    FrameCaptureBuffer   buffer;
    buffer.width = buffer.height = 2;
    buffer.bytes                 = std::make_shared<const std::vector<uint8_t>>(16, 255);
    snapshot.buffers.push_back(buffer);
    auto complete = provider.store.complete(token, std::move(snapshot));
    REQUIRE(complete.ok());
    provider.store.retire(token);
    std::string image;
    for (int i = 0; i < 200; ++i) {
        image = dev::callMcpEntityInspectionTool("eve_entity_views_status", poll);
        if (image.find("image/png") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(image.find("image/png") != std::string::npos);
    REQUIRE(image.find("world-normal-rgb") != std::string::npos);
    REQUIRE(image.find("sameFrozenGeometry") != std::string::npos);
    const auto release = dev::callMcpEntityInspectionTool("eve_entity_views_release", poll);
    REQUIRE(release.find("released") != std::string::npos);
    REQUIRE(dev::callMcpEntityInspectionTool("eve_entity_views_status", poll).find("image/png") == std::string::npos);
}
