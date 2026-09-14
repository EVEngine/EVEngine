#include "fluids/VolumeFluidBindingInternal.inc"

namespace eve::fluids {
void exposeVolumeFluidType(ssq::Table& table) {
    const auto vm             = table.getHandle();
    auto       contactTracker = table.addClass<VolumeFluidContactTracker>(
        "VolumeFluidContactTracker",
        std::function<VolumeFluidContactTracker*()>([] { return new VolumeFluidContactTracker(); }), true);
    contactTracker.addFunc("advance", [vm](VolumeFluidContactTracker* self, VolumeFluid* fluid, float threshold) {
        if (!fluid)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing contact source",
                                                  "fluids.volume.contacts")),
                false, false);
        auto events = self->advance(fluid->contacts(), threshold);
        if (!events) return script::projectStatusResult(vm, events.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidContactEvents(events.value()));
    });
    contactTracker.addFunc("reset", [](VolumeFluidContactTracker* self) { self->reset(); });
    auto foam = table.addClass<VolumeFluidFoam>(
        "VolumeFluidFoam", std::function<VolumeFluidFoam*()>([] { return new VolumeFluidFoam(); }), true);
    foam.addFunc("snapshot", [vm](VolumeFluidFoam* self) {
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidFoam(self->snapshot()));
    });
    foam.addFunc("restore", [vm](VolumeFluidFoam* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidFoam(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->restore(decoded.value()));
    });
    foam.addFunc("advance", [vm](VolumeFluidFoam* self, VolumeFluid* fluid, VolumeFluidDiffuse* pool, float dt) {
        if (!fluid || !pool)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing foam source or pool",
                                                  "fluids.volume.foam")),
                false, false);
        auto emitted = self->advance(*fluid, *pool, dt);
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    foam.addFunc("advanceFromActor", [vm](VolumeFluidFoam* self, VolumeFluid* fluid, VolumeFluidDiffuse* pool, float dt,
                                          int actorGroup) {
        if (!fluid || !pool || actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    actorGroup < 0 ? "Actor group must be nonnegative" : "Missing foam source or pool",
                    "fluids.volume.foam")),
                false, false);
        auto emitted = self->advanceFromActor(*fluid, *pool, dt, unsigned(actorGroup));
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    foam.addFunc("advanceFromActorInterpolated", [vm](VolumeFluidFoam* self, VolumeFluid* fluid,
                                                      VolumeFluidDiffuse* pool, float dt, int actorGroup, float alpha) {
        if (!fluid || !pool || actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument,
                    actorGroup < 0 ? "Actor group must be nonnegative" : "Missing foam source or pool",
                    "fluids.volume.foam")),
                false, false);
        auto emitted = self->advanceFromActorInterpolated(*fluid, *pool, dt, unsigned(actorGroup), alpha);
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    auto diffuse = table.addClass<VolumeFluidDiffuse>(
        "VolumeFluidDiffuse", std::function<VolumeFluidDiffuse*()>([] { return new VolumeFluidDiffuse(); }), true);
    diffuse.addFunc("snapshot", [vm](VolumeFluidDiffuse* self) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidDiffuse(self->snapshot()));
    });
    diffuse.addFunc("restore", [vm](VolumeFluidDiffuse* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidDiffuse(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->restore(decoded.value()));
    });
    diffuse.addFunc("emit", [vm](VolumeFluidDiffuse* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidDiffuseParticles(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->emit(decoded.value()));
    });
    diffuse.addFunc("advance", [vm](VolumeFluidDiffuse* self, VolumeFluid* fluid, float dt, int minimumNeighbors) {
        if (!fluid)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing fluid field source",
                                                  "fluids.volume.diffuse")),
                false, false);
        return script::projectResult(vm, self->advance(*fluid, dt, unsigned(minimumNeighbors)));
    });
    diffuse.addFunc("getParticleCount", [](VolumeFluidDiffuse* self) { return int(self->particleCount()); });
    diffuse.addFunc("getAvailableCapacity", [](VolumeFluidDiffuse* self) { return int(self->availableCapacity()); });
    diffuse.addFunc("clear", [](VolumeFluidDiffuse* self) { self->clear(); });
    auto coupling = table.addClass<VolumeFluidCoupling>(
        "VolumeFluidCoupling", std::function<VolumeFluidCoupling*()>([] { return new VolumeFluidCoupling(); }), true);
    coupling.addFunc("attach", [vm](VolumeFluidCoupling* self, physics::Body3D* body, ssq::Object object) {
        if (!body)
            return script::projectStatusResult(
                vm,
                Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing rigid body", "fluids.volume.coupling")),
                false, false);
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        Value::Array batch;
        batch.push_back(std::move(value).takeValue());
        auto decoded = decodeVolumeFluidColliders(Value(std::move(batch)));
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->attach(*body, decoded.value()[0]));
    });
    coupling.addFunc("detach", [](VolumeFluidCoupling* self, int label) { self->detach(unsigned(label)); });
    coupling.addFunc("setImpulseLimits", [vm](VolumeFluidCoupling* self, float linear, float angular) {
        return script::projectResult(vm, self->setImpulseLimits(linear, angular));
    });
    coupling.addFunc("lastClampedBodyCount", &VolumeFluidCoupling::lastClampedBodyCount);
    coupling.addFunc(
        "step", [vm](VolumeFluidCoupling* self, physics::World3D* world, VolumeFluid* fluid, float dt, int substeps) {
            if (!world || !fluid)
                return script::projectStatusResult(
                    vm,
                    Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing rigid world or fluid",
                                                      "fluids.volume.coupling")),
                    false, false);
            auto result = self->step(*world, *fluid, dt, unsigned(substeps));
            if (!result) return script::projectStatusResult(vm, result.status(), false, false);
            return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(result.value())));
        });
    coupling.addFunc("stepWithThermalContacts", [vm](VolumeFluidCoupling* self, physics::World3D* world,
                                                     VolumeFluid* fluid, float dt, int substeps, ssq::Object object) {
        const auto failure = [&](const Status& status) {
            return script::projectStatusResult(vm, status, false, false);
        };
        if (!world || !fluid)
            return failure(Status::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Missing rigid world or fluid", "fluids.volume.coupling")));
        auto value = script::valueFromSquirrel(object);
        if (!value) return failure(value.status());
        auto rules = decodeVolumeFluidThermalRules(value.value());
        if (!rules) return failure(rules.status());
        auto result = self->step(*world, *fluid, dt, unsigned(substeps), rules.value());
        if (!result) return failure(result.status());
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(result.value())));
    });
    auto jet = table.addClass<VolumeFluidJetEmitter>(
        "VolumeFluidJetEmitter", std::function<VolumeFluidJetEmitter*()>([] { return new VolumeFluidJetEmitter(); }),
        true);
    exposeEmitterState<VolumeFluidJetEmitter>(jet, vm);
    jet.addFunc(
        "advanceMoving", [vm](VolumeFluidJetEmitter* self, VolumeFluid* solver, ssq::Object object, ssq::Object begin,
                              ssq::Object end, float dt, int limit, float threshold, float inheritVelocity) {
            const auto failure = [&](const Status& status) {
                return script::projectStatusResult(vm, status, false, false);
            };
            if (!solver)
                return failure(Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver", "fluids.volume.jet")));
            auto value = script::valueFromSquirrel(object);
            if (!value) return failure(value.status());
            auto first = script::valueFromSquirrel(begin);
            if (!first) return failure(first.status());
            auto last = script::valueFromSquirrel(end);
            if (!last) return failure(last.status());
            auto emission = decodeVolumeFluidEmission(value.value());
            if (!emission) return failure(emission.status());
            auto firstPose = decodeVolumeFluidNozzlePose(first.value());
            if (!firstPose) return failure(firstPose.status());
            auto lastPose = decodeVolumeFluidNozzlePose(last.value());
            if (!lastPose) return failure(lastPose.status());
            auto emitted = self->advanceMoving(*solver, emission.value(), firstPose.value(), lastPose.value(), dt,
                                               unsigned(limit), threshold, inheritVelocity);
            if (!emitted) return failure(emitted.status());
            return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
        });
    jet.addFunc("advance", [vm](VolumeFluidJetEmitter* self, VolumeFluid* solver, ssq::Object object, float dt,
                                int limit, float threshold) {
        if (!solver)
            return script::projectStatusResult(
                vm,
                Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver", "fluids.volume.jet")),
                false, false);
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidEmission(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        auto emitted = self->advance(*solver, decoded.value(), dt, unsigned(limit), threshold);
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    auto emitter = table.addClass<VolumeFluidEmitter>(
        "VolumeFluidEmitter", std::function<VolumeFluidEmitter*()>([] { return new VolumeFluidEmitter(); }), true);
    exposeEmitterState<VolumeFluidEmitter>(emitter, vm);
    emitter.addFunc("emitParticle",
                    [vm](VolumeFluidEmitter* self, VolumeFluid* solver, ssq::Object object, float offset, float dt) {
                        if (!solver)
                            return script::projectStatusResult(
                                vm,
                                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                  "Missing volume solver", "fluids.volume.emitter")),
                                false, false);
                        auto value = script::valueFromSquirrel(object);
                        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
                        auto decoded = decodeVolumeFluidEmission(value.value());
                        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
                        return script::projectResult(vm, self->emitParticle(*solver, decoded.value(), offset, dt),
                                                     [](unsigned count) { return Value(int64_t(count)); });
                    });
    emitter.addFunc("advance", [vm](VolumeFluidEmitter* self, VolumeFluid* solver, ssq::Object object, float dt,
                                    float rate, int limit, float threshold) {
        if (!solver)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing volume solver",
                                                  "fluids.volume.emitter")),
                false, false);
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidEmission(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        auto emitted = self->advance(*solver, decoded.value(), dt, rate, unsigned(limit), threshold);
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    emitter.addFunc("advanceBurst", [vm](VolumeFluidEmitter* self, VolumeFluid* solver, ssq::Object object, int count,
                                         float threshold) {
        if (!solver || count <= 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  solver ? "Burst count must be positive" : "Missing volume solver",
                                                  "fluids.volume.emitter")),
                false, false);
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidEmission(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        auto emitted = self->advanceBurst(*solver, decoded.value(), unsigned(count), threshold);
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    auto cls = table.addClass<VolumeFluid>(
        "VolumeFluid", std::function<VolumeFluid*()>([]() -> VolumeFluid* { return nullptr; }), true);
    cls.addFunc("step", [vm](VolumeFluid* self, float dt, int substeps) {
        return script::projectResult(vm, self->step(dt, unsigned(substeps)));
    });
    cls.addFunc("setGravity", [vm](VolumeFluid* self, float x, float y, float z) {
        return script::projectResult(vm, self->setGravity({x, y, z}));
    });
    cls.addFunc("setParticleWinds", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto winds = decodeVolumeFluidFieldPositions(value.value());
        if (!winds) return script::projectStatusResult(vm, winds.status(), false, false);
        return script::projectResult(vm, self->setParticleWinds(winds.value()));
    });
    cls.addFunc("setParticleExternalForces", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto forces = decodeVolumeFluidFieldPositions(value.value());
        if (!forces) return script::projectStatusResult(vm, forces.status(), false, false);
        return script::projectResult(vm, self->setParticleExternalForces(forces.value()));
    });
    cls.addFunc("accumulateWindZones", [vm](VolumeFluid* self, ssq::Object object, float fixedTimeSeconds) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto zones = decodeVolumeFluidWindZones(value.value());
        if (!zones) return script::projectStatusResult(vm, zones.status(), false, false);
        return script::projectResult(vm, self->accumulateWindZones(zones.value(), fixedTimeSeconds));
    });
    cls.addFunc("accumulateExternalForceZones", [vm](VolumeFluid* self, ssq::Object object, float fixedTimeSeconds) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto zones = decodeVolumeFluidWindZones(value.value());
        if (!zones) return script::projectStatusResult(vm, zones.status(), false, false);
        return script::projectResult(vm, self->accumulateExternalForceZones(zones.value(), fixedTimeSeconds));
    });
    cls.addFunc("stepWithThermalContacts", [vm](VolumeFluid* self, float dt, int substeps, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto rules = decodeVolumeFluidThermalRules(value.value());
        if (!rules) return script::projectStatusResult(vm, rules.status(), false, false);
        return script::projectResult(vm, self->stepWithThermalContacts(dt, unsigned(substeps), rules.value()));
    });
    cls.addFunc("snapshot", [vm](VolumeFluid* self) {
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluid(self->snapshot()));
    });
    cls.addFunc("restore", [vm](VolumeFluid* self, ssq::Object object) {
        auto decoded = read(object);
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->restore(decoded.value()));
    });
    cls.addFunc("emit", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidParticles(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->emit(decoded.value()));
    });
    cls.addFunc("configureParticleEvents", [vm](VolumeFluid* self, int capacity) {
        if (capacity < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Particle event capacity must be nonnegative",
                                                  "fluids.volume.particleEvents")),
                false, false);
        return script::projectResult(vm, self->configureParticleEvents(unsigned(capacity)));
    });
    cls.addFunc("drainParticleEvents", [vm](VolumeFluid* self) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidParticleEvents(self->drainParticleEvents()));
    });
    cls.addFunc("clear", [](VolumeFluid* self) { self->clear(); });
    cls.addFunc("killParticle", [vm](VolumeFluid* self, int particleIndex) {
        if (particleIndex < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Particle index must be nonnegative",
                                                  "fluids.volume.killParticle")),
                false, false);
        return script::projectResult(vm, self->killParticle(unsigned(particleIndex)));
    });
    cls.addFunc("bindStaticParticles",
                [vm](VolumeFluid* self, ssq::Object object, int colliderLabel, bool constrainOrientation) {
                    if (colliderLabel < 0)
                        return script::projectStatusResult(
                            vm,
                            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "Attachment collider label must be nonnegative",
                                                              "fluids.volume.attachment")),
                            false, false);
                    auto indices = readParticleIndices(object);
                    if (!indices) return script::projectStatusResult(vm, indices.status(), false, false);
                    return script::projectResult(
                        vm, self->bindStaticParticles(indices.value(), unsigned(colliderLabel), constrainOrientation),
                        [](unsigned count) { return Value(int64_t(count)); });
                });
    cls.addFunc("bindDynamicParticles", [vm](VolumeFluid* self, ssq::Object object, int colliderLabel, float compliance,
                                             float breakThreshold, bool constrainOrientation) {
        if (colliderLabel < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Attachment collider label must be nonnegative",
                                                  "fluids.volume.attachment")),
                false, false);
        auto indices = readParticleIndices(object);
        if (!indices) return script::projectStatusResult(vm, indices.status(), false, false);
        return script::projectResult(vm,
                                     self->bindDynamicParticles(indices.value(), unsigned(colliderLabel), compliance,
                                                                breakThreshold, constrainOrientation),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("unbindStaticParticles", [vm](VolumeFluid* self, ssq::Object object) {
        auto indices = readParticleIndices(object);
        if (!indices) return script::projectStatusResult(vm, indices.status(), false, false);
        return script::projectResult(vm, self->unbindStaticParticles(indices.value()),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("setStitches", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto stitches = decodeVolumeFluidStitches(value.value());
        if (!stitches) return script::projectStatusResult(vm, stitches.status(), false, false);
        return script::projectResult(vm, self->setStitches(stitches.value()));
    });
    cls.addFunc("sampleField", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto positions = decodeVolumeFluidFieldPositions(value.value());
        if (!positions) return script::projectStatusResult(vm, positions.status(), false, false);
        auto samples = self->sampleField(positions.value());
        if (!samples) return script::projectStatusResult(vm, samples.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidFieldSamples(samples.value()));
    });
    cls.addFunc("debugParticleGrid", [vm](VolumeFluid* self, int maxCells) {
        if (maxCells < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Particle-grid cell budget must be nonnegative",
                                                  "fluids.volume.gridDebug")),
                false, false);
        auto cells = self->debugParticleGrid(unsigned(maxCells));
        if (!cells) return script::projectStatusResult(vm, cells.status(), false, false);
        Value::Array encoded;
        encoded.reserve(cells.value().size());
        for (const auto& cell : cells.value())
            encoded.push_back(Value::object({{"center", Value::array({cell.center.x, cell.center.y, cell.center.z})},
                                             {"size", Value::array({cell.size.x, cell.size.y, cell.size.z})},
                                             {"particleCount", int64_t(cell.particleCount)}}));
        return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
    });
    cls.addFunc("debugParticleFrames", [vm](VolumeFluid* self, int actorGroup, float size, int maxParticles) {
        if (actorGroup < 0 || maxParticles < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Particle-frame arguments must be nonnegative",
                                                  "fluids.volume.frameDebug")),
                false, false);
        auto frames = self->debugParticleFrames(unsigned(actorGroup), size, unsigned(maxParticles));
        if (!frames) return script::projectStatusResult(vm, frames.status(), false, false);
        Value::Array encoded;
        encoded.reserve(frames.value().size());
        for (const auto& frame : frames.value())
            encoded.push_back(Value::object({{"particleIndex", int64_t(frame.particleIndex)},
                                             {"origin", Value::array({frame.origin.x, frame.origin.y, frame.origin.z})},
                                             {"x", Value::array({frame.x.x, frame.x.y, frame.x.z})},
                                             {"y", Value::array({frame.y.x, frame.y.y, frame.y.z})},
                                             {"z", Value::array({frame.z.x, frame.z.y, frame.z.z})}}));
        return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
    });
    cls.addFunc("particleInstances", [vm](VolumeFluid* self, int actorGroup, float sx, float sy, float sz, float alpha,
                                          int maxInstances) {
        if (actorGroup < 0 || maxInstances < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Particle-instance group and budget must be nonnegative",
                                                  "fluids.volume.particleInstances")),
                false, false);
        auto copied = self->particleInstances(unsigned(actorGroup), {sx, sy, sz}, alpha, unsigned(maxInstances));
        if (!copied) return script::projectStatusResult(vm, copied.status(), false, false);
        Value::Array encoded;
        encoded.reserve(copied.value().size());
        for (const auto& instance : copied.value())
            encoded.push_back(Value::object(
                {{"particleIndex", int64_t(instance.particleIndex)},
                 {"position", Value::array({instance.position.x, instance.position.y, instance.position.z})},
                 {"orientation", Value::array({instance.orientation.x, instance.orientation.y, instance.orientation.z,
                                               instance.orientation.w})},
                 {"scale", Value::array({instance.scale.x, instance.scale.y, instance.scale.z})},
                 {"color", Value::array({instance.color.x, instance.color.y, instance.color.z, instance.color.w})}}));
        return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
    });
    cls.addFunc("particleImpostors", [vm](VolumeFluid* self, int actorGroup, float radiusScale, float r, float g,
                                          float b, float a, float alpha, int maxInstances) {
        if (actorGroup < 0 || maxInstances < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Particle-impostor group and budget must be nonnegative",
                                                  "fluids.volume.particleImpostors")),
                false, false);
        auto copied =
            self->particleImpostors(unsigned(actorGroup), radiusScale, {r, g, b, a}, alpha, unsigned(maxInstances));
        if (!copied) return script::projectStatusResult(vm, copied.status(), false, false);
        Value::Array encoded;
        encoded.reserve(copied.value().size());
        for (const auto& instance : copied.value())
            encoded.push_back(Value::object(
                {{"particleIndex", int64_t(instance.particleIndex)},
                 {"position", Value::array({instance.position.x, instance.position.y, instance.position.z})},
                 {"orientation", Value::array({instance.orientation.x, instance.orientation.y, instance.orientation.z,
                                               instance.orientation.w})},
                 {"scale", Value::array({instance.scale.x, instance.scale.y, instance.scale.z})},
                 {"color", Value::array({instance.color.x, instance.color.y, instance.color.z, instance.color.w})}}));
        return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
    });
    cls.addFunc("debugSdfSlice",
                [vm](VolumeFluid* self, int colliderLabel, int axis, float slice, float maxDistance, int maxSamples) {
                    if (colliderLabel < 0 || maxSamples < 0)
                        return script::projectStatusResult(
                            vm,
                            Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                              "SDF slice label and budget must be nonnegative",
                                                              "fluids.volume.sdfSliceDebug")),
                            false, false);
                    auto sampled = self->debugSdfSlice(unsigned(colliderLabel), VolumeFluidSdfSliceAxis(axis), slice,
                                                       maxDistance, unsigned(maxSamples));
                    if (!sampled) return script::projectStatusResult(vm, sampled.status(), false, false);
                    const auto&  result = sampled.value();
                    Value::Array values;
                    values.reserve(result.values.size());
                    for (float value : result.values) values.emplace_back(value);
                    return script::projectStatusResult(
                        vm, Status::success(), true, true,
                        Value::object({{"width", int64_t(result.width)},
                                       {"height", int64_t(result.height)},
                                       {"origin", Value::array({result.origin.x, result.origin.y, result.origin.z})},
                                       {"stepX", Value::array({result.stepX.x, result.stepX.y, result.stepX.z})},
                                       {"stepY", Value::array({result.stepY.x, result.stepY.y, result.stepY.z})},
                                       {"values", Value(std::move(values))}}));
                });
    cls.addFunc("applySolidColor", [vm](VolumeFluid* self, float r, float g, float b, float a) {
        return script::projectResult(vm, self->applySolidColor({r, g, b, a}));
    });
    cls.addFunc("applyVelocityColors", [vm](VolumeFluid* self, float sensibility) {
        return script::projectResult(vm, self->applyVelocityColors(sensibility));
    });
    cls.addFunc("applyActorGroupColors",
                [vm](VolumeFluid* self) { return script::projectResult(vm, self->applyActorGroupColors()); });
    cls.addFunc("applyDataColors", [vm](VolumeFluid* self, int channel, ssq::Object object) {
        if (channel < 0)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                                     "Invalid data color channel",
                                                                                     "fluids.volume.applyDataColors")));
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto gradient = decodeVolumeFluidColorGradient(value.value());
        if (!gradient) return script::projectStatusResult(vm, gradient.status(), false, false);
        return script::projectResult(vm, self->applyDataColors(unsigned(channel), gradient.value()));
    });
    cls.addFunc("applyRandomColors", [vm](VolumeFluid* self, ssq::Object object, int seed) {
        if (seed < 0)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "Invalid random color seed",
                                                 "fluids.volume.applyRandomColors")));
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto gradient = decodeVolumeFluidColorGradient(value.value());
        if (!gradient) return script::projectStatusResult(vm, gradient.status(), false, false);
        return script::projectResult(vm, self->applyRandomColors(gradient.value(), uint32_t(seed)));
    });
    cls.addFunc("addRandomVelocity", [vm](VolumeFluid* self, float intensity, int seed) {
        if (seed < 0)
            return script::projectResult(vm, Result<void>::failure(Diagnostic::error(
                                                 DiagnosticCode::InvalidArgument, "Invalid random velocity seed",
                                                 "fluids.volume.addRandomVelocity")));
        return script::projectResult(vm, self->addRandomVelocity(intensity, uint32_t(seed)));
    });
    cls.addFunc("teleportActor", [vm](VolumeFluid* self, ssq::Object currentObject, ssq::Object targetObject) {
        auto currentValue = script::valueFromSquirrel(currentObject);
        if (!currentValue) return script::projectStatusResult(vm, currentValue.status(), false, false);
        auto targetValue = script::valueFromSquirrel(targetObject);
        if (!targetValue) return script::projectStatusResult(vm, targetValue.status(), false, false);
        auto current = decodeVolumeFluidNozzlePose(currentValue.value());
        if (!current) return script::projectStatusResult(vm, current.status(), false, false);
        auto target = decodeVolumeFluidNozzlePose(targetValue.value());
        if (!target) return script::projectStatusResult(vm, target.status(), false, false);
        return script::projectResult(vm, self->teleportActor(current.value().position, current.value().rotation,
                                                             target.value().position, target.value().rotation));
    });
    cls.addFunc("actorMassProperties", [vm](VolumeFluid* self, int actorGroup) {
        if (actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Actor group must be nonnegative",
                                                  "fluids.volume.actorMass")),
                false, false);
        auto properties = self->actorMassProperties(unsigned(actorGroup));
        if (!properties) return script::projectStatusResult(vm, properties.status(), false, false);
        const auto& p = properties.value();
        return script::projectStatusResult(
            vm, Status::success(), true, true,
            Value::object({{"mass", p.mass},
                           {"centerOfMass", Value::array({p.centerOfMass.x, p.centerOfMass.y, p.centerOfMass.z})},
                           {"particleCount", int64_t(p.particleCount)}}));
    });
    cls.addFunc("setActorFilterCategory", [vm](VolumeFluid* self, int actorGroup, int category) {
        if (actorGroup < 0 || category < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Actor group and category must be nonnegative",
                                                  "fluids.volume.filter")),
                false, false);
        return script::projectResult(vm, self->setActorFilterCategory(unsigned(actorGroup), unsigned(category)));
    });
    cls.addFunc("setActorCollisionFilter", [vm](VolumeFluid* self, int actorGroup, int64_t collisionFilter) {
        if (actorGroup < 0 || collisionFilter < 0 || collisionFilter > int64_t(0xffffffffu))
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Actor group and packed collision filter are out of range",
                                                  "fluids.volume.filter")),
                false, false);
        return script::projectResult(vm,
                                     self->setActorCollisionFilter(unsigned(actorGroup), unsigned(collisionFilter)));
    });
    cls.addFunc("updateActorMaterial", [vm](VolumeFluid* self, int actorGroup, ssq::Object emissionObject) {
        if (actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Actor group must be nonnegative",
                                                  "fluids.volume.actorMaterial")),
                false, false);
        auto value = script::valueFromSquirrel(emissionObject);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto emission = decodeVolumeFluidEmission(value.value());
        if (!emission) return script::projectStatusResult(vm, emission.status(), false, false);
        return script::projectResult(vm, self->updateActorMaterial(unsigned(actorGroup), emission.value().prototype));
    });
    cls.addFunc("setActorSelfCollisions", [vm](VolumeFluid* self, int actorGroup, bool enabled) {
        if (actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Actor group must be nonnegative",
                                                  "fluids.volume.selfCollisions")),
                false, false);
        return script::projectResult(vm, self->setActorSelfCollisions(unsigned(actorGroup), enabled));
    });
    cls.addFunc("killActorParticles", [vm](VolumeFluid* self, int actorGroup) {
        if (actorGroup < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Actor group must be nonnegative",
                                                  "fluids.volume.killActor")),
                false, false);
        return script::projectResult(vm, self->killActorParticles(unsigned(actorGroup)),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("applyParticleDrag", [vm](VolumeFluid* self, int particleIndex, float x, float y, float z,
                                          float stiffness, float damping, float seconds) {
        if (particleIndex < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Particle index must be nonnegative",
                                                  "fluids.volume.drag")),
                false, false);
        return script::projectResult(
            vm, self->applyParticleDrag(unsigned(particleIndex), {x, y, z}, stiffness, damping, seconds));
    });
    cls.addFunc("grabContactParticles", [vm](VolumeFluid* self, int colliderLabel, float x, float y, float z, float qx,
                                             float qy, float qz, float qw, float distanceThreshold) {
        if (colliderLabel < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Collider label must be nonnegative",
                                                  "fluids.volume.contactGrabber")),
                false, false);
        return script::projectResult(
            vm, self->grabContactParticles(unsigned(colliderLabel), {x, y, z}, {qx, qy, qz, qw}, distanceThreshold),
            [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("updateGrabbedParticles", [vm](VolumeFluid* self, int colliderLabel, float x, float y, float z,
                                               float qx, float qy, float qz, float qw) {
        if (colliderLabel < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Collider label must be nonnegative",
                                                  "fluids.volume.contactGrabber")),
                false, false);
        return script::projectResult(vm,
                                     self->updateGrabbedParticles(unsigned(colliderLabel), {x, y, z}, {qx, qy, qz, qw}),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("releaseGrabbedParticles", [vm](VolumeFluid* self, int colliderLabel) {
        if (colliderLabel < 0)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Collider label must be nonnegative",
                                                  "fluids.volume.contactGrabber")),
                false, false);
        return script::projectResult(vm, self->releaseGrabbedParticles(unsigned(colliderLabel)),
                                     [](unsigned count) { return Value(int64_t(count)); });
    });
    cls.addFunc("applyMaterialChannels",
                [vm](VolumeFluid* self) { return script::projectResult(vm, self->applyMaterialChannels()); });
    cls.addFunc("applyMaterialChannelsWithColors", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto keys = decodeVolumeFluidViscosityColors(value.value());
        if (!keys) return script::projectStatusResult(vm, keys.status(), false, false);
        return script::projectResult(vm, self->applyMaterialChannels(keys.value()));
    });
    cls.addFunc("overlapSphere", [vm](VolumeFluid* self, float x, float y, float z, float radius) {
        auto hits = self->overlap({x, y, z}, radius);
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidParticles(hits.value()));
    });
    cls.addFunc("paintSphere", [vm](VolumeFluid* self, float x, float y, float z, float radius, ssq::Object material) {
        auto value = script::valueFromSquirrel(material);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidMaterial(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->paint({x, y, z}, radius, decoded.value()));
    });
    cls.addFunc("overlapBox", [vm](VolumeFluid* self, float x, float y, float z, float hx, float hy, float hz, float qx,
                                   float qy, float qz, float qw) {
        auto hits = self->overlapBox({x, y, z}, {hx, hy, hz}, {qx, qy, qz, qw});
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidParticles(hits.value()));
    });
    cls.addFunc("raycast", [vm](VolumeFluid* self, float ox, float oy, float oz, float dx, float dy, float dz,
                                float maxDistance, int maxHits, int phaseMask) {
        auto hits = self->raycast({ox, oy, oz}, {dx, dy, dz}, maxDistance, unsigned(maxHits), unsigned(phaseMask));
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidRayHits(hits.value()));
    });
    cls.addFunc("querySphere", [vm](VolumeFluid* self, float x, float y, float z, float radius, float contactOffset,
                                    float maxDistance, int maxHits, int phaseMask) {
        auto hits =
            self->querySphere({x, y, z}, radius, contactOffset, maxDistance, unsigned(maxHits), unsigned(phaseMask));
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidDistanceHits(hits.value()));
    });
    cls.addFunc("queryBox",
                [vm](VolumeFluid* self, float x, float y, float z, float hx, float hy, float hz, float qx, float qy,
                     float qz, float qw, float contactOffset, float maxDistance, int maxHits, int phaseMask) {
                    auto hits = self->queryBox({x, y, z}, {hx, hy, hz}, {qx, qy, qz, qw}, contactOffset, maxDistance,
                                               unsigned(maxHits), unsigned(phaseMask));
                    if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
                    return script::projectStatusResult(vm, Status::success(), true, true,
                                                       encodeVolumeFluidDistanceHits(hits.value()));
                });
    cls.addFunc("queryBatch", [vm](VolumeFluid* self, ssq::Object object, int maxHitsPerQuery) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto queries = decodeVolumeFluidQueries(value.value());
        if (!queries) return script::projectStatusResult(vm, queries.status(), false, false);
        auto hits = self->queryBatch(queries.value(), unsigned(maxHitsPerQuery));
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidQueryHits(hits.value()));
    });
    cls.addFunc("applyQueryColors", [vm](VolumeFluid* self, ssq::Object queryObject, ssq::Object colorObject, float r,
                                         float g, float b, float a, int maxHitsPerQuery) {
        auto queryValue = script::valueFromSquirrel(queryObject);
        if (!queryValue) return script::projectStatusResult(vm, queryValue.status(), false, false);
        auto colorValue = script::valueFromSquirrel(colorObject);
        if (!colorValue) return script::projectStatusResult(vm, colorValue.status(), false, false);
        auto queries = decodeVolumeFluidQueries(queryValue.value());
        if (!queries) return script::projectStatusResult(vm, queries.status(), false, false);
        auto colors = decodeVolumeFluidColors(colorValue.value());
        if (!colors) return script::projectStatusResult(vm, colors.status(), false, false);
        auto counts = self->applyQueryColors(queries.value(), colors.value(), {r, g, b, a}, unsigned(maxHitsPerQuery));
        if (!counts) return script::projectStatusResult(vm, counts.status(), false, false);
        Value::Array encoded;
        encoded.reserve(counts.value().size());
        for (unsigned count : counts.value()) encoded.emplace_back(int64_t(count));
        return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
    });
    cls.addFunc("applyQueryColorsPreservingOutside",
                [vm](VolumeFluid* self, ssq::Object queryObject, ssq::Object colorObject, int maxHitsPerQuery) {
                    auto queryValue = script::valueFromSquirrel(queryObject);
                    if (!queryValue) return script::projectStatusResult(vm, queryValue.status(), false, false);
                    auto colorValue = script::valueFromSquirrel(colorObject);
                    if (!colorValue) return script::projectStatusResult(vm, colorValue.status(), false, false);
                    auto queries = decodeVolumeFluidQueries(queryValue.value());
                    if (!queries) return script::projectStatusResult(vm, queries.status(), false, false);
                    auto colors = decodeVolumeFluidColors(colorValue.value());
                    if (!colors) return script::projectStatusResult(vm, colors.status(), false, false);
                    auto counts = self->applyQueryColorsPreservingOutside(queries.value(), colors.value(),
                                                                          unsigned(maxHitsPerQuery));
                    if (!counts) return script::projectStatusResult(vm, counts.status(), false, false);
                    Value::Array encoded;
                    encoded.reserve(counts.value().size());
                    for (unsigned count : counts.value()) encoded.emplace_back(int64_t(count));
                    return script::projectStatusResult(vm, Status::success(), true, true, Value(std::move(encoded)));
                });
    cls.addFunc("setSimplexes", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto simplexes = decodeVolumeFluidSimplexes(value.value());
        if (!simplexes) return script::projectStatusResult(vm, simplexes.status(), false, false);
        return script::projectResult(vm, self->setSimplexes(simplexes.value()));
    });
    cls.addFunc("querySimplexes", [vm](VolumeFluid* self, ssq::Object object, int maxHitsPerQuery) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto queries = decodeVolumeFluidQueries(value.value());
        if (!queries) return script::projectStatusResult(vm, queries.status(), false, false);
        auto hits = self->querySimplexes(queries.value(), unsigned(maxHitsPerQuery));
        if (!hits) return script::projectStatusResult(vm, hits.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidSimplexHits(hits.value()));
    });
    cls.addFunc("setColliders", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidColliders(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->setColliders(decoded.value()));
    });
    cls.addFunc("setSdfColliders", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidSdfColliders(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->setSdfColliders(decoded.value()));
    });
    cls.addFunc("setHeightFieldColliders", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidHeightFieldColliders(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->setHeightFieldColliders(decoded.value()));
    });
    cls.addFunc("updateSdfColliderPoses", [vm](VolumeFluid* self, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidSdfPoses(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        return script::projectResult(vm, self->updateSdfColliderPoses(decoded.value()));
    });
    cls.addFunc("contacts", [vm](VolumeFluid* self) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidContacts(self->contacts()));
    });
    cls.addFunc("emitBurst", [vm](VolumeFluid* self, ssq::Object object, int count) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto decoded = decodeVolumeFluidEmission(value.value());
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        auto emitted = emitVolumeFluidBurst(*self, decoded.value(), unsigned(count));
        if (!emitted) return script::projectStatusResult(vm, emitted.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true, Value(int64_t(emitted.value())));
    });
    cls.addFunc("getParticleCount", [](VolumeFluid* self) { return int(self->particleCount()); });
}

}  // namespace eve::fluids
