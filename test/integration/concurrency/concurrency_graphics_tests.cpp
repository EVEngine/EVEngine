// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "default": "concurrency",
  "module": "animation",
  "modules": [
    "animation",
    "cmdline",
    "common",
    "devtools",
    "network",
    "pixelworld_thread",
    "procgen",
    "thread",
    "voxel"
  ],
  "origins": [
    "animation_batch_lod.cpp",
    "animation_binary.cpp",
    "cmdline_sdk_hash.cpp",
    "common_diagnostic_vector_instances.cpp",
    "mcp_architecture_graph.cpp",
    "mcp_binding_architecture.cpp",
    "mcp_console.cpp",
    "mcp_domain_tools.cpp",
    "mcp_editor_targets.cpp",
    "mcp_input_tools.cpp",
    "mcp_stdio.cpp",
    "mcp_tool_filter.cpp",
    "network.cpp",
    "pixelworld_parallel.cpp",
    "procgen_result_contract.cpp",
    "procgen_rng_contract.cpp",
    "render_graph.cpp",
    "thread.cpp",
    "voxel.cpp"
  ],
  "cases": {
    "platform-network": [
      "event.VariantMessage",
      "network.Channel.fragmentedAndCoalescedFrames",
      "network.Channel.rejectsOversizedFrameHeader",
      "network.NetReaderOverrunIsSticky",
      "network.NetWorkerQueue",
      "network.NetWriterReaderRoundtrip",
      "network.Network.cppFactoriesReturnOwners",
      "network.Network.timeoutAndVerifySsl"
    ],
    "runtime-lifecycle": [
      "devtools.mcp.inputBatchWaitCompletesAcrossPoll"
    ],
    "script-binding": [
      "devtools.mcp.consoleToolsRoundTripScriptPrint",
      "procgen.result.squirrelExecutionAndSchedulerShareProjection",
      "procgen.scriptHostCommitsAndRollsBackGeneratorsAtomically"
    ]
  }
}
*/
// clang-format on

// Related integration contracts share one translation unit to amortize header parsing.

// Contract section: animation batch lod
#include "PrivateTestScope.h"
#include "zeroerr/assert.h"

#include <cmath>
#include <memory>
#include <vector>
#include "animation/AnimBatch.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"
#include "zeroerr/unittest.h"
// animation_batch_lod.cpp
namespace test_animation_batch_lod {
using namespace eve::animation;

TEST_CASE("animation.batch.parallelEvaluationAndBoneLod") {
    AnimSkeleton skeleton;
    const int    root   = skeleton.addBone("root");
    const int    spine  = skeleton.addBone("spine", root);
    const int    finger = skeleton.addBone("finger", spine);
    skeleton.setBindPosition(finger, 3.f, 0.f, 0.f);
    skeleton.setBoneLodLimit(finger, 0);

    AnimClip clip("move");
    clip.setDuration(1.f);
    clip.addPositionKey(root, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(root, 1.f, 10.f, 0.f, 0.f);
    clip.addPositionKey(finger, 0.f, 3.f, 0.f, 0.f);
    clip.addPositionKey(finger, 1.f, 9.f, 0.f, 0.f);

    std::vector<std::unique_ptr<AnimPose>> poses;
    AnimBatch                              batch;
    for (int i = 0; i < 8; ++i) {
        poses.push_back(std::make_unique<AnimPose>());
        batch.add(&clip, &skeleton, poses.back().get(), 0.25f, 1);
    }
    batch.evaluate(4);

    CHECK(batch.getLastWorkerCount() == 4);
    for (const auto& pose : poses) {
        CHECK(std::fabs(pose->getLocalPositionX(root) - 2.5f) < 1e-5f);
        CHECK(std::fabs(pose->getLocalPositionX(finger) - 3.f) < 1e-5f);
    }
}
}  // namespace test_animation_batch_lod

// Contract section: animation binary
#include <stdexcept>
#include "PrivateTestScope.h"

#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include "PrivateTestScope.h"
#include "animation/AnimClip.h"
#include "animation/AnimClipBinary.h"
#include "animation/AnimParallelInternal.h"
// animation_binary.cpp
namespace test_animation_binary {
using namespace eve::animation;
namespace {
using Bytes = std::vector<std::byte>;
[[maybe_unused]] void integer(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::byte>((v >> (8 * i)) & 255));
}
[[maybe_unused]] void number(Bytes& b, float v) { integer(b, std::bit_cast<std::uint32_t>(v)); }
[[maybe_unused]] void string(Bytes& b, const std::string& v) {
    integer(b, static_cast<std::uint32_t>(v.size()));
    for (auto c : v) b.push_back(static_cast<std::byte>(c));
}
[[maybe_unused]] Bytes fixture() {
    Bytes b;
    integer(b, 0x43415645);
    integer(b, 1);
    integer(b, 1);
    number(b, 2.f);
    number(b, 30.f);
    integer(b, 1);
    string(b, "test");
    string(b, "root");
    integer(b, 2);
    for (float t : {0.f, 2.f}) {
        number(b, t);
        number(b, 0.f);
        number(b, 0.f);
        number(b, 3.f * t);
    }
    integer(b, 1);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 0.f);
    number(b, 1.f);
    integer(b, 1);
    number(b, 0.f);
    number(b, 1.f);
    number(b, 1.f);
    number(b, 1.f);
    return b;
}
}  // namespace
TEST_CASE("animation.binary.parallelBatchMatchesSerialAndRejectsAtomically") {
    AnimSkeleton sk;
    sk.addBone("root");
    const auto bytes = fixture();
    for (int workers : {1, 2, 8}) {
        std::vector<AnimClip>            clips(32);
        std::vector<AnimationTrackInput> inputs;
        for (auto& clip : clips) inputs.push_back({clip, bytes});
        auto loaded = loadAnimationTrackBatch(inputs, sk, workers);
        REQUIRE(loaded.ok());
        CHECK(loaded.value() >= 1);
        CHECK(loaded.value() <= workers);
        for (auto& clip : clips) {
            AnimPose pose;
            clip.sample(0.5f, &pose, &sk);
            CHECK(pose.getLocalPositionZ(0) == 1.5f);
            clip.setName("unchanged");
        }
        inputs[17].bytes = std::span<const std::byte>(bytes).first(12);
        auto rejected    = loadAnimationTrackBatch(inputs, sk, workers);
        CHECK(!rejected.ok());
        for (auto& clip : clips) CHECK(clip.getName() == "unchanged");
        inputs[17].bytes       = bytes;
        inputs[31].destination = clips[0];
        auto duplicate         = loadAnimationTrackBatch(inputs, sk, workers);
        CHECK(!duplicate.ok());
        for (auto& clip : clips) CHECK(clip.getName() == "unchanged");
    }
    auto empty = loadAnimationTrackBatch({}, sk);
    REQUIRE(empty.ok());
    CHECK(empty.value() == 0);
    auto invalid = loadAnimationTrackBatch({}, sk, -1);
    CHECK(!invalid.ok());
}
TEST_CASE("animation.binary.parallelWorkersJoinBeforeFailureReturns") {
    std::vector<int> visited(64, 0);
    bool             caught = false;
    try {
        eve::animation::detail::parallelAnimationItems(visited.size(), 8, [&](std::size_t i) {
            visited[i] = 1;
            if (i == 3 || i == 30) throw std::runtime_error(std::to_string(i));
        });
    } catch (const std::runtime_error& error) {
        caught = true;
        CHECK(std::string(error.what()) == "3");
    }
    CHECK(caught);
    for (int item : visited) CHECK(item == 1);
}
}  // namespace test_animation_binary

// Contract section: cmdline sdk hash
#include "PrivateTestScope.h"

#include <array>
#include <barrier>
#include <fstream>
#include <future>
#include <string>
#include "Fixtures.h"
#include "cmdline/sdk_tools.h"
// cmdline_sdk_hash.cpp
namespace test_cmdline_sdk_hash {
TEST_CASE("cmdline.sdkSha256IndependentConcurrentReads") {
    TempDir dir;
    REQUIRE(!dir.path().empty());
    std::string binary;
    for (int i = 0; i < 10000; ++i) binary.append("\0\xff", 2);
    const std::array<std::string, 4> contents{"", "abc", "def", binary};
    const std::array<std::string, 4> expected{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                              "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
                                              "42f918becde1d7a197cb96baebd4e9f7217e4492414f277e39198c4a7d17721d"};
    std::array<std::string, 4>       paths;
    for (size_t i = 0; i < paths.size(); ++i) {
        paths[i] = (dir.path() / (std::to_string(i) + ".bin")).string();
        std::ofstream out(paths[i], std::ios::binary);
        out.write(contents[i].data(), static_cast<std::streamsize>(contents[i].size()));
        REQUIRE(out.good());
    }
    // Calls on different files must not share a subprocess output file. The
    // larger binary also crosses the hash reader's internal chunk boundary.
    std::barrier                                         start(8);
    std::vector<std::future<std::array<std::string, 4>>> reads;
    for (int worker = 0; worker < 8; ++worker) {
        reads.push_back(std::async(std::launch::async, [&, worker] {
            std::array<std::string, 4> result;
            start.arrive_and_wait();
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = eve::cmd::sdk::fileSha256(paths[(i + static_cast<size_t>(worker)) % paths.size()]);
            return result;
        }));
    }
    for (size_t worker = 0; worker < reads.size(); ++worker) {
        const auto result = reads[worker].get();
        for (size_t i = 0; i < result.size(); ++i) REQUIRE_EQ(result[i], expected[(i + worker) % expected.size()]);
    }
    REQUIRE(eve::cmd::sdk::fileSha256((dir.path() / "missing.bin").string()).empty());
}
}  // namespace test_cmdline_sdk_hash

// Contract section: common diagnostic vector instances
#include "PrivateTestScope.h"

#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include "common/Diagnostic.h"
#include "common/StringMapInstances.h"
#include "common/Subscription.h"
// common_diagnostic_vector_instances.cpp
namespace test_common_diagnostic_vector_instances {
// common_diagnostic_vector_instances.cpp


TEST_CASE("common.diagnosticVectorInstances.independentContainersOnThreads") {
    std::atomic<unsigned> failures{0};
    auto                  run = [&] {
        for (unsigned iteration = 0; iteration < 32; ++iteration) {
            std::vector<eve::Diagnostic> values;
            values.push_back(eve::Diagnostic::error(eve::DiagnosticCode::Failed, std::string(2048, 'a')));
            auto copied  = values;
            copied.at(0) = eve::Diagnostic::warning(eve::DiagnosticCode::Conflict, "changed");
            copied.reserve(128);
            if (values.at(0).message().size() != 2048 || copied.at(0).message() != "changed") ++failures;
        }
    };
    std::thread a(run), b(run), c(run), d(run);
    a.join();
    b.join();
    c.join();
    d.join();
    CHECK(failures.load() == 0u);
}

// common_string_map_instances.cpp
namespace {
using AttributeMap = std::unordered_map<std::string, std::string>;
static_assert(std::is_move_constructible_v<AttributeMap>);
static_assert(std::is_copy_constructible_v<AttributeMap>);
}  // namespace


TEST_CASE("common.stringMap.independentContainersOnThreads") {
    std::atomic<int>         failures{0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([worker, &failures] {
            for (int iteration = 0; iteration < 32; ++iteration) {
                const std::string expected(2048, static_cast<char>('a' + worker));
                AttributeMap      original{{"value", expected}};
                AttributeMap      copy(original);
                copy["iteration"] = std::to_string(iteration);
                copy.rehash(64);
                if (copy.at("value") != expected || original.size() != 1) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(failures.load() == 0);
}

// common_subscription.cpp
}  // namespace test_common_diagnostic_vector_instances

// Contract section: mcp architecture graph
#include "PrivateTestScope.h"

#include <Poco/JSON/Parser.h>
#include <chrono>
#include <sstream>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/McpArchitectureTools.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/McpToolFilter.hpp"
// mcp_architecture_graph.cpp
namespace test_mcp_architecture_graph {
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
}  // namespace test_mcp_architecture_graph

// Contract section: mcp binding architecture
#include "PrivateTestScope.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
// mcp_binding_architecture.cpp
namespace test_mcp_binding_architecture {
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

TEST_CASE("devtools.mcp.eveApiGetSuggestsOnMiss") {
    auto& server = eve::dev::McpServer::instance();
    server.stop();
    std::istringstream input(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_api_get\",\"arguments\":{\"key\":\"dialogue/publishGameplay\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_api_get\",\"arguments\":{\"key\":\"dialogueFlow.publishGameplay\"}}}\n");
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
    bool               sawSuggestions = false;
    bool               sawResolved    = false;
    while (std::getline(responses, line)) {
        Poco::JSON::Parser parser;
        auto               response = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        const int          id       = response->getValue<int>("id");
        if (id == 1) continue;
        auto content = response->getObject("result")->getArray("content");
        auto body =
            parser.parse(content->getObject(0)->getValue<std::string>("text")).extract<Poco::JSON::Object::Ptr>();
        if (id == 2) {
            CHECK(!body->getValue<bool>("ok"));
            auto suggestions = body->getArray("suggestions");
            REQUIRE(suggestions);
            REQUIRE(suggestions->size() > 0);
            bool found = false;
            for (size_t i = 0; i < suggestions->size(); ++i) {
                if (suggestions->getObject(static_cast<unsigned>(i))->getValue<std::string>("key") ==
                    "dialogueFlow/publishGameplay")
                    found = true;
            }
            CHECK(found);
            sawSuggestions = true;
        }
        if (id == 3) {
            REQUIRE(body->getValue<bool>("ok"));
            CHECK_EQ(body->getObject("contract")->getValue<std::string>("key"),
                     std::string("dialogueFlow/publishGameplay"));
            sawResolved = true;
        }
    }
    CHECK(sawSuggestions);
    CHECK(sawResolved);
}
}  // namespace test_mcp_binding_architecture

// Contract section: mcp console
#include "PrivateTestScope.h"

#include <Poco/Dynamic/Var.h>
#include <simplesquirrel/simplesquirrel.hpp>
#include "devtools/ConsolePanel.hpp"
#include "devtools/DevTool.hpp"
// mcp_console.cpp
namespace test_mcp_console {
// MCP runtime-console tools: an unattended agent must be able to read the same
// ordered diagnosis stream the game writes through print()/script errors, and
// must be able to tell "nothing new" apart from "lines were dropped".
//
// These cases pin three things:
//   1. ConsolePanel sequence cursors survive eviction and clear().
//   2. The MCP console tools round-trip over the stdio transport, including a
//      Squirrel print() captured into the same log.
//   3. Every tool advertised by tools/list is actually routed by tools/call, so
//      the hand-maintained schema table cannot drift from the dispatcher.


using namespace eve::dev;

namespace {

/** Run one pre-buffered stdio session and return the newline-framed responses. */
[[maybe_unused]] std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

[[maybe_unused]] Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Text payload of a tools/call response line (empty when the shape is wrong). */
[[maybe_unused]] std::string responseContentText(const std::string& line) {
    try {
        auto object = parseObject(line);
        if (!object || !object->has("result")) return {};
        auto result = object->getObject("result");
        if (!result || !result->has("content")) return {};
        auto content = result->getArray("content");
        if (!content || content->size() == 0) return {};
        auto item = content->getObject(0);
        if (!item || !item->has("text")) return {};
        return item->get("text").convert<std::string>();
    } catch (...) {
        return {};
    }
}

[[maybe_unused]] std::string initializeRequest(int id, const std::string& protocol) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"" + protocol + "\",\"capabilities\":{}," +
           "\"clientInfo\":{\"name\":\"mcp-console-test\",\"version\":\"0\"}}}";
}

[[maybe_unused]] std::string callRequest(int id, const std::string& name, const std::string& argumentsJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
           name + "\",\"arguments\":" + argumentsJson + "}}";
}

[[maybe_unused]] std::string joinRequests(const std::vector<std::string>& requests) {
    std::string out;
    for (const auto& request : requests) {
        out += request;
        out += '\n';
    }
    return out;
}

}  // namespace


TEST_CASE("devtools.mcp.consoleToolsRoundTripScriptPrint") {
    auto& console = ConsolePanel::instance();
    auto& dt      = DevTool::instance();
    dt.detach();
    console.setMaxEntries(500);
    console.clear();

    ssq::VM vm(512, ssq::Libs::ALL);
    dt.attach(vm, false);
    REQUIRE(console.isAttached());
    {
        auto script = vm.compileSource("print(\"boot-marker\\n\");\n");
        vm.run(script);
    }

    const std::vector<std::string> responses =
        runStdioSession(joinRequests({
                            initializeRequest(1, "2025-06-18"),
                            callRequest(2, "eve_console_write", "{\"text\":\"agent-marker\",\"level\":\"warn\"}"),
                            callRequest(3, "eve_console_read", "{\"sinceSeq\":0,\"limit\":50}"),
                            callRequest(4, "eve_console_write", "{\"text\":\"bad\",\"level\":\"print\"}"),
                            callRequest(5, "eve_console_read", "{\"level\":\"nope\"}"),
                            callRequest(6, "eve_console_clear", "{}"),
                            callRequest(7, "eve_console_read", "{\"sinceSeq\":0}"),
                        }),
                        7);
    REQUIRE(responses.size() == 7);

    // The Squirrel print() is now reachable through MCP: this is the whole point
    // of the console tool family.
    const std::string readText = responseContentText(responses[2]);
    CHECK(readText.find("boot-marker") != std::string::npos);
    CHECK(readText.find("agent-marker") != std::string::npos);
    CHECK(readText.find("\"attached\":true") != std::string::npos);
    CHECK(readText.find("\"truncated\":false") != std::string::npos);

    auto readObject = parseObject(readText);
    REQUIRE(readObject);
    const Poco::Int64 nextSeq = readObject->getValue<Poco::Int64>("nextSeq");
    CHECK(nextSeq > 1);
    auto lines = readObject->getArray("lines");
    REQUIRE(lines && lines->size() >= 2);
    Poco::Int64 previous = 0;
    for (std::size_t index = 0; index < lines->size(); ++index) {
        const Poco::Int64 seq = lines->getObject(static_cast<unsigned>(index))->getValue<Poco::Int64>("seq");
        CHECK(seq > previous);
        previous = seq;
    }
    CHECK(previous == nextSeq - 1);
    CHECK(readObject->getValue<Poco::Int64>("cursor") == nextSeq - 1);

    // An agent marker must not impersonate the game's print/result channels.
    CHECK(responseContentText(responses[3]).find("unknown level") != std::string::npos);
    CHECK(responseContentText(responses[4]).find("unknown level") != std::string::npos);

    // A tail read after clear reports the discarded history as a gap.
    auto cleared = parseObject(responseContentText(responses[5]));
    REQUIRE(cleared);
    CHECK(cleared->getValue<std::int64_t>("droppedThrough") == nextSeq - 1);
    auto afterClear = parseObject(responseContentText(responses[6]));
    REQUIRE(afterClear);
    CHECK_EQ(afterClear->getValue<int>("count"), 0);
    CHECK(afterClear->getValue<bool>("truncated"));
    CHECK_EQ(afterClear->getValue<std::int64_t>("droppedThrough"), nextSeq - 1);

    // Resuming from the last covered sequence has no gap or repeated lines.
    const auto resumed = runStdioSession(
        joinRequests({initializeRequest(1, "2025-06-18"),
                      callRequest(2, "eve_console_read", "{\"sinceSeq\":" + std::to_string(nextSeq - 1) + "}")}),
        2);
    REQUIRE_EQ(resumed.size(), std::size_t(2));
    auto incremental = parseObject(responseContentText(resumed[1]));
    REQUIRE(incremental);
    CHECK(!incremental->getValue<bool>("truncated"));
    auto resumedLines = incremental->getArray("lines");
    REQUIRE(resumedLines);
    CHECK_EQ(incremental->getValue<std::size_t>("count"), resumedLines->size());
    for (std::size_t index = 0; index < resumedLines->size(); ++index)
        CHECK(resumedLines->getObject(static_cast<unsigned>(index))->getValue<Poco::Int64>("seq") > nextSeq - 1);

    dt.detach();
    CHECK(!console.isAttached());
    console.setMaxEntries(500);
    console.clear();
}

TEST_CASE("devtools.mcp.everyDeclaredToolIsRouted") {
    const std::vector<std::string> listResponses =
        runStdioSession(joinRequests({initializeRequest(1, "2025-06-18"),
                                      "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}"}),
                        2);
    REQUIRE(listResponses.size() == 2);

    auto listObject = parseObject(listResponses[1]);
    REQUIRE(listObject);
    auto result = listObject->getObject("result");
    REQUIRE(result);
    auto tools = result->getArray("tools");
    REQUIRE(tools);
    REQUIRE(tools->size() > 100);

    std::vector<std::string> requests{initializeRequest(1, "2025-06-18")};
    std::vector<std::string> names;
    for (std::size_t index = 0; index < tools->size(); ++index) {
        auto tool = tools->getObject(static_cast<unsigned>(index));
        REQUIRE(tool);
        const std::string name = tool->getValue<std::string>("name");
        REQUIRE(!name.empty());
        names.push_back(name);
        requests.push_back(callRequest(static_cast<int>(index) + 2, name, "{}"));
    }

    const std::vector<std::string> responses = runStdioSession(joinRequests(requests), names.size() + 1);
    REQUIRE(responses.size() == names.size() + 1);
    std::string unrouted;
    for (std::size_t index = 0; index < names.size(); ++index) {
        const std::string text = responseContentText(responses[index + 1]);
        if (text.find("unknown tool") != std::string::npos) unrouted += names[index] + " ";
    }
    // Advertised tools with no dispatch branch fail here, with their names.
    CHECK_EQ(unrouted, std::string());
}

TEST_CASE("devtools.mcp.initializeNegotiatesSupportedProtocol") {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1, "2025-06-18"), initializeRequest(2, "1999-01-01")}), 2);
    REQUIRE(responses.size() == 2);

    auto supported = parseObject(responses[0])->getObject("result");
    REQUIRE(supported);
    CHECK(supported->getValue<std::string>("protocolVersion") == "2025-06-18");
    auto serverInfo = supported->getObject("serverInfo");
    REQUIRE(serverInfo);
    CHECK(!serverInfo->getValue<std::string>("name").empty());

    // An unknown revision must not be echoed back as if it were implemented.
    auto unknown = parseObject(responses[1])->getObject("result");
    REQUIRE(unknown);
    CHECK(unknown->getValue<std::string>("protocolVersion") == "2025-06-18");
}
}  // namespace test_mcp_console

// Contract section: mcp domain tools
#include "PrivateTestScope.h"

#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "common/Module.h"
#include "common/Profile.h"
#include "common/ProfilerQuery.h"
#include "common/SensingQuery.h"
#include "decal/Decal.h"
#include "devtools/McpDomainTools.hpp"
#include "profiler/Profiler.h"
#include "sensing/Sensing.h"
// mcp_domain_tools.cpp
namespace test_mcp_domain_tools {
// Capability-backed MCP domain tools.
//
// These families exist so a module that already owns a query capability is not
// invisible to an agent. Two contracts matter and are pinned here: the tool
// answers with live provider data, and when the provider is absent it says which
// capability is missing instead of silently reporting an empty world.


using namespace eve::dev;

namespace {

std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::string requests =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},\"clientInfo\":{\"name\":\"domain-test\"}}}\n") +
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
        "\",\"arguments\":" + argumentsJson + "}}\n";
    const std::vector<std::string> responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return {};
    try {
        Poco::JSON::Parser parser;
        auto               object  = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
        auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.decalToolsDriveTheLiveProvider") {
    auto* const decalModule = eve::ModuleManager::requireInstance<eve::decal::Decal>("Decal");
    REQUIRE(decalModule != nullptr);
    auto* provider = eve::cap::query<eve::IDecalQuery>();
    REQUIRE(provider != nullptr);
    provider->clearAll();
    provider->setLimit("blood", 0);  // no eviction, so counts stay predictable

    auto status = parseObject(callToolOnce("eve_decal_status", "{}"));
    REQUIRE(status);
    CHECK(status->getValue<bool>("ok"));
    CHECK_EQ(status->getValue<int>("count"), 0);

    auto projected = parseObject(callToolOnce("eve_decal_project", R"({"x":1.5,"y":0,"z":-2,"kind":"blood"})"));
    REQUIRE(projected);
    CHECK(projected->getValue<bool>("ok"));
    const int id = projected->getValue<int>("id");
    CHECK(id > 0);
    CHECK_EQ(projected->getValue<int>("count"), 1);
    CHECK_EQ(provider->count(), 1);

    // The default normal is up, so a caller that omits it gets a ground decal
    // rather than a rejected request.
    CHECK(callToolOnce("eve_decal_project", R"({"x":0,"y":0,"z":0,"kind":"blood"})").find("\"ok\":true") !=
          std::string::npos);

    const std::string removed = callToolOnce("eve_decal_remove", "{\"id\":" + std::to_string(id) + "}");
    CHECK(removed.find("\"ok\":true") != std::string::npos);
    CHECK(removed.find("\"count\":1") != std::string::npos);

    // Unknown ids and missing fields are refusals, not silent successes.
    CHECK(callToolOnce("eve_decal_remove", "{\"id\":999999}").find("\"ok\":false") != std::string::npos);
    CHECK(callToolOnce("eve_decal_remove", "{}").find("missing decal id") != std::string::npos);
    CHECK(callToolOnce("eve_decal_set_limit", "{\"kind\":\"blood\"}").find("limit must be") != std::string::npos);

    const std::string cleared = callToolOnce("eve_decal_clear", "{}");
    CHECK(cleared.find("\"ok\":true") != std::string::npos);
    CHECK_EQ(provider->count(), 0);
    CHECK(callToolOnce("eve_decal_set_limit", R"({"kind":"blood","limit":4})").find("\"ok\":true") !=
          std::string::npos);
}

TEST_CASE("devtools.mcp.decalToolsReportMissingProvider") {
    // No decal module in this process: the tool must name the absent capability
    // instead of reporting an empty decal list as if the world were clean.
    REQUIRE(eve::cap::query<eve::IDecalQuery>() == nullptr);
    const std::string status = callToolOnce("eve_decal_status", "{}");
    CHECK(status.find("\"ok\":false") != std::string::npos);
    CHECK(status.find("IDecalQuery") != std::string::npos);
    CHECK(callToolOnce("eve_decal_project", R"({"x":0,"y":0,"z":0})").find("IDecalQuery") != std::string::npos);
    CHECK(callToolOnce("eve_decal_clear", "{}").find("IDecalQuery") != std::string::npos);
}

TEST_CASE("devtools.mcp.profilerToolsReadTheLiveCore") {
    auto* const profilerModule = eve::ModuleManager::requireInstance<eve::profiler::Profiler>("Profiler");
    REQUIRE(profilerModule != nullptr);
    auto* provider = eve::cap::query<eve::IProfilerQuery>();
    REQUIRE(provider != nullptr);

    // Aggregate one synthetic frame the way the frame loop does, so the tool has
    // real zone data to report.
    const bool enabledBefore = profilerModule->enabled();
    profilerModule->setEnabled(true);
    profilerModule->reset();
    profilerModule->beginFrame();
    {
        eve::prof::Profiler::zoneBegin("mcp.test.zone", "mcp.test");
        eve::prof::Profiler::zoneEnd();
    }
    profilerModule->endFrame();

    const std::string frame = callToolOnce("eve_profiler_frame", "{}");
    REQUIRE(!frame.empty());
    CHECK(frame.find("\"schema\":\"eve.profiler.frame\"") != std::string::npos);
    CHECK(frame.find("\"hasFrame\":true") != std::string::npos);
    CHECK(frame.find("mcp.test.zone") != std::string::npos);
    CHECK(frame.find("\"gpuTimingAvailable\":") != std::string::npos);

    // The text report is the same hotspot view the profiler panel shows.
    CHECK(callToolOnce("eve_profiler_report", "{}").find("mcp.test.zone") != std::string::npos);
    profilerModule->setEnabled(enabledBefore);
}

TEST_CASE("devtools.mcp.spatialQueryToolsValidateAndReportMissingProviders") {
    // 3D spatial queries ride on capabilities the base test process does not
    // load, so this pins both the argument validation and the named-unavailable
    // contract for each of them.
    const std::string castMissing = callToolOnce("eve_physics_sphere_cast", R"({"from":[0,1,0],"to":[0,0,0]})");
    CHECK(castMissing.find("\"ok\":false") != std::string::npos);
    CHECK(castMissing.find("ICameraObstructionQuery") != std::string::npos);
    CHECK(callToolOnce("eve_physics_sphere_cast", "{}").find("from and to are required") != std::string::npos);

    const std::string projectMissing = callToolOnce("eve_world_project_down", R"({"x":1,"z":2})");
    CHECK(projectMissing.find("\"ok\":false") != std::string::npos);
    CHECK(projectMissing.find("IProcgenWorldQuery") != std::string::npos);

    // Both tools are advertised, so an agent can discover them before a provider
    // exists; routing is covered by devtools.mcp.everyDeclaredToolIsRouted.
    CHECK(eve::dev::isMcpDomainTool("eve_physics_sphere_cast"));
    CHECK(eve::dev::isMcpDomainTool("eve_world_project_down"));
}

TEST_CASE("devtools.mcp.sensingToolsReadLiveWorldQueries") {
    auto* const sensingModule = eve::ModuleManager::requireInstance<eve::sensing::Sensing>("Sensing");
    REQUIRE(sensingModule != nullptr);
    auto* provider = eve::cap::query<eve::ISensingQuery>();
    REQUIRE(provider != nullptr);
    CHECK_EQ(provider->worldCount(), 0);
    CHECK(provider->lastQueries().empty());

    const std::string empty = callToolOnce("eve_sensing_last_query", "{}");
    CHECK(empty.find("\"ok\":true") != std::string::npos);
    CHECK(empty.find("\"worldCount\":0") != std::string::npos);
    CHECK(callToolOnce("eve_sensing_last_query", R"({"world":3})").find("no live sensing world") != std::string::npos);
}
}  // namespace test_mcp_domain_tools

// Contract section: mcp editor targets
#include "PrivateTestScope.h"

#include "common/EditorAutomation.h"
#include "editor/Editor.h"
#include "voxel/editor/VoxelEditorModule.h"
// mcp_editor_targets.cpp
namespace test_mcp_editor_targets {
// Editor target discovery over MCP.
//
// Before eve_editor_target_list an agent could neither enumerate the targets a
// project script had bound (a tile layer, a height map, a voxel world) nor learn
// which `type` values eve_editor_target_create accepts: the schema hard-coded a
// four-name enum that had already drifted from the loaded adapters. These cases
// pin the discovery contract and the drift-proof type source.


using namespace eve::dev;

namespace {

std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

std::string initializeRequest(int id) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"initialize\",\"params\":{" +
           "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},\"clientInfo\":{\"name\":\"target-test\"}}}";
}

std::string callRequest(int id, const std::string& name, const std::string& argumentsJson) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"method\":\"tools/call\",\"params\":{\"name\":\"" +
           name + "\",\"arguments\":" + argumentsJson + "}}";
}

std::string joinRequests(const std::vector<std::string>& requests) {
    std::string out;
    for (const auto& request : requests) {
        out += request;
        out += '\n';
    }
    return out;
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

/** Text payload of one tools/call round trip over a fresh stdio session. */
std::string callToolOnce(const std::string& name, const std::string& argumentsJson) {
    const std::vector<std::string> responses =
        runStdioSession(joinRequests({initializeRequest(1), callRequest(2, name, argumentsJson)}), 2);
    if (responses.size() < 2) return {};
    try {
        auto object  = parseObject(responses[1]);
        auto result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

/** Entry of the `targets` array with the given id, or null. */
Poco::JSON::Object::Ptr findTarget(const std::string& listText, const std::string& id) {
    try {
        auto object  = parseObject(listText);
        auto targets = object ? object->getArray("targets") : Poco::JSON::Array::Ptr();
        if (!targets) return {};
        for (std::size_t index = 0; index < targets->size(); ++index) {
            auto item = targets->getObject(static_cast<unsigned>(index));
            if (item && item->optValue<std::string>("id", "") == id) return item;
        }
    } catch (...) {
    }
    return {};
}

}  // namespace

TEST_CASE("devtools.mcp.editorTargetListReportsRuntimeTypes") {
    auto& mcp = McpServer::instance();
    mcp.stop();
    eve::editor::Editor editor;

    // With no adapter loaded the advertised set is empty: it is a query of the
    // loaded factories, never a constant copied from the old schema.
    const std::string bare = callToolOnce("eve_editor_target_list", "{}");
    REQUIRE(!bare.empty());
    CHECK(bare.find("supportedTypes") != std::string::npos);
    CHECK(bare.find("voxel-catalog") == std::string::npos);

    {
        eve::voxel_editor::VoxelEditorModule adapter;

        const std::string listed = callToolOnce("eve_editor_target_list", "{}");
        CHECK(listed.find("voxel-catalog") != std::string::npos);
        CHECK(listed.find("voxel-model") != std::string::npos);
        CHECK(findTarget(listed, "agent.voxel") == nullptr);  // not created yet

        const std::string created =
            callToolOnce("eve_editor_target_create", R"({"target":"agent.voxel","type":"voxel-catalog"})");
        REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

        const std::string afterCreate = callToolOnce("eve_editor_target_list", "{}");
        auto              entry       = findTarget(afterCreate, "agent.voxel");
        REQUIRE(entry);
        CHECK(entry->getValue<std::string>("type") == "voxel-catalog");
        CHECK(entry->optValue<bool>("owned", false));
        CHECK(entry->has("revision"));
        CHECK(entry->has("generation"));

        const std::string closed = callToolOnce("eve_editor_target_close", R"({"target":"agent.voxel"})");
        REQUIRE(closed.find("\"status\":\"applied\"") != std::string::npos);
        CHECK(findTarget(callToolOnce("eve_editor_target_list", "{}"), "agent.voxel") == nullptr);
    }

    // Unloading the adapter removes its types from discovery: the list tracks the
    // loaded providers instead of drifting from them.
    const std::string afterUnload = callToolOnce("eve_editor_target_list", "{}");
    CHECK(afterUnload.find("voxel-catalog") == std::string::npos);
}

TEST_CASE("devtools.mcp.editorTargetListRejectsUnknownType") {
    auto& mcp = McpServer::instance();
    mcp.stop();
    eve::editor::Editor editor;

    // An unknown type must be refused with a diagnostic naming the requested
    // value; discovering the accepted set is eve_editor_target_list's job.
    const std::string created =
        callToolOnce("eve_editor_target_create", R"({"target":"agent.nope","type":"definitely-not-a-type"})");
    CHECK(created.find("definitely-not-a-type") != std::string::npos);
    CHECK(created.find("\"status\":\"applied\"") == std::string::npos);
    CHECK(findTarget(callToolOnce("eve_editor_target_list", "{}"), "agent.nope") == nullptr);
}
}  // namespace test_mcp_editor_targets

// Contract section: mcp input tools
#include "PrivateTestScope.h"

#include "common/InputInjection.h"
#include "common/Result.h"
#include "devtools/McpInputTools.hpp"
// mcp_input_tools.cpp
namespace test_mcp_input_tools {
// MCP playtest input tools: inject via IInputInjection, defer wait/capture across
// McpServer::poll ticks, and degrade with named unavailable when the provider is
// absent.


using namespace eve::dev;

namespace {

class FakeInputInjection final : public eve::IInputInjection {
public:
    int         keys    = 0;
    int         moves   = 0;
    int         buttons = 0;
    int         wheels  = 0;
    int         texts   = 0;
    std::string lastKey;
    bool        lastDown = false;

    [[nodiscard]] eve::Result<void> injectKey(std::string_view key, bool down, bool /*repeat*/) override {
        ++keys;
        lastKey  = std::string(key);
        lastDown = down;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectMouseButton(int /*button*/, bool /*down*/, float /*x*/,
                                                      float /*y*/) override {
        ++buttons;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectMouseMove(float /*x*/, float /*y*/, float /*dx*/, float /*dy*/) override {
        ++moves;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectWheel(float /*x*/, float /*y*/) override {
        ++wheels;
        return eve::Result<void>::success();
    }
    [[nodiscard]] eve::Result<void> injectText(std::string_view /*text*/) override {
        ++texts;
        return eve::Result<void>::success();
    }
};

std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses,
                                         bool resetBatches = true) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    if (resetBatches) resetMcpInputBatchesForTest();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        // Drain queued stdio requests once we have enough replies. Extra poll()
        // ticks would advance pending eve_input_batch waitFrames, so stop as
        // soon as the expected responses arrive.
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

std::string contentTextFromResponse(const std::string& line) {
    try {
        Poco::JSON::Parser parser;
        auto               object  = parser.parse(line).extract<Poco::JSON::Object::Ptr>();
        auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
        auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
        if (!content || content->size() == 0) return {};
        return content->getObject(0)->getValue<std::string>("text");
    } catch (...) {
        return {};
    }
}

std::string callToolOnce(const std::string& name, const std::string& argumentsJson, bool resetBatches = true) {
    const std::string requests =
        std::string(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
            "\"capabilities\":{},\"clientInfo\":{\"name\":\"input-test\"}}}\n") +
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" + name +
        "\",\"arguments\":" + argumentsJson + "}}\n";
    const std::vector<std::string> responses = runStdioSession(requests, 2, resetBatches);
    if (responses.size() < 2) return {};
    return contentTextFromResponse(responses[1]);
}

Poco::JSON::Object::Ptr parseObject(const std::string& json) {
    Poco::JSON::Parser parser;
    return parser.parse(json).extract<Poco::JSON::Object::Ptr>();
}

}  // namespace

TEST_CASE("devtools.mcp.inputBatchInjectsThroughCapability") {
    resetMcpInputBatchesForTest();
    FakeInputInjection fake;
    eve::cap::provide<eve::IInputInjection>(&fake);

    auto status = parseObject(callToolOnce("eve_input_status", "{}"));
    REQUIRE(status);
    CHECK(status->getValue<bool>("ok"));
    CHECK(status->getValue<bool>("injection"));

    auto batch = parseObject(callToolOnce(
        "eve_input_batch",
        R"({"actions":[{"op":"key","key":"W","down":true},{"op":"key","key":"W","down":false},{"op":"mouse_move","x":10,"y":20},{"op":"wheel","y":-1},{"op":"text","text":"hi"}]})"));
    REQUIRE(batch);
    CHECK(batch->getValue<bool>("ok"));
    CHECK_EQ(batch->getValue<std::string>("status"), std::string("ready"));
    CHECK_EQ(batch->getValue<int>("injected"), 5);
    CHECK_EQ(fake.keys, 2);
    CHECK_EQ(fake.moves, 1);
    CHECK_EQ(fake.wheels, 1);
    CHECK_EQ(fake.texts, 1);
    CHECK_EQ(fake.lastKey, std::string("W"));
    CHECK(!fake.lastDown);

    eve::cap::revoke<eve::IInputInjection>(&fake);
    resetMcpInputBatchesForTest();
}

TEST_CASE("devtools.mcp.inputBatchWaitCompletesAcrossPoll") {
    // Drive the satellite API directly: stdio reader threads enqueue every
    // pre-buffered line at once, so a multi-call wait test cannot stage
    // idle poll ticks between tools/call requests.
    resetMcpInputBatchesForTest();
    FakeInputInjection fake;
    eve::cap::provide<eve::IInputInjection>(&fake);

    Poco::JSON::Parser      parser;
    Poco::JSON::Object::Ptr args =
        parser.parse(R"({"actions":[{"op":"key","key":"A","down":true},{"op":"wait","frames":2}]})")
            .extract<Poco::JSON::Object::Ptr>();
    const std::string batchEnvelope = callMcpInputTool("eve_input_batch", args);
    // Satellite tools return a full tools/call content envelope.
    auto batchResult = parser.parse(batchEnvelope).extract<Poco::JSON::Object::Ptr>();
    REQUIRE(batchResult);
    auto batchContent = batchResult->getArray("content");
    REQUIRE(batchContent);
    auto batch = parseObject(batchContent->getObject(0)->getValue<std::string>("text"));
    REQUIRE(batch);
    CHECK(batch->getValue<bool>("ok"));
    CHECK_EQ(batch->getValue<std::string>("status"), std::string("pending"));
    const auto id = batch->getValue<Poco::Int64>("id");
    CHECK(id > 0);

    Poco::JSON::Object::Ptr pollArgs = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    pollArgs->set("id", static_cast<Poco::Int64>(id));
    auto pendingEnvelope = callMcpInputTool("eve_input_poll", pollArgs);
    auto pendingResult   = parser.parse(pendingEnvelope).extract<Poco::JSON::Object::Ptr>();
    auto pending         = parseObject(pendingResult->getArray("content")->getObject(0)->getValue<std::string>("text"));
    REQUIRE(pending);
    CHECK_EQ(pending->getValue<std::string>("status"), std::string("pending"));

    pollMcpInputBatches();  // 2 -> 1
    pollMcpInputBatches();  // 1 -> 0, ready

    auto readyEnvelope = callMcpInputTool("eve_input_poll", pollArgs);
    auto readyResult   = parser.parse(readyEnvelope).extract<Poco::JSON::Object::Ptr>();
    auto ready         = parseObject(readyResult->getArray("content")->getObject(0)->getValue<std::string>("text"));
    REQUIRE(ready);
    CHECK_EQ(ready->getValue<std::string>("status"), std::string("ready"));
    CHECK(ready->getValue<bool>("ok"));
    CHECK_EQ(ready->getValue<Poco::Int64>("id"), id);

    eve::cap::revoke<eve::IInputInjection>(&fake);
    resetMcpInputBatchesForTest();
}

TEST_CASE("devtools.mcp.inputBatchReportsMissingProvider") {
    resetMcpInputBatchesForTest();
    // Ensure no leftover provider from another case.
    if (auto* existing = eve::cap::query<eve::IInputInjection>()) {
        eve::cap::revoke<eve::IInputInjection>(existing);
    }

    auto batch = parseObject(callToolOnce("eve_input_batch", R"({"actions":[{"op":"key","key":"A","down":true}]})"));
    REQUIRE(batch);
    CHECK(!batch->getValue<bool>("ok"));
    if (!batch->has("unavailable")) {
        CHECK(batch->has("error"));
    }
    CHECK(batch->has("hint"));
}
}  // namespace test_mcp_input_tools

// Contract section: mcp stdio
#include "PrivateTestScope.h"

// mcp_stdio.cpp
namespace test_mcp_stdio {
TEST_CASE("devtools.mcp.stdioTransport") {
    auto& mcp = eve::dev::McpServer::instance();
    mcp.stop();

    // Finish the input before starting its reader. Concurrent stringstream
    // writes race with getline and can leave the reader permanently at EOF.
    std::istringstream in(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{"
        "\"protocolVersion\":\"2025-06-18\",\"capabilities\":{},"
        "\"clientInfo\":{\"name\":\"stdio-test\",\"version\":\"0\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{"
        "\"name\":\"eve_host_status\",\"arguments\":{}}}\n");
    std::ostringstream out;
    struct StopReader {
        eve::dev::McpServer& server;
        ~StopReader() { server.stop(); }
    } stopReader{mcp};
    REQUIRE(mcp.listenStdio(in, out));

    for (int i = 0; i < 100; ++i) {
        mcp.poll();
        if (out.str().find("\"id\":3") != std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    mcp.stop();
    const std::string resp = out.str();
    REQUIRE(resp.find("\"id\":1") != std::string::npos);
    REQUIRE(resp.find("evengine") != std::string::npos);
    REQUIRE(resp.find("\"id\":2") != std::string::npos);
    REQUIRE(resp.find("eve_host_editor_apply") != std::string::npos);
    REQUIRE(resp.find("eve_host_shutdown") != std::string::npos);
    REQUIRE(resp.find("\"id\":3") != std::string::npos);
    REQUIRE(resp.find("eve_host_status") != std::string::npos);
}
}  // namespace test_mcp_stdio

// Contract section: mcp tool filter
#include "PrivateTestScope.h"

#include <cstdlib>
#include "devtools/AgentDevelopmentSession.hpp"
#include "devtools/McpTaskProfile.hpp"
// mcp_tool_filter.cpp
namespace test_mcp_tool_filter {
// MCP toolset / read-only / exclude filters: tools/list and tools/call must agree.


using namespace eve::dev;

namespace {

#if defined(_WIN32)
[[maybe_unused]] int  setEnv(const char* key, const char* value) { return _putenv_s(key, value ? value : ""); }
[[maybe_unused]] void unsetEnv(const char* key) { _putenv_s(key, ""); }
#else
[[maybe_unused]] int  setEnv(const char* key, const char* value) { return setenv(key, value, 1); }
[[maybe_unused]] void unsetEnv(const char* key) { unsetenv(key); }
#endif

struct EnvGuard {
    explicit EnvGuard(std::initializer_list<std::pair<const char*, const char*>> values) {
        for (const auto& [key, value] : values) {
            keys.push_back(key);
            if (value)
                setEnv(key, value);
            else
                unsetEnv(key);
        }
        mcpToolFilterReloadForTest();
    }
    ~EnvGuard() {
        for (const char* key : keys) unsetEnv(key);
        mcpToolFilterReloadForTest();
    }
    std::vector<const char*> keys;
};

[[maybe_unused]] std::vector<std::string> runStdioSession(const std::string& requests, std::size_t expectedResponses) {
    auto& mcp = McpServer::instance();
    mcp.stop();
    std::istringstream       in(requests);
    std::ostringstream       out;
    const bool               started = mcp.listenStdio(in, out);
    std::vector<std::string> responses;
    if (started) {
        for (int attempt = 0; attempt < 400; ++attempt) {
            mcp.poll();
            responses.clear();
            std::istringstream collected(out.str());
            std::string        line;
            while (std::getline(collected, line)) {
                if (!line.empty()) responses.push_back(line);
            }
            if (responses.size() >= expectedResponses) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    mcp.stop();
    return responses;
}

[[maybe_unused]] bool toolsListContains(const std::string& name) {
    const std::string requests =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"filter-test\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}\n";
    const auto responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return false;
    Poco::JSON::Parser parser;
    auto               object = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
    auto               result = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
    auto               tools  = result ? result->getArray("tools") : Poco::JSON::Array::Ptr();
    if (!tools) return false;
    for (std::size_t i = 0; i < tools->size(); ++i) {
        auto tool = tools->getObject(static_cast<unsigned>(i));
        if (tool && tool->getValue<std::string>("name") == name) return true;
    }
    return false;
}

[[maybe_unused]] std::string callToolText(const std::string& name) {
    const std::string requests =
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
        "\"capabilities\":{},\"clientInfo\":{\"name\":\"filter-test\"}}}\n"
        "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"" +
        name + "\",\"arguments\":{}}}\n";
    const auto responses = runStdioSession(requests, 2);
    if (responses.size() < 2) return {};
    Poco::JSON::Parser parser;
    auto               object  = parser.parse(responses[1]).extract<Poco::JSON::Object::Ptr>();
    auto               result  = object ? object->getObject("result") : Poco::JSON::Object::Ptr();
    auto               content = result ? result->getArray("content") : Poco::JSON::Array::Ptr();
    if (!content || content->size() == 0) return {};
    return content->getObject(0)->getValue<std::string>("text");
}

}  // namespace


TEST_CASE("devtools.mcp.toolsetFilterHidesAndRejects") {
    AgentDevelopmentSession::instance().reset();
    mcpSetSessionProfile({});
    mcpFilterStatsResetForTest();
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "core"},
                      {"EVE_MCP_READONLY", nullptr},
                      {"EVE_MCP_EXCLUDE", nullptr},
                      {"EVE_MCP_PROFILE", nullptr}}};
        CHECK(mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_eval"));
        CHECK(toolsListContains("eve_status"));
        CHECK(!toolsListContains("eve_eval"));
        const std::string text = callToolText("eve_eval");
        CHECK(text.find("\"filtered\":true") != std::string::npos);
        CHECK(text.find("hint") != std::string::npos);
    }
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"},
                      {"EVE_MCP_READONLY", "1"},
                      {"EVE_MCP_EXCLUDE", nullptr},
                      {"EVE_MCP_PROFILE", nullptr}}};
        CHECK(mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_eval"));
        CHECK(!mcpToolAllowed("eve_input_batch"));
        CHECK(mcpToolAllowed("eve_input_poll"));
    }
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"},
                      {"EVE_MCP_READONLY", nullptr},
                      {"EVE_MCP_EXCLUDE", "eve_status,eve_restart"},
                      {"EVE_MCP_PROFILE", nullptr}}};
        CHECK(!mcpToolAllowed("eve_status"));
        CHECK(!mcpToolAllowed("eve_restart"));
        CHECK(mcpToolAllowed("eve_console_read"));
    }
}

TEST_CASE("devtools.mcp.taskProfileStacksWithToolsetAndEscalates") {
    AgentDevelopmentSession::instance().reset();
    mcpSetSessionProfile({});
    mcpFilterStatsResetForTest();
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"},
                      {"EVE_MCP_READONLY", nullptr},
                      {"EVE_MCP_EXCLUDE", nullptr},
                      {"EVE_MCP_PROFILE", "verify.play"}}};
        CHECK(mcpToolAllowed("eve_play"));
        CHECK(mcpToolAllowed("eve_agent_session_status"));
        CHECK(!mcpToolAllowed("eve_scene_batch"));
        CHECK(!mcpToolAllowed("eve_eval"));
        const std::string text = callToolText("eve_scene_batch");
        CHECK(text.find("\"filtered\":true") != std::string::npos);
        CHECK(text.find("\"profile\":\"verify.play\"") != std::string::npos);
        CHECK(text.find("\"escalate\"") != std::string::npos);
        CHECK(text.find("content.scene") != std::string::npos);
    }
    {
        EnvGuard env{{{"EVE_MCP_TOOLSET", "all"},
                      {"EVE_MCP_READONLY", nullptr},
                      {"EVE_MCP_EXCLUDE", nullptr},
                      {"EVE_MCP_PROFILE", nullptr}}};
        auto&    session = AgentDevelopmentSession::instance();
        session.reset();
        REQUIRE(session.start("Session overlay", {{"play", "ok", true, false}}, "verify", "verify.play").isAccepted());
        CHECK(mcpToolAllowed("eve_play"));
        CHECK(!mcpToolAllowed("eve_scene_batch"));
        REQUIRE(session.escalate(session.sessionId(), "content.scene").isAccepted());
        CHECK(mcpToolAllowed("eve_scene_batch"));
        CHECK(!mcpToolAllowed("eve_eval"));
        session.reset();
    }
    CHECK_EQ(mcpProfileToolsetEnv("verify.play"), std::string("agent,core,play,input"));
}
}  // namespace test_mcp_tool_filter

// Contract section: network
#include "PrivateTestScope.h"

#include <cstdio>
#include <cstring>
#include "data/ByteData.h"
#include "network/Channel.h"
#include "network/HttpRequest.h"
#include "network/NetHost.h"
#include "network/NetRpc.h"
#include "network/NetStream.h"
#include "network/NetWorker.h"
#include "network/Network.h"
#include "network/Session.h"
#include "network/TcpSocket.h"
#include "network/UdpLink.h"
#include "network/UdpSocket.h"
#include "platform_event/PlatformEvent.h"
// network.cpp
namespace test_network {
using eve::platform_event::Message;
using eve::platform_event::PlatformEvent;
using eve::platform_event::Variant;

TEST_CASE("event.VariantMessage") {
    auto*                ev = eve::platform_event::PlatformEvent::create();
    std::vector<Variant> args;
    args.push_back(Variant::makeInt(200));
    args.push_back(Variant::makeString("ok"));
    args.push_back(Variant::makePtr(nullptr));
    ev->push(std::make_unique<Message>("httpresp", args));
    auto m = ev->pollOwned();
    REQUIRE(m.get() != nullptr);
    CHECK(m->name == "httpresp");
    REQUIRE(m->args.size() == 3);
    CHECK(static_cast<int>(m->args[0].type) == static_cast<int>(Variant::Type::Int));
    CHECK(m->args[0].i == 200);
    CHECK(m->args[1].s == "ok");
}

TEST_CASE("network.NetWorkerQueue") {
    eve::network::NetWorker     worker(nullptr);
    eve::network::NetCompletion c;
    c.type   = eve::network::NetEvType::Err;
    c.reason = "timeout";
    worker.post(std::move(c));
    std::vector<eve::network::NetCompletion> out;
    worker.drain(out);
    REQUIRE(out.size() == 1);
    CHECK(out[0].reason == "timeout");
}

TEST_CASE("network.NetWorker.stopFromJobDoesNotDeadlock") {
    eve::network::NetWorker worker(nullptr);
    std::atomic<bool>       stopped{false};
    worker.start();
    worker.submit([&] {
        worker.stop();
        stopped = true;
    });
    for (int i = 0; i < 200 && !stopped; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(stopped.load());
    worker.stop();
}

TEST_CASE("network.Channel.fragmentedAndCoalescedFrames") {
    auto*                   net = eve::network::Network::create();
    auto*                   ev  = eve::platform_event::PlatformEvent::create();
    eve::network::TcpSocket socket(net);
    eve::network::Channel   channel(&socket);

    channel.feed({0, 0});
    channel.feed({0, 2, 'a'});
    CHECK(!ev->pollOwned());
    channel.feed({'b', 0, 0, 0, 1, 'c'});

    auto first  = ev->pollOwned();
    auto second = ev->pollOwned();
    REQUIRE(static_cast<bool>(first));
    REQUIRE(static_cast<bool>(second));
    CHECK(first->name == "chmsg");
    CHECK(second->name == "chmsg");
    REQUIRE(first->args.size() == 2);
    REQUIRE(second->args.size() == 2);
    auto* firstData  = static_cast<eve::data::ByteData*>(first->args[1].p);
    auto* secondData = static_cast<eve::data::ByteData*>(second->args[1].p);
    REQUIRE(firstData != nullptr);
    REQUIRE(secondData != nullptr);
    CHECK(firstData->getSize() == 2);
    CHECK(secondData->getSize() == 1);
}

TEST_CASE("network.Channel.rejectsOversizedFrameHeader") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::platform_event::PlatformEvent::create();
    while (ev->pollOwned()) {
    }
    eve::network::TcpSocket socket(net);
    eve::network::Channel   channel(&socket);
    channel.feed({0x01, 0x00, 0x00, 0x01});
    net->pump();

    bool sawClose = false;
    bool sawError = false;
    while (auto message = ev->pollOwned()) {
        if (message->name == "chclose") sawClose = true;
        if (message->name == "neterr") sawError = true;
    }
    CHECK(sawClose);
    CHECK(sawError);
}

TEST_CASE("network.TcpEcho") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::platform_event::PlatformEvent::create();
    REQUIRE(net != nullptr);
    REQUIRE(ev != nullptr);

    const uint16_t port   = 39281;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool                     clientOk = false;
    eve::network::TcpSocket* peer     = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);

    eve::data::ByteData payload("ping", 4);
    REQUIRE(client->send(&payload));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata") {
                got = true;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.UdpSendTo") {
    auto* net = eve::network::Network::create();
    auto* ev  = eve::platform_event::PlatformEvent::create();
    auto* a   = net->newUdp();
    auto* b   = net->newUdp();
    REQUIRE(a->bind(39301));
    REQUIRE(b->bind(39302));
    eve::data::ByteData payload("udp!", 4);
    REQUIRE(a->sendTo(&payload, "127.0.0.1", 39302));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 1 && m->args[0].p == b) {
                got = true;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    a->close();
    b->close();
    delete a;
    delete b;
}

TEST_CASE("network.HttpGetLocal") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39311;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* req = net->newHttp("GET", "http://127.0.0.1:" + std::to_string(port) + "/");
    REQUIRE(req->submit());

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {
        }
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    const char*         httpResp = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello";
    eve::data::ByteData respData(httpResp, std::strlen(httpResp));
    REQUIRE(peer->send(&respData));

    bool got    = false;
    int  status = 0;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "httpresp" && m->args.size() >= 2) {
                got    = true;
                status = static_cast<int>(m->args[1].i);
                REQUIRE(m->args.size() >= 3);
                CHECK(m->args[2].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got);
    CHECK(status == 200);

    peer->close();
    server->close();
    delete peer;
    delete server;
    delete req;
}

TEST_CASE("network.ChannelFrames") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39321;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {
        }
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chSend = net->newChannel(client);
    auto* chRecv = net->newChannel(peer);
    REQUIRE(chSend->sendMsgString("ab"));
    REQUIRE(chSend->sendMsgString("cd"));

    int msgs = 0;
    for (int i = 0; i < 400 && msgs < 2; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "chmsg") {
                ++msgs;
                REQUIRE(m->args.size() >= 2);
                CHECK(m->args[1].ownsPointer());
            }
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[1].p) CHECK(m->args[1].ownsPointer());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(msgs == 2);

    auto* sess = net->newSession();
    sess->add("main", chRecv);
    CHECK(sess->get("main") == chRecv);
    sess->closeAll();

    delete sess;
    delete chSend;
    delete chRecv;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Network.timeoutAndVerifySsl") {
    auto* net = eve::network::Network::create();
    CHECK(net->getTimeout() == 10000);
    CHECK(net->getVerifySsl() == true);
    net->setTimeout(5000);
    net->setVerifySsl(false);
    CHECK(net->getTimeout() == 5000);
    CHECK(net->getVerifySsl() == false);
}

TEST_CASE("network.Network.cppFactoriesReturnOwners") {
    auto* net     = eve::network::Network::create();
    auto  tcp     = net->makeTcp();
    auto  udp     = net->makeUdp();
    auto  request = net->makeHttp("GET", "http://127.0.0.1/");
    auto  session = net->makeSession();
    auto  writer  = net->makeWriter();
    auto  reader  = net->makeReader("bytes");
    REQUIRE(static_cast<bool>(tcp));
    REQUIRE(static_cast<bool>(udp));
    REQUIRE(static_cast<bool>(request));
    REQUIRE(static_cast<bool>(session));
    REQUIRE(static_cast<bool>(writer));
    REQUIRE(static_cast<bool>(reader));
    CHECK(!net->makeChannel(nullptr));
    CHECK(!net->makeUdpLink(nullptr));
    CHECK(!net->makeRpc(nullptr));
    REQUIRE(static_cast<bool>(net->makeHost()));
}

TEST_CASE("network.Tcp.connectedAndLocalPort") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39291;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));
    CHECK(server->getLocalPort() == port);
    CHECK(!server->isConnected());

    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool                     clientOk = false;
    eve::network::TcpSocket* peer     = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);
    CHECK(client->isConnected());
    CHECK(peer->isConnected());
    CHECK(client->getLocalPort() != 0);
    CHECK(peer->getLocalPort() != 0);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Session.addGetRemoveCloseAll") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39331;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {
        }
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chClient = net->newChannel(client);
    auto* chServer = net->newChannel(peer);
    auto* sess     = net->newSession();

    sess->add("client", chClient);
    sess->add("server", chServer);
    CHECK(sess->get("client") == chClient);
    CHECK(sess->get("server") == chServer);
    CHECK(sess->get("missing") == nullptr);

    sess->remove("client");
    CHECK(sess->get("client") == nullptr);
    CHECK(sess->get("server") == chServer);

    sess->closeAll();
    CHECK(sess->get("server") == nullptr);

    delete sess;
    delete chClient;
    delete chServer;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.Http.setHeaderAndBody") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39341;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));

    auto* req = net->newHttp("POST", "http://127.0.0.1:" + std::to_string(port) + "/test");
    req->setHeader("X-Test-Header", "custom-value");
    req->setBodyString("post-body");
    REQUIRE(req->submit());

    std::string received;
    eve::network::TcpSocket* peer = nullptr;
    bool complete = false;
    for (int i = 0; i < 400 && !complete; ++i) {
        net->pump();
        // Accept before draining events: the worker can queue request bytes
        // along with the connection, so discarding early events loses headers.
        if (!peer) peer = server->accept();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer && m->args[1].p) {
                auto* data = static_cast<eve::data::ByteData*>(m->args[1].p);
                received.append(static_cast<char*>(data->getData()), data->getSize());
            }
        }
        // TCP may deliver headers and body separately. The known request body
        // is nine bytes; do not reply or assert until all of it has arrived.
        const auto headerEnd = received.find("\r\n\r\n");
        complete = headerEnd != std::string::npos && received.size() >= headerEnd + 4 + 9;
        if (!complete) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);
    REQUIRE(complete);
    REQUIRE(received.find("X-Test-Header: custom-value") != std::string::npos);
    REQUIRE_EQ(received.substr(received.find("\r\n\r\n") + 4), std::string("post-body"));

    const char*         httpResp = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";
    eve::data::ByteData respData(httpResp, std::strlen(httpResp));
    REQUIRE(peer->send(&respData));

    bool got = false;
    for (int i = 0; i < 400 && !got; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "httpresp" && !m->args.empty() && m->args[0].p == req) {
                got = true;
                REQUIRE(m->args.size() >= 3);
                REQUIRE(m->args[2].ownsPointer());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(got);

    peer->close();
    server->close();
    delete peer;
    delete server;
    delete req;
}

TEST_CASE("network.Http.httpsReturnsTlsWithoutSslBackend") {
    auto* net = eve::network::Network::create();
    REQUIRE(net != nullptr);
    net->setVerifySsl(false);
    auto* http = net->newHttp("GET", "https://example.com/");
    REQUIRE(http != nullptr);
    http->setVerifySsl(true);  // request-level override is stored / resolved in submit()
    REQUIRE(http->submit());

    auto* ev = eve::platform_event::PlatformEvent::create();
    REQUIRE(ev != nullptr);
    bool gotTls = false;
    for (int i = 0; i < 200 && !gotTls; ++i) {
        net->pump();
        while (auto msg = ev->pollOwned()) {
            if (msg->name == "neterr" && msg->args.size() >= 2 && msg->args[1].s == "tls") gotTls = true;
        }
        if (!gotTls) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(gotTls);
    delete http;
}

TEST_CASE("network.NetWriterReaderRoundtrip") {
    eve::network::NetWriter w;
    w.writeU8(0xAB);
    w.writeI8(-3);
    w.writeU16(0xBEEF);
    w.writeI16(-1234);
    w.writeU32(0xDEADBEEFu);
    w.writeI32(-70000);
    w.writeU64(0x0123456789ABCDEFull);
    w.writeI64(-1234567890123ll);
    w.writeF32(1.5f);
    w.writeF64(-2.25);
    w.writeBool(true);
    w.writeBool(false);
    w.writeString("hello");
    w.writeString(std::string("a\0b", 3));
    const char raw[] = {0x00, 0x01, '\xFE', '\xFF'};
    w.writeBytes(raw, sizeof(raw));

    REQUIRE(w.size() > 0);
    eve::network::NetReader r(w.data(), w.size());
    REQUIRE(r.ok());
    CHECK(r.u8() == 0xAB);
    CHECK(r.i8() == -3);
    CHECK(r.u16() == 0xBEEF);
    CHECK(r.i16() == -1234);
    CHECK(r.u32() == 0xDEADBEEFu);
    CHECK(r.i32() == -70000);
    CHECK(r.u64() == 0x0123456789ABCDEFull);
    CHECK(r.i64() == -1234567890123ll);
    CHECK(r.f32() == 1.5f);
    CHECK(r.f64() == -2.25);
    CHECK(r.b() == true);
    CHECK(r.b() == false);
    CHECK(r.str() == "hello");
    CHECK(r.str() == std::string("a\0b", 3));
    auto b = r.bytes(sizeof(raw));
    REQUIRE(b.size() == sizeof(raw));
    CHECK(b[0] == 0x00);
    CHECK(b[1] == 0x01);
    CHECK(b[2] == '\xFE');
    CHECK(b[3] == '\xFF');
    CHECK(r.remaining() == 0);
    CHECK(r.ok());
    CHECK(r.pos() == w.size());
}

TEST_CASE("network.NetReaderOverrunIsSticky") {
    eve::network::NetWriter w;
    w.writeU32(7);
    eve::network::NetReader r(w.data(), w.size());
    REQUIRE(r.ok());
    CHECK(r.u32() == 7);
    CHECK(r.u16() == 0);  // overrun
    CHECK(!r.ok());
    CHECK(r.u8() == 0);  // safe default after failure
    CHECK(r.remaining() == 0);
    CHECK(r.str().empty());

    eve::network::NetReader bad(nullptr, 4);
    CHECK(!bad.ok());

    eve::network::NetWriter w2;
    w2.writeString("abc");
    eve::network::NetReader r2(w2.data(), w2.size());
    CHECK(r2.str() == "abc");
    CHECK(r2.u32() == 0);  // overrun after payload
    CHECK(!r2.ok());
}

TEST_CASE("network.TcpLargeSendFlushesFully") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39351;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    bool                     clientOk = false;
    eve::network::TcpSocket* peer     = nullptr;
    for (int i = 0; i < 400 && (!clientOk || !peer); ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netconn" && m->args.size() >= 2 && m->args[1].s == "ok") {
                if (m->args[0].p == client) clientOk = true;
            }
        }
        if (!peer) peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(clientOk);
    REQUIRE(peer != nullptr);

    const size_t total = 2 * 1024 * 1024;  // 2 MiB, larger than any single send buffer
    std::string  payload(total, 'x');
    for (size_t i = 0; i < total; ++i) payload[i] = static_cast<char>('a' + (i % 26));
    size_t got      = 0;
    bool   mismatch = false;

    for (size_t off = 0; off < total; off += 64 * 1024) {
        // Respect the 1 MiB send-buffer cap: wait for the worker to drain.
        for (int stall = 0; stall < 2000; ++stall) {
            if (client->pendingSendBytes() <= 128 * 1024) break;
            net->pump();
            while (auto m = ev->pollOwned()) {
                if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer && m->args[1].p) {
                    auto*       d = static_cast<eve::data::ByteData*>(m->args[1].p);
                    const char* p = static_cast<const char*>(d->getData());
                    for (size_t j = 0; j < d->getSize(); ++j) {
                        if (p[j] != payload[got + j]) mismatch = true;
                    }
                    got += d->getSize();
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        eve::data::ByteData chunk(payload.data() + off, 64 * 1024);
        REQUIRE(client->send(&chunk));
    }

    for (int i = 0; i < 2000 && got < total && !mismatch; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "netdata" && m->args.size() >= 2 && m->args[0].p == peer && m->args[1].p) {
                auto*       d = static_cast<eve::data::ByteData*>(m->args[1].p);
                const char* p = static_cast<const char*>(d->getData());
                for (size_t j = 0; j < d->getSize(); ++j) {
                    if (p[j] != payload[got + j]) mismatch = true;
                }
                got += d->getSize();
            }
        }
        if (got < total) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(!mismatch);
    CHECK(got == total);
    CHECK(client->pendingSendBytes() == 0);

    client->close();
    peer->close();
    server->close();
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.ChannelLargeMessageReassembles") {
    auto*          net    = eve::network::Network::create();
    auto*          ev     = eve::platform_event::PlatformEvent::create();
    const uint16_t port   = 39361;
    auto*          server = net->newTcp();
    REQUIRE(server->listen(port));
    auto* client = net->newTcp();
    REQUIRE(client->connect("127.0.0.1", port));

    eve::network::TcpSocket* peer = nullptr;
    for (int i = 0; i < 400 && !peer; ++i) {
        net->pump();
        while (ev->pollOwned()) {
        }
        peer = server->accept();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(peer != nullptr);

    auto* chSend = net->newChannel(client);
    auto* chRecv = net->newChannel(peer);
    REQUIRE(chRecv != nullptr);

    const size_t bigSize = 512 * 1024;
    std::string  big(bigSize, 'z');
    REQUIRE(chSend->sendMsgString(big));

    std::string got;
    for (int i = 0; i < 4000 && got.size() < bigSize; ++i) {
        net->pump();
        while (auto m = ev->pollOwned()) {
            if (m->name == "chmsg" && m->args.size() >= 2 && m->args[1].p) {
                auto* d = static_cast<eve::data::ByteData*>(m->args[1].p);
                got.assign(static_cast<const char*>(d->getData()), d->getSize());
            }
        }
        if (got.size() < bigSize) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got == big);

    delete chSend;
    delete chRecv;
    delete client;
    delete peer;
    delete server;
}

TEST_CASE("network.UdpReliableUnderLoss") {
    auto* net = eve::network::Network::create();
    auto* a   = net->newUdp();
    auto* b   = net->newUdp();
    REQUIRE(a->bind(39670));
    REQUIRE(b->bind(39671));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39671));
    REQUIRE(linkB->setRemote("127.0.0.1", 39670));
    linkA->setLossRate(0.25f);
    linkB->setLossRate(0.25f);

    std::vector<std::string> received;
    linkB->setMessageHandler(
        [&received](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) { received.emplace_back(d, n); });

    const int count = 40;
    for (int i = 0; i < count; ++i) {
        std::string msg = "m" + std::to_string(i) + std::string(64 - std::to_string(i).size(), 'x');
        linkA->sendString(eve::network::UdpLink::MsgType::Reliable, 0, msg);
    }
    for (int i = 0; i < 2400 && received.size() < static_cast<size_t>(count); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 600 && linkA->pendingReliable() > 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(received.size() == static_cast<size_t>(count));
    CHECK(linkA->pendingReliable() == 0);
    bool inOrder = received.size() == static_cast<size_t>(count);
    for (int i = 0; i < count && inOrder; ++i) {
        std::string expect = "m" + std::to_string(i) + std::string(64 - std::to_string(i).size(), 'x');
        if (received[static_cast<size_t>(i)] != expect) inOrder = false;
    }
    CHECK(inOrder);
}

TEST_CASE("network.UdpFragmentReassembly") {
    auto* net = eve::network::Network::create();
    auto* a   = net->newUdp();
    auto* b   = net->newUdp();
    REQUIRE(a->bind(39672));
    REQUIRE(b->bind(39673));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39673));
    REQUIRE(linkB->setRemote("127.0.0.1", 39672));
    linkA->setLossRate(0.1f);
    linkB->setLossRate(0.1f);

    std::string big(48 * 1024, 'q');
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<char>('a' + (i % 26));
    std::string got;
    linkB->setMessageHandler(
        [&got](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) { got.assign(d, n); });

    linkA->sendString(eve::network::UdpLink::MsgType::Reliable, 0, big);
    for (int i = 0; i < 2400 && got.size() < big.size(); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 600 && linkA->pendingReliable() > 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(got == big);
    CHECK(linkA->pendingReliable() == 0);
}

TEST_CASE("network.UdpUnreliableOrdered") {
    auto* net = eve::network::Network::create();
    auto* a   = net->newUdp();
    auto* b   = net->newUdp();
    REQUIRE(a->bind(39674));
    REQUIRE(b->bind(39675));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39675));
    REQUIRE(linkB->setRemote("127.0.0.1", 39674));

    std::vector<std::string> received;
    linkB->setMessageHandler(
        [&received](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) { received.emplace_back(d, n); });
    const int count = 12;
    for (int i = 0; i < count; ++i) {
        linkA->sendString(eve::network::UdpLink::MsgType::UnreliableOrdered, 1, "o" + std::to_string(i));
    }
    for (int i = 0; i < 400 && received.size() < static_cast<size_t>(count); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(received.size() == static_cast<size_t>(count));
    bool inOrder = received.size() == static_cast<size_t>(count);
    for (int i = 0; i < count && inOrder; ++i) {
        if (received[static_cast<size_t>(i)] != "o" + std::to_string(i)) inOrder = false;
    }
    CHECK(inOrder);
}

TEST_CASE("network.UdpRpcRoundtrip") {
    auto* net = eve::network::Network::create();
    auto* a   = net->newUdp();
    auto* b   = net->newUdp();
    REQUIRE(a->bind(39676));
    REQUIRE(b->bind(39677));
    auto* linkA = net->newUdpLink(a);
    auto* linkB = net->newUdpLink(b);
    REQUIRE(linkA->setRemote("127.0.0.1", 39677));
    REQUIRE(linkB->setRemote("127.0.0.1", 39676));
    auto* rpcA = net->newRpc(linkA);
    auto* rpcB = net->newRpc(linkB);
    REQUIRE(rpcA != nullptr);
    REQUIRE(rpcB != nullptr);

    std::string gotB, gotA;
    rpcB->registerHandler(7, [&gotB](eve::network::NetReader& r) { gotB = r.str(); });
    rpcA->registerHandler(8, [&gotA](eve::network::NetReader& r) { gotA = r.str(); });

    eve::network::NetWriter w;
    w.writeString("hello-from-a");
    rpcA->call(7, w.data(), w.size(), true);
    for (int i = 0; i < 600 && (gotB.empty() || gotA.empty()); ++i) {
        net->pump();
        if (!gotB.empty() && gotA.empty()) {
            eve::network::NetWriter w2;
            w2.writeString("reply-from-b");
            rpcB->call(8, w2.data(), w2.size(), true);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(gotB == "hello-from-a");
    CHECK(gotA == "reply-from-b");
}

TEST_CASE("network.NetHostPeers") {
    auto       phaseStart  = std::chrono::steady_clock::now();
    const auto recordPhase = [&](const char* phase) {
        const auto now = std::chrono::steady_clock::now();
        std::fprintf(stderr, "NetHostPeers %s: %.3f ms\n", phase,
                     std::chrono::duration<double, std::milli>(now - phaseStart).count());
        phaseStart = now;
    };
    auto*       net = eve::network::Network::create();
    std::string hostGot, clientGot;
    int         connectedPeer    = -1;
    int         disconnectedPeer = -1;
    auto        host             = net->makeHost();
    REQUIRE(host->start(39680));
    host->setTimeoutMs(500);

    host->setMessageHandler([&hostGot](uint32_t, eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) {
        hostGot.assign(d, n);
    });
    host->setPeerConnectedHandler([&connectedPeer](uint32_t id) { connectedPeer = static_cast<int>(id); });
    host->setPeerDisconnectedHandler([&disconnectedPeer](uint32_t id) { disconnectedPeer = static_cast<int>(id); });

    auto csock = net->makeUdp();
    REQUIRE(csock->connect("127.0.0.1", 39680));
    auto clink = net->makeUdpLink(csock.get());
    REQUIRE(clink->setRemote("127.0.0.1", 39680));
    clink->setTimeoutMs(500);
    clink->setMessageHandler(
        [&clientGot](eve::network::UdpLink::MsgType, uint8_t, const char* d, size_t n) { clientGot.assign(d, n); });

    recordPhase("setup");
    clink->sendString(eve::network::UdpLink::MsgType::Reliable, 0, "ping");
    for (int i = 0; i < 600 && (hostGot.empty() || connectedPeer < 0); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    recordPhase("receive ping");
    REQUIRE(hostGot == "ping");
    REQUIRE(connectedPeer == 1);
    REQUIRE(host->peerCount() == 1);

    host->sendStringTo(1, eve::network::UdpLink::MsgType::Reliable, 0, "pong");
    for (int i = 0; i < 600 && clientGot.empty(); ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    recordPhase("receive pong");
    REQUIRE(clientGot == "pong");

    clink->setLossRate(1.0f);
    for (int i = 0; i < 600 && disconnectedPeer < 0; ++i) {
        net->pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    recordPhase("loss and timeout");
    REQUIRE(disconnectedPeer == 1);
    REQUIRE(host->peerCount() == 0);
    clink.reset();
    csock.reset();
    host.reset();
    recordPhase("destroy client and host");
}
}  // namespace test_network

// Contract section: pixelworld parallel
#include "PrivateTestScope.h"

#include <algorithm>
#include <iostream>
#include "pixelworld/PixelWorld.h"
#include "pixelworld/thread/PixelWorldThread.h"
#include "thread/JobSystem.h"
// pixelworld_parallel.cpp
namespace test_pixelworld_parallel {
using namespace eve::pixelworld;

TEST_CASE("pixelworld.parallel_thermal_candidates_are_bit_exact_with_reference") {
    PixelWorld reference(7001);
    PixelWorld parallel(7001);
    for (PixelWorld* world : {&reference, &parallel}) {
        for (int x = -70; x <= 70; ++x) world->setMaterial(x, 80, "stone");
        world->paintCircle(-35, 20, 18, "sand");
        world->paintCircle(35, 20, 18, "water");
        world->paintCircle(0, 40, 12, "lava");
        world->paintCircle(0, 58, 16, "ice");
    }

    std::unique_ptr<eve::thread::JobSystem>         jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    CHECK_EQ(scheduler.workerCount(), std::size_t(4));
    for (std::uint64_t tick = 1; tick <= 100; ++tick) {
        reference.advance(eve::SimulationTick(tick)).expect("reference tick");
        const StepStats stats =
            parallel.advanceScheduled(eve::SimulationTick(tick), scheduler).expect("scheduled tick");
        CHECK(stats.parallelTasks > 1);
    }
    const auto expected = reference.saveSnapshot().expect("reference snapshot");
    const auto actual   = parallel.saveSnapshot().expect("parallel snapshot");
    REQUIRE_EQ(expected.size(), actual.size());
    CHECK(std::equal(expected.begin(), expected.end(), actual.begin()));
}

TEST_CASE("pixelworld.benchmark_million_active_pixels_reports_p50_p95") {
    if (!std::getenv("EVENGINE_PIXELWORLD_MILLION_BENCHMARK")) return;
    PixelWorld world(7002);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 1024; ++x) world.setMaterial(x, y, "stone");
    std::unique_ptr<eve::thread::JobSystem>         jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    std::vector<double>                             milliseconds;
    std::uint64_t                                   totalReactions = 0;
    for (std::uint64_t tick = 1; tick <= 10; ++tick) {
        for (int cy = 0; cy < 16; ++cy)
            for (int cx = 0; cx < 16; ++cx) world.setMaterial(cx * kPixelChunkSize, cy * kPixelChunkSize, "stone");
        const auto started = std::chrono::steady_clock::now();
        const auto stats =
            world.advanceScheduled(eve::SimulationTick(tick), scheduler).expect("million-pixel scheduled tick");
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        totalReactions += stats.reactions;
        CHECK_EQ(stats.chunksVisited, std::uint32_t(256));
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const double p50 = milliseconds[milliseconds.size() / 2];
    const double p95 = milliseconds.back();
    std::cout << "PIXELWORLD_BENCHMARK_JSON={\"occupiedPixels\":1048576,\"chunks\":256,"
                 "\"workload\":\"uniform-stone-force-awake\",\"samples\":10,\"workers\":"
              << scheduler.workerCount() << ",\"reactions\":" << totalReactions << ",\"p50Ms\":" << p50
              << ",\"p95Ms\":" << p95 << "}" << std::endl;
    CHECK(p50 > 0.0);
    CHECK(p95 >= p50);
}

TEST_CASE("pixelworld.benchmark_million_mobile_pixels_reports_p50_p95") {
    if (!std::getenv("EVENGINE_PIXELWORLD_ACTIVE_BENCHMARK")) return;
    PixelWorld world(7003);
    for (int y = 0; y < 1024; ++y)
        for (int x = 0; x < 1024; ++x) world.setMaterial(x, y, "sand");
    std::unique_ptr<eve::thread::JobSystem>         jobs(eve::thread::createJobSystem(4));
    eve::pixelworld_thread::JobSystemPixelScheduler scheduler(*jobs);
    std::vector<double>                             milliseconds;
    std::uint64_t                                   totalVisited = 0;
    std::uint64_t                                   totalMoved   = 0;
    for (std::uint64_t tick = 1; tick <= 5; ++tick) {
        const auto started = std::chrono::steady_clock::now();
        const auto stats =
            world.advanceScheduled(eve::SimulationTick(tick), scheduler).expect("million-mobile-pixel scheduled tick");
        milliseconds.push_back(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count());
        totalVisited += stats.cellsVisited;
        totalMoved += stats.cellsMoved;
        CHECK(stats.chunksVisited > 0);
        CHECK(stats.parallelTasks > 1);
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const double p50 = milliseconds[milliseconds.size() / 2];
    const double p95 = milliseconds.back();
    std::cout << "PIXELWORLD_ACTIVE_BENCHMARK_JSON={\"mobilePixels\":1048576,\"chunks\":256,"
                 "\"workload\":\"packed-sand-over-empty\",\"samples\":5,\"workers\":"
              << scheduler.workerCount() << ",\"visited\":" << totalVisited << ",\"moved\":" << totalMoved
              << ",\"p50Ms\":" << p50 << ",\"p95Ms\":" << p95 << "}" << std::endl;
    CHECK(p50 > 0.0);
    CHECK(p95 >= p50);
}
}  // namespace test_pixelworld_parallel

// Contract section: procgen result contract
#include "PrivateTestScope.h"

#include "procgen/PointGraph.h"
#include "procgen/Procgen.h"
#include "procgen/RuntimeGeneration.h"
// procgen_result_contract.cpp
namespace test_procgen_result_contract {
using namespace eve;
using namespace eve::procgen;

namespace {
[[maybe_unused]] void configure(PointGraph& graph) {
    REQUIRE(graph.addNode("source", "input"));
    REQUIRE(graph.addNode("out", "transform"));
    REQUIRE(graph.connect("source", "out"));
    REQUIRE(graph.setComputePolicy("cpu"));
    PointSet points;
    points.add(1.f, 0.f, 2.f);
    REQUIRE(graph.setNodePoints("source", &points));
}
template <class T>
[[maybe_unused]] void requireFailure(const Result<T>& result, DiagnosticCode code, const std::string& path) {
    REQUIRE(!result.ok());
    const auto* diagnostic = result.status().primaryDiagnostic();
    REQUIRE(diagnostic != nullptr);
    REQUIRE(diagnostic->code() == code);
    REQUIRE_EQ(diagnostic->path(), path);
}
}  // namespace


TEST_CASE("procgen.result.cleanupDistinguishesEmptyThreadAndStale") {
    RuntimeGeneration runtime(17), other(17);
    auto              empty = runtime.nextCleanupRequest();
    REQUIRE(empty.ok());
    REQUIRE(!empty.value().has_value());
    const int level = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(level, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    auto completed = runtime.completeGenerationJob({*issued.value(), PointSet{}});
    REQUIRE(completed.ok());
    runtime.updateSource(1000.f, 1000.f, 1.f, 0.f);
    auto wrongThread = std::async(std::launch::async, [&] { return runtime.nextCleanupRequest(); });
    auto rejected    = wrongThread.get();
    requireFailure(rejected, DiagnosticCode::Conflict, "thread");
    auto cleanup = runtime.nextCleanupRequest();
    REQUIRE(cleanup.ok());
    REQUIRE(cleanup.value().has_value());
    const auto request = *cleanup.value();
    auto       foreign = other.completeCleanupRequest(request);
    REQUIRE(!foreign.ok());
    auto cleaned = runtime.completeCleanupRequest(request);
    REQUIRE(cleaned.ok());
    REQUIRE_EQ(cleaned.value(), uint64_t(1));
    auto replay = runtime.completeCleanupRequest(request);
    REQUIRE(!replay.ok());
}


TEST_CASE("procgen.result.workerGraphResultCommitsOnOwner") {
    RuntimeGeneration runtime(41);
    const int         level = runtime.addLevel(10.f, 4.f, 2.f);
    REQUIRE_EQ(level, 0);
    runtime.updateSource(5.f, 5.f, 1.f, 0.f);
    auto issued = runtime.nextGenerationJob();
    REQUIRE(issued.ok());
    REQUIRE(issued.value().has_value());
    const auto job       = *issued.value();
    auto       worker    = std::async(std::launch::async, [job] {
        PointGraph graph;
        const bool configured = graph.addNode("source", "input") && graph.setComputePolicy("cpu");
        if (!configured)
            return Result<PointSet>::failure(Diagnostic::error(DiagnosticCode::Failed, "worker setup failed"));
        PointSet points;
        points.add(job.getMinX(), 0.f, job.getMinZ());
        if (!graph.setNodePoints("source", &points))
            return Result<PointSet>::failure(Diagnostic::error(DiagnosticCode::Failed, "worker input failed"));
        return graph.executeResult("source");
    });
    auto       generated = worker.get();
    REQUIRE(generated.ok());
    auto committed = runtime.completeGenerationJob({job, std::move(generated).takeValue()});
    REQUIRE(committed.ok());
    REQUIRE_EQ(committed.value(), uint64_t(1));
    REQUIRE_EQ(runtime.getActiveCellCount(), 1);
}

TEST_CASE("procgen.result.squirrelExecutionAndSchedulerShareProjection") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local procgen = eve.Procgen();
        local graph = procgen.newPointGraph().value;
        local points = procgen.sampleGrid(1, 1, 1.0, 18, 0.0).value;
        assert(graph.addNode("source", "input"));
        assert(graph.setNodePoints("source", points));
        assert(graph.addNode("prune", "self.prune"));
        assert(graph.connect("source", "prune"));
        assert(graph.connect("source", "prune", 0));
        assert(graph.validateResult().ok);
        local result = graph.executeResult("source");
        assert(result.ok && result.hasValue && result.value.getCount() == 1);
        local objectLayer = eve.ProcgenObjectBuildLayer();
        assert(objectLayer.addAsset("props/rock", 1.0).ok);
        objectLayer.setSeed(99);
        assert(objectLayer.setRandomScale(0.8, 1.2, 0.8, 1.2, 0.8, 1.2, true).ok);
        assert(objectLayer.addChild("props/rubble", 2, 0.5, 0.2, 0.4, -180.0, 180.0).ok);
        local builtObjects = objectLayer.build(result.value);
        assert(builtObjects.ok && builtObjects.value.getCount() == 3);
        assert(builtObjects.value.getStringAttribute(0, "asset", "") == "props/rock");
        assert(builtObjects.value.getStringAttribute(1, "object_role", "") == "child");
        assert(builtObjects.value.getIntAttribute(1, "parent_index", -2) == 0);
        local buildStack = eve.ProcgenBuildLayerStack();
        assert(buildStack.addTileLayer("tiles", true, 1.0, 0.25, "script").ok);
        assert(buildStack.addObjectLayer("objects", true, objectLayer).ok);
        local stackDefinition = buildStack.serializeDefinition();
        local restoredStack = eve.ProcgenBuildLayerStack();
        assert(restoredStack.deserializeDefinition(stackDefinition).ok);
        local stackGrid = procgen.newGrid(1, 1).value;
        assert(stackGrid.setCell(0, 0, 2).ok);
        local stackResult = restoredStack.execute(stackGrid, result.value);
        assert(stackResult.ok && stackResult.value.getCount() == 2);
        assert(stackResult.value.getType(0) == "mesh" && stackResult.value.getMesh(0).ok);
        assert(stackResult.value.getType(1) == "points" && stackResult.value.getPoints(1).ok);
        local incremental = eve.ProcgenIncrementalBuildExecutor();
        local firstDelta = incremental.update(restoredStack, stackGrid, result.value, 1, 1.0);
        assert(firstDelta.ok && firstDelta.value.getCount() == 1);
        assert(firstDelta.value.getClusterX(0) == 0 && !firstDelta.value.isRemoved(0));
        assert(firstDelta.value.getArtifacts(0).ok);
        local stableDelta = incremental.update(restoredStack, stackGrid, result.value, 1, 1.0);
        assert(stableDelta.ok && stableDelta.value.getCount() == 0);
        assert(incremental.getCachedClusterCount() == 1);
        assert(incremental.getCachedArtifacts(0, 0).ok);
        local missing = graph.executeResult("missing");
        assert(!missing.ok && missing.value == null && missing.diagnostics[0].path == "missing");
        local gridGraph = procgen.newGridGraph().value;
        assert(gridGraph.addNode("noise", "generate.random_noise").ok);
        assert(gridGraph.setNodeInt("noise", "width", 4).ok);
        assert(gridGraph.setNodeInt("noise", "height", 4).ok);
        assert(gridGraph.setNodeInt("noise", "seed", 7).ok);
        assert(gridGraph.setNodeFloat("noise", "x", 1.0).ok);
        local generatedGrid = gridGraph.execute("noise");
        assert(generatedGrid.ok && generatedGrid.value.getWidth() == 4);
        local ownedGrid = procgen.newGrid(3, 2);
        assert(ownedGrid.ok && ownedGrid.value.fill(0).ok);
        assert(ownedGrid.value.setCell(1, 1, 1).ok);
        local ownedInputGraph = procgen.newGridGraph().value;
        assert(ownedInputGraph.addNode("input", "grid.input").ok);
        assert(ownedInputGraph.setNodeGrid("input", ownedGrid.value).ok);
        local ownedGridCopy = ownedInputGraph.execute("input");
        assert(ownedGridCopy.ok && ownedGridCopy.value.getCell(1, 1) == 1);
        local registryGraph = procgen.newGridGraph().value;
        assert(registryGraph.addNode("level", "generate.registry").ok);
        assert(registryGraph.setNodeString("level", "algorithm", "level.roguelike").ok);
        assert(registryGraph.setNodeInt("level", "width", 25).ok);
        assert(registryGraph.setNodeInt("level", "height", 19).ok);
        assert(registryGraph.setNodeInt("level", "seed", 435).ok);
        assert(registryGraph.addNode("floors", "select.semantic").ok);
        assert(registryGraph.setNodeInt("floors", "semantic", 2).ok);
        assert(registryGraph.connect("level", "floors", 0).ok);
        local semanticFloors = registryGraph.execute("floors");
        assert(semanticFloors.ok && semanticFloors.value.getWidth() == 25);
        local meshGraph = procgen.newMeshGraph().value;
        assert(meshGraph.addNode("grid", "grid.input").ok);
        assert(meshGraph.addNode("tiles", "mesh.grid_tiles").ok);
        assert(meshGraph.setNodeGrid("grid", generatedGrid.value).ok);
        assert(meshGraph.connect("grid", "tiles", 0).ok);
        local generatedMesh = meshGraph.execute("tiles");
        assert(generatedMesh.ok && generatedMesh.value.getIndexCount() > 0);
        local runtime = procgen.newRuntimeGeneration(19).value;
        local empty = runtime.nextCleanupRequest();
        assert(empty.ok && empty.value == null);
        runtime.addLevel(10.0, 4.0, 2.0);
        runtime.updateSource(5.0, 5.0, 1.0, 0.0);
        local job = runtime.nextGenerationJob();
        assert(job.ok && job.value != null);
        assert(runtime.completeGenerationJob(job.value, result.value).ok);
        assert(!runtime.completeGenerationJob(job.value, result.value).ok);
        runtime.updateSource(1000.0, 1000.0, 1.0, 0.0);
        local cleanup = runtime.nextCleanupRequest();
        assert(cleanup.ok && cleanup.value != null);
        assert(runtime.completeCleanupRequest(cleanup.value).ok);
        assert(!runtime.completeCleanupRequest(cleanup.value).ok);
        runtime.updateSource(1005.0, 1005.0, 1.0, 0.0);
        local retry = runtime.nextGenerationJob();
        assert(retry.ok && retry.value != null);
        assert(runtime.failGenerationJob(retry.value).ok);
        assert(!runtime.failGenerationJob(retry.value).ok);
    )"));
}

TEST_CASE("procgen.scriptHostCommitsAndRollsBackGeneratorsAtomically") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        local procgen = eve.Procgen();
        local params = procgen.newParams().value;
        params.setInt("count", 3);
        local good = {
            generate = function(params, ctx) {
                local points = procgen.sampleGrid(params.getInt("count", 1), 1, 2.0,
                                                  ctx.seedFor("points"), 0.0).value;
                if (!ctx.captureDebug("sampled", points)) throw ctx.getError();
                if (!ctx.publish("points", points)) throw ctx.getError();
            }
        };
        firstRun <- procgen.runScriptGenerator(good, params, "script-host-test", 41);
        firstOutput <- procgen.getSystemOutput("script-host-test", "points");
        firstCount <- firstOutput.value.getCount();
        firstRevision <- procgen.getSystemRevision("script-host-test");
        local broken = {
            generate = function(params, ctx) {
                local replacement = procgen.sampleGrid(1, 1, 1.0, 1, 0.0).value;
                ctx.publish("points", replacement);
                throw "intentional generator failure";
            }
        };
        failedRun <- procgen.runScriptGenerator(broken, params, "script-host-test", 99);
        afterFailure <- procgen.getSystemOutput("script-host-test", "points");
        afterFailureCount <- afterFailure.value.getCount();
        afterFailureRevision <- procgen.getSystemRevision("script-host-test");
        local unfinished = {
            generate = function(params, ctx) { ctx.beginTrace("open", 0); }
        };
        unfinishedRun <- procgen.runScriptGenerator(unfinished, params, "script-host-test", 99);
        afterUnfinishedRevision <- procgen.getSystemRevision("script-host-test");
    )"));

    auto first = vm.find("firstRun").toTable();
    REQUIRE(first.get<bool>("ok"));
    auto receipt = first.get<ssq::Table>("value");
    CHECK_EQ(receipt.get<std::int64_t>("revision"), std::int64_t(1));
    CHECK_EQ(vm.find("firstCount").toInt(), 3);
    CHECK_EQ(vm.find("firstRevision").toInt(), 1);
    CHECK(!vm.find("failedRun").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterFailureCount").toInt(), 3);
    CHECK_EQ(vm.find("afterFailureRevision").toInt(), 1);
    CHECK(!vm.find("unfinishedRun").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("afterUnfinishedRevision").toInt(), 1);
}
}  // namespace test_procgen_result_contract

// Contract section: procgen rng contract
#include "PrivateTestScope.h"

#include <DTL/Shape/CellularAutomatonIsland.hpp>
#include "procgen/GeneratorRegistry.h"
#include "procgen/Semantic.h"
// procgen_rng_contract.cpp
namespace test_procgen_rng_contract {
using namespace eve::procgen;

namespace {
[[maybe_unused]] bool sameCells(const Grid2D& a, const Grid2D& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (a.getCell(x, y) != b.getCell(x, y)) return false;
    return true;
}
}  // namespace


TEST_CASE("procgen.cave.cellular.parallelJobsHaveIsolatedRandomStreams") {
    GeneratorRegistry::instance().registerBuiltins();
    auto generate = [](uint32_t seed) {
        Params params;
        params.setSeed(seed);
        params.setSize(48, 36);
        params.setInt("loops", 5);
        params.setFloat("fill", 0.45f);
        Grid2D      grid;
        std::string error;
        const bool  generated = GeneratorRegistry::instance().generate("cave.cellular", params, grid, error);
        return std::make_pair(generated, std::move(grid));
    };
    const auto expectedA = generate(101);
    const auto expectedB = generate(202);
    REQUIRE(expectedA.first);
    REQUIRE(expectedB.first);

    auto       first   = std::async(std::launch::async, generate, uint32_t(101));
    auto       second  = std::async(std::launch::async, generate, uint32_t(202));
    const auto actualA = first.get();
    const auto actualB = second.get();
    REQUIRE(actualA.first);
    REQUIRE(actualB.first);
    CHECK(sameCells(expectedA.second, actualA.second));
    CHECK(sameCells(expectedB.second, actualB.second));
}
}  // namespace test_procgen_rng_contract

#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "PrivateTestScope.h"
#include "graphics/vulkan/FrameGraphJobs.h"
#include "vkbuilder/framegraph.hpp"
// render_graph.cpp
namespace test_render_graph {
// Device-free tests for the FrameGraph-based render pipeline planning and the
// JobSystem parallel-recording executor (see docs/dev/framegraph-migration.md).
//
// The planning tests never touch a GPU: vkb::FrameGraph(nullptr, ...) plans the
// pass graph (dependencies / layers / barriers) without creating any Vulkan
// object. The executor tests run the exact executor the render backend will
// hand to vkb::FrameGraph::record().


namespace {

using namespace eve::graphics;

vkb::TextureDesc colorTexDesc() {
    vkb::TextureDesc td;
    td.extent      = vk::Extent3D(800, 600, 1);
    td.usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
    td.afterLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    return td;
}

vkb::TextureDesc depthTexDesc() {
    vkb::TextureDesc td;
    td.format      = vk::Format::eD32Sfloat;
    td.extent      = vk::Extent3D(2048, 2048, 1);
    td.aspect      = vk::ImageAspectFlagBits::eDepth;
    td.usage       = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eDepthStencilAttachment;
    td.afterLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    return td;
}

/**
 * @brief The pass topology the Vulkan backend targets once the framegraph
 * migration lands: 3 CSM cascades + G-buffer fill concurrently, forward scene
 * pass next, present last. Resource declarations mirror the engine's
 * shadow map / G-buffer / scene color attachments.
 */
std::unique_ptr<vkb::FrameGraph> buildEngineTopologyGraph() {
    auto graph = std::make_unique<vkb::FrameGraph>(nullptr, 2);

    vkb::TextureHandle shadow[3];
    for (int c = 0; c < 3; ++c) shadow[c] = graph->createTexture("shadowCascade" + std::to_string(c), depthTexDesc());
    auto gbNormal      = graph->createTexture("gbNormal", colorTexDesc());
    auto gbDepthColor  = graph->createTexture("gbDepthColor", colorTexDesc());
    auto gbAlbedo      = graph->createTexture("gbAlbedo", colorTexDesc());
    auto gbHwDepth     = graph->createTexture("gbHwDepth", depthTexDesc());
    auto sceneColor    = graph->createTexture("sceneColor", colorTexDesc());
    auto presentTarget = graph->createTexture("presentTarget", colorTexDesc());
    graph->markOutput(presentTarget);

    vk::ClearValue depthClear{};
    depthClear.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};
    for (int c = 0; c < 3; ++c) {
        graph->addPass("shadow" + std::to_string(c))
            .depthAttachment(shadow[c], vkb::AttachmentOp::clear(depthClear))
            .record([](vkb::FrameGraphPassContext&) {});
    }
    graph->addPass("gbuffer")
        .colorAttachment(gbNormal, vkb::AttachmentOp::clearColor(0, 0, 0, 0))
        .colorAttachment(gbDepthColor, vkb::AttachmentOp::clearColor(1, 1, 1, 1))
        .colorAttachment(gbAlbedo, vkb::AttachmentOp::clearColor(0, 0, 0, 0))
        .depthAttachment(gbHwDepth, vkb::AttachmentOp::clear(depthClear))
        .record([](vkb::FrameGraphPassContext&) {});
    graph->addPass("forward")
        .sample(shadow[0])
        .sample(shadow[1])
        .sample(shadow[2])
        .sample(gbNormal)
        .sample(gbDepthColor)
        .sample(gbAlbedo)
        .sample(gbHwDepth)
        .colorAttachment(sceneColor, vkb::AttachmentOp::clearColor(0, 0, 0, 1))
        .record([](vkb::FrameGraphPassContext&) {});
    graph->addPass("present")
        .sample(sceneColor)
        .colorAttachment(presentTarget, vkb::AttachmentOp::clearColor(0, 0, 0, 1))
        .record([](vkb::FrameGraphPassContext&) {});
    return graph;
}

}  // namespace

TEST_CASE("render_graph.engineTopology") {
    auto graph = buildEngineTopologyGraph();
    graph->compile();
    const auto& plan = graph->compiled();

    CHECK_EQ(plan.passes.size(), size_t(6));

    // Shadow cascades and the G-buffer fill share no dependency edges, so the
    // planner must put them in one layer: that layer records concurrently on
    // the JobSystem workers.
    REQUIRE_EQ(plan.layers.size(), size_t(3));
    CHECK_EQ(plan.layers[0].size(), size_t(4));
    CHECK_EQ(plan.layers[1].size(), size_t(1));
    CHECK_EQ(plan.layers[2].size(), size_t(1));
    CHECK_EQ(plan.passes[plan.layers[0][0]].name, std::string("shadow0"));
    CHECK_EQ(plan.passes[plan.layers[1][0]].name, std::string("forward"));
    CHECK_EQ(plan.passes[plan.layers[2][0]].name, std::string("present"));

    // Dependency edges keep the topological order: shadow/gbuffer before
    // forward before present.
    const auto* forward = &plan.passes[0];
    const auto* present = &plan.passes[0];
    for (const auto& p : plan.passes) {
        if (p.name == "forward") forward = &p;
        if (p.name == "present") present = &p;
    }
    CHECK(forward->order > plan.passes[plan.layers[0][0]].order);
    CHECK(present->order > forward->order);

    // Attachment -> sampled transitions fold into render-pass final layouts:
    // the forward pass samples G-buffer + shadow maps with zero explicit
    // image barriers, and present samples scene color with zero explicit
    // barriers too.
    for (const auto& p : plan.passes) {
        if (p.name == "forward") {
            CHECK(p.barrier.images.empty());
            CHECK(p.barrier.buffers.empty());
        }
        if (p.name == "present") {
            CHECK(p.barrier.images.empty());
            CHECK(p.barrier.buffers.empty());
        }
        if (p.name == "gbuffer") {
            for (const auto& ab : p.attachments) {
                const bool folded = ab.finalLayout == vk::ImageLayout::eShaderReadOnlyOptimal ||
                                    ab.finalLayout == vk::ImageLayout::eDepthStencilReadOnlyOptimal;
                CHECK(folded);
            }
        }
        if (p.name == "shadow0" || p.name == "shadow1" || p.name == "shadow2") {
            REQUIRE_EQ(p.attachments.size(), size_t(1));
            // Match the next sample() access, not the resource's end-of-frame layout.
            CHECK_EQ(p.attachments[0].finalLayout, vk::ImageLayout::eShaderReadOnlyOptimal);
        }
    }
}

TEST_CASE("render_graph.jobSystemExecutorParallel") {
    auto* jobs = eve::thread::createJobSystem(4);
    REQUIRE(jobs != nullptr);
    jobs->beginFrame();

    std::mutex                   mu;
    std::vector<int>             recorded;
    std::vector<std::thread::id> threads;
    std::condition_variable      overlap;
    int                          active = 0, peakActive = 0;
    const auto                   deadline  = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    const auto                   recordOne = [&](uint32_t order) {
        std::unique_lock<std::mutex> lk(mu);
        peakActive = std::max(peakActive, ++active);
        overlap.notify_all();
        overlap.wait_until(lk, deadline, [&] { return peakActive >= 2; });
        recorded.push_back(int(order));
        threads.push_back(std::this_thread::get_id());
        --active;
    };

    // Independent passes must overlap, even when a busy CI host schedules workers late.
    std::vector<uint32_t> layer{0, 1, 2, 3, 4, 5, 6, 7};
    auto                  executor = vulkan::jobSystemPassExecutor(jobs);
    executor(layer, recordOne);

    jobs->endFrame();
    delete jobs;

    CHECK_EQ(recorded.size(), layer.size());
    std::sort(recorded.begin(), recorded.end());
    for (size_t i = 0; i < recorded.size(); ++i) CHECK_EQ(recorded[i], int(i));
    // Every pass recorded exactly once (recorded already proves count; sort
    // check proves no duplicates and no gaps).
    CHECK_GE(peakActive, 2);
    // At least two distinct threads participated -> the layer was truly
    // recorded in parallel, not just serialized through the executor.
    std::sort(threads.begin(), threads.end());
    const auto distinctEnd = std::unique(threads.begin(), threads.end());
    CHECK(std::distance(threads.begin(), distinctEnd) >= 2);
}

TEST_CASE("render_graph.jobSystemExecutorSerialFallback") {
    std::mutex       mu;
    std::vector<int> recorded;
    const auto       recordOne = [&](uint32_t order) {
        std::lock_guard<std::mutex> lk(mu);
        recorded.push_back(int(order));
    };

    auto executor = vulkan::jobSystemPassExecutor(nullptr);
    executor(std::vector<uint32_t>{0, 1, 2}, recordOne);
    executor(std::vector<uint32_t>{3}, recordOne);

    CHECK_EQ(recorded.size(), size_t(4));
    for (size_t i = 0; i < recorded.size(); ++i) CHECK_EQ(recorded[i], int(i));
}

TEST_CASE("render_graph.jobSystemGroupStress") {
    // Replicates the engine's per-frame pattern (beginFrame -> 4-way fork/join
    // -> endFrame) which exposed a scheduler race that loses a forked job and
    // hangs the join.
    auto* jobs = eve::thread::createJobSystem(4);
    REQUIRE(jobs != nullptr);
    std::atomic<int> ran{0};
    int              failedIter = -1;
    for (int iter = 0; iter < 4000; ++iter) {
        jobs->beginFrame();
        auto* group = jobs->createFrameTaskGroup();
        ran.store(0);
        for (int i = 0; i < 4; ++i) group->fork([&] { ran.fetch_add(1); });
        group->wait();
        jobs->endFrame();
        if (ran.load() != 4) {
            failedIter = iter;
            break;
        }
    }
    jobs->stop();
    delete jobs;
    CHECK_EQ(failedIter, -1);
}

TEST_CASE("render_graph.jobSystemEngineFramePattern") {
    // Mirrors the engine frame: prepareFrame3D's light job + parallel_for
    // bracket, then recordDeferredFrameGraph's 4-way fork/join bracket, 4000x.
    auto* jobs = eve::thread::createJobSystem(0);  // hardware concurrency, like the engine
    REQUIRE(jobs != nullptr);
    std::atomic<int> ran{0};
    std::atomic<int> sum{0};
    int              failedIter = -1;
    for (int iter = 0; iter < 4000; ++iter) {
        jobs->beginFrame();
        auto* light = jobs->submitFrame([&] { sum.fetch_add(1); });
        auto* loop  = jobs->parallelForFrame(
            0, 64,
            [&](int first, int last) {
                for (int i = first; i < last; ++i) sum.fetch_add(1);
            },
            16);
        loop->wait();
        light->wait();
        jobs->endFrame();

        jobs->beginFrame();
        auto* group = jobs->createFrameTaskGroup();
        ran.store(0);
        for (int i = 0; i < 4; ++i) group->fork([&] { ran.fetch_add(1); });
        group->wait();
        jobs->endFrame();
        if (ran.load() != 4) {
            failedIter = iter;
            break;
        }
    }
    jobs->stop();
    delete jobs;
    CHECK_EQ(failedIter, -1);
    CHECK_EQ(sum.load(), 4000 * 65);
}

TEST_CASE("render_graph.recordCycleLifecycle") {
    // vkb::fg::RecordCycle turns the FrameGraph recording contract into
    // hard errors: build -> compile -> record -> submit, record exactly once
    // per compile, no mutation while recording.
    vkb::fg::RecordCycle cycle;

    // record() without compile() is a violation.
    CHECK_THROWS((cycle.beginRecord(1), false));

    // Happy path: compile -> record every pass once -> submit.
    cycle.beginCompile();
    cycle.endCompile();
    cycle.beginRecord(1);
    cycle.claimPass(0);
    cycle.releasePass(0);
    cycle.endRecord();
    CHECK(int(cycle.phase()) == int(vkb::fg::RecordCycle::Phase::kRecorded));

    // A second record() before submit() is a violation.
    CHECK_THROWS((cycle.beginRecord(1), false));
    cycle.submitted();

    // record() after submit() without a fresh compile() is a violation.
    CHECK_THROWS((cycle.beginRecord(1), false));

    // Mutating the graph after compile() without recompiling is a violation.
    cycle.beginCompile();
    cycle.endCompile();
    cycle.markDirty();
    CHECK_THROWS((cycle.beginRecord(1), false));

    // Mutating the graph while recording is a violation.
    cycle.beginCompile();
    cycle.endCompile();
    cycle.beginRecord(1);
    CHECK_THROWS((cycle.markDirty(), false));
    CHECK_THROWS((cycle.beginCompile(), false));
    CHECK_THROWS((cycle.submitted(), false));
    cycle.claimPass(0);
    cycle.releasePass(0);
    cycle.endRecord();

    // Mutating between record() and submit() is a violation.
    CHECK_THROWS((cycle.markDirty(), false));
    CHECK_THROWS((cycle.beginCompile(), false));
    cycle.submitted();

    // An executor that skips a pass is caught at endRecord().
    cycle.beginCompile();
    cycle.endCompile();
    cycle.beginRecord(2);
    cycle.claimPass(0);
    cycle.releasePass(0);
    CHECK_THROWS((cycle.endRecord(), false));

    // abortRecord() (the exception path of record()) forces a fresh compile
    // before the next attempt.
    cycle.abortRecord();
    CHECK_THROWS((cycle.beginRecord(1), false));
    cycle.beginCompile();
    cycle.endCompile();
    cycle.beginRecord(1);
    cycle.claimPass(0);
    CHECK_THROWS((cycle.claimPass(0), false));  // double claim of the same pass
    cycle.releasePass(0);
    cycle.endRecord();
    cycle.submitted();
}

TEST_CASE("render_graph.recordCycleConcurrent") {
    // Concurrent record() on the same graph: exactly one caller wins, the
    // other gets an exception instead of a data race.
    vkb::fg::RecordCycle cycle;
    cycle.beginCompile();
    cycle.endCompile();
    std::atomic<int> winners{0};
    auto             tryRecord = [&] {
        try {
            cycle.beginRecord(4);
            winners.fetch_add(1);
        } catch (...) {
        }
    };
    std::thread a(tryRecord);
    std::thread b(tryRecord);
    a.join();
    b.join();
    CHECK_EQ(winners.load(), 1);

    // The winner completes a normal cycle; the loser's state was untouched.
    for (uint32_t i = 0; i < 4; ++i) {
        cycle.claimPass(i);
        cycle.releasePass(i);
    }
    cycle.endRecord();
    cycle.submitted();

    // Concurrent claim of the same pass: exactly one succeeds.
    cycle.beginCompile();
    cycle.endCompile();
    cycle.beginRecord(1);
    std::atomic<int> claims{0};
    auto             tryClaim = [&] {
        try {
            cycle.claimPass(0);
            claims.fetch_add(1);
        } catch (...) {
        }
    };
    std::thread c(tryClaim);
    std::thread d(tryClaim);
    c.join();
    d.join();
    CHECK_EQ(claims.load(), 1);
    cycle.releasePass(0);
    cycle.endRecord();
    cycle.submitted();
}
}  // namespace test_render_graph
#endif

// Contract section: thread
#include "PrivateTestScope.h"

#include <functional>
#include "common/Exception.h"
#include "thread/Channel.h"
#include "thread/Task.h"
#include "thread/Thread.h"
#include "thread/ThreadPool.h"
// thread.cpp
namespace test_thread {
namespace {

eve::thread::Thread* threadModule() { return eve::thread::Thread::create(); }

bool expectException(const std::function<void()>& fn) {
    try {
        fn();
    } catch (const eve::Exception&) {
        return true;
    }
    return false;
}

}  // namespace

TEST_CASE("thread.create") {
    auto* mod = threadModule();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Thread"));
    CHECK(mod->getHardwareConcurrency() >= 1);
}

TEST_CASE("thread.channel.pushPop") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    REQUIRE(ch.get() != nullptr);
    CHECK(!ch->hasData());
    CHECK_EQ(ch->getCount(), 0);

    std::string empty = ch->pop();
    CHECK_EQ(empty, std::string(""));

    ch->push("a");
    ch->push("b");
    CHECK(ch->hasData());
    CHECK_EQ(ch->getCount(), 2);
    std::string first  = ch->pop();
    std::string second = ch->pop();
    CHECK_EQ(first, std::string("a"));
    CHECK_EQ(second, std::string("b"));
    CHECK(!ch->hasData());
}

TEST_CASE("thread.channel.namedShared") {
    auto* mod = threadModule();
    auto* a   = mod->getChannel("results");
    auto* b   = mod->getChannel("results");
    REQUIRE(a != nullptr);
    CHECK_EQ(a, b);
    CHECK_EQ(a->getName(), std::string("results"));
    a->clear();
    a->push("ok");
    std::string got = b->pop();
    CHECK_EQ(got, std::string("ok"));
    CHECK(expectException([&] { mod->getChannel(""); }));
}

TEST_CASE("thread.channel.supplyTimeout") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    auto                                  t0  = std::chrono::steady_clock::now();
    std::string                           got = ch->supply(30);
    CHECK_EQ(got, std::string(""));
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms >= 20);
}

TEST_CASE("thread.channel.crossThread") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    std::thread                           producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ch->push("from-worker");
    });
    std::string                           got = ch->supply(1000);
    producer.join();
    CHECK_EQ(got, std::string("from-worker"));
}

TEST_CASE("thread.channel.demandBlocksUntilValue") {
    std::unique_ptr<eve::thread::Channel> ch(threadModule()->newChannel());
    std::thread                           producer([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ch->push("demanded");
    });
    std::string                           got = ch->demand();
    producer.join();
    CHECK_EQ(got, std::string("demanded"));
    CHECK(!ch->hasData());
}

TEST_CASE("thread.postMain.pushesToEventQueue") {
    auto* ev = eve::platform_event::PlatformEvent::create();
    REQUIRE(ev != nullptr);
    ev->clear();

    threadModule()->postMain("thread.task.done", "result-1");
    threadModule()->postMain("thread.task.done", "result-2");
    CHECK_EQ(ev->pollName(), std::string("thread.task.done"));
    CHECK_EQ(ev->pollData(), std::string("result-2"));
}

TEST_CASE("thread.postMain.emptyNameThrows") {
    CHECK(expectException([&] { threadModule()->postMain(""); }));
}

TEST_CASE("thread.postMain.fromWorkerThread") {
    auto* ev = eve::platform_event::PlatformEvent::create();
    REQUIRE(ev != nullptr);
    ev->clear();

    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::unique_ptr<eve::thread::Task>       task(
        pool->submit([mod = threadModule()] { mod->postMain("thread.worker.done", "w"); }));
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
    CHECK_EQ(ev->pollName(), std::string("thread.worker.done"));
    CHECK_EQ(ev->getLastData(), std::string("w"));
    CHECK_EQ(ev->pollName(), std::string());
}

TEST_CASE("thread.pool.submitSleep") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(2));
    REQUIRE(pool.get() != nullptr);
    CHECK_EQ(pool->getWorkerCount(), 2);
    CHECK(pool->isRunning());

    std::unique_ptr<eve::thread::Task> task(pool->submitSleep(20));
    REQUIRE(task.get() != nullptr);
    task->wait();
    CHECK(task->isDone());
    CHECK(!task->hasFailed());
    CHECK_EQ(task->getStatus(), std::string("done"));
    pool->waitAll();
}

TEST_CASE("thread.pool.submitPush") {
    auto*                                 mod = threadModule();
    std::unique_ptr<eve::thread::Channel> ch(mod->newChannel());

    std::unique_ptr<eve::thread::ThreadPool> pool(mod->newThreadPool(2));
    std::unique_ptr<eve::thread::Task>       task(pool->submitPush(ch.get(), "hello", 10));
    REQUIRE(task.get() != nullptr);
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
    CHECK(!task->hasFailed());
    std::string got = ch->supply(500);
    CHECK_EQ(got, std::string("hello"));
}

TEST_CASE("thread.pool.parallelWork") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(4));
    std::atomic<int>                         counter{0};
    std::vector<eve::thread::Task*>          tasks;
    for (int i = 0; i < 32; ++i) {
        tasks.push_back(pool->submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));
    }
    pool->waitAll();
    for (auto* t : tasks) {
        REQUIRE(t != nullptr);
        CHECK(t->isDone());
        delete t;
    }
    CHECK_EQ(counter.load(), 32);
}

TEST_CASE("thread.pool.taskFailure") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::unique_ptr<eve::thread::Task>       task(pool->submit([] { throw eve::Exception("boom"); }));
    task->wait();
    CHECK(task->hasFailed());
    CHECK_EQ(task->getStatus(), std::string("failed"));
    CHECK_EQ(task->getError(), std::string("boom"));
}

TEST_CASE("thread.defaultPool") {
    auto* pool = threadModule()->getPool();
    REQUIRE(pool != nullptr);
    CHECK(pool->getWorkerCount() >= 1);
    CHECK_EQ(pool, threadModule()->getPool());
}

TEST_CASE("thread.pool.stopRejectsSubmit") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    pool->stop();
    CHECK(!pool->isRunning());
    CHECK(expectException([&] { pool->submitSleep(1); }));
}

TEST_CASE("thread.pool.queueOwnsTaskState") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    eve::thread::Task*                       task = pool->submitSleep(10);
    delete task;
    pool->waitAll();
    CHECK_EQ(pool->getPendingCount(), 0);
}

TEST_CASE("thread.pool.jobOwnsAnonymousChannelState") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    auto*                                    channel = threadModule()->newChannel();
    std::unique_ptr<eve::thread::Task>       task(pool->submitPush(channel, "late", 10));
    delete channel;
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
}

TEST_CASE("thread.pool.workerWaitAllRejected") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::atomic<bool>                        rejected{false};
    std::unique_ptr<eve::thread::Task>       task(pool->submit([&] {
        try {
            pool->waitAll();
        } catch (const eve::Exception&) {
            rejected.store(true, std::memory_order_relaxed);
        }
    }));
    task->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
}

TEST_CASE("thread.pool.workerStopRejected") {
    std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(1));
    std::atomic<bool>                        rejected{false};
    std::unique_ptr<eve::thread::Task>       task(pool->submit([&] {
        try {
            pool->stop();
        } catch (const eve::Exception&) {
            rejected.store(true, std::memory_order_relaxed);
        }
    }));
    task->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
    CHECK(pool->isRunning());
}

TEST_CASE("thread.pool.workerCountBounded") {
    CHECK(expectException([&] { std::unique_ptr<eve::thread::ThreadPool> pool(threadModule()->newThreadPool(257)); }));
}

TEST_CASE("thread.pool.destructorFromWorkerIsSafe") {
    auto*                              pool = threadModule()->newThreadPool(1);
    std::unique_ptr<eve::thread::Task> task(pool->submit([pool] { delete pool; }));
    task->wait();
    CHECK_EQ(task->getStatus(), std::string("done"));
}

TEST_CASE("thread.pool.workerDestructorDoesNotJoinDependentWorker") {
    auto*                           pool = threadModule()->newThreadPool(2);
    std::atomic<eve::thread::Task*> deletingTask{nullptr};
    std::atomic<bool>               waiterReady{false};

    std::unique_ptr<eve::thread::Task> waiter(pool->submit([&] {
        eve::thread::Task* target = nullptr;
        while ((target = deletingTask.load(std::memory_order_acquire)) == nullptr) std::this_thread::yield();
        waiterReady.store(true, std::memory_order_release);
        target->wait();
    }));
    std::unique_ptr<eve::thread::Task> destroyer(pool->submit([&] {
        while (!waiterReady.load(std::memory_order_acquire)) std::this_thread::yield();
        delete pool;
    }));
    deletingTask.store(destroyer.get(), std::memory_order_release);

    destroyer->wait();
    waiter->wait();
    CHECK_EQ(destroyer->getStatus(), std::string("done"));
    CHECK_EQ(waiter->getStatus(), std::string("done"));
}

// ---- JobSystem ----

TEST_CASE("thread.jobsystem.create") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    REQUIRE(js.get() != nullptr);
    CHECK_EQ(js->getWorkerCount(), 2);
    CHECK(js->isRunning());
    CHECK_EQ(js->getOutstandingCount(), 0);
    js->waitAll();
    js->stop();
    CHECK(!js->isRunning());
    js->stop();  // idempotent
}

TEST_CASE("thread.jobsystem.submitWait") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        counter{0};
    std::vector<eve::thread::Job*>          jobs;
    for (int i = 0; i < 16; ++i) {
        jobs.push_back(js->submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }));
    }
    for (auto* j : jobs) {
        REQUIRE(j != nullptr);
        j->wait();
        CHECK(j->isDone());
        delete j;
    }
    CHECK_EQ(counter.load(), 16);
    js->waitAll();
}

TEST_CASE("thread.jobsystem.parallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(4));
    constexpr int                           n = 1000;
    std::vector<int>                        data(n, -1);
    eve::thread::Job*                       loop = js->parallelFor(
        0, n,
        [&data](int first, int last) {
            for (int i = first; i < last; ++i) data[i] = i;
        },
        37);
    REQUIRE(loop != nullptr);
    loop->wait();
    CHECK(loop->isDone());
    for (int i = 0; i < n; ++i) CHECK_EQ(data[i], i);
    delete loop;
    js->waitAll();
}

TEST_CASE("thread.jobsystem.parallelForEmpty") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        calls{0};
    eve::thread::Job*                       loop =
        js->parallelFor(5, 5, [&calls](int, int) { calls.fetch_add(1, std::memory_order_relaxed); }, 4);
    REQUIRE(loop != nullptr);
    loop->wait();
    CHECK(loop->isDone());
    CHECK_EQ(calls.load(), 0);
    delete loop;
}

TEST_CASE("thread.jobsystem.parallelForAutoChunk") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<long long>                  sum{0};
    eve::thread::Job*                       loop = js->parallelFor(
        0, 10000,
        [&sum](int first, int last) {
            long long s = 0;
            for (int i = first; i < last; ++i) s += i;
            sum.fetch_add(s, std::memory_order_relaxed);
        },
        0);
    loop->wait();
    CHECK(sum.load() == 10000LL * 9999 / 2);
    delete loop;
}

TEST_CASE("thread.jobsystem.parallelForCompletion") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        callbacks{0};
    eve::thread::Job*                       loop = js->parallelFor(0, 100, [](int, int) {}, 10);
    loop->setCompletionCallback([&callbacks] { callbacks.fetch_add(1, std::memory_order_relaxed); });
    loop->wait();
    CHECK_EQ(callbacks.load(), 1);
    delete loop;
}

TEST_CASE("thread.jobsystem.taskGroup") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(3));
    std::atomic<int>                        counter{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    REQUIRE(group.get() != nullptr);
    for (int i = 0; i < 24; ++i) {
        group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    group->wait();
    CHECK_EQ(group->getPendingCount(), 0);
    CHECK_EQ(counter.load(), 24);
}

TEST_CASE("thread.jobsystem.taskGroupReuse") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        counter{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 8; ++i) group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        group->wait();
        CHECK_EQ(group->getPendingCount(), 0);
    }
    CHECK_EQ(counter.load(), 24);
}

TEST_CASE("thread.jobsystem.taskGroupCompletionCallbacks") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        counter{0};
    std::atomic<int>                        callbacks{0};
    std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
    for (int i = 0; i < 8; ++i) {
        group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); },
                    [&callbacks] { callbacks.fetch_add(1, std::memory_order_relaxed); });
    }
    group->wait();
    CHECK_EQ(counter.load(), 8);
    CHECK_EQ(callbacks.load(), 8);
}

TEST_CASE("thread.jobsystem.dependencies") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        produced{0};
    std::atomic<int>                        seen{0};
    eve::thread::Job*                       a = js->createJob([&produced] { produced.store(42); });
    eve::thread::Job*                       b = js->createJob([&produced, &seen] { seen.store(produced.load()); });
    b->addDependency(a);
    CHECK_EQ(b->getPendingDependencyCount(), 1);
    js->schedule(b);  // waits for a
    js->schedule(a);
    b->wait();
    a->wait();
    CHECK_EQ(seen.load(), 42);
    CHECK(a->isDone());
    CHECK(b->isDone());
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.heapDeleteWhileCompletingStress") {
    // Regression for a heap-use-after-free in releaseDependents: with the
    // completionDone flag and the dependents-release split across two lock
    // acquisitions, a waiter could return from wait() and delete the heap job
    // while the completing worker was still iterating job->dependents (ASan:
    // voxel.world.cross_chunk_seam_culled). Deleting immediately after wait()
    // from the waiting thread widens the race; 200 iterations raise the odds.
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(4));
    std::atomic<int>                        ran{0};
    for (int i = 0; i < 200; ++i) {
        eve::thread::Job* child     = js->createJob([&ran] { ran.fetch_add(1, std::memory_order_relaxed); });
        eve::thread::Job* dependent = js->createJob([] {});
        dependent->addDependency(child);
        js->schedule(child);
        js->schedule(dependent);
        child->wait();
        delete child;  // frees the dependents vector while the worker may
                       // still be in completeJob's tail (pre-fix UAF)
        dependent->wait();
        delete dependent;
    }
    CHECK_EQ(ran.load(), 200);
}

TEST_CASE("thread.jobsystem.dependencyOnCompletedJobIsNoOp") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        counter{0};
    eve::thread::Job*                       a = js->submit([&counter] { counter.fetch_add(1); });
    a->wait();
    eve::thread::Job* b = js->createJob([&counter] { counter.fetch_add(1); });
    b->addDependency(a);  // a already done -> ignored
    CHECK_EQ(b->getPendingDependencyCount(), 0);
    js->schedule(b);
    b->wait();
    CHECK_EQ(counter.load(), 2);
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.failedPredecessorStillReleasesDependent") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        bRuns{0};
    eve::thread::Job*                       a = js->createJob([] { throw eve::Exception("boom"); });
    eve::thread::Job*                       b = js->createJob([&bRuns] { bRuns.store(1); });
    b->addDependency(a);
    js->schedule(a);
    js->schedule(b);
    b->wait();
    a->wait();
    CHECK(a->hasFailed());
    CHECK(b->isDone());
    CHECK_EQ(bRuns.load(), 1);
    delete a;
    delete b;
}

TEST_CASE("thread.jobsystem.joinNode") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        runs{0};
    eve::thread::Job*                       leaf = js->createJob([&runs] { runs.fetch_add(1); });
    eve::thread::Job*                       join = js->createJob(eve::thread::JobFunc{});  // pure dependency node
    join->addDependency(leaf);
    js->schedule(leaf);
    js->schedule(join);
    join->wait();
    leaf->wait();
    CHECK_EQ(runs.load(), 1);
    delete leaf;
    delete join;
}

TEST_CASE("thread.jobsystem.completionCallback") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    std::atomic<int>                        bodyRan{0};
    std::atomic<int>                        callbackRan{0};
    eve::thread::Job*                       job = js->submit([&bodyRan] { bodyRan.store(1); });
    job->setCompletionCallback([&bodyRan, &callbackRan] { callbackRan.store(bodyRan.load()); });
    job->wait();
    CHECK_EQ(callbackRan.load(), 1);
    delete job;
}

TEST_CASE("thread.jobsystem.failure") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job*                       job = js->submit([] { throw eve::Exception("kaboom"); });
    job->wait();
    CHECK(job->isDone());
    CHECK(job->hasFailed());
    CHECK_EQ(job->getError(), std::string("kaboom"));
    delete job;
}

TEST_CASE("thread.jobsystem.workerForkJoin") {
    // Fork/join from inside a pool worker must not deadlock even with a single
    // worker: the waiting worker helps execute the ready children.
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<int>                        counter{0};
    eve::thread::Job*                       outer = js->submit([&] {
        std::unique_ptr<eve::thread::TaskGroup> group(js->createTaskGroup());
        for (int i = 0; i < 32; ++i) {
            group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
        }
        group->wait();
    });
    outer->wait();
    CHECK_EQ(counter.load(), 32);
    delete outer;
}

TEST_CASE("thread.jobsystem.workerParallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<long long>                  sum{0};
    eve::thread::Job*                       outer = js->submit([&] {
        eve::thread::Job* loop = js->parallelFor(
            0, 500,
            [&sum](int first, int last) {
                long long s = 0;
                for (int i = first; i < last; ++i) s += i;
                sum.fetch_add(s, std::memory_order_relaxed);
            },
            10);
        loop->wait();
        delete loop;
    });
    outer->wait();
    CHECK(sum.load() == 500LL * 499 / 2);
    delete outer;
}

TEST_CASE("thread.jobsystem.frameArena") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    for (int frame = 0; frame < 3; ++frame) {
        js->beginFrame();
        std::atomic<int>               counter{0};
        std::vector<eve::thread::Job*> jobs;
        for (int i = 0; i < 64; ++i) {
            jobs.push_back(js->submitFrame([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
        }
        for (auto* j : jobs) j->wait();
        js->endFrame();  // joins (already done) and recycles the arena
        CHECK_EQ(counter.load(), 64);
    }
    js->waitAll();
}

TEST_CASE("thread.jobsystem.frameParallelFor") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    js->beginFrame();
    std::vector<int>  data(200, -1);
    eve::thread::Job* loop = js->parallelForFrame(
        0, 200,
        [&data](int first, int last) {
            for (int i = first; i < last; ++i) data[i] = i;
        },
        16);
    loop->wait();
    js->endFrame();
    for (int i = 0; i < 200; ++i) CHECK_EQ(data[i], i);
}

TEST_CASE("thread.jobsystem.frameTaskGroup") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(2));
    js->beginFrame();
    std::atomic<int>        counter{0};
    eve::thread::TaskGroup* group = js->createFrameTaskGroup();
    for (int i = 0; i < 32; ++i) {
        group->fork([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }
    group->wait();
    CHECK_EQ(counter.load(), 32);
    js->endFrame();  // destroys the group and its arena-allocated children
}

TEST_CASE("thread.jobsystem.dependencyAfterScheduleRejected") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job*                       a = js->submit([] {});
    eve::thread::Job*                       b = js->createJob([] {});
    CHECK(expectException([&] { a->addDependency(b); }));
    CHECK(expectException([&] { a->addDependency(a); }));
    a->wait();
    delete a;
    js->schedule(b);
    b->wait();
    delete b;
}

TEST_CASE("thread.jobsystem.stopRejectsSchedule") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    eve::thread::Job*                       job = js->createJob([] {});
    js->stop();
    CHECK(!js->isRunning());
    CHECK(expectException([&] { js->schedule(job); }));
    CHECK(expectException([&] { js->submit([] {}); }));
    delete job;
}

TEST_CASE("thread.jobsystem.workerWaitAllRejected") {
    std::unique_ptr<eve::thread::JobSystem> js(eve::thread::createJobSystem(1));
    std::atomic<bool>                       rejected{false};
    eve::thread::Job*                       job = js->submit([&] {
        try {
            js->waitAll();
        } catch (const eve::Exception&) {
            rejected.store(true, std::memory_order_relaxed);
        }
    });
    job->wait();
    CHECK(rejected.load(std::memory_order_relaxed));
    delete job;
}

TEST_CASE("thread.jobsystem.defaultSystem") {
    auto* jobs = threadModule()->getJobSystem();
    REQUIRE(jobs != nullptr);
    CHECK(jobs->getWorkerCount() >= 1);
    CHECK_EQ(jobs, threadModule()->getJobSystem());
    std::atomic<int>  counter{0};
    eve::thread::Job* job = jobs->submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    job->wait();
    CHECK_EQ(counter.load(), 1);
    delete job;
}
}  // namespace test_thread

// Contract section: voxel
#include "PrivateTestScope.h"

#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainSampler.h"
#include "voxel/Chunk.h"
#include "voxel/CubeTypeRegistry.h"
#include "voxel/FaceDir.h"
#include "voxel/Frustum.h"
#include "voxel/GreedyMesher.h"
#include "voxel/Voxel.h"
#include "voxel/VoxelPack.h"
#include "voxel/VoxelRectDecode.h"
#include "voxel/VoxelWorld.h"
// voxel.cpp
namespace test_voxel {
using namespace eve::voxel;

[[maybe_unused]] static void lookAtRH(float eyeX, float eyeY, float eyeZ, float cx, float cy, float cz, float ux,
                                      float uy, float uz, float out[16]) {
    float fx = cx - eyeX, fy = cy - eyeY, fz = cz - eyeZ;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl;
    fy /= fl;
    fz /= fl;
    float sx = fy * uz - fz * uy;
    float sy = fz * ux - fx * uz;
    float sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl;
    sy /= sl;
    sz /= sl;
    float rx = sy * fz - sz * fy;
    float ry = sz * fx - sx * fz;
    float rz = sx * fy - sy * fx;
    out[0]   = sx;
    out[1]   = rx;
    out[2]   = -fx;
    out[3]   = 0;
    out[4]   = sy;
    out[5]   = ry;
    out[6]   = -fy;
    out[7]   = 0;
    out[8]   = sz;
    out[9]   = rz;
    out[10]  = -fz;
    out[11]  = 0;
    out[12]  = -(sx * eyeX + sy * eyeY + sz * eyeZ);
    out[13]  = -(rx * eyeX + ry * eyeY + rz * eyeZ);
    out[14]  = -(-fx * eyeX - fy * eyeY - fz * eyeZ);
    out[15]  = 1;
}

[[maybe_unused]] static void perspectiveRH_ZO(float fovyRad, float aspect, float zNear, float zFar, float out[16]) {
    const float f = 1.f / std::tan(fovyRad * 0.5f);
    std::memset(out, 0, sizeof(float) * 16);
    out[0]  = f / aspect;
    out[5]  = f;
    out[10] = zFar / (zNear - zFar);
    out[11] = -1.f;
    out[14] = (zFar * zNear) / (zNear - zFar);
}

[[maybe_unused]] static void mul4(const float a[16], const float b[16], float out[16]) {
    float t[16];
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            t[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] +
                           a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
    std::memcpy(out, t, sizeof(t));
}


TEST_CASE("voxel.world.remeshDirty_parallel_equals_serial") {
    auto build = []() {
        auto w = std::make_unique<VoxelWorld>();
        for (int cz = -1; cz <= 1; ++cz)
            for (int cy = -1; cy <= 1; ++cy)
                for (int cx = -1; cx <= 1; ++cx) {
                    Chunk* c = w->getOrCreateChunk(cx, cy, cz);
                    c->fill(uint8_t(1 + ((cx * 7 + cy * 5 + cz * 3) & 3)));
                    // Carve holes (incl. on seams) to make neighbor culling exercise paths.
                    w->setVoxel(cx * 32 + 15, cy * 32 + 15, cz * 32 + 15, 0);
                    w->setVoxel(cx * 32 + 16, cy * 32 + 16, cz * 32 + 16, 0);
                }
        return w;
    };

    auto serial   = build();
    auto parallel = build();
    CHECK_EQ(serial->remeshDirty(1), 27);
    CHECK_EQ(parallel->remeshDirty(4), 27);
    CHECK_EQ(parallel->remeshDirty(4), 0);  // nothing left dirty

    for (int cz = -1; cz <= 1; ++cz)
        for (int cy = -1; cy <= 1; ++cy)
            for (int cx = -1; cx <= 1; ++cx) {
                const Chunk* a = serial->getChunk(cx, cy, cz);
                const Chunk* b = parallel->getChunk(cx, cy, cz);
                REQUIRE(a != nullptr);
                REQUIRE(b != nullptr);
                CHECK_EQ(a->totalRectCount(), b->totalRectCount());
                for (int d = 0; d < faceDirCount(); ++d) {
                    const auto&     fa  = a->faceRects(FaceDir(d));
                    const auto&     fb  = b->faceRects(FaceDir(d));
                    const uint32_t* aoa = a->faceAOPackedData(FaceDir(d));
                    const uint32_t* aob = b->faceAOPackedData(FaceDir(d));
                    CHECK_EQ(int(fa.size()), int(fb.size()));
                    for (size_t i = 0; i < fa.size() && i < fb.size(); ++i) CHECK_EQ(fa[i].bits, fb[i].bits);
                    if (aoa && aob)
                        for (size_t i = 0; i < fa.size() && i < fb.size(); ++i) CHECK_EQ(aoa[i], aob[i]);
                }
            }
}


TEST_CASE("voxel.world.streamAround_create_budget_spreads_across_calls") {
    VoxelWorld world;
    world.setTerrainParams(42, 1, 2, 3, 8.f, 14.f, 1.f / 32.f);

    StreamStats first = world.streamAround(0, 0, 0, 2, {}, 4);
    CHECK(first.created + world.getInflightStreamCount() >= 4);
    CHECK(world.getChunkCount() <= 4);
    CHECK_EQ(first.pending + world.getChunkCount(), 33);

    for (int i = 0; i < 400 && world.getChunkCount() < 33; ++i) {
        world.streamAround(0, 0, 0, 2, {}, 8);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK_EQ(world.getChunkCount(), 33);
    CHECK_EQ(world.getInflightStreamCount(), 0);
}
}  // namespace test_voxel
