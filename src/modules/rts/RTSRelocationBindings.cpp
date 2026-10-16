#include "common/SquirrelBinding.h"
#include "rts/RTS.h"
#include "rts/RTSScriptInternal.h"

namespace eve::rts::script_internal {

void exposeRelocationBindings(ssq::Class& cls) {
    cls.addFunc("relocateUnits", [vm = cls.getHandle()](RTS* self, ssq::Array units, int maxChecks) -> ssq::Table {
        UnitRelocationBatch batch;
        batch.maxChecks = maxChecks;
        try {
            if (units.size() > 1024) throw ssq::TypeException("Relocation batches support at most 1024 units");
            batch.units.reserve(units.size());
            for (std::size_t index = 0; index < units.size(); ++index) {
                auto entry   = units.get<ssq::Table>(index);
                auto subject = parseScriptSubject(entry.get<std::string>("subject"), "relocateUnits.subject");
                if (!subject) return script::projectStatusResult(vm, subject.status());
                batch.units.push_back({subject.value(), {entry.get<float>("x"), entry.get<float>("y")}});
            }
        } catch (const std::exception& error) {
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error.what(), "relocateUnits")));
        }
        return script::projectResult(vm, self->relocateUnits(batch),
                                     [](std::size_t count) { return Value(static_cast<std::int64_t>(count)); });
    });
}

}  // namespace eve::rts::script_internal
