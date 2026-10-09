// Workspace recipe: Combat Action timeline
// Full reference: examples/combat-action-editor
// Contract: docs/dev/2026-09-04-domain-editor-workspace-ui.md §3
//
// Composition order to copy into a project:
//   1. Create Editor workspace / UI presenter host
//   2. ActionTimelineEditor.configureWorkspace(workspace)
//      panels: action.assets | action.preview | action.inspector | action.timeline
//   3. Bind the same previewTime to animation player + 3D viewport
//   4. Route pointer Down…Up into one undo transaction
//
// Run the full editor with:
//   cd examples/combat-action-editor && eve run

function eve_init() {
    print("[workspace-recipe] combat-action: see examples/combat-action-editor for the full workspace");
}

function eve_update(dt) {}
function eve_render() {}
