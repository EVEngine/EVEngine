#pragma once

#include "editing/EditingResult.h"
#include "editor/presentation/PreviewState.h"

#include <chrono>
#include <thread>

namespace preview_test {
/** @brief Owner-thread pump with a bounded deadline; production code never blocks for preview completion. */
template <class Editor>
[[nodiscard]] eve::editing::Result<void> finish(Editor& editor) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        auto result = editor.pollPreview();
        if (!result.ok()) return result;
        if (editor.previewState().phase != eve::editor::PreviewPhase::Pending) return eve::editing::applied<void>();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return eve::editing::failed<void>(eve::editing::Status::Failed, eve::editing::RuleId("test.preview.timeout"),
                                      "Preview did not finish");
}
}  // namespace preview_test
