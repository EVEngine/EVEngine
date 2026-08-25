#include "common/ScriptModule.h"

#include "common/Capability.h"
#include "common/ServiceInterfaces.h"

#include <squirrel.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace eve::script {
namespace {

std::string normalizedPath(std::string value, bool& escaped) {
    std::replace(value.begin(), value.end(), '\\', '/');
    std::vector<std::string> parts;
    size_t                   begin = 0;
    while (begin <= value.size()) {
        const size_t      end  = value.find('/', begin);
        const std::string part = value.substr(begin, end - begin);
        if (part.empty() || part == ".") {
        } else if (part == "..") {
            if (parts.empty()) {
                escaped = true;
                return {};
            }
            parts.pop_back();
        } else {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out << '/';
        out << parts[i];
    }
    return out.str();
}

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

class GameFileProvider final : public IScriptModuleProvider {
public:
    ScriptModuleStatus resolve(const ScriptModuleRequest& request, std::string& canonicalUri,
                               std::string& error) override {
        if (!ScriptModuleResolver::canonicalize(request, canonicalUri, error)) return ScriptModuleStatus::Error;
        return canonicalUri.rfind("game:/", 0) == 0 ? ScriptModuleStatus::Found : ScriptModuleStatus::NotHandled;
    }

    ScriptModuleStatus load(std::string_view canonicalUri, ScriptModuleSource& source, std::string& error) override {
        if (canonicalUri.rfind("game:/", 0) != 0) return ScriptModuleStatus::NotHandled;
        const std::string    path(canonicalUri.substr(6));
        std::vector<uint8_t> bytes;
        if (auto* filesystem = eve::cap::query<eve::service::IFileSystem>();
            filesystem && filesystem->readFile(path, bytes)) {
            source.utf8Source.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                error = "script module not found: " + std::string(canonicalUri);
                return ScriptModuleStatus::Error;
            }
            source.utf8Source.assign(std::istreambuf_iterator<char>(input), {});
        }
        source.canonicalUri = std::string(canonicalUri);
        source.debugOrigin  = path;
        source.contentHash  = hashSource(source.utf8Source);
        return ScriptModuleStatus::Found;
    }
};

}  // namespace

struct ScriptModuleResolver::Impl {
    struct ProviderEntry {
        ProviderId                             id       = 0;
        int                                    priority = 0;
        std::shared_ptr<IScriptModuleProvider> provider;
    };
    struct Module {
        enum class State { Compiling, Compiled, Instantiating, Ready };
        ScriptModuleSource       source;
        HSQOBJECT                closure{};
        HSQOBJECT                exports{};
        State                    state = State::Compiling;
        std::vector<std::string> dependencies;
    };

    explicit Impl(SQVM* inVm) : vm(inVm) { sq_setmodulehandlers(vm, &dependencyCallback, &importCallback, this); }
    ~Impl() {
        sq_setmodulehandlers(vm, nullptr, nullptr, nullptr);
        for (auto& [_, module] : modules) {
            if (!sq_isnull(module.closure)) sq_release(vm, &module.closure);
            if (!sq_isnull(module.exports)) sq_release(vm, &module.exports);
        }
    }

    static SQRESULT dependencyCallback(HSQUIRRELVM, const SQChar* importer, const SQChar* specifier,
                                       SQUserPointer user) {
        auto& self = *static_cast<Impl*>(user);
        try {
            const std::string dependency = self.ensureCompiled(importer, specifier);
            auto&             edges      = self.dependencies[importer];
            if (std::find(edges.begin(), edges.end(), dependency) == edges.end()) edges.push_back(dependency);
            return SQ_OK;
        } catch (const std::exception& error) {
            self.lastError = error.what();
            return SQ_ERROR;
        }
    }

    static SQRESULT importCallback(HSQUIRRELVM, const SQChar* importer, const SQChar* specifier, SQUserPointer user,
                                   HSQOBJECT* exports) {
        auto& self = *static_cast<Impl*>(user);
        try {
            const std::string canonical = self.resolve({importer, specifier});
            const auto        found     = self.modules.find(canonical);
            if (found == self.modules.end() || found->second.state != Module::State::Ready)
                throw std::runtime_error("module was not instantiated before import: " + canonical);
            *exports = found->second.exports;
            return SQ_OK;
        } catch (const std::exception& error) {
            self.lastError = error.what();
            return SQ_ERROR;
        }
    }

    std::string resolve(const ScriptModuleRequest& request) {
        std::string attempts;
        for (const ProviderEntry& entry : providers) {
            std::string canonical;
            std::string error;
            const auto  status = entry.provider->resolve(request, canonical, error);
            if (status == ScriptModuleStatus::Found) return canonical;
            if (status == ScriptModuleStatus::Error)
                throw std::runtime_error(error.empty() ? "module resolution failed" : error);
            if (!attempts.empty()) attempts += ", ";
            attempts += std::to_string(entry.id);
        }
        throw std::runtime_error("no script module provider handled '" + request.specifier + "' imported by '" +
                                 request.importerUri + "' (providers: " + attempts + ")");
    }

    ScriptModuleSource load(const std::string& canonical) {
        for (const ProviderEntry& entry : providers) {
            ScriptModuleSource source;
            std::string        error;
            const auto         status = entry.provider->load(canonical, source, error);
            if (status == ScriptModuleStatus::NotHandled) continue;
            if (status == ScriptModuleStatus::Error)
                throw std::runtime_error(error.empty() ? "module load failed" : error);
            if (source.canonicalUri != canonical)
                throw std::runtime_error("module provider returned inconsistent identity: " + source.canonicalUri +
                                         " != " + canonical);
            return source;
        }
        throw std::runtime_error("no script module provider can load: " + canonical);
    }

    std::string ensureCompiled(std::string_view importer, std::string_view specifier) {
        const std::string canonical = resolve({std::string(importer), std::string(specifier)});
        if (const auto found = modules.find(canonical); found != modules.end()) {
            if (found->second.state == Module::State::Compiling)
                throw std::runtime_error("cyclic script import: " + canonical);
            return canonical;
        }

        Module& module = modules[canonical];
        sq_resetobject(&module.closure);
        sq_resetobject(&module.exports);
        module.source       = load(canonical);
        module.state        = Module::State::Compiling;
        const SQInteger top = sq_gettop(vm);
        if (SQ_FAILED(sq_compilebuffer(vm, module.source.utf8Source.c_str(),
                                       static_cast<SQInteger>(module.source.utf8Source.size()), canonical.c_str(),
                                       SQTrue))) {
            sq_settop(vm, top);
            modules.erase(canonical);
            if (!lastError.empty()) throw std::runtime_error(lastError);
            throw std::runtime_error("failed to compile script module: " + canonical);
        }
        sq_getstackobj(vm, -1, &module.closure);
        sq_addref(vm, &module.closure);
        sq_settop(vm, top);
        module.dependencies = dependencies[canonical];
        module.state        = Module::State::Compiled;
        return canonical;
    }

    void instantiate(const std::string& canonical) {
        auto found = modules.find(canonical);
        if (found == modules.end()) throw std::runtime_error("unknown script module: " + canonical);
        instantiateModule(canonical, found->second);
    }

    void instantiateModule(const std::string& canonical, Module& module) {
        if (module.state == Module::State::Ready) return;
        if (module.state == Module::State::Instantiating)
            throw std::runtime_error("cyclic script module instantiation: " + canonical);
        module.state = Module::State::Instantiating;
        for (const std::string& dependency : module.dependencies) instantiate(dependency);

        const SQInteger top = sq_gettop(vm);
        sq_newtable(vm);                     // env
        sq_pushroottable(vm);                // env, root
        sq_setdelegate(vm, -2);              // env
        sq_pushstring(vm, "__exports", -1);  // env, key
        sq_newtable(vm);                     // env, key, exports
        sq_getstackobj(vm, -1, &module.exports);
        sq_addref(vm, &module.exports);
        sq_newslot(vm, -3, SQFalse);        // env
        sq_pushobject(vm, module.closure);  // env, closure
        sq_push(vm, -2);                    // env, closure, env
        sq_setclosureroot(vm, -2);          // env, closure
        sq_push(vm, -2);                    // env, closure, env(this)
        if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
            sq_settop(vm, top);
            module.state = Module::State::Compiled;
            sq_release(vm, &module.exports);
            sq_resetobject(&module.exports);
            throw std::runtime_error("failed to instantiate script module: " + canonical);
        }
        sq_settop(vm, top);
        module.state = Module::State::Ready;
    }

    void release(Module& module) {
        if (!sq_isnull(module.closure)) sq_release(vm, &module.closure);
        if (!sq_isnull(module.exports)) sq_release(vm, &module.exports);
        sq_resetobject(&module.closure);
        sq_resetobject(&module.exports);
    }

    void reload(const std::string& canonical) {
        const auto found = modules.find(canonical);
        if (found == modules.end()) throw std::runtime_error("cannot reload unknown script module: " + canonical);
        Module&                        previous      = found->second;
        const Module::State            previousState = previous.state;
        const std::vector<std::string> previousEdges = dependencies[canonical];
        Module                         candidate;
        sq_resetobject(&candidate.closure);
        sq_resetobject(&candidate.exports);
        try {
            candidate.source = load(canonical);
            candidate.state  = Module::State::Compiling;
            dependencies.erase(canonical);
            previous.state      = Module::State::Compiling;
            const SQInteger top = sq_gettop(vm);
            if (SQ_FAILED(sq_compilebuffer(vm, candidate.source.utf8Source.c_str(),
                                           static_cast<SQInteger>(candidate.source.utf8Source.size()),
                                           canonical.c_str(), SQTrue))) {
                sq_settop(vm, top);
                throw std::runtime_error("failed to compile script module generation: " + canonical);
            }
            sq_getstackobj(vm, -1, &candidate.closure);
            sq_addref(vm, &candidate.closure);
            sq_settop(vm, top);
            candidate.dependencies = dependencies[canonical];
            candidate.state        = Module::State::Compiled;
            previous.state         = previousState;
            instantiateModule(canonical, candidate);
        } catch (...) {
            previous.state          = previousState;
            dependencies[canonical] = previousEdges;
            release(candidate);
            throw;
        }
        release(previous);
        previous = std::move(candidate);
    }

    SQVM*                                                     vm             = nullptr;
    ProviderId                                                nextProviderId = 1;
    std::vector<ProviderEntry>                                providers;
    std::unordered_map<std::string, Module>                   modules;
    std::unordered_map<std::string, std::vector<std::string>> dependencies;
    std::string                                               lastError;
};

ScriptModuleResolver::ScriptModuleResolver(SQVM* vm) : impl_(std::make_unique<Impl>(vm)) {}
ScriptModuleResolver::~ScriptModuleResolver() = default;

ScriptModuleResolver::ProviderId ScriptModuleResolver::registerProvider(std::shared_ptr<IScriptModuleProvider> provider,
                                                                        int priority) {
    if (!provider) return 0;
    const ProviderId id = impl_->nextProviderId++;
    impl_->providers.push_back({id, priority, std::move(provider)});
    std::stable_sort(impl_->providers.begin(), impl_->providers.end(),
                     [](const auto& a, const auto& b) { return a.priority > b.priority; });
    return id;
}

bool ScriptModuleResolver::unregisterProvider(ProviderId id) {
    const auto oldSize = impl_->providers.size();
    impl_->providers.erase(std::remove_if(impl_->providers.begin(), impl_->providers.end(),
                                          [id](const auto& entry) { return entry.id == id; }),
                           impl_->providers.end());
    return oldSize != impl_->providers.size();
}

void ScriptModuleResolver::registerDefaultProviders() { registerProvider(std::make_shared<GameFileProvider>()); }

void ScriptModuleResolver::beginCompilation(std::string_view importerUri) {
    impl_->lastError.clear();
    impl_->dependencies.erase(std::string(importerUri));
}

void ScriptModuleResolver::prepareDependencies(std::string_view importerUri) {
    if (!impl_->lastError.empty()) throw std::runtime_error(impl_->lastError);

    enum class Visit { Visiting, Ready };
    std::unordered_map<std::string, Visit> visits;
    std::vector<std::string>               stack;
    const auto verify = [&](const auto& self, const std::string& uri) -> void {
        if (const auto seen = visits.find(uri); seen != visits.end()) {
            if (seen->second == Visit::Ready) return;
            std::string cycle;
            const auto  begin = std::find(stack.begin(), stack.end(), uri);
            for (auto it = begin; it != stack.end(); ++it) {
                if (!cycle.empty()) cycle += " -> ";
                cycle += *it;
            }
            if (!cycle.empty()) cycle += " -> ";
            throw std::runtime_error("cyclic script import: " + cycle + uri);
        }

        visits.emplace(uri, Visit::Visiting);
        stack.push_back(uri);
        const auto edges = impl_->dependencies.find(uri);
        if (edges != impl_->dependencies.end()) {
            for (const std::string& dependency : edges->second) {
                if (impl_->modules.find(dependency) == impl_->modules.end())
                    throw std::runtime_error("script dependency was not compiled: " + dependency);
                self(self, dependency);
            }
        }
        stack.pop_back();
        visits[uri] = Visit::Ready;
    };

    const std::string root(importerUri);
    const auto        edges = impl_->dependencies.find(root);
    if (edges == impl_->dependencies.end()) return;
    for (const std::string& dependency : edges->second) verify(verify, dependency);
}

void ScriptModuleResolver::instantiateDependencies(std::string_view importerUri) {
    const auto found = impl_->dependencies.find(std::string(importerUri));
    if (found == impl_->dependencies.end()) return;
    for (const std::string& dependency : found->second) impl_->instantiate(dependency);
}

void ScriptModuleResolver::invalidate(std::string_view canonicalUri) {
    std::unordered_set<std::string> invalid{std::string(canonicalUri)};
    bool                            changed = true;
    while (changed) {
        changed = false;
        for (const auto& [importer, deps] : impl_->dependencies) {
            if (!invalid.count(importer) &&
                std::any_of(deps.begin(), deps.end(), [&](const auto& dep) { return invalid.count(dep); })) {
                invalid.insert(importer);
                changed = true;
            }
        }
    }
    for (const std::string& uri : invalid) {
        const auto found = impl_->modules.find(uri);
        if (found == impl_->modules.end()) continue;
        if (!sq_isnull(found->second.closure)) sq_release(impl_->vm, &found->second.closure);
        if (!sq_isnull(found->second.exports)) sq_release(impl_->vm, &found->second.exports);
        impl_->modules.erase(found);
        impl_->dependencies.erase(uri);
    }
}

void ScriptModuleResolver::reload(std::string_view canonicalUri) { impl_->reload(std::string(canonicalUri)); }

const std::string& ScriptModuleResolver::lastError() const noexcept { return impl_->lastError; }

std::vector<std::string> ScriptModuleResolver::dependencies(std::string_view importerUri) const {
    const auto found = impl_->dependencies.find(std::string(importerUri));
    return found == impl_->dependencies.end() ? std::vector<std::string>{} : found->second;
}

std::vector<std::string> ScriptModuleResolver::reverseDependencies(std::string_view canonicalUri) const {
    std::vector<std::string> result;
    for (const auto& [importer, dependencies] : impl_->dependencies) {
        if (std::find(dependencies.begin(), dependencies.end(), canonicalUri) != dependencies.end())
            result.push_back(importer);
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool ScriptModuleResolver::canonicalize(const ScriptModuleRequest& request, std::string& output, std::string& error) {
    std::string  scheme = "game:";
    std::string  path;
    const size_t schemeEnd = request.specifier.find(":/");
    if (schemeEnd != std::string::npos) {
        scheme = request.specifier.substr(0, schemeEnd + 1);
        path   = request.specifier.substr(schemeEnd + 2);
    } else if (request.specifier.rfind("./", 0) == 0 || request.specifier.rfind("../", 0) == 0) {
        std::string  importer       = request.importerUri;
        const size_t importerScheme = importer.find(":/");
        if (importerScheme != std::string::npos) {
            scheme   = importer.substr(0, importerScheme + 1);
            importer = importer.substr(importerScheme + 2);
        }
        const size_t slash = importer.find_last_of("/\\");
        path = (slash == std::string::npos ? std::string() : importer.substr(0, slash + 1)) + request.specifier;
    } else {
        path = request.specifier;
    }
    bool escaped = false;
    path         = normalizedPath(path, escaped);
    if (escaped || path.empty()) {
        error = escaped ? "script import escapes its module root: " + request.specifier : "empty script module path";
        return false;
    }
    output = scheme + "/" + path;
    return true;
}

}  // namespace eve::script
