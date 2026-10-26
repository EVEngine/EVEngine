#pragma once

#include "common/SquirrelBinding.h"
#include "editor/presentation/PreviewState.h"
#include "editor/presentation/WorkspacePresentation.h"

namespace eve::editor {
/**
 * @brief Install the same presentation projection on each domain's owned script editor.
 * @ownership Script editor owns controller/domain; all adapters are call-local borrows.
 * @thread VM owner thread. No script callbacks; no cross-frame raw references.
 */
template <class ScriptEditor, class Source>
void exposeWorkspacePresentation(ssq::Class& cls, HSQUIRRELVM vm) {
    cls.addFunc("presentationState", [vm](ScriptEditor* self) -> ssq::Table {
        Source     source(self->editor());
        const auto state   = source.state();
        const auto preview = self->editor().previewState();
        return script::projectStatusResult(
            vm, Status::success(),
            Value::Object{{"revision", static_cast<std::int64_t>(state.revision)},
                          {"context", state.context},
                          {"playing", state.playing},
                          {"preview", previewPhaseName(preview.phase)},
                          {"previewRequest", static_cast<std::int64_t>(preview.request)},
                          {"providerGeneration", static_cast<std::int64_t>(preview.providerGeneration)}});
    });
    cls.addFunc("presentationSnapshot", [vm](ScriptEditor* self) -> ssq::Table {
        Source source(self->editor());
        return script::projectStatusResult(vm, Status::success(), self->presentation().snapshot(source));
    });
    cls.addFunc(
        "presentationEdit",
        [vm](ScriptEditor* self, const std::string& path, ssq::Object input, bool staged) -> ssq::Table {
            auto value = script::valueFromSquirrel(input);
            if (!value.ok()) return script::projectStatusResult(vm, value.status());
            Source source(self->editor());
            auto&  controller = self->presentation();
            return script::projectResult(
                vm, staged ? controller.stage(source, PropertyPath(path), toEditorValue(std::move(value).takeValue()))
                           : controller.edit(source, PropertyPath(path), toEditorValue(std::move(value).takeValue())));
        });
    cls.addFunc("presentationCommit", [vm](ScriptEditor* self) -> ssq::Table {
        Source source(self->editor());
        return script::projectResult(vm, self->presentation().commit(source));
    });
    cls.addFunc("presentationClose", [](ScriptEditor* self) -> void {
        self->presentation().cancel();
        self->editor().closePreview();
    });
    cls.addFunc("presentationCancel", [](ScriptEditor* self) -> void { self->presentation().cancel(); });
    cls.addFunc("presentationAction", [vm](ScriptEditor* self, const std::string& id) -> ssq::Table {
        Source source(self->editor());
        return script::projectResult(vm, self->presentation().action(source, id));
    });
}
}  // namespace eve::editor
