#include "common/Capability.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "common/ServiceInterfaces.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::filesystem::path repository() { return std::filesystem::path(__FILE__).parent_path().parent_path(); }
std::string           readSource(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read documentation fixture: " + path.string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}
void runChapter(const char* chapter, int selectedBlock = -1, const std::string& prefix = "",
                const std::string& suffix = "") {
    const auto  text = readSource(repository() / "docs/usr/modules" / (std::string(chapter) + ".md"));
    std::string code;
    size_t      position = 0;
    int         block    = 0;
    while ((position = text.find("```squirrel", position)) != std::string::npos) {
        const auto start = text.find('\n', position) + 1;
        const auto end   = text.find("```", start);
        if (end == std::string::npos) throw std::runtime_error("unclosed documentation code fence");
        if (selectedBlock < 0 || selectedBlock == block) code += text.substr(start, end - start) + "\n";
        ++block;
        position = end + 3;
    }
    if (code.empty()) throw std::runtime_error("documentation has no executable example");
    eve::Runtime runtime(2048, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(
        "attributes <- eve.Attributes(); decision <- eve.Decision(); "
        "definitions <- eve.Definitions(); tags <- eve.Tags(); "
        "game_event <- eve.GameEvent(); policies <- eve.PolicyRegistryModule(); "
        "transaction <- eve.Transaction();",
        "doc-fixture");
    runtime.runSource(prefix + "\nfunction checked_doc_example() {\n" + code + suffix + "\n}\nchecked_doc_example();\n",
                      chapter);
}
class TutorialFiles final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        if (path != "scripts/rules.nut") return false;
        const auto source = readSource(repository() / "examples/game-tutorial/scripts/rules.nut");
        output.assign(source.begin(), source.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return path == "scripts/rules.nut"; }
};
}  // namespace

TEST_CASE("userDocs.economy") { runChapter("economy"); }
TEST_CASE("userDocs.attributes") { runChapter("attributes"); }
TEST_CASE("userDocs.decision") { runChapter("decision"); }
TEST_CASE("userDocs.definitions") { runChapter("definitions"); }
TEST_CASE("userDocs.tags") { runChapter("tags"); }
TEST_CASE("userDocs.gameEvent") { runChapter("game_event"); }
TEST_CASE("userDocs.policyRegistry") { runChapter("policyregistry"); }
TEST_CASE("userDocs.transaction") { runChapter("transaction"); }
TEST_CASE("userDocs.profiler") { runChapter("profiler"); }
TEST_CASE("userDocs.rope") { runChapter("physics_rope"); }
TEST_CASE("userDocs.ecsLifecycle") {
    runChapter("entity", 1,
               "eve_init <- null; eve_update <- null; eve_render <- null; eve_quit <- null; "
               "config <- {width=960, height=540}; gfx <- {"
               "setBackgroundColor=function(r,g,b,a) {}, clear=function() {}, "
               "drawSolidRect=function(x,y,w,h,r,g,b,a) {}};",
               "\neve_init(); eve_update(1.0); assert(mover.position.x == 200.0); "
               "eve_render(); eve_update(10.0); assert(!mover.isAlive()); "
               "assert(eve.view(Mover).len() == 0); eve_quit();");
}
TEST_CASE("userDocs.tutorialRules") {
    eve::cap::detail::clearAllRaw();
    TutorialFiles files;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    {
        eve::Runtime runtime(2048, ssq::Libs::ALL);
        runtime.runSource(R"(
import { new_state, target_at, update_state, encode_save, decode_save } from "game:/scripts/rules.nut"
local state = new_state();
assert(decode_save(encode_save(state)).score == 0);
foreach (invalid in ["", "tutorial.collect|2|1", "tutorial.collect|1|-1",
                     "tutorial.collect|1|01", "tutorial.collect|1|1000001", "tutorial.collect|1|1|"])
    { local rejected = false; try { decode_save(invalid); } catch (error) { rejected = true; }
      assert(rejected); }
state.score = 999;
assert(decode_save(encode_save(state)).score == 999);
state.x = 0.0; state.y = 0.0;
update_state(state, -1.0, -1.0, 1.0, 960.0, 540.0);
assert(state.x >= 0.0 && state.y >= 0.0);
local goal = target_at(state.score);
state.x = goal.x; state.y = goal.y;
update_state(state, 0.0, 0.0, 0.0, 960.0, 540.0);
assert(state.score == 1000);
)",
                          "game:/main.nut");
    }
    eve::cap::detail::clearAllRaw();
}

TEST_CASE("userDocs.tutorialLifecycleAndSaveFailure") {
    eve::cap::detail::clearAllRaw();
    TutorialFiles files;
    eve::cap::provide<eve::service::IFileSystem>(&files);
    {
        eve::Runtime runtime(2048, ssq::Libs::ALL);
        runtime.runSource(R"(
eve_init <- null; eve_update <- null; eve_render <- null; eve_quit <- null;
config <- {width=960, height=540};
texture_creations <- 0; texture_updates <- 0;
test_files <- {}; failed_write <- "";
gfx <- {
    setBackgroundColor=function(r,g,b,a) {}, clear=function() {},
    drawTexturedRect=function(t,x,y,w,h,r,g,b,a) {},
    drawSolidRect=function(x,y,w,h,r,g,b,a) {},
    newTexture=function(p,u,v) { ++texture_creations; return {}; },
    updateTextureFromImageData=function(t,p) { ++texture_updates; }
};
eve <- {};
eve.Image <- function() { return {newEmptyImageData=function(w,h,f) {return {setPixel=function(x,y,r,g,b,a) {}};}}; };
ui <- {
    beginBuild=function() {}, beginWindow=function(title,id) {},
    text=function(text,id) {}, button=function(text,id) {}, end=function() {},
    mountBuildAs=function(name) {},
    setHostPos=function(x,y,px,py) {}, setHostSize=function(w,h) {},
    setHostMovable=function(on) {}, setHostResizable=function(on) {}, consumeClick=function() {return "";},
    setText=function(id,text) {}, beginFrameAndRender=function() {}
};
keyboard <- {isDown=function(key) {return false;}};
key_just_pressed <- function(key) {return false;};
fs <- {
    setIdentity=function(id,append) {return true;}, setupWriteDirectory=function() {return true;},
    readText=function(path) {return path in test_files ? test_files[path] : "";},
    writeTextAtomic=function(path,text) {
        if (path == failed_write) return 0;
        test_files[path] <- text; return 1;
    }
};
)",
                          "tutorial-fixture");
        runtime.runSource(readSource(repository() / "examples/game-tutorial/main.nut"), "game:/main.nut");
        runtime.runSource(R"(
eve_init(); eve_update(1.0 / 60.0); eve_render();
assert(texture_creations == 1);
tutorial_state.score = 7; save_game(); restart_game(); load_game();
assert(tutorial_state.score == 7);
tutorial_state.score = 8; save_game();
assert(test_files["tutorial-slot.backup.txt"] == "tutorial.collect|1|7");
test_files["tutorial-slot.txt"] = "broken"; load_game(); assert(tutorial_state.score == 7);
test_files["tutorial-slot.backup.txt"] = "broken";
tutorial_state.score = 9; load_game(); assert(tutorial_state.score == 9);
test_files["tutorial-slot.txt"] = "tutorial.collect|1|8";
failed_write = "tutorial-slot.backup.txt"; save_game();
assert(test_files["tutorial-slot.txt"] == "tutorial.collect|1|8");
assert(tutorial_state.score == 9);
eve_reload(); assert(texture_creations == 1 && texture_updates == 1);
assert(tutorial_state.score == 9); eve_quit();
)",
                          "tutorial-assertions");
    }
    eve::cap::detail::clearAllRaw();
}
