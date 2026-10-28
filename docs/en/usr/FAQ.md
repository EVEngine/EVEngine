# User FAQ

[User guide](README.md) · [Examples](EXAMPLES.md) · [AI and MCP](AI-MCP.md)

## Do I need to compile the engine?

For script game development, download a prebuilt SDK from [Releases](https://github.com/EVEngine/EVEngine/releases). Start with the bundled `share/eve/examples/basic`. Native engine changes require the [source build guide](../dev/BUILDING.md). Check each release's platform requirements and notes before choosing an archive.

## Do I need the Vulkan SDK?

Running a desktop SDK needs a working Vulkan graphics driver. It does not require the Vulkan development SDK, CMake or a compiler. Follow the [platform setup instructions](README.md). Run the bundled basic example before investigating your own game's scripts.

## Why can I not find a showcase from the README?

Only basic is bundled as the SDK reference game. Other examples come from the source repository. Download the source for your SDK's release tag and use the SDK executable to run it. A `dev` example can depend on unreleased APIs; [the example index](../../../examples/README.en.md) explains the two workflows.

## Why does an API in the online docs fail in my SDK?

The online site can reflect newer source than your installed release. Also, a C++ public method is not necessarily bound to EveScript. Use the SDK's `share/eve/ai/eve-api.json` or MCP lookup, and read [API conventions](API-CONVENTIONS.md). A custom build may omit modules; test optional capabilities with `has_module("slot")`.

## Where do I start when the game does not launch?

Run it from a terminal so you can retain the diagnostic output. Check the executable path, the game directory containing `config.nut` and `main.nut`, and asset paths relative to that directory. Try the bundled basic game, then use `eve run --debug /path/to/game` for development diagnostics. If basic also fails, investigate the runtime and graphics driver first.

## How should I report a problem?

Open an [issue](https://github.com/EVEngine/EVEngine/issues) with the SDK version or source commit, operating system, GPU/driver, exact launch command, relevant terminal output and a small reproduction. For a visual issue, include an image and expected behavior. Avoid including private project data or credentials. Use [Discussions](https://github.com/EVEngine/EVEngine/discussions) for usage questions.

## Does hot reload save my game state?

`persist` retains selected script root state across soft reloads. It does not automatically preserve every native object or provide a player save format. Layout changes need migration. Read [Saving Games](SAVE.md) for the distinction between durable saves, reload state and debug snapshots.

## Can I connect an AI client without writing a bot?

Yes. Configure an MCP connection using [AI and MCP](AI-MCP.md). The running-game bridge uses Node.js; the native `eve mcp` host serves standard I/O directly. These are interactive development connections, not a documentation translation service. The AI Game and AI Editor example drivers can also be tried without an LLM.

## Are all guides available in English?

The common user and developer paths have manual English guides. Some advanced module/design pages remain Chinese and are marked in the [module handbook](MODULES.md). Chinese remains authoritative. Existing gallery images are source-project captures, not proof that every feature is available on every released target.

## Where are the license terms?

Read the repository's [LICENSE](https://github.com/EVEngine/EVEngine/blob/dev/LICENSE), [open-source terms](https://github.com/EVEngine/EVEngine/blob/dev/LICENSE-OPENSOURCE) and [commercial terms](https://github.com/EVEngine/EVEngine/blob/dev/LICENSE-COMMERCIAL). They already contain English translations; the Chinese terms prevail as stated in those documents. Consult the actual terms before distribution.
