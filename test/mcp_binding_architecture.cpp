#include "devtools/McpServer.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <chrono>
#include <sstream>
#include <thread>

TEST_CASE("devtools.mcp.bindingArchitectureEvidence") {
    auto& server = eve::dev::McpServer::instance();
    server.stop();
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_api_get\",\"arguments\":{\"key\":\"scene/SceneNodeRef.setVisible\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_api_get\",\"arguments\":{\"key\":\"scene/SceneNodeRef.isVisible\"}}}\n");
    std::ostringstream output;
    struct StopServer {
        eve::dev::McpServer& server;
        ~StopServer() { server.stop(); }
    } cleanup{server};
    REQUIRE(server.listenStdio(input, output));
    for (int attempt = 0; attempt < 200; ++attempt) {
        server.poll();
        if (output.str().find("\"id\":3") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    server.stop();
    std::istringstream responses(output.str());
    std::string        line;
    int                checked = 0;
    while (std::getline(responses, line)) {
        Poco::JSON::Parser parser;
        auto               response = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        const int          id       = response->getValue<int>("id");
        if (id == 1) continue;
        auto content = response->getObject("result")->getArray("content");
        auto payload =
            parser.parse(content->getObject(0)->getValue<std::string>("text")).extract<Poco::JSON::Object::Ptr>();
        REQUIRE(payload->getValue<bool>("ok"));
        auto contract = payload->getObject("contract");
        auto mapping  = contract->getObject("architecture");
        if (id == 2) {
            CHECK_EQ(mapping->getValue<std::string>("status"), std::string("mapped"));
            CHECK_EQ(mapping->getValue<std::string>("stateOwner"), std::string("eve::scene::SceneHost"));
            CHECK_EQ(contract->getValue<std::string>("cppSymbol"), std::string("SceneNodeRef::setVisible"));
            CHECK_EQ(mapping->getArray("tests")->size(), size_t(3));
            CHECK_EQ(mapping->getValue<std::string>("inputHash").size(), size_t(64));
            CHECK(contract->getValue<std::string>("source").find("Scene.cpp:") != std::string::npos);
        } else {
            CHECK_EQ(mapping->getValue<std::string>("status"), std::string("unknown"));
        }
        ++checked;
    }
    CHECK_EQ(checked, 2);
}
