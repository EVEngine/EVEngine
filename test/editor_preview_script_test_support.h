#pragma once

#include <simplesquirrel/simplesquirrel.hpp>
#include "editor_preview_test_support.h"

namespace preview_test {
/** @brief Pump a real VM-owned editor on its owner thread until its asynchronous preview is observable. */
[[nodiscard]] inline eve::editing::Result<void> finishScript(ssq::VM& vm, const std::string& editor) {
    const std::string code = "__previewPump <- " + editor + ".update(0.0); __previewPhase <- " + editor +
                             ".presentationState().value.preview;";
    auto       script   = vm.compileSource(code.c_str());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        vm.run(script);
        if (!vm.find("__previewPump").toTable().get<bool>("ok"))
            return eve::editing::failed<void>(eve::editing::Status::Failed, eve::editing::RuleId("test.preview.script"),
                                              "Script preview update failed");
        if (vm.find("__previewPhase").toString() != "pending") return eve::editing::applied<void>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return eve::editing::failed<void>(eve::editing::Status::Failed, eve::editing::RuleId("test.preview.script-timeout"),
                                      "Script preview did not finish");
}
}  // namespace preview_test
