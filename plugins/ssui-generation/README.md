# SSUI Generation native plugin

A native EVEngine plugin and material editor using the open SSUI generation v1 HTTP protocol. Runtime uses C++/EveScript; no Python process or LLM is embedded. The bundled panel submits `mock.checker@1`; the client accepts generic recipe IDs, JSON inputs and opaque context.

## Build and verify

Build with the SDK produced by the same engine revision, platform, architecture and configuration as the host:

```sh
cmake -S plugins/ssui-generation -B build/ssui-generation -DCMAKE_BUILD_TYPE=Debug \
  -DEVEngine_DIR=/path/to/matching-sdk/cmake -DCMAKE_PREFIX_PATH=/path/to/poco-prefix
cmake --build build/ssui-generation
ctest --test-dir build/ssui-generation --output-on-failure
cmake --install build/ssui-generation --prefix /path/to/material-project
```

Tests require this repository's initialized `external/zeroerr` submodule and Python 3. To build only the distributable from a copied plugin directory, pass `-DBUILD_TESTING=OFF`. `--target package` produces a platform-specific ZIP. For renderer/VM-independent client tests, configure with `-DSSUI_BUILD_PLUGIN=OFF` (no SDK required), then build and run CTest. This verifies L2 independence from graphics, UI and editor, using the real common Result implementation.

On macOS the client translation unit matches prebuilt Poco's default-visible RTTI; the final library exports only `eve_plugin_init`. This preserves Poco JSON type comparisons while keeping dependency symbols private to each plugin generation. The script binding translation unit retains the SDK's hidden visibility.

Start SSUI's `yarn dev:bridge`, then run the matching `eve mcp --timeout-ms 30000 --root /path/to/material-project`. The editor restores automatically: Connect → Generate → Preview → Apply → Undo. The project owns `generated/material.json`; PNG files use SHA256 names. This editor changes its own material document and preview, not an arbitrary scene's selected material.

```squirrel
function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local plugins = eve.Plugins();
checked(plugins.loadPlugin("./ssui_generation.dylib")); // .dll / .so on other hosts
local client = eve.SSUIGeneration();
checked(client.configure("http://127.0.0.1:7423", "."));
local requestId = checked(client.start("mock.checker@1", "{\"prompt\":\"brick\",\"size\":256}", "{}"));
// On later frames, wait for !busy(), then check completion().
// completion().value is an owning candidate path. Preview before checked(client.apply()).
// checked(client.undo()) restores the previous binding.
// requestCancel() signals cancellation; shutdown() cancels and joins without committing.
```

`configure/start/completion/apply/undo` return canonical `eve::Result` and the engine's common Squirrel projection. `status()` is display text only; branch on Result codes. Start acceptance and asynchronous completion are separate outcomes. The request ID is also the server idempotency key. One client has at most one active request/candidate. `requestCancel()` is an explicit fire-and-forget signal: local publication is suppressed immediately, remote cancellation is best-effort, and `completion()` reports `Cancelled`. No callbacks run while the client mutex is held. Clients are independent; configure only one writer per project.

## Ownership and persistence

ModuleManager retains a stateless bootstrap per native factory generation. Each `eve.SSUIGeneration()` creates a script-owned client with the SDK release hook; GC/VM destruction joins its worker before releasing members. The allocator also supplies the SDK type-generation discriminator, so old closures remain bound to their original native layout. Worker cleanup does not depend on destruction of process-resident module instances. All commands and destruction belong to the main/composition thread; the worker only performs HTTP, CPU validation and file I/O. Queries return synchronized owning snapshots. No engine, VM, graphics or document pointer is passed to the worker.

Libraries and retired instances remain resident according to the current SDK. Before explicitly reloading, call `shutdown()` on the old client, check `plugins.reload(path)`, create the new `eve.SSUIGeneration()`, then configure it to restore the document. Old script references still address the old generation; do not use them as a second writer. There is no physical unload or automatic migration of an in-flight request. Dropping a script client or closing its VM cancels/joins the worker; closing the panel while a root reference remains keeps the client alive. Call `shutdown()` for an explicit earlier boundary. A missing plugin produces the loader's structured failure; a missing HTTP service produces a completion failure and no publication.

Material schema: `schema: "eve.ssui-material", version: 1`, with integer `revision`, string `baseColor` and string-array `history`. Unknown fields and future versions are rejected. The previous unversioned native prototype is v0: it is validated into temporary state and upgraded on the next successful write. Invalid restore leaves the active state and original file intact. The separate Python reference prototype's nullable fields are not this format.

Documents are bounded to 1 MiB / 4096 history entries. Configure validates all fields before publication. Apply/undo compare the disk revision, write a sibling temporary file and atomically replace the document before publishing memory state. One writer per project is required; this is optimistic stale-write detection, not a cross-process database lock. Candidate files may remain unreferenced after cancellation; only a successful apply changes the binding. No automatic asset deletion occurs. Restore never resumes old HTTP jobs.

## Module interface and mechanism review

This external plugin has no entry in the built-in module manifest. `plugin.cpp` is its registration/binding source of truth; this section records the six boundary faces without a parallel machine catalogue (R-FACE-1..3):

| Face | Boundary |
| --- | --- |
| Capability input | Hard native dependency on common ModuleManager, Result/Status/Diagnostic, Value and Squirrel projection; Poco Net/JSON/Foundation and Threads |
| Capability output | No capability provider registration; script-owned `SSUIGeneration` clients registered by a stateless bootstrap |
| ECS / Link data | No ECS entities, Systems or cross-domain Links; owning paths and request IDs only |
| Events | No subscriptions/callbacks; bounded worker state polled by the editor |
| Script | Checked commands listed above; presentation-only progress/status/path snapshots |
| Tool/protocol | Generic SSUI `/api/generation/v1`; no new MCP tools; existing EditorHost tools drive smoke tests |

The editor additionally requires Graphics and UI; the native client does not. The absent-provider and malformed-provider CTest cases verify explicit failure (L4); the client-only target links without a renderer or VM (L2). No broader runtime profile trim claim is made (R-BOUND-1..4).

Connection choice (R-MECH-1): one known client ↔ one configured service, low-frequency asynchronous requests, no unknown observers, prepare/commit atomicity owned by the client. Direct HTTP plus owning snapshots is sufficient; there is no second provider registry or event bus. Frame hooks poll cheap state and render an already-loaded texture; disk/network work is outside rendering. Expensive API costs are documented in `GenerationClient.h` (R-COST-1..4).

The wire schema is identified by capabilities (`protocol: "ssui-generation", version: "1"`) and the `/api/generation/v1` routes. Unknown response fields are ignored for v1 extensibility; unknown material-document fields are rejected as described above. Recipe IDs version the input contract; the provider validates its inputs/context.

Transport constraints: trusted local HTTP only, no TLS downgrade; same-origin URLs, no redirects, PNG signature and SHA256 verification, 16 MiB artifact limit, five-second socket timeouts and a 120-second job-poll deadline. Deadlines and request UUIDs are transport concerns, not simulation time/RNG. Generation determinism belongs to the selected recipe/seed; this client preserves received bytes and verifies hashes.

## Integration test

```sh
python3 plugins/ssui-generation/tests/protocol_test.py --mode success --native-eve /path/to/matching/eve \
  --project /path/to/disposable-installed-plugin-project
```

This starts an ephemeral protocol fixture and uses the repository's stdio MCP driver to exercise the real plugin/panel, generation, preview, apply, screenshot, undo, cancellation, Result projection and native generation reload. It runs the actual worker and checks its clean exit code. To use a running SSUI service instead, invoke `tests/native_smoke.py --eve ... --project ... --server http://127.0.0.1:7423`. Python is a development test driver only. CTest covers protocol success, bad hash, missing artifacts, job failure, service absence, v0 migration, restore rejection, persistence failure, revision conflicts, busy rejection, in-flight cancellation and worker destruction.
