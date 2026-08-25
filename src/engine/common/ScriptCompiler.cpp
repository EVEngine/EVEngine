#include "common/ScriptCompiler.h"

#include "common/ScriptError.h"
#include "common/ScriptModule.h"

#include <simplesquirrel/vm.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <regex>
#include <sstream>

namespace eve::script {
namespace {

std::mutex                                       compilerRegistryMutex;
std::unordered_map<HSQUIRRELVM, ScriptCompiler*> compilerRegistry;

std::string hashSource(std::string_view source) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char c : source) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}

ScriptSourcePosition positionAt(std::string_view source, size_t offset) {
    ScriptSourcePosition position;
    for (size_t i = 0; i < offset && i < source.size(); ++i) {
        if (source[i] == '\n') {
            ++position.line;
            position.column = 1;
        } else {
            ++position.column;
        }
    }
    return position;
}

template <typename Callback>
void forMatches(std::string_view source, const std::regex& pattern, Callback callback) {
    const std::string owned(source);
    for (std::sregex_iterator it(owned.begin(), owned.end(), pattern), end; it != end; ++it)
        callback(*it, static_cast<size_t>(it->position()));
}

void appendUnique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(std::move(value));
}

std::string trim(std::string_view value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) return {};
    const size_t end = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(begin, end - begin + 1));
}

std::string attributeValue(std::string_view value) {
    std::string result = trim(value);
    if (result.size() >= 2 &&
        ((result.front() == '"' && result.back() == '"') || (result.front() == '\'' && result.back() == '\'')))
        return result.substr(1, result.size() - 2);
    return result;
}

std::vector<std::string> splitArguments(std::string_view arguments) {
    std::vector<std::string> result;
    size_t                   begin = 0;
    char                     quote = 0;
    for (size_t i = 0; i <= arguments.size(); ++i) {
        const char c = i == arguments.size() ? ',' : arguments[i];
        if ((c == '"' || c == '\'') && (i == 0 || arguments[i - 1] != '\\'))
            quote = quote == 0 ? c : (quote == c ? 0 : quote);
        if (c == ',' && quote == 0) {
            result.push_back(trim(arguments.substr(begin, i - begin)));
            begin = i + 1;
        }
    }
    if (result.size() == 1 && result.front().empty()) result.clear();
    return result;
}

std::vector<std::string> typeChoices(std::string_view type) {
    std::vector<std::string> result;
    static const std::regex  choicePattern(R"([\"']([^\"']+)[\"'])");
    forMatches(type, choicePattern, [&](const std::smatch& match, size_t) { appendUnique(result, match[1].str()); });
    return result;
}

std::string diagnosticCode(std::string_view message) {
    if (message.find("not assignable") != std::string_view::npos) return "EVE2002";
    if (message.find("outside the allowed choices") != std::string_view::npos) return "EVE2101";
    if (message.find("named argument") != std::string_view::npos) return "EVE2201";
    if (message.find("Binding Contract") != std::string_view::npos) return "EVE2202";
    if (message.find("incompatible unit") != std::string_view::npos) return "EVE2301";
    if (message.find("non-exhaustive match") != std::string_view::npos) return "EVE2401";
    if (message.find("persist is only allowed") != std::string_view::npos) return "EVE2501";
    if (message.find("await is only allowed") != std::string_view::npos) return "EVE2601";
    if (message.find("cannot resolve") != std::string_view::npos) return "EVE1101";
    if (message.find("escapes") != std::string_view::npos) return "EVE1102";
    if (message.find("cyclic import") != std::string_view::npos) return "EVE1103";
    if (message.find("export") != std::string_view::npos) return "EVE1104";
    if (message.find("identity") != std::string_view::npos) return "EVE1105";
    return "EVE0001";
}

ScriptDiagnostic makeDiagnostic(std::string_view error, std::string_view uri) {
    std::string source;
    std::string message(error);
    int         line   = 1;
    int         column = 1;
    parseCompileError(std::string(error), &source, &line, &column, &message);
    ScriptDiagnostic diagnostic;
    diagnostic.code            = diagnosticCode(message);
    diagnostic.message         = std::move(message);
    diagnostic.canonicalUri    = source.empty() ? std::string(uri) : std::move(source);
    diagnostic.position.line   = static_cast<uint32_t>(std::max(line, 1));
    diagnostic.position.column = static_cast<uint32_t>(std::max(column, 1));
    if (diagnostic.code == "EVE2201") diagnostic.fix = "check the parameter names and required arguments";
    if (diagnostic.code == "EVE2401") diagnostic.fix = "add the missing cases or an else arm";
    if (diagnostic.code == "EVE2601") diagnostic.fix = "mark the containing function async";
    return diagnostic;
}

}  // namespace

ScriptSourcePosition ScriptSourceMap::originalPosition(ScriptSourcePosition generated) const noexcept {
    ScriptSourcePosition result = generated;
    for (const ScriptSourceMapEntry& entry : entries) {
        if (entry.generated.line > generated.line ||
            (entry.generated.line == generated.line && entry.generated.column > generated.column))
            break;
        result.line   = entry.original.line + (generated.line - entry.generated.line);
        result.column = generated.line == entry.generated.line
                            ? entry.original.column + (generated.column - entry.generated.column)
                            : generated.column;
    }
    return result;
}

std::string BindingContract::key() const {
    std::string result = module;
    result += '/';
    if (!scriptClass.empty()) {
        result += scriptClass;
        result += '.';
    }
    result += method;
    return result;
}

void BindingContractRegistry::registerContract(BindingContract contract) {
    contracts_[contract.key()] = std::move(contract);
}

bool BindingContractRegistry::unregisterContract(std::string_view key) {
    return contracts_.erase(std::string(key)) != 0;
}

const BindingContract* BindingContractRegistry::find(std::string_view key) const noexcept {
    const auto found = contracts_.find(std::string(key));
    return found == contracts_.end() ? nullptr : &found->second;
}

const BindingContract* BindingContractRegistry::findMethod(std::string_view method) const noexcept {
    const BindingContract* result = nullptr;
    for (const auto& [_, contract] : contracts_) {
        if (contract.method != method) continue;
        if (result != nullptr) return nullptr;
        result = &contract;
    }
    return result;
}

std::vector<BindingContract> BindingContractRegistry::snapshot() const {
    std::vector<BindingContract> result;
    result.reserve(contracts_.size());
    for (const auto& [_, contract] : contracts_) result.push_back(contract);
    std::sort(result.begin(), result.end(),
              [](const BindingContract& a, const BindingContract& b) { return a.key() < b.key(); });
    return result;
}

ScriptCompiler::ScriptCompiler(ssq::VM& vm, ScriptModuleResolver& modules) : vm_(&vm), modules_(&modules) {
    {
        std::lock_guard lock(compilerRegistryMutex);
        compilerRegistry[vm_->getHandle()] = this;
    }
    sq_setnamedargresolver(
        vm_->getHandle(),
        [](HSQUIRRELVM, const SQChar* callee, SQInteger index, const SQChar** type, SQBool* nullable,
           const SQChar** unit, const SQChar** choices, SQUserPointer user) -> const SQChar* {
            const auto&            self     = *static_cast<ScriptCompiler*>(user);
            const BindingContract* contract = self.bindings_.findMethod(callee);
            if (contract == nullptr || index < 0 || static_cast<size_t>(index) >= contract->parameters.size())
                return nullptr;
            const BindingParameterContract& parameter = contract->parameters[static_cast<size_t>(index)];
            if (type != nullptr) *type = parameter.type.c_str();
            if (nullable != nullptr) *nullable = parameter.nullable ? SQTrue : SQFalse;
            static const char* units[] = {nullptr, "seconds", "milliseconds", "radians", "degrees", "pixels", "meters"};
            if (unit != nullptr) *unit = units[static_cast<size_t>(parameter.unit)];
            if (choices != nullptr) {
                static thread_local std::string choiceBuffer;
                choiceBuffer.clear();
                for (size_t i = 0; i < parameter.choices.size(); ++i) {
                    if (i != 0) choiceBuffer += ',';
                    choiceBuffer += parameter.choices[i];
                }
                *choices = choiceBuffer.empty() ? nullptr : choiceBuffer.c_str();
            }
            return parameter.name.c_str();
        },
        this);
    sq_setannotationresolver(
        vm_->getHandle(),
        [](HSQUIRRELVM, const SQChar* annotation, SQUserPointer user) -> SQBool {
            const auto& names = static_cast<ScriptCompiler*>(user)->annotations_;
            return names.find(annotation) == names.end() ? SQFalse : SQTrue;
        },
        this);
}

ScriptCompiler::~ScriptCompiler() {
    {
        std::lock_guard lock(compilerRegistryMutex);
        compilerRegistry.erase(vm_->getHandle());
    }
    sq_setannotationresolver(vm_->getHandle(), nullptr, nullptr);
    sq_setnamedargresolver(vm_->getHandle(), nullptr, nullptr);
}

ssq::Script ScriptCompiler::compileSource(std::string_view source, std::string_view sourceName) {
    const std::string uri(sourceName);
    modules_->beginCompilation(uri);
    ScriptMetadata next = analyze(source, uri);
    try {
        ssq::Script compiled = vm_->compileSource(std::string(source).c_str(), uri.c_str());
        modules_->prepareDependencies(uri);
        next.dependencies = modules_->dependencies(uri);
        metadata_[uri]    = std::move(next);
        refreshDependencyMetadata();
        return compiled;
    } catch (const std::exception& error) {
        next.diagnostics.push_back(makeDiagnostic(error.what(), uri));
        metadata_[uri] = std::move(next);
        throw;
    }
}

ssq::Script ScriptCompiler::compileFile(std::string_view path) {
    const std::string uri(path);
    ScriptMetadata    next;
    std::ifstream     input(uri, std::ios::binary);
    if (input) {
        const std::string source(std::istreambuf_iterator<char>(input), {});
        next = analyze(source, uri);
    } else {
        next.canonicalUri           = uri;
        next.sourceMap.canonicalUri = uri;
    }
    modules_->beginCompilation(uri);
    try {
        ssq::Script compiled = vm_->compileFile(uri.c_str());
        modules_->prepareDependencies(uri);
        next.dependencies = modules_->dependencies(uri);
        metadata_[uri]    = std::move(next);
        refreshDependencyMetadata();
        return compiled;
    } catch (const std::exception& error) {
        next.diagnostics.push_back(makeDiagnostic(error.what(), uri));
        metadata_[uri] = std::move(next);
        throw;
    }
}

const ScriptMetadata* ScriptCompiler::metadata(std::string_view canonicalUri) const noexcept {
    const auto found = metadata_.find(std::string(canonicalUri));
    return found == metadata_.end() ? nullptr : &found->second;
}

std::vector<ScriptMetadata> ScriptCompiler::metadataSnapshot() const {
    std::vector<ScriptMetadata> result;
    result.reserve(metadata_.size());
    for (const auto& [_, metadata] : metadata_) result.push_back(metadata);
    std::sort(result.begin(), result.end(),
              [](const ScriptMetadata& a, const ScriptMetadata& b) { return a.canonicalUri < b.canonicalUri; });
    return result;
}

void ScriptCompiler::refreshDependencyMetadata() {
    for (auto& [_, metadata] : metadata_) metadata.reverseDependencies.clear();
    for (const auto& [importer, metadata] : metadata_) {
        for (const std::string& dependency : metadata.dependencies) {
            const auto found = metadata_.find(dependency);
            if (found != metadata_.end()) appendUnique(found->second.reverseDependencies, importer);
        }
    }
    for (auto& [_, metadata] : metadata_)
        std::sort(metadata.reverseDependencies.begin(), metadata.reverseDependencies.end());
}

BindingContractRegistry&       ScriptCompiler::bindings() noexcept { return bindings_; }
const BindingContractRegistry& ScriptCompiler::bindings() const noexcept { return bindings_; }

void ScriptCompiler::registerAnnotation(std::string name) {
    if (!name.empty()) annotations_.insert(std::move(name));
}

bool ScriptCompiler::unregisterAnnotation(std::string_view name) { return annotations_.erase(std::string(name)) != 0; }

std::vector<ScriptCompletion> ScriptCompiler::completions(std::string_view canonicalUri,
                                                          std::string_view prefix) const {
    std::vector<ScriptCompletion> result;
    const auto                    matches = [prefix](std::string_view value) { return value.rfind(prefix, 0) == 0; };
    if (const ScriptMetadata* unit = metadata(canonicalUri)) {
        for (const ScriptSymbolMetadata& symbol : unit->symbols) {
            if (matches(symbol.name)) result.push_back({symbol.name, symbol.kind, symbol.erasedType, symbol.name});
        }
        for (const std::string& name : unit->exports) {
            if (matches(name)) result.push_back({name, "export", "module export", name});
        }
    }
    for (const BindingContract& contract : bindings_.snapshot()) {
        if (!matches(contract.method)) continue;
        std::string signature = contract.method + "(";
        std::string insertion = contract.method + "(";
        for (size_t i = 0; i < contract.parameters.size(); ++i) {
            if (i != 0) {
                signature += ", ";
                insertion += ", ";
            }
            signature += contract.parameters[i].name + ": " + contract.parameters[i].type;
            insertion += contract.parameters[i].name + ": ";
        }
        signature += ") -> " + contract.returnType;
        insertion += ')';
        result.push_back({contract.method, "function", std::move(signature), std::move(insertion)});
    }
    std::sort(result.begin(), result.end(), [](const ScriptCompletion& a, const ScriptCompletion& b) {
        if (a.label != b.label) return a.label < b.label;
        return a.kind < b.kind;
    });
    result.erase(std::unique(result.begin(), result.end(),
                             [](const ScriptCompletion& a, const ScriptCompletion& b) {
                                 return a.label == b.label && a.kind == b.kind;
                             }),
                 result.end());
    return result;
}

std::optional<ScriptHover> ScriptCompiler::hover(std::string_view canonicalUri, std::string_view symbol) const {
    if (const ScriptMetadata* unit = metadata(canonicalUri)) {
        for (const ScriptSymbolMetadata& candidate : unit->symbols) {
            if (candidate.name == symbol)
                return ScriptHover{candidate.name,
                                   "`" + candidate.kind + " " + candidate.name + ": " + candidate.erasedType + "`",
                                   candidate.position};
        }
    }
    if (const BindingContract* contract = bindings_.findMethod(symbol)) {
        std::string markdown = "`" + contract->method + "(";
        for (size_t i = 0; i < contract->parameters.size(); ++i) {
            if (i != 0) markdown += ", ";
            markdown += contract->parameters[i].name + ": " + contract->parameters[i].type;
        }
        markdown += ") -> " + contract->returnType + "`";
        if (!contract->documentationId.empty()) markdown += "\n\n" + contract->documentationId;
        return ScriptHover{contract->method, std::move(markdown), {1, 1}};
    }
    return std::nullopt;
}

std::vector<ScriptDiagnostic> ScriptCompiler::diagnostics(std::string_view canonicalUri) const {
    const ScriptMetadata* unit = metadata(canonicalUri);
    return unit == nullptr ? std::vector<ScriptDiagnostic>{} : unit->diagnostics;
}

SQRESULT ScriptCompiler::compileBuffer(HSQUIRRELVM vm, const SQChar* source, SQInteger size, const SQChar* sourceName,
                                       SQBool raiseError) {
    ScriptCompiler* compiler = nullptr;
    {
        std::lock_guard lock(compilerRegistryMutex);
        const auto      found = compilerRegistry.find(vm);
        if (found != compilerRegistry.end()) compiler = found->second;
    }
    if (compiler == nullptr) return sq_compilebuffer(vm, source, size, sourceName, raiseError);

    const std::string_view text(source, static_cast<size_t>(size));
    const std::string      uri  = sourceName == nullptr ? "buffer" : sourceName;
    ScriptMetadata         next = analyze(text, uri);
    compiler->modules_->beginCompilation(uri);
    const SQInteger top    = sq_gettop(vm);
    const SQRESULT  result = sq_compilebuffer(vm, source, size, sourceName, raiseError);
    if (SQ_FAILED(result)) {
        const ScriptErrorContext context = captureCompileError(vm);
        ScriptDiagnostic         diagnostic;
        diagnostic.code            = diagnosticCode(context.message);
        diagnostic.message         = context.message.empty() ? "script compilation failed" : context.message;
        diagnostic.canonicalUri    = context.source.empty() ? uri : context.source;
        diagnostic.position.line   = static_cast<uint32_t>(std::max(context.line, 1));
        diagnostic.position.column = static_cast<uint32_t>(std::max(context.column, 1));
        next.diagnostics.push_back(std::move(diagnostic));
        compiler->metadata_[uri] = std::move(next);
        return result;
    }
    try {
        compiler->modules_->prepareDependencies(uri);
    } catch (const std::exception& error) {
        sq_settop(vm, top);
        next.diagnostics.push_back(makeDiagnostic(error.what(), uri));
        compiler->metadata_[uri] = std::move(next);
        return sq_throwerror(vm, error.what());
    }
    next.dependencies        = compiler->modules_->dependencies(uri);
    compiler->metadata_[uri] = std::move(next);
    compiler->refreshDependencyMetadata();
    return SQ_OK;
}

std::vector<ScriptDiagnostic> ScriptCompiler::validateProjectConfig(std::string_view                source,
                                                                    std::string_view                canonicalUri,
                                                                    const std::vector<std::string>& knownModules,
                                                                    const std::vector<std::string>& activeModules) {
    std::vector<ScriptDiagnostic> result;
    static const std::regex       listPattern(R"(\b(modules|optionalModules)\s*=\s*\[([^\]]*)\])");
    static const std::regex       valuePattern(R"([\"']([^\"']+)[\"'])");
    forMatches(source, listPattern, [&](const std::smatch& list, size_t listOffset) {
        const bool        required = list[1].str() == "modules";
        const std::string values   = list[2].str();
        forMatches(values, valuePattern, [&](const std::smatch& value, size_t valueOffset) {
            const std::string name  = value[1].str();
            const bool        known = std::find(knownModules.begin(), knownModules.end(), name) != knownModules.end();
            const bool active = std::find(activeModules.begin(), activeModules.end(), name) != activeModules.end();
            if (known && (!required || active)) return;
            ScriptDiagnostic diagnostic;
            diagnostic.code         = known ? "EVE1002" : "EVE1001";
            diagnostic.message      = known ? "required module is absent from the selected profile: " + name
                                            : "config contains an unknown module root slot: " + name;
            diagnostic.canonicalUri = std::string(canonicalUri);
            diagnostic.position = positionAt(source, listOffset + static_cast<size_t>(list.position(2)) + valueOffset);
            diagnostic.related  = name;
            diagnostic.fix      = known ? "enable the module in the selected build profile"
                                        : "use a root slot from the generated Module Contract";
            result.push_back(std::move(diagnostic));
        });
    });
    return result;
}

ScriptMetadata ScriptCompiler::analyze(std::string_view source, std::string_view canonicalUri) {
    ScriptMetadata result;
    result.sourceHash             = hashSource(source);
    result.canonicalUri           = std::string(canonicalUri);
    result.providerOrigin         = std::string(canonicalUri);
    result.sourceMap.canonicalUri = result.canonicalUri;
    result.sourceMap.entries.push_back({{1, 1}, {1, 1}});

    static const std::regex importPattern(
        R"(\bimport\s+(?:\*\s+as\s+[A-Za-z_]\w*|\{[^}]*\})\s+from\s+[\"']([^\"']+)[\"'])");
    static const std::regex exportPattern(R"(\bexport\s+(?:function|class|const)\s+([A-Za-z_]\w*))");
    static const std::regex persistPattern(R"(\bpersist\s+([A-Za-z_]\w*)\s*(?::[^=]+)?=)");
    static const std::regex symbolPattern(R"(\b(local|function|class)\s+([A-Za-z_]\w*)\s*(?::\s*([^=,\)\{\n]+))?)");
    static const std::regex asyncPattern(R"(\basync\s+function\s+([A-Za-z_]\w*))");
    static const std::regex awaitPattern(R"(\bawait\b)");
    static const std::regex propertyPattern(
        R"(((?:[ \t]*@[A-Za-z_]\w*\([^\r\n]*\)[ \t]*\r?\n)+)[ \t]*([A-Za-z_]\w*)[ \t]*:[ \t]*([^=\r\n]+)[ \t]*=)");
    static const std::regex annotationPattern(R"(@([A-Za-z_]\w*)\(([^)]*)\))");

    forMatches(source, importPattern, [&](const std::smatch& match, size_t) {
        appendUnique(result.imports, match[1].str());
        appendUnique(result.moduleReferences, match[1].str());
    });
    forMatches(source, exportPattern,
               [&](const std::smatch& match, size_t) { appendUnique(result.exports, match[1].str()); });
    forMatches(source, persistPattern, [&](const std::smatch& match, size_t offset) {
        appendUnique(result.persistRoots, match[1].str());
        result.symbols.push_back({match[1].str(), "persist", "dynamic", positionAt(source, offset)});
    });
    forMatches(source, symbolPattern, [&](const std::smatch& match, size_t offset) {
        result.symbols.push_back({match[2].str(), match[1].str(), match[3].matched ? match[3].str() : "dynamic",
                                  positionAt(source, offset)});
    });
    forMatches(source, asyncPattern,
               [&](const std::smatch& match, size_t) { appendUnique(result.asyncFunctions, match[1].str()); });
    forMatches(source, awaitPattern,
               [&](const std::smatch&, size_t offset) { result.awaitLocations.push_back(positionAt(source, offset)); });
    forMatches(source, propertyPattern, [&](const std::smatch& match, size_t offset) {
        ScriptPropertyMetadata property;
        property.name                 = match[2].str();
        property.erasedType           = trim(match[3].str());
        property.position             = positionAt(source, offset + static_cast<size_t>(match.position(2)));
        const std::string annotations = match[1].str();
        forMatches(annotations, annotationPattern, [&](const std::smatch& annotation, size_t) {
            const std::string              name = annotation[1].str();
            const std::vector<std::string> args = splitArguments(annotation[2].str());
            for (size_t i = 0; i < args.size(); ++i) {
                const size_t colon = args[i].find(':');
                if (i == 0 && colon == std::string::npos)
                    property.attributes[name] = attributeValue(args[i]);
                else if (colon != std::string::npos)
                    property.attributes[trim(std::string_view(args[i]).substr(0, colon))] =
                        attributeValue(std::string_view(args[i]).substr(colon + 1));
            }
        });
        property.choices  = typeChoices(property.erasedType);
        const auto editor = property.attributes.find("editor");
        if (editor != property.attributes.end() && editor->second == "combo" && !property.choices.empty()) {
            std::string options;
            for (size_t i = 0; i < property.choices.size(); ++i) {
                if (i != 0) options += ',';
                options += property.choices[i];
            }
            property.attributes["options"] = std::move(options);
        }
        result.properties.push_back(std::move(property));
    });
    return result;
}

}  // namespace eve::script
