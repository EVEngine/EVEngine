# EVEngine Developer Documentation

[中文](../../dev/README.md) · [Game developer guide](../usr/README.md)

These guides are for maintainers and contributors changing the engine itself. To make a game, download a prebuilt SDK and start with the user guide; an engine build is unnecessary.

English documentation is maintained by hand. Chinese is authoritative: update the Chinese source first when behavior or policy changes, then update its English counterpart. These practical guides translate commonly needed instructions. Detailed design specifications, historical progress tables and implementation records remain linked in Chinese.

## Start here

1. [Building](BUILDING.md): tools, checkout, desktop/mobile targets and incremental builds.
2. [Testing](TESTING.md): source checks, CTest, CPU/GPU fixtures and coverage conventions.
3. [Debugging](DEBUGGING.md): validation, crash traces, reproductions and stale artifacts.
4. [Dependencies](DEPENDENCIES.md): source management and prebuilt third-party libraries.
5. [Module development](MODULE-DEVELOPMENT.md): boundaries, mechanisms and review evidence.
6. [Squirrel bindings](SCRIPT-BINDINGS.md): shared helpers and generated signatures.
7. [Result contracts](RESULT-CONTRACTS.md): checked results and ownership tokens.
8. [Release process](RELEASING.md): branches, SDK gates, promotion and recovery.

Public signatures belong in the generated [C++ reference](https://evengine.github.io/EVEngine/) and the SDK's `share/eve/ai/eve-api.json`. Guides explain workflows and behavioral constraints rather than repeating method-name lists.

## Repository map

- `src/engine/`: startup, command-line tools, common mechanisms and developer tools.
- `src/modules/`: optional domains and their satellites.
- `src/scripts/`: bootstrap scripts and shaders.
- `cmake/`: module manifest, profiles, build support and SDK packaging.
- `platform/`: platform integration and application templates.
- `test/`: C++ tests, fixtures and assets.
- `examples/`: runnable games and reviewed non-runnable examples.
- `scripts/`: source checks, generators and release tooling.
- `docs/usr/` and `docs/en/usr/`: Chinese and English game-development guides.
- `docs/dev/`: authoritative architecture requirements and implementation records.

## Detailed sources

The [Chinese index](../../dev/README.md) distinguishes mandatory specifications, product plans, module designs, current schemas and temporary implementation records. A file not indexed there is not a navigation entry.

Read the [module interface specification (Chinese)](../../dev/模块接口契约与机制选型规范.md), [refactoring requirements (Chinese)](../../dev/重构代码质量与系统完整性规范.md) and [domain composition architecture (Chinese)](../../dev/领域短根继承与跨域组合架构.md) before changing public APIs, ECS relationships or persistence. Stable rule IDs remain review references; plans do not override these requirements.

Current resource schema versions are listed in the Chinese index. Older `vN` documents describe migration input, not the format new code should write. `superpowers/` holds staged plans/specifications; once implemented, unique material should move into maintained documentation and obsolete plans should be removed.

## Build the documentation

Install Doxygen, then build without configuring the engine:

```sh
cmake -S docs -B build/docs
cmake --build build/docs --target docs
```

You can also use `make docs` or an existing engine build's `docs` target. Output is `docs/api/html/`, ignored by Git. Chinese and English guides share the C++ reference and existing GitHub Pages deployment.
