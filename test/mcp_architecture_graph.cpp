#include <Poco/JSON/Parser.h>
#include <chrono>
#include <sstream>
#include <thread>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/McpArchitectureTools.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/McpToolFilter.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
Poco::JSON::Object::Ptr payload(const std::string& envelope) {
    Poco::JSON::Parser parser;
    auto               outer = parser.parse(envelope).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(outer->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
Poco::JSON::Object::Ptr arguments(const std::string& seed) {
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object();
    args->set("seed", seed);
    return args;
}
bool containsNode(Poco::JSON::Object::Ptr result, const std::string& id) {
    auto nodes = result->getArray("nodes");
    for (size_t i = 0; i < nodes->size(); ++i)
        if (nodes->getObject(static_cast<unsigned>(i))->getValue<std::string>("id") == id) return true;
    return false;
}
}  // namespace
TEST_CASE("devtools.architecture.impactConnectsWriterAndReader") {
    auto       args   = arguments("inventory/Bag.setExtra");
    const auto result = payload(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    REQUIRE(result->getValue<bool>("ok"));
    CHECK(containsNode(result, "api:inventory/Bag.getExtra"));
    CHECK(containsNode(result, "state:eve::inventory::Bag.extra_"));
    CHECK(containsNode(result, "owner:eve::inventory::Bag"));
    CHECK(!containsNode(result, "api:scene/SceneNodeRef.setVisible"));
    CHECK(!result->getValue<bool>("truncated"));
    CHECK(result->getArray("tests")->size() > 0);
    CHECK(result->getArray("inputHashes")->size() == 2);
    const auto repeat = eve::dev::callMcpArchitectureTool("eve_architecture_impact", args);
    CHECK_EQ(repeat, eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    CHECK(!eve::dev::mcpToolMutates("eve_architecture_impact"));
    auto sceneArgs = arguments("scene/SceneNodeRef.setVisible");
    sceneArgs->set("task", "Investigate visibility");
    auto scene = payload(eve::dev::callMcpArchitectureTool("eve_task_context", sceneArgs));
    REQUIRE(scene->getValue<bool>("ok"));
    CHECK(containsNode(scene, "owner:eve::scene::SceneHost"));
    CHECK(containsNode(scene, "state:eve::scene::SceneNode.visible"));
    CHECK(!containsNode(scene, "api:inventory/Bag.setExtra"));
}
TEST_CASE("devtools.architecture.budgetsUnknownsAndStaleEvidence") {
    auto args    = arguments("scene/SceneNodeRef.isVisible");
    auto unknown = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    CHECK_EQ(unknown->getValue<std::string>("error"), std::string("invalid_query"));
    args->set("task", "Investigate visibility");
    unknown = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    REQUIRE(unknown->getValue<bool>("ok"));
    CHECK(unknown->getArray("unknowns")->size() > 0);
    args = arguments("inventory/Bag.setExtra");
    args->set("maxNodes", 1);
    auto bounded = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK(bounded->getValue<bool>("truncated"));
    CHECK_EQ(bounded->getArray("nodes")->size(), size_t(1));
    args->set("maxNodes", 64);
    args->set("maxDepth", 0);
    auto shallow = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK(shallow->getValue<bool>("truncated"));
    CHECK_EQ(shallow->getArray("nodes")->size(), size_t(1));
    args->set("maxDepth", 6);
    args->set("maxBytes", 1024);
    auto exhausted = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(exhausted->getValue<std::string>("error"), std::string("context_budget_exceeded"));
    args->set("maxBytes", 16384);
    args->set("expectedInputHash", "old-version");
    auto stale = payload(eve::dev::callMcpArchitectureTool("eve_architecture_impact", args));
    CHECK_EQ(stale->getValue<std::string>("error"), std::string("stale_evidence"));
    const auto* contract = eve::script::generatedBindingContracts().find("inventory/Bag.setExtra");
    REQUIRE(contract != nullptr);
    REQUIRE(contract->architecture.has_value());
    args->set("expectedInputHash", contract->architecture->inputHash);
    args->set("task", "Change quest extras");
    auto context = payload(eve::dev::callMcpArchitectureTool("eve_task_context", args));
    REQUIRE(context->getValue<bool>("ok"));
    CHECK_EQ(context->getValue<std::string>("schema"), std::string("eve.task-context"));
    CHECK(containsNode(context, "api:inventory/Bag.getExtra"));
    CHECK(context->getArray("tests")->size() > 0);
    CHECK_EQ(context->getArray("contracts")->size(), size_t(2));
    bool readerContract = false;
    for (size_t index = 0; index < context->getArray("contracts")->size(); ++index) {
        auto item = context->getArray("contracts")->getObject(static_cast<unsigned>(index));
        if (item->getValue<std::string>("method") != "getExtra") continue;
        CHECK_EQ(item->getArray("parameters")->size(), size_t(2));
        CHECK_EQ(item->getObject("architecture")->getArray("reads")->size(), size_t(1));
        CHECK_EQ(item->getObject("architecture")->getArray("writes")->size(), size_t(0));
        readerContract = true;
    }
    CHECK(readerContract);
    args->set("seed", "not-a-real-api");
    auto missing = payload(eve::dev::callMcpArchitectureTool("eve_architecture_query", args));
    CHECK_EQ(missing->getValue<std::string>("error"), std::string("unknown_seed"));
}
TEST_CASE("devtools.architecture.mcpStdioListsAndQueriesGraph") {
    auto& server = eve::dev::McpServer::instance();
    server.stop();
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":0,\"method\":\"initialize\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/"
        "call\",\"params\":{\"name\":\"eve_task_context\",\"arguments\":{\"seed\":\"inventory/"
        "Bag.setExtra\",\"task\":\"Change quest extras\"}}}\n");
    std::ostringstream output;
    struct Stop {
        eve::dev::McpServer& server;
        ~Stop() { server.stop(); }
    } stop{server};
    REQUIRE(server.listenStdio(input, output));
    for (int attempt = 0; attempt < 200; ++attempt) {
        server.poll();
        if (output.str().find("\"id\":2") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    server.stop();
    CHECK(output.str().find("eve_architecture_query") != std::string::npos);
    std::istringstream responses(output.str());
    std::string        line;
    bool               checked = false;
    while (std::getline(responses, line)) {
        Poco::JSON::Parser parser;
        auto               response = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        if (response->getValue<int>("id") != 2) continue;
        auto content = response->getObject("result")->getArray("content");
        auto context =
            parser.parse(content->getObject(0)->getValue<std::string>("text")).extract<Poco::JSON::Object::Ptr>();
        REQUIRE(context->getValue<bool>("ok"));
        CHECK(containsNode(context, "api:inventory/Bag.getExtra"));
        checked = true;
    }
    CHECK(checked);
}
