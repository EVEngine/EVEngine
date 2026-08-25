#include "common/ScriptCompiler.h"

#include "common/ScriptModule.h"

#include <simplesquirrel/vm.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <regex>
#include <sstream>

namespace eve::script {
namespace {

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
    sq_setnamedargresolver(
        vm_->getHandle(),
        [](HSQUIRRELVM, const SQChar* callee, SQInteger index, SQUserPointer user) -> const SQChar* {
            const auto&            self     = *static_cast<ScriptCompiler*>(user);
            const BindingContract* contract = self.bindings_.findMethod(callee);
            if (contract == nullptr || index < 0 || static_cast<size_t>(index) >= contract->parameters.size())
                return nullptr;
            return contract->parameters[static_cast<size_t>(index)].name.c_str();
        },
        this);
}

ScriptCompiler::~ScriptCompiler() { sq_setnamedargresolver(vm_->getHandle(), nullptr, nullptr); }

ssq::Script ScriptCompiler::compileSource(std::string_view source, std::string_view sourceName) {
    const std::string uri(sourceName);
    modules_->beginCompilation(uri);
    ScriptMetadata next     = analyze(source, uri);
    ssq::Script    compiled = vm_->compileSource(std::string(source).c_str(), uri.c_str());
    modules_->prepareDependencies(uri);
    metadata_[uri] = std::move(next);
    return compiled;
}

ssq::Script ScriptCompiler::compileFile(std::string_view path) {
    const std::string uri(path);
    modules_->beginCompilation(uri);
    ssq::Script compiled = vm_->compileFile(uri.c_str());
    modules_->prepareDependencies(uri);
    ScriptMetadata next;
    std::ifstream  input(uri, std::ios::binary);
    if (input) {
        const std::string source(std::istreambuf_iterator<char>(input), {});
        next = analyze(source, uri);
    } else {
        next.canonicalUri           = uri;
        next.sourceMap.canonicalUri = uri;
    }
    metadata_[uri] = std::move(next);
    return compiled;
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

BindingContractRegistry&       ScriptCompiler::bindings() noexcept { return bindings_; }
const BindingContractRegistry& ScriptCompiler::bindings() const noexcept { return bindings_; }

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
    return result;
}

}  // namespace eve::script
