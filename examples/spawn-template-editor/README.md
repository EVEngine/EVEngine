# Spawn Template Editor

Runnable template authoring entry using the actual RPG skill/unit resolver, canonical template compiler,
existing property form and undo/redo, CAS asset save and atomic runtime JSON export. No map or actors are created.

Build with `map_rpg_spawn_editor` enabled, then run from the repository root:

```sh
make run GAME=examples/spawn-template-editor
```

The fixed layout requires at least 1280 by 800 logical pixels. The left panel edits the template's content,
schedule, limits and placement rules. Use Edit structure / Apply draft for branch or array changes;
invalid candidates preserve committed source. Undo/Redo use the same document transaction history.

Save stores the editor envelope in this project's `Content/spawn-editor/wolves-template.json`.
The document is reopened on the next launch using its explicit asset key and URI; saved content supersedes the initial source.
Prepare/Write in the runtime export panel writes bare template JSON to `spawn-editor/wolves.template.json`
under the engine filesystem's `evengine-spawn-editor` save directory. Save and Export are separate operations.

Window close requests remain in the session panel. Save committed source and resolve drafts before
Close when saved, or explicitly Discard changes and close. Template edits do not publish definitions or change
running map instances. Import the exported template explicitly in a runtime project's content pipeline.

The `rpgSpawnEditor.openTemplate(win, fs, sourceJson, root, assetId, documentUri, exportPath)` binding
opens this mode. Required skills and units must already have been admitted through the canonical RPG and
`publishUnit` APIs. One builder owns either a map or template session; opening another requires explicit shutdown.
`render(gfx, null, ui)` renders its mounted panels; no offscreen canvas is required in template mode.

Smoke with the built binary:

```sh
EVE="$PWD/build/macosx-debug/src/engine/eve" MIN_RUN_SECONDS=2 bash scripts/smoke_examples.sh spawn-template-editor
```

Runtime simulation preview, hot reload, resizing and full RPG/RTS restoration remain outside this example.
