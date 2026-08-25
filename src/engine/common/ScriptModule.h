#pragma once

#include "common/Export.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SQVM;

namespace eve::script {

/** @brief Result of asking a provider to resolve or load a script module. */
enum class ScriptModuleStatus { NotHandled, Found, Error };

/** @brief Logical module request emitted by an EveScript import declaration. */
struct EVENGINE_API ScriptModuleRequest {
    std::string importerUri;
    std::string specifier;
};

/** @brief UTF-8 source and stable identity returned by a module provider. */
struct EVENGINE_API ScriptModuleSource {
    std::string canonicalUri;
    std::string utf8Source;
    std::string contentHash;
    std::string debugOrigin;
};

/** @brief Pluggable script source backend for directories, archives, plugins, or memory. */
class EVENGINE_API IScriptModuleProvider {
public:
    virtual ~IScriptModuleProvider() = default;

    /** @brief Resolves a specifier to a canonical URI. */
    virtual ScriptModuleStatus resolve(const ScriptModuleRequest& request, std::string& canonicalUri,
                                       std::string& error) = 0;
    /** @brief Loads UTF-8 source for a canonical URI. */
    virtual ScriptModuleStatus load(std::string_view canonicalUri, ScriptModuleSource& source, std::string& error) = 0;
};

/**
 * @brief Owns providers, the static dependency graph, compiled closures, and module instances.
 *
 * Providers never execute Squirrel. Dependencies are compiled before their importer executes;
 * the VM import opcode therefore performs a cache-only lookup.
 */
class EVENGINE_API ScriptModuleResolver {
public:
    using ProviderId = uint64_t;

    explicit ScriptModuleResolver(SQVM* vm);
    ~ScriptModuleResolver();
    ScriptModuleResolver(const ScriptModuleResolver&)            = delete;
    ScriptModuleResolver& operator=(const ScriptModuleResolver&) = delete;

    /** @brief Registers a provider. Higher priorities are tried first. */
    ProviderId registerProvider(std::shared_ptr<IScriptModuleProvider> provider, int priority = 0);
    /** @brief Removes a provider; existing compiled module records remain valid. */
    bool unregisterProvider(ProviderId id);
    /** @brief Installs the default game:/ provider backed by IFileSystem and disk fallback. */
    void registerDefaultProviders();

    /** @brief Clears prior diagnostics and dependency edges before compiling an importer. */
    void beginCompilation(std::string_view importerUri);
    /** @brief Compiles all imports discovered while compiling an importer. */
    void prepareDependencies(std::string_view importerUri);
    /** @brief Instantiates the importer's dependencies in topological order. */
    void instantiateDependencies(std::string_view importerUri);
    /** @brief Invalidates a module and all reverse dependencies. */
    void invalidate(std::string_view canonicalUri);
    /** @brief Transactionally reloads one module, retaining the prior generation on failure. */
    void reload(std::string_view canonicalUri);
    /** @brief Last resolver diagnostic produced by a Squirrel callback. */
    const std::string& lastError() const noexcept;

    /** @brief Normalizes a logical module URI and rejects root traversal. */
    static bool canonicalize(const ScriptModuleRequest& request, std::string& output, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::script
