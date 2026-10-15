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
[[maybe_unused]] Poco::JSON::Object::Ptr payload(const std::string& envelope) {
    Poco::JSON::Parser parser;
    auto               outer = parser.parse(envelope).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(outer->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
[[maybe_unused]] Poco::JSON::Object::Ptr arguments(const std::string& seed) {
    Poco::JSON::Object::Ptr args = new Poco::JSON::Object();
    args->set("seed", seed);
    return args;
}
[[maybe_unused]] bool containsNode(Poco::JSON::Object::Ptr result, const std::string& id) {
    auto nodes = result->getArray("nodes");
    for (size_t i = 0; i < nodes->size(); ++i)
        if (nodes->getObject(static_cast<unsigned>(i))->getValue<std::string>("id") == id) return true;
    return false;
}
}  // namespace


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
