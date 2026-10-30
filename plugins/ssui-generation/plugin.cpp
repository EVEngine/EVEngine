#include "GenerationClient.h"
#include "common/Module.h"
#include "common/Export.h"
#include <simplesquirrel/simplesquirrel.hpp>
namespace eve::ssuiplugin {
/** @brief Script-facing SSUI generation plugin; keep loaded for the host lifetime. */
class SSUIGeneration : public Module {
public:
    Module_REG(SSUIGeneration);
    /** @brief Sets HTTP server and writable material project. */
    void configure(const std::string& server,const std::string& project) { client_.configure(server,project); }
    /** @brief Submits recipe and JSON inputs/context asynchronously. */
    bool start(const std::string& recipe,const std::string& inputs,const std::string& context) { return client_.start(recipe,inputs,context); }
    /** @brief Cancels the active job. */
    void cancel() { client_.cancel(); }
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
    std::string apply() { return client_.apply(); }
    /** @brief Restores the previous material binding. */
    std::string undo() { return client_.undo(); }
private:
    ssui::GenerationClient client_;
};
// Static ownership joins network work during library/process teardown.
static SSUIGeneration instance;
Module_IMPL(SSUIGeneration, &instance);
void SSUIGeneration::expose(ssq::Table& table) { auto cls=table.addClass(name,SSUIGeneration::create,false); expose(cls); }
void SSUIGeneration::expose(ssq::Class& cls) {
    cls.addFunc("configure",&SSUIGeneration::configure); cls.addFunc("start",&SSUIGeneration::start);
    cls.addFunc("cancel",&SSUIGeneration::cancel); cls.addFunc("shutdown",&SSUIGeneration::shutdown);
    cls.addFunc("busy",&SSUIGeneration::busy); cls.addFunc("status",&SSUIGeneration::status);
    cls.addFunc("progress",&SSUIGeneration::progress); cls.addFunc("candidate",&SSUIGeneration::candidate);
    cls.addFunc("applied",&SSUIGeneration::applied); cls.addFunc("apply",&SSUIGeneration::apply); cls.addFunc("undo",&SSUIGeneration::undo);
}
}
EVE_PLUGIN_EXPORT int eve_plugin_init(void) { return 0; }
