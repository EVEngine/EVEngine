// Workspace recipe: Audio Source
// Full reference: examples/audio-source-editor
// Contract: docs/dev/2026-09-04-domain-editor-workspace-ui.md (audio workspace)
//
// Composition order:
//   1. Register audio editing provider on the host ExtensionProviderRegistry
//   2. configureWorkspace with list / waveform preview / inspector panels
//   3. Preview generation must carry document revision; discard stale results
//
// Run the full editor with:
//   cd examples/audio-source-editor && eve run

function eve_init() {
    print("[workspace-recipe] audio-source: see examples/audio-source-editor for the full workspace");
}

function eve_update(dt) {}
function eve_render() {}
