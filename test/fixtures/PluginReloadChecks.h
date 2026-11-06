#pragma once

#include "plugins/Plugins.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>

namespace plugin_reload_checks {

class Probe {
public:
    explicit Probe(ssq::VM& machine) : machine_(machine) {
        for (;;) {
            directory_ = std::filesystem::temp_directory_path() /
                         ("eve-plugin-reload-" + std::to_string(std::random_device{}()));
            if (std::filesystem::create_directory(directory_)) break;
        }
        path_ = directory_ / ("live" + std::string(EVENGINE_TEST_SHARED_LIBRARY_SUFFIX));
        machine_.set("pluginPath", path_.string());
        machine_.set("pluginAlias", (directory_ / "." / path_.filename()).string());
    }

    ~Probe() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    void replace(const std::string& variant) {
        machine_.set("pluginVariant", variant);
        const auto source = std::filesystem::path(EVENGINE_TEST_BINARY_DIR) /
                            ("reload_test_plugin_" + variant + EVENGINE_TEST_SHARED_LIBRARY_SUFFIX);
        std::filesystem::copy_file(source, path_, std::filesystem::copy_options::overwrite_existing);
    }

    void run(const char* source) {
        try {
            machine_.run(machine_.compileSource(source, "plugin-reload-check"));
        } catch (const std::exception& error) {
            std::cerr << "plugin-reload-check failed: " << error.what() << "\n" << source << std::endl;
            throw;
        }
    }

    void corrupt() { std::ofstream(path_, std::ios::binary | std::ios::trunc) << "not a shared library"; }

    void rejectNestedReload() {
        auto registration = eve::ModuleManager::beginPluginRegistration();
        if (!registration.ok()) throw std::runtime_error("cannot open test registration transaction");
        eve::plugins::Plugins other;
        auto                  nested   = other.reload(path_.string());
        const bool            rejected = !nested.ok();
        auto                  rollback = eve::ModuleManager::finishPluginRegistration(false);
        if (!rollback.ok() || !rejected) throw std::runtime_error("nested reload was not rejected");
    }

    void load() {
        replace("v1");
        run(R"(
            plugins <- eve.Plugins();
            assert(plugins.load(pluginPath));
            oldPlugin <- eve.ReloadTestPlugin();
            oldClass <- eve.ReloadTestPlugin;
            removedClass <- eve.RemovedTestPlugin;
            cachedMethod <- oldPlugin.version;
            assert(oldPlugin.version() == 1);
            assert(oldPlugin.increment() == 1);
        )");
    }

private:
    ssq::VM&              machine_;
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

inline void replacement(ssq::VM& machine) {
    Probe probe(machine);
    probe.load();
    probe.replace("v2");
    probe.run(R"(
        // A plain load remains idempotent. Only explicit reload advances code.
        assert(plugins.load(pluginPath));
        assert(plugins.loadPlugin(pluginAlias).code == "no_op");
        assert(plugins.isLoaded(pluginAlias));
        assert(eve.ReloadTestPlugin().version() == 1);
        print("plugin reload: checked original generation\n");
        local result = plugins.reload(pluginPath);
        assert(result.ok && result.code == "applied");
        currentPlugin <- eve.ReloadTestPlugin();
        assert(currentPlugin.version() == 2);
        assert(currentPlugin.count() == 0);
        assert(currentPlugin.increment() == 1);
        print("plugin reload: checked new generation\n");
        assert(oldPlugin.version() == 1 && oldPlugin.count() == 1);
        assert(oldPlugin.self().version() == 1 && oldPlugin.self().count() == 1);
        assert(currentPlugin.self().version() == 2);
        print("plugin reload: checked generation self pointers\n");
        local rejected = false;
        try { currentPlugin.increment.call(oldPlugin); } catch (error) { rejected = true; }
        assert(rejected);
        print("plugin reload: checked cross-generation call\n");
        assert(cachedMethod.call(oldPlugin) == 1);
        print("plugin reload: checked captured method\n");
        assert(oldClass().version() == 1 && oldClass().count() == 1);
        // This old factory had no instance before reload; it must still create v1.
        assert(removedClass().version() == 1);
        assert(!("RemovedTestPlugin" in eve));
    )");
    if (eve::ModuleManager::find("RemovedTestPlugin") != nullptr)
        throw std::runtime_error("removed plugin module remains active");
}

inline void failedReplacement(ssq::VM& machine) {
    Probe probe(machine);
    probe.load();
    probe.rejectNestedReload();
    for (const auto* variant : {"fail_init", "throw_init", "fail_bind", "conflict", "no_entry"}) {
        probe.replace(variant);
        probe.run(R"(
            local result = plugins.reload(pluginPath);
            if (result.ok) throw "bad reload candidate accepted: " + pluginVariant;
            assert(result.diagnostics.len() > 0);
            assert(plugins.isLoaded(pluginPath));
            assert(eve.ReloadTestPlugin == oldClass);
            assert(eve.ReloadTestPlugin().version() == 1);
            assert(oldPlugin.count() == 1);
            assert(oldPlugin.self().count() == 1);
            assert(!("CandidateOnly" in eve));
            assert(typeof eve.Plugins == "class");
        )");
    }
    probe.corrupt();
    probe.run(R"(
        assert(!plugins.reload(pluginPath).ok);
        assert(eve.ReloadTestPlugin == oldClass);
        assert(oldPlugin.count() == 1);
    )");
    probe.replace("v2");
    probe.run(R"(
        assert(plugins.reload(pluginPath).ok);
        assert(eve.ReloadTestPlugin().version() == 2);
        assert(oldPlugin.version() == 1);
    )");
}

inline void failedInitialLoad(ssq::VM& machine) {
    Probe probe(machine);
    probe.run("plugins <- eve.Plugins();");
    for (const auto* variant : {"fail_init", "throw_init", "fail_bind", "conflict", "no_entry"}) {
        probe.replace(variant);
        probe.run(R"(
            local result = plugins.loadPlugin(pluginPath);
            if (result.ok) throw "bad initial candidate accepted: " + pluginVariant;
            assert(!plugins.isLoaded(pluginPath));
            assert(!("ReloadTestPlugin" in eve));
            assert(!("CandidateOnly" in eve));
        )");
        if (eve::ModuleManager::find("ReloadTestPlugin") != nullptr)
            throw std::runtime_error("failed initial load leaked module registration");
    }
    probe.replace("v1");
    probe.run(R"(
        assert(plugins.loadPlugin(pluginPath).ok);
        assert(eve.ReloadTestPlugin().version() == 1);
    )");
}

inline void repeatedReplacement(ssq::VM& machine) {
    Probe probe(machine);
    probe.load();
    for (int generation = 0; generation < 8; ++generation) {
        const bool v2 = generation % 2 == 0;
        probe.replace(v2 ? "v2" : "v1");
        machine.set("expectedVersion", v2 ? 2 : 1);
        probe.run(R"(
            local result = plugins.reload(pluginPath);
            assert(result.ok);
            local current = eve.ReloadTestPlugin();
            assert(current.version() == expectedVersion);
            assert(current.count() == 0);
            assert(oldPlugin.version() == 1 && oldPlugin.count() == 1);
            assert(oldClass().count() == 1);
        )");
    }
}

}  // namespace plugin_reload_checks
