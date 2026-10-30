#include <simplesquirrel/simplesquirrel.hpp>
#include "GenerationClient.h"
#include "common/Export.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
namespace eve::ssuiplugin {
/** @brief Script-owned client; GC/VM teardown joins its worker before releasing data.
 * @ownership Each script instance owns one client; old instances survive code reload.
 * @thread Main/script thread; worker never calls back into the VM.
 */
class SSUIGeneration {
public:
    SSQ_REG
    /** @brief Binding ABI allocator; transfers ownership to the VM release hook.
     * @ownership Owned by the created script instance; never null on success.
     * @lifetime Until GC/VM destruction; destruction joins the worker.
     * @thread Main/script thread only; no callbacks or reentrancy.
     */
    [[nodiscard]] static SSUIGeneration* allocate() { return new SSUIGeneration(); }
    /** @brief SDK type-generation discriminator; keeps old closures tied to old layouts. */
    static auto nativeModuleFactory() { return &allocate; }
    /** @brief Sets HTTP server and writable material project. */
    [[nodiscard]] Result<void> configure(const std::string& server, const std::string& project) {
        return client_.configure(server, project);
    }
    /** @brief Submits recipe and JSON inputs/context asynchronously. */
    [[nodiscard]] Result<std::string> start(const std::string& recipe, const std::string& inputs,
                                            const std::string& context) {
        return client_.start(recipe, inputs, context);
    }
    /** @brief Cancels the active job. */
    void requestCancel() { client_.requestCancel(); }
    /** @brief Stops background work before host shutdown. */
    void shutdown() { client_.shutdown(); }
    /** @brief Returns whether background work is active. */
    bool busy() const { return client_.busy(); }
    /** @brief Returns current status or error text. */
    std::string status() const { return client_.status(); }
    /** @brief Returns progress in percent. */
    int progress() const { return client_.progress(); }
    /** @brief Returns the candidate PNG path. */
    std::string candidate() const { return client_.candidate(); }
    /** @brief Returns the committed PNG path. */
    std::string applied() const { return client_.applied(); }
    /** @brief Commits the candidate to the material document. */
    [[nodiscard]] Result<std::string> apply() { return client_.apply(); }
    /** @brief Restores the previous material binding. */
    [[nodiscard]] Result<std::string> undo() { return client_.undo(); }
    /** @brief Returns the checked asynchronous outcome; does not commit it. */
    [[nodiscard]] Result<std::string> completion() const { return client_.completion(); }

private:
    ssui::GenerationClient client_;
};
/** @brief Registers the script-owned service; this bootstrap module holds no worker. */
class SSUIGenerationModule : public Module {
public:
    Module_REG(SSUIGenerationModule);
};
Module_IMPL(SSUIGenerationModule, new SSUIGenerationModule());
void SSUIGenerationModule::expose(ssq::Table& table) { SSUIGeneration::expose(table); }
void SSUIGenerationModule::expose(ssq::Class&) {}
void SSUIGeneration::expose(ssq::Table& table) {
    // SimpleSquirrel's owning constructor installs the native destructor hook.
    auto cls = table.addClass("SSUIGeneration", &SSUIGeneration::allocate, true);
    expose(cls);
}
void SSUIGeneration::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("configure", [vm](SSUIGeneration* self, const std::string& server, const std::string& project) {
        return script::projectResult(vm, self->configure(server, project));
    });
    cls.addFunc("start", [vm](SSUIGeneration* self, const std::string& recipe, const std::string& inputs,
                              const std::string& context) {
        return script::projectResult(vm, self->start(recipe, inputs, context),
                                     [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("completion", [vm](SSUIGeneration* self) {
        return script::projectResult(vm, self->completion(), [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("apply", [vm](SSUIGeneration* self) {
        return script::projectResult(vm, self->apply(), [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("undo", [vm](SSUIGeneration* self) {
        return script::projectResult(vm, self->undo(), [](std::string value) { return Value(std::move(value)); });
    });
    cls.addFunc("requestCancel", &SSUIGeneration::requestCancel);
    cls.addFunc("shutdown", &SSUIGeneration::shutdown);
    cls.addFunc("busy", &SSUIGeneration::busy);
    cls.addFunc("status", &SSUIGeneration::status);
    cls.addFunc("progress", &SSUIGeneration::progress);
    cls.addFunc("candidate", &SSUIGeneration::candidate);
    cls.addFunc("applied", &SSUIGeneration::applied);
}
}  // namespace eve::ssuiplugin
EVE_PLUGIN_EXPORT int eve_plugin_init(void) { return 0; }
