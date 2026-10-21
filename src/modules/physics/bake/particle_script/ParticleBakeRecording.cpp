#include "physics/bake/particle_script/ParticleBakeRecording.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include "common/BorrowedRef.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "particles/ParticleRecording.h"
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/particle_capture/ParticleBakeCapture.h"
#include "physics/bake/script/PhysicsBake.h"
namespace eve::physics {
namespace {
struct JournalData {
    float                                       duration = 0;
    std::vector<particles::ParticleRecordEvent> events;
};
struct ScriptJournal {
    std::shared_ptr<const JournalData> data;
};
struct ArchiveSource {
    PersistentId                       id;
    std::shared_ptr<const JournalData> data;
};
struct ScriptArchive {
    BakeRecipe                 recipe;
    std::vector<ArchiveSource> sources;
    explicit ScriptArchive(BakeRecipe value) : recipe(std::move(value)) {}
};
Diagnostic invalid(const char* text) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, text, "particleBakeRecording.script");
}
template <class T>
ResultRef<T> argument(const ssq::Object& object) {
    if (object.getType() == ssq::Type::INSTANCE && object.getTypeTag() == script::detail::squirrelTypeHash<T*>()) {
        try {
            if (auto* value = object.to<T*>()) return ResultRef<T>::success(std::ref(*value));
        } catch (const ssq::TypeException&) { /* Invalid wrapper is an ordinary admission failure. */
        }
    }
    return ResultRef<T>::failure(invalid("null or wrong native argument type"));
}
template <class T>
ssq::Table own(HSQUIRRELVM vm, std::unique_ptr<T> value) {
    auto object = script::makeOwnedSquirrelInstance<T>(vm, std::move(value));
    if (!object) return script::projectStatusResult(vm, object.status());
    return script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
}
Result<void> addJournal(ScriptArchive& archive, const ssq::Object& object, const std::string& identity) {
    auto journal = argument<ScriptJournal>(object);
    if (!journal) return Result<void>::failure(journal.status());
    const auto id = PersistentId::parse(identity);
    if (!id || id->isNil() || archive.sources.size() >= 65536)
        return Result<void>::failure(invalid("invalid session identity or archive session budget exceeded"));
    const auto& data  = journal.value().get().data;
    std::size_t total = data->events.size();
    for (const auto& source : archive.sources) {
        if (source.id == *id || source.data == data)
            return Result<void>::failure(invalid("duplicate session identity or journal"));
        if (source.data->events.size() > 2000000 - total)
            return Result<void>::failure(invalid("combined particle event budget exceeded"));
        total += source.data->events.size();
    }
    try {
        archive.sources.push_back({*id, data});
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "archive binding allocation failed",
                                                       "particleBakeRecording.script"));
    }
    return Result<void>::success();
}
Result<void> saveArchive(const ScriptArchive& archive, const std::string& path) {
    if (archive.sources.empty())
        return Result<void>::failure(invalid("archive requires at least one completed journal"));
    try {
        std::vector<ParticleRecordingSource> sources;
        sources.reserve(archive.sources.size());
        float duration = 0;
        for (const auto& source : archive.sources) {
            sources.push_back({source.id, source.data->events});
            duration = std::max(duration, source.data->duration);
        }
        auto track = buildParticleTrack(duration, sources, archive.recipe);
        if (!track) return Result<void>::failure(track.status());
        return track.value().save(path);
    } catch (const std::bad_alloc&) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, "archive conversion allocation failed",
                                                       "particleBakeRecording.script"));
    }
}
}  // namespace
Module_IMPL(ParticleBakeRecording, new ParticleBakeRecording());
void ParticleBakeRecording::expose(ssq::Table& table) {
    const auto vm      = table.getHandle();
    auto       journal = table.addClass<ScriptJournal>(
        "RecordedParticleJournal", std::function<ScriptJournal*()>([]() -> ScriptJournal* { return nullptr; }), true);
    journal.addFunc("duration", [](ScriptJournal* self) { return self->data->duration; });
    journal.addFunc("eventCount", [](ScriptJournal* self) { return static_cast<int>(self->data->events.size()); });
    auto archive = table.addClass<ScriptArchive>(
        "ParticleBakeArchive", std::function<ScriptArchive*()>([]() -> ScriptArchive* { return nullptr; }), true);
    archive.addFunc("addJournal", [vm](ScriptArchive* self, const ssq::Object& value, const std::string& identity) {
        return script::projectResult(vm, addJournal(*self, value, identity));
    });
    archive.addFunc("sourceCount", [](ScriptArchive* self) { return static_cast<int>(self->sources.size()); });
    archive.addFunc("save", [vm](ScriptArchive* self, const std::string& path) {
        return script::projectResult(vm, saveArchive(*self, path));
    });
    auto cls = table.addClass(name, ParticleBakeRecording::create, false);
    expose(cls);
}
void ParticleBakeRecording::expose(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("begin", [vm](ParticleBakeRecording*, const ssq::Object& object, int maximumEvents) {
        if (maximumEvents <= 0)
            return script::projectStatusResult(vm, Status::failure(invalid("event budget must be positive")));
        auto emitter = argument<particles::ParticleEmitter>(object);
        if (!emitter) return script::projectStatusResult(vm, emitter.status());
        return script::projectResult(
            vm, particles::prepareParticleRecording(emitter.value().get(), static_cast<std::size_t>(maximumEvents)));
    });
    cls.addFunc("finish", [vm](ParticleBakeRecording*, const ssq::Object& object) {
        auto emitter = argument<particles::ParticleEmitter>(object);
        if (!emitter) return script::projectStatusResult(vm, emitter.status());
        const double seconds = emitter.value().get().sim()->recordingSeconds;
        if (!std::isfinite(seconds) || seconds < 0 || seconds > std::numeric_limits<float>::max())
            return script::projectStatusResult(vm, Status::failure(invalid("recording duration is not representable")));
        try {
            // Prepare owning storage before transferring the emitter's journal.
            auto data      = std::make_shared<JournalData>();
            auto wrapper   = std::make_unique<ScriptJournal>();
            data->duration = static_cast<float>(seconds);
            if (double(data->duration) < seconds)
                data->duration = std::nextafter(data->duration, std::numeric_limits<float>::infinity());
            wrapper->data = data;
            auto object   = script::makeOwnedSquirrelInstance<ScriptJournal>(vm, std::move(wrapper));
            if (!object) return script::projectStatusResult(vm, object.status());
            // Allocate the complete VM result before consuming the emitter log.
            auto result = script::projectStatusResult(vm, Status::success(), std::move(object).takeValue());
            auto events = particles::finishParticleRecording(emitter.value().get());
            if (!events) return script::projectStatusResult(vm, events.status());
            data->events = std::move(events).takeValue();
            return result;
        } catch (const std::bad_alloc&) {
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::Failed, "particle journal allocation failed",
                                                      "particleBakeRecording.script")));
        }
    });
    cls.addFunc("newArchive", [vm](ParticleBakeRecording*, const ssq::Object& object) {
        auto recipe = copyScriptBakeRecipe(object);
        if (!recipe) return script::projectStatusResult(vm, recipe.status());
        auto encoded = recipe.value().encode();
        if (!encoded) return script::projectStatusResult(vm, encoded.status());
        if (recipe.value().coordinates() != BakeCoordinates::PlanarPixels)
            return script::projectStatusResult(
                vm, Status::failure(invalid("particle recording requires pixels2d coordinates")));
        return own(vm, std::make_unique<ScriptArchive>(std::move(recipe).takeValue()));
    });
}
}  // namespace eve::physics
