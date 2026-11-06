# SSUI generation client / EVEngine Material Lab

The native plugin is now available at [plugins/ssui-generation](../../plugins/ssui-generation/README.md). Prefer it for runtime integration. This Python prototype remains as a reference client and MCP integration-test driver.

A real EVEngine native editor backed by the open SSUI generation protocol. No LLM, browser UI, Python package installation, or C++ rebuild is required when an `eve` executable with MCP editor-host support is available.

## Run

Start the mock service in the SSUI repository:

```sh
yarn dev:bridge
```

In the EVEngine repository (Python 3.10+):

```sh
python3 tools/ssui-client/editor.py \
  --eve /absolute/path/to/eve \
  --project /absolute/path/to/material-project \
  --server http://127.0.0.1:7423
```

The project directory is created if absent. Use a dedicated material project for this prototype. `--eve` must point to a desktop executable built with `eve mcp` / EditorHost support (the standard build/run instructions are in the root Readme.en.md and Makefile).

1. Enter a description and seed; click **Generate**.
2. **Preview candidate** displays the downloaded PNG in the native renderer without changing the applied material.
3. **Exit preview** restores the applied material.
4. **Apply to material** saves the baseColor binding and undo history in `generated/material.json`.
5. **Undo application** restores the previous binding, including after restarting the editor.
6. **Cancel generation** requests server cancellation and discards any late local result.

Images use content-addressed names under `generated/`. Assets are retained for undo. Generation, polling and downloads run on a worker thread; the engine keeps rendering. HTTP has bounded timeouts; job waiting is capped at 120 seconds.

## Responsibility boundary

`client.py` speaks HTTP `/api/generation/v1`: discovers capabilities/recipes, submits asynchronous jobs, polls/cancels, downloads PNGs and verifies SHA256. It can be reused independently of the native editor. The editor currently selects `mock.checker@1`; it does not pretend to expose arbitrary recipe schemas as UI.

`MaterialDocument` is EVEngine-side state. Revision, baseColor slot, preview and undo are client responsibilities. SSUI receives only the open protocol envelope with opaque context. Existing documents are checked for revision changes before staging/applying; this prototype expects one writer per project (no cross-process transaction lock).

`editor.py` starts an owned `eve mcp --root <project>` process. Its deterministic controller translates native editor button events into HTTP operations. MCP here is the existing engine-control transport; no LLM is involved. Texture loading/rendering uses `eve.Graphics` in the actual engine VM. Only generated, validated asset filenames enter engine scripts; user prompts are sent as JSON to SSUI, never evaluated as code.

This is a standalone native material document/editor, **not yet a selection adapter for an existing running game's scene graph**. To integrate that later, replace MaterialDocument with the application's entity/material binding and undo commands; keep GenerationClient and the SSUI protocol unchanged. This prototype supports baseColor PNGs only, not complete PBR materials or 3D generation.

## Verification

```sh
python3 -m unittest discover -s tools/ssui-client -p 'test_*.py' -v
python3 tools/ssui-client/editor.py --eve /absolute/path/to/eve \
  --project /absolute/path/to/disposable-smoke-project --smoke
```

The native smoke performs discovery → generation → SHA256-checked download → engine texture preview → persistent apply → undo. It writes `applied.png` from the engine framebuffer before undoing, then exits. Use a disposable project: smoke deliberately creates an undo entry and increments revision twice.

Unit tests cover persistence/undo, stale revisions, corrupt downloads, artifact origin checks, cancellation, job failure and timeout. Current development verification used the locally available macOS engine executable from `worktrees/webgpu-mac-repro/build/macosx-debug/src/engine/eve`; no engine source build was required.

Local trusted development only: the current SSUI mock server has no authentication. Artifact downloads are limited to the configured server origin and redirects are rejected. The PNG size is capped at 16 MiB. On macOS this demo compensates for the tested host's high-DPI UI scaling; other platforms use the normal UI scale.
