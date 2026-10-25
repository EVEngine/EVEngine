#include "zeroerr/unittest.h"

#include "cmdline/cmdline.h"
#include "common/Module.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

class CapturedOutput {
public:
    CapturedOutput() : oldOut_(std::cout.rdbuf(out_.rdbuf())), oldError_(std::cerr.rdbuf(error_.rdbuf())) {}
    ~CapturedOutput() {
        std::cout.rdbuf(oldOut_);
        std::cerr.rdbuf(oldError_);
    }
    std::string standard() const { return out_.str(); }
    std::string combined() const { return out_.str() + error_.str(); }

private:
    std::ostringstream out_;
    std::ostringstream error_;
    std::streambuf*    oldOut_;
    std::streambuf*    oldError_;
};

int invoke(std::vector<std::string> words) {
    std::vector<char*> argv;
    argv.reserve(words.size());
    for (auto& word : words) argv.push_back(word.data());
    auto* cli = eve::ModuleManager::requireInstance<eve::cmd::Cmdline>("Cmdline");
    return cli->runArgs(static_cast<unsigned>(argv.size()), argv.data());
}

}  // namespace

TEST_CASE("cmdline.runArgs: informational flags return success and print their documented output") {
    SUB_CASE("--help lists the registered commands") {
        CapturedOutput output;
        CHECK_EQ(invoke({"eve", "--help"}), 0);
        const auto text = output.standard();
        CHECK(text.find("Subcommands:") != std::string::npos);
        for (const char* command : {"run", "create", "zip", "package", "dev", "doc", "clean", "test", "build", "get",
                                    "mcp", "language-server"})
            CHECK(text.find(command) != std::string::npos);
    };

    SUB_CASE("--version begins with v and -v prints the identical version") {
        std::string longVersion;
        {
            CapturedOutput output;
            CHECK_EQ(invoke({"eve", "--version"}), 0);
            longVersion = output.standard();
        }
        REQUIRE(!longVersion.empty());
        CHECK_EQ(longVersion.front(), 'v');
        CapturedOutput output;
        CHECK_EQ(invoke({"eve", "-v"}), 0);
        CHECK_EQ(output.standard(), longVersion);
    };
}

TEST_CASE("cmdline.runArgs rejects unknown commands and malformed platform options") {
    struct Example {
        std::vector<std::string> argv;
        int                      expectedExit;
        const char*              explanation;
    };
    const Example examples[] = {
        {{"eve", "frobnicate"}, -1, "not expected"},
        {{"eve", "build"}, 2, "missing platform"},
        {{"eve", "build", "-p", "nonsense"}, 2, "unknown platform"},
        {{"eve", "get", "ios"}, 2, "not implemented"},
        {{"eve", "build", "-d", "-r", "android"}, 1, "both debug and release"},
    };
    for (const auto& example : examples) {
        int         exit = 0;
        std::string message;
        {
            CapturedOutput output;
            exit    = invoke(example.argv);
            message = output.combined();
        }
        CHECK(exit != 0);
        if (example.expectedExit >= 0) CHECK_EQ(exit, example.expectedExit);
        REQUIRE(message.find(example.explanation) != std::string::npos);
    }
}

TEST_CASE("cmdline.Doc: no-open documents the requested symbol without opening a browser") {
    SUB_CASE("doc with a symbol prints that symbol and the documentation URL") {
        CapturedOutput output;
        CHECK_EQ(invoke({"eve", "doc", "--no-open", "gfx.drawSolidRect"}), 0);
        CHECK(output.standard().find("EVEngine docs for 'gfx.drawSolidRect'") != std::string::npos);
        CHECK(output.standard().find("https://evengine.github.io/EVEngine/") != std::string::npos);
    };

    SUB_CASE("doc without a symbol prints the documentation index") {
        CapturedOutput output;
        CHECK_EQ(invoke({"eve", "doc", "--no-open"}), 0);
        CHECK(output.standard().find("EVEngine docs:") != std::string::npos);
    };

    SUB_CASE("the direct API preserves the exact symbol spelling") {
        auto*       cli = eve::ModuleManager::requireInstance<eve::cmd::Cmdline>("Cmdline");
        std::string actual;
        int         status = -1;
        {
            CapturedOutput output;
            status = cli->Doc("graphics.Renderer", true);
            actual = output.standard();
        }
        REQUIRE_EQ(status, 0);
        REQUIRE_EQ(actual,
                   std::string("EVEngine docs for 'graphics.Renderer': ") + "https://evengine.github.io/EVEngine/\n");
    };
}
