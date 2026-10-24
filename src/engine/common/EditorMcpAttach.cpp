#include "common/EditorMcpAttach.h"

namespace eve {
namespace {

EditorMcpAttachFn g_attach = nullptr;
EditorMcpDetachFn g_detach = nullptr;

}  // namespace

void setEditorMcpAttachHooks(EditorMcpAttachFn attach, EditorMcpDetachFn detach) {
    g_attach = attach;
    g_detach = detach;
}

EditorMcpAttachFn editorMcpAttachHook() { return g_attach; }

EditorMcpDetachFn editorMcpDetachHook() { return g_detach; }

}  // namespace eve
