#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Module.h"
#include "editor/Editor.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorValue.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace eve::editor;

TEST_CASE("editor.v2.script_discovers_plans_and_executes_registered_command") {
    Editor* editor   = Editor::create();
    auto&   commands = editor->commandService();

    const CommandId commandId("test.script.place-attraction");
    commands.unregisterCommand(commandId);
    CommandDescriptor descriptor;
    descriptor.id          = commandId;
    descriptor.ownerModule = "test.script";
    descriptor.displayName = "Place Attraction";
    descriptor.category    = "Build";

    bool plannerSawScriptSource = false;
    int  executedAmount         = 0;
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      plannerSawScriptSource = request.source == CommandSource::Script;
                      const auto* object     = request.payload.getIf<EditorValue::Object>();
                      if (!object)
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected, RuleId("test.script.payload"),
                                                                  "Object payload required");
                      const auto amount = object->find("amount");
                      if (amount == object->end() || !amount->second.getIf<int64_t>())
                          return eve::editing::failed<CommandPlan>(EditorStatus::Rejected, RuleId("test.script.amount"),
                                                                  "Integer amount required");
                      CommandPlan plan;
                      plan.summary = "Place an attraction from a script-authored request";
                      DomainOperation operation;
                      operation.type        = "test.place";
                      operation.inverseType = "test.unplace";
                      operation.payload     = request.payload;
                      operation.hasInverse  = true;
                      plan.operations.push_back(std::move(operation));
                      return eve::editing::applied<CommandPlan>(std::move(plan));
                  },
                  [&](const CommandRequest& request, const CommandPlan& plan) {
                      const auto& object = *request.payload.getIf<EditorValue::Object>();
                      executedAmount     = static_cast<int>(*object.at("amount").getIf<int64_t>());
                      TransactionReceipt receipt;
                      receipt.id               = TransactionId(plan.id.value());
                      receipt.state            = TransactionState::Committed;
                      receipt.beforeRevision   = plan.baseRevision;
                      receipt.afterRevision    = plan.baseRevision + 1;
                      receipt.authorityReceipt = "local:test.script";
                      return eve::editing::applied<TransactionReceipt>(std::move(receipt));
                  })
              .ok());

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        session <- editor.newSession();
        found <- false;
        for (local i = 0; i < session.getCommandCount(); ++i) {
            if (session.getCommandId(i) == "test.script.place-attraction" &&
                session.getCommandName(i) == "Place Attraction" &&
                session.getCommandCategory(i) == "Build") found = true;
        }
        planned <- session.planCommand("test.script.place-attraction", {
            amount = 7,
            placement = { x = 12.5, y = 4.0 },
            tags = ["ride", "family"]
        });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("found").toBool());
    ssq::Table planned(vm.find("planned"));
    ssq::Table executed(vm.find("executed"));
    CHECK(planned.get<bool>("accepted"));
    CHECK_EQ(planned.get<std::string>("status"), std::string("applied"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("transactionState"), std::string("committed"));
    CHECK_EQ(executed.get<int64_t>("afterRevision"), int64_t{1});
    CHECK(plannerSawScriptSource);
    CHECK_EQ(executedAmount, 7);

    CHECK(commands.unregisterCommand(commandId, "test.script"));
}

TEST_CASE("editor.v2.script_game_injects_command_into_the_same_session_protocol") {
    Editor* editor = Editor::create();
    editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"));

    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        placed <- 0;
        registered <- editor.registerScriptCommand(
            "test.script.spawn-tree", "Spawn Tree", "Build",
            function(payload) {
                if (!("count" in payload) || payload.count <= 0) return false;
                placed += payload.count;
                return true;
            });
        session <- editor.newSession();
        planned <- session.planCommand("test.script.spawn-tree", { count = 3 });
        executed <- session.executePlan(planned.planId, {});
    )"));

    CHECK(vm.find("registered").toBool());
    CHECK_EQ(vm.find("placed").toInt(), 3);
    ssq::Table executed(vm.find("executed"));
    CHECK(executed.get<bool>("accepted"));
    CHECK_EQ(executed.get<std::string>("authorityReceipt"), std::string("script:local"));

    CHECK(editor->commandService().unregisterCommand(CommandId("test.script.spawn-tree"),
                                                     "script:test.script.spawn-tree"));
}

TEST_CASE("editor.v2.runtime_builder_quick_add_click_executes_command") {
    const std::filesystem::path sourceRoot = std::filesystem::path(__FILE__).parent_path().parent_path();
    const std::filesystem::path scriptPath = sourceRoot / "examples" / "editor-api-v2" / "main.nut";
    std::ifstream               input(scriptPath, std::ios::binary);
    REQUIRE(input.is_open());
    std::ostringstream source;
    source << input.rdbuf();

    const std::string harness = R"(
        eve_init <- null;
        eve_update <- null;
        eve_render <- null;
        persist <- function(key, factory) { return factory(); };
        gfx <- {
            rects = [],
            canvasDraws = 0,
            newCanvas = function(w, h) { return { width=w, height=h }; },
            setCanvas = function(canvas) {},
            drawCanvas = function(canvas, x, y, w, h) { canvasDraws += 1; },
            setBackgroundColor = function(r, g, b, a) {},
            clear = function() {},
            drawSolidRect = function(x, y, w, h, r, g, b, a) {
                rects.append({ x=x, y=y, w=w, h=h, r=r, g=g, b=b, a=a });
            }
        };
        mouse <- {
            isDown = function(button) { return false; },
            getX = function() { return 0.0; },
            getY = function() { return 0.0; }
        };
        ui <- {
            clicks = [],
            setTheme = function(name) {},
            beginBuild = function() {},
            beginWindow = function(title, id) {},
            text = function(value, id) {},
            beginRow = function(id, gap) {},
            button = function(label, id) {},
            end = function() {},
            mountBuildAs = function(name) {},
            select = function(name) {},
            setHostOverlay = function(enabled) {},
            setHostPos = function(x, y, z, w) {},
            setHostSize = function(w, h) {},
            setText = function(id, value) {},
            wantCaptureMouse = function() { return false; },
            beginFrameAndRender = function() {},
            consumeClick = function() {
                if (clicks.len() == 0) return "";
                local value = clicks[0];
                clicks.remove(0);
                return value;
            }
        };
    )";

    ssq::VM vm(4096, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    const std::string executable = harness + source.str() + R"(
        eve_init();
        ui.clicks.append("editor-v2/asset-tree");
        eve_update(0.016);
        eve_render();
        quickAddObjectCount <- editorV2.objects.len();
        quickAddCredits <- editorV2.credits;
        quickAddAsset <- editorV2.objects[0].asset;
        quickAddCanvasDraws <- gfx.canvasDraws;
        quickAddRenderedTree <- false;
        foreach (rect in gfx.rects) {
            if (rect.w >= 18.0 && rect.h >= 12.0 && rect.g > 0.6)
                quickAddRenderedTree = true;
        }
        cleanupResult <- editorV2.editor.unregisterScriptCommand("park.scene.place-asset");
    )";
    vm.run(vm.compileSource(executable.c_str(), "examples/editor-api-v2/main.nut"));

    CHECK_EQ(vm.find("quickAddObjectCount").toInt(), 1);
    CHECK_EQ(vm.find("quickAddCredits").toInt(), 580);
    CHECK_EQ(vm.find("quickAddAsset").toString(), std::string("park.asset.tree"));
    CHECK_EQ(vm.find("quickAddCanvasDraws").toInt(), 1);
    CHECK(vm.find("quickAddRenderedTree").toBool());
    CHECK(vm.find("cleanupResult").toBool());
}
