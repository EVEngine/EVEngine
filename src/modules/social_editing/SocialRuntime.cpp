#include "social/SocialGraph.h"
#include "social_editing/SocialDocument.h"

namespace eve::social_editing {
EditorResult<void> SocialRuntimeApplier::apply(const SocialDocumentTarget& document,
                                               social::SocialGraph* runtime) const {
    if (!runtime)
        return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("editor.social.runtime"),
                                          "Live SocialGraph is required");
    const auto diagnostics = document.validate();
    for (const auto& d : diagnostics)
        if (d.severity() == DiagnosticSeverity::Error)
            return EditorResult<void>::failure(eve::Status(EditorStatus::Rejected, diagnostics));
    runtime->clear();
    for (const auto& e : document.edges()) {
        bool accepted = false;
        if (e.kind == "owner")
            accepted = runtime->setOwner(e.source.value(), e.target.value());
        else if (e.kind == "controller")
            accepted = runtime->setController(e.source.value(), e.target.value());
        else if (e.kind == "assignment")
            accepted = runtime->assign(e.source.value(), e.type, e.target.value());
        else
            accepted = runtime->setRelation(e.source.value(), e.target.value(), e.type, e.weight);
        if (!accepted)
            return eve::editing::failed<void>(EditorStatus::Failed,
                                              RuleId("editor.social.runtime-edge"),
                                              "Social runtime rejected an authored edge");
    }
    return eve::editing::applied<void>();
}
}  // namespace eve::social_editing
