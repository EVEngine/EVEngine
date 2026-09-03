#pragma once

#include "common/BorrowedRef.h"

#include "common/Export.h"

#include <simplesquirrel/script.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ssq {
class VM;
}

namespace eve::script {

class ScriptModuleResolver;

/** @brief Severity of a structured EveScript compiler diagnostic. */
enum class ScriptDiagnosticSeverity { Info, Warning, Error };

/** @brief Original EveScript source position. Lines and columns are one-based. */
struct EVENGINE_API ScriptSourcePosition {
    uint32_t line   = 1;
    uint32_t column = 1;
};

/** @brief Stable structured diagnostic shared by Runtime, LSP, MCP, and editor tools. */
struct EVENGINE_API ScriptDiagnostic {
    std::string              code;
    ScriptDiagnosticSeverity severity = ScriptDiagnosticSeverity::Error;
    std::string              message;
    std::string              canonicalUri;
    ScriptSourcePosition     position;
    std::string              related;
    std::string              fix;
};

/** @brief One generated-to-original mapping segment. Native syntax uses identity mappings. */
struct EVENGINE_API ScriptSourceMapEntry {
    ScriptSourcePosition generated;
    ScriptSourcePosition original;
};

/** @brief Source map for a compiled script. */
struct EVENGINE_API ScriptSourceMap {
    std::string                       canonicalUri;
    std::vector<ScriptSourceMapEntry> entries;

    /** @brief Maps a generated location back to the nearest original location. */
    ScriptSourcePosition originalPosition(ScriptSourcePosition generated) const noexcept;
    /** @brief Maps an original location to the nearest generated location. */
    ScriptSourcePosition generatedPosition(ScriptSourcePosition original) const noexcept;
};

/** @brief Erased source-level symbol retained for tooling. */
struct EVENGINE_API ScriptSymbolMetadata {
    std::string          name;
    std::string          kind;
    std::string          erasedType;
    ScriptSourcePosition position;
};

/** @brief Inspector-facing metadata for one annotated script property. */
struct EVENGINE_API ScriptPropertyMetadata {
    std::string                                  name;
    std::string                                  erasedType;
    ScriptSourcePosition                         position;
    std::unordered_map<std::string, std::string> attributes;
    std::vector<std::string>                     choices;
};

/** @brief Metadata retained for one compiled EveScript source unit. */
struct EVENGINE_API ScriptMetadata {
    uint32_t                            languageVersion = 1;
    std::string                         sourceHash;
    std::string                         canonicalUri;
    std::string                         providerOrigin;
    std::vector<std::string>            imports;
    std::vector<std::string>            exports;
    std::vector<std::string>            dependencies;
    std::vector<std::string>            reverseDependencies;
    std::vector<ScriptSymbolMetadata>   symbols;
    std::vector<ScriptPropertyMetadata> properties;
    std::vector<std::string>            persistRoots;
    std::vector<std::string>            moduleReferences;
    std::vector<std::string>            asyncFunctions;
    std::vector<ScriptSourcePosition>   awaitLocations;
    ScriptSourceMap                     sourceMap;
    std::vector<ScriptDiagnostic>       diagnostics;
};

/** @brief Tool-neutral completion item suitable for LSP, MCP, and editor adapters. */
struct EVENGINE_API ScriptCompletion {
    std::string label;
    std::string kind;
    std::string detail;
    std::string insertText;
};

/** @brief Tool-neutral hover result for one script or binding symbol. */
struct EVENGINE_API ScriptHover {
    std::string          symbol;
    std::string          markdown;
    ScriptSourcePosition position;
};

/** @brief Unit attached to a binding parameter or return value. */
enum class ScriptUnit { None, Seconds, Milliseconds, Radians, Degrees, Pixels, Meters };

/** @brief One native binding parameter visible to EveScript tools and named arguments. */
struct EVENGINE_API BindingParameterContract {
    std::string                name;
    std::string                type     = "dynamic";
    bool                       nullable = false;
    std::optional<std::string> scriptDefault;
    ScriptUnit                 unit = ScriptUnit::None;
    std::vector<std::string>   choices;
};

/** @brief Complete script-facing contract for a native function or method. */
struct EVENGINE_API BindingContract {
    std::string                           module;
    std::string                           scriptClass;
    std::string                           method;
    std::vector<BindingParameterContract> parameters;
    std::string                           returnType     = "dynamic";
    bool                                  returnNullable = false;
    std::string                           ownership;
    std::string                           threadAffinity = "main";
    std::vector<std::string>              platforms;
    std::string                           documentationId;

    /** @brief Stable lookup key in the form module/class.method. */
    std::string key() const;
};

/** @brief Registry consumed by compiler checks and tooling protocol adapters. */
class EVENGINE_API BindingContractRegistry {
public:
    /** @brief Adds or atomically replaces a binding contract by key. */
    void registerContract(BindingContract contract);
    /** @brief Removes a binding contract. */
    bool unregisterContract(std::string_view key);
    /**
     * @brief Finds a binding contract.
     * @return Non-owning pointer, or nullptr when metadata is unavailable.
     * @ownership Borrowed; the registry retains the contract.
     * @lifetime Valid until the matching contract is replaced, unregistered, or this registry is destroyed.
     * @thread Same thread as the owning ScriptCompiler / Runtime.
     */
    const BindingContract* find(std::string_view key) const noexcept;
    /**
     * @brief Finds a uniquely named method.
     * @return Non-owning pointer, or nullptr when absent or ambiguous.
     * @ownership Borrowed; the registry retains the contract.
     * @lifetime Valid until the matching contract is replaced, unregistered, or this registry is destroyed.
     * @thread Same thread as the owning ScriptCompiler / Runtime.
     */
    const BindingContract* findMethod(std::string_view method) const noexcept;
    /**
     * @brief Finds a method on one script class.
     * @return Non-owning pointer, or nullptr when the class has no such method.
     * @ownership Borrowed; the registry retains the contract.
     * @lifetime Valid until the matching contract is replaced, unregistered, or this registry is destroyed.
     * @thread Same thread as the owning ScriptCompiler / Runtime.
     */
    const BindingContract* findMethod(std::string_view scriptClass, std::string_view method) const noexcept;
    /** @brief True when generated contracts include this script class. */
    bool hasScriptClass(std::string_view scriptClass) const noexcept;
    /** @brief Returns a stable snapshot sorted by contract key. */
    std::vector<BindingContract> snapshot() const;

private:
    struct Storage {
        std::unordered_map<std::string, BindingContract> contracts;
    };

    void                     ensureUnique();
    std::shared_ptr<Storage> storage_ = std::make_shared<Storage>();
};

/** @brief Unified Runtime compiler facade for metadata, diagnostics, and module preparation. */
class EVENGINE_API ScriptCompiler {
public:
    ScriptCompiler(ssq::VM& vm, ScriptModuleResolver& modules);
    ~ScriptCompiler();

    /** @brief Compiles UTF-8 source and records metadata under sourceName. */
    ssq::Script compileSource(std::string_view source, std::string_view sourceName);
    /** @brief Compiles a file through the VM and records its canonical identity. */
    ssq::Script compileFile(std::string_view path);
    /**
     * @brief Borrows metadata for the most recent successful compilation.
     * @return Empty when no compiled unit exists for the URI.
     * @note The compiler owns the metadata. The reference is invalidated by the
     * next compile or source-map update and by destruction of this compiler.
     * It must not be retained across those operations or across threads.
     */
    [[nodiscard]] eve::OptionalRef<const ScriptMetadata> metadata(std::string_view canonicalUri) const noexcept;
    /** @brief Returns all successful compilation metadata sorted by URI. */
    std::vector<ScriptMetadata> metadataSnapshot() const;
    /** @brief Native binding contracts used by semantic checks and tools. */
    BindingContractRegistry& bindings() noexcept;
    /** @brief Const native binding contract registry. */
    const BindingContractRegistry& bindings() const noexcept;
    /** @brief Registers one plugin annotation name accepted by the parser. */
    void registerAnnotation(std::string name);
    /** @brief Removes one plugin annotation name. */
    bool unregisterAnnotation(std::string_view name);
    /** @brief Returns completion candidates from script metadata and native contracts. */
    std::vector<ScriptCompletion> completions(std::string_view canonicalUri, std::string_view prefix = {}) const;
    /** @brief Returns hover information for a script or native symbol. */
    std::optional<ScriptHover> hover(std::string_view canonicalUri, std::string_view symbol) const;
    /** @brief Returns the latest diagnostics for one source unit. */
    std::vector<ScriptDiagnostic> diagnostics(std::string_view canonicalUri) const;
    /** @brief Replaces the source map retained for a compiled unit. */
    bool setSourceMap(std::string_view canonicalUri, ScriptSourceMap sourceMap);
    /** @brief Maps a VM/generated location through any active compiler metadata. */
    static ScriptSourcePosition toOriginalPosition(std::string_view source, ScriptSourcePosition generated);
    /** @brief Maps an IDE/original location to the VM location used for breakpoints. */
    static ScriptSourcePosition toGeneratedPosition(std::string_view source, ScriptSourcePosition original);
    /** @brief Unified raw-VM entry used by REPL, debugger, MCP, and editor adapters. */
    static SQRESULT compileBuffer(HSQUIRRELVM vm, const SQChar* source, SQInteger size, const SQChar* sourceName,
                                  SQBool raiseError);
    /** @brief Statically checks literal config module lists without executing config.nut. */
    static std::vector<ScriptDiagnostic> validateProjectConfig(std::string_view source, std::string_view canonicalUri,
                                                               const std::vector<std::string>& knownModules,
                                                               const std::vector<std::string>& activeModules);

    /** @brief Performs source-only metadata extraction without executing code. */
    static ScriptMetadata analyze(std::string_view source, std::string_view canonicalUri);

private:
    void                                            refreshDependencyMetadata();
    ssq::VM*                                        vm_;
    ScriptModuleResolver*                           modules_;
    BindingContractRegistry                         bindings_;
    std::unordered_set<std::string>                 nativeGlobals_;
    std::unordered_set<std::string>                 annotations_;
    std::unordered_map<std::string, ScriptMetadata> metadata_;
};

}  // namespace eve::script
