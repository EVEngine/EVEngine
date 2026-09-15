#include "fluids/VolumeFluidBindingInternal.inc"
#if defined(EVE_FLUIDS_HAS_MODEL3D)
#include "model3d/ModelData.h"
#endif

namespace eve::fluids {
void exposeVolumeFluidFactory(ssq::Class& cls) {
    const auto vm = cls.getHandle();
    cls.addFunc("volumeThermalRuleDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidThermalRule(VolumeFluidThermalRule{}));
    });
    cls.addFunc("volumeWindZoneDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidWindZone(VolumeFluidWindZone{}));
    });
    cls.addFunc("volumeColliderDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidCollider(VolumeFluidCollider{}));
    });
    cls.addFunc("volumeSdfColliderDefaults", [vm](Fluids*) {
        VolumeFluidSdfCollider collider;
        collider.sdf = MeshSdf::makeSphere(glm::vec3(0.f), .5f, {16, 16, 16});
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidSdfCollider(collider));
    });
    cls.addFunc("volumeHeightFieldColliderDefaults", [vm](Fluids*) {
        VolumeFluidHeightFieldCollider collider;
        collider.heights.assign(4, 0.f);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidHeightFieldCollider(collider));
    });
    cls.addFunc("volumeSdfPoseDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidSdfPose(VolumeFluidSdfPose{}));
    });
#if defined(EVE_FLUIDS_HAS_MODEL3D)
    cls.addFunc("volumeSdfColliderFromModel", [vm](Fluids*, model3d::ModelData* model, int meshIndex, float sx,
                                                   float sy, float sz, int resolution) {
        const auto failure = [&](const char* message) {
            return script::projectStatusResult(
                vm,
                Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.sdfColliderFromModel")),
                false, false);
        };
        if (!model || meshIndex < 0 || meshIndex >= model->getMeshCount() || resolution < 8 || resolution > 128)
            return failure("Invalid model mesh slot or SDF resolution");
        const int      vertexCount = model->getVertexCount(meshIndex), faceCount = model->getFaceCount(meshIndex);
        const uint64_t samples = uint64_t(resolution) * uint64_t(resolution) * uint64_t(resolution);
        if (vertexCount <= 0 || vertexCount > 1000000 || faceCount <= 0 || faceCount > 65536 ||
            samples * uint64_t(faceCount) > 4000000)
            return failure("Model SDF bake exceeds geometry or 4M sample/triangle budget");
        const glm::vec3 scale(sx, sy, sz);
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || sx == 0 || sy == 0 || sz == 0)
            return failure("Invalid model SDF scale");
        std::vector<glm::vec3> vertices(static_cast<size_t>(vertexCount), glm::vec3(0.f));
        std::vector<uint32_t>  indices;
        indices.reserve(size_t(faceCount) * 3);
        try {
            for (int i = 0; i < vertexCount; ++i)
                vertices[size_t(i)] = {model->getVertexPosition(meshIndex, i, 0) * sx,
                                       model->getVertexPosition(meshIndex, i, 1) * sy,
                                       model->getVertexPosition(meshIndex, i, 2) * sz};
            for (int i = 0; i < faceCount; ++i)
                for (int corner = 0; corner < 3; ++corner) {
                    const int index = model->getFaceVertexIndex(meshIndex, i, corner);
                    if (index < 0 || index >= vertexCount) return failure("Model contains invalid SDF triangle index");
                    indices.push_back(uint32_t(index));
                }
            for (size_t i = 0; i < indices.size(); i += 3)
                if (glm::length(glm::cross(vertices[indices[i + 1]] - vertices[indices[i]],
                                           vertices[indices[i + 2]] - vertices[indices[i]])) < 1e-8f)
                    return failure("Model contains degenerate SDF triangle");
        } catch (const std::exception&) {
            return failure("Could not read model mesh for SDF bake");
        }
        VolumeFluidSdfCollider collider;
        collider.sdf = MeshSdf::makeFromTriangles(vertices, indices, {resolution, resolution, resolution});
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidSdfCollider(collider));
    });
    cls.addFunc("volumeEmissionFromModel", [vm](Fluids*, model3d::ModelData* model, int meshIndex, float sx, float sy,
                                                float sz, float spacing) {
        const auto failure = [&](const char* message) {
            return script::projectStatusResult(
                vm,
                Status::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.meshDistribution")),
                false, false);
        };
        if (!model || meshIndex < 0 || meshIndex >= model->getMeshCount()) return failure("Invalid model mesh slot");
        const int vertexCount = model->getVertexCount(meshIndex), faceCount = model->getFaceCount(meshIndex);
        if (vertexCount <= 0 || vertexCount > 1000000 || faceCount <= 0 || faceCount > 65536)
            return failure("Model exceeds voxelization geometry budget");
        std::vector<glm::vec3> vertices(size_t(vertexCount), glm::vec3(0.f));
        std::vector<uint32_t>  indices;
        indices.reserve(size_t(faceCount) * 3);
        try {
            for (int i = 0; i < vertexCount; ++i)
                for (int c = 0; c < 3; ++c) vertices[size_t(i)][c] = model->getVertexPosition(meshIndex, i, c);
            for (int i = 0; i < faceCount; ++i)
                for (int corner = 0; corner < 3; ++corner) {
                    const int index = model->getFaceVertexIndex(meshIndex, i, corner);
                    if (index < 0) return failure("Model contains a nontriangular or invalid face");
                    indices.push_back(uint32_t(index));
                }
        } catch (const eve::Exception& error) {
            return failure(error.what());
        }
        auto points = buildVolumeFluidMeshDistribution(vertices, indices, {sx, sy, sz}, spacing);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape        = VolumeFluidEmissionShape::Distribution;
        emission.distribution = std::move(points).takeValue();
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
#endif
    cls.addFunc("volumeEmissionFromImage", [vm](Fluids*, image::ImageData* image, float pixelScale, float maximumSize,
                                                float spacing, float threshold, bool srgb) {
        if (!image || image->getWidth() <= 0 || image->getHeight() <= 0 || image->getFormat() != "RGBA8" ||
            uint64_t(image->getWidth()) * image->getHeight() > 16777216 ||
            uint64_t(image->getWidth()) * image->getHeight() * 4 != image->getSize() || !image->getData())
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Expected a valid RGBA8 image of at most 16M pixels",
                                                  "fluids.volume.imageDistribution")),
                false, false);
        const unsigned         width = unsigned(image->getWidth()), height = unsigned(image->getHeight());
        const auto*            bytes = static_cast<const uint8_t*>(image->getData());
        std::vector<glm::vec4> pixels(size_t(width) * height);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const size_t source = (size_t(height - 1 - y) * width + x) * 4;
                glm::vec4    color;
                for (int c = 0; c < 4; ++c) {
                    float value = float(bytes[source + size_t(c)]) / 255.f;
                    if (srgb && c < 3)
                        value = value <= .04045f ? value / 12.92f : std::pow((value + .055f) / 1.055f, 2.4f);
                    color[c] = value;
                }
                pixels[size_t(y) * width + x] = color;
            }
        auto points =
            buildVolumeFluidImageDistribution(pixels, width, height, pixelScale, maximumSize, spacing, threshold);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape           = VolumeFluidEmissionShape::Distribution;
        emission.distribution    = std::move(points).takeValue();
        emission.prototype.color = glm::vec4(1.f);
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
    cls.addFunc("volumeEmissionFromSphere", [vm](Fluids*, float radius, float spacing, bool surface) {
        auto points = buildVolumeFluidSphereDistribution(radius, spacing, surface);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape        = VolumeFluidEmissionShape::Distribution;
        emission.distribution = std::move(points).takeValue();
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
    cls.addFunc("volumeEmissionFromCube", [vm](Fluids*, float sx, float sy, float sz, float spacing, bool surface) {
        auto points = buildVolumeFluidCubeDistribution({sx, sy, sz}, spacing, surface);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape        = VolumeFluidEmissionShape::Distribution;
        emission.distribution = std::move(points).takeValue();
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
    cls.addFunc("volumeEmissionFromEdge", [vm](Fluids*, float length, float spacing, float radialVelocityDegrees) {
        auto points = buildVolumeFluidEdgeDistribution(length, spacing, radialVelocityDegrees);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape        = VolumeFluidEmissionShape::Distribution;
        emission.distribution = std::move(points).takeValue();
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
    cls.addFunc("volumeEmissionFromDisk", [vm](Fluids*, float radius, float spacing, bool edgeEmission) {
        auto points = buildVolumeFluidDiskDistribution(radius, spacing, edgeEmission);
        if (!points) return script::projectStatusResult(vm, points.status(), false, false);
        VolumeFluidEmission emission;
        emission.shape        = VolumeFluidEmissionShape::Distribution;
        emission.distribution = std::move(points).takeValue();
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluidEmission(emission));
    });
    cls.addFunc("volumeEmissionDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmission(VolumeFluidEmission{}));
    });
    cls.addFunc("composeVolumeEmitterShapes", [vm](Fluids*, ssq::Object baseObject, ssq::Object shapesObject) {
        auto baseValue = script::valueFromSquirrel(baseObject);
        if (!baseValue) return script::projectStatusResult(vm, baseValue.status(), false, false);
        auto base = decodeVolumeFluidEmission(baseValue.value());
        if (!base) return script::projectStatusResult(vm, base.status(), false, false);
        auto shapeValues = script::valueFromSquirrel(shapesObject);
        if (!shapeValues) return script::projectStatusResult(vm, shapeValues.status(), false, false);
        if (!shapeValues.value().isArray() || shapeValues.value().arraySize() > 64)
            return script::projectStatusResult(
                vm,
                Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                  "Emitter shapes must be an array of at most 64 descriptions",
                                                  "fluids.volume.emitterShapes")),
                false, false);
        std::vector<VolumeFluidEmission> shapes;
        shapes.reserve(shapeValues.value().arraySize());
        for (size_t i = 0; i < shapeValues.value().arraySize(); ++i) {
            auto shape = decodeVolumeFluidEmission(shapeValues.value().at(i));
            if (!shape) return script::projectStatusResult(vm, shape.status(), false, false);
            shapes.push_back(std::move(shape).takeValue());
        }
        return script::projectResult(
            vm, composeVolumeFluidEmitterShapes(base.value(), shapes),
            [](const VolumeFluidEmission& emission) { return encodeVolumeFluidEmission(emission); });
    });
    cls.addFunc("volumeFluidEmitterBlueprintDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmitterBlueprint3D(VolumeFluidEmitterBlueprint3D{}));
    });
    cls.addFunc("prepareVolumeFluidEmitterBlueprint3D", [vm](Fluids*, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto blueprint = decodeVolumeFluidEmitterBlueprint3D(value.value());
        if (!blueprint) return script::projectStatusResult(vm, blueprint.status(), false, false);
        auto prepared = prepareVolumeFluidEmitterBlueprint3D(blueprint.value());
        if (!prepared) return script::projectStatusResult(vm, prepared.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmitterBlueprintApplication3D(prepared.value()));
    });
    cls.addFunc("volumeGranularEmitterBlueprintDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeGranularEmitterBlueprint3D(VolumeGranularEmitterBlueprint3D{}));
    });
    cls.addFunc("prepareVolumeGranularEmitterBlueprint3D", [vm](Fluids*, ssq::Object object) {
        auto value = script::valueFromSquirrel(object);
        if (!value) return script::projectStatusResult(vm, value.status(), false, false);
        auto blueprint = decodeVolumeGranularEmitterBlueprint3D(value.value());
        if (!blueprint) return script::projectStatusResult(vm, blueprint.status(), false, false);
        auto prepared = prepareVolumeGranularEmitterBlueprint3D(blueprint.value());
        if (!prepared) return script::projectStatusResult(vm, prepared.status(), false, false);
        return script::projectStatusResult(vm, Status::success(), true, true,
                                           encodeVolumeFluidEmitterBlueprintApplication3D(prepared.value()));
    });
    cls.addFunc("volumeEmitterBlueprintMetrics", [vm](Fluids*, float resolution, float restDensity, float smoothing) {
        auto evaluated = evaluateVolumeFluidEmitterBlueprint3D(resolution, restDensity, smoothing);
        if (!evaluated) return script::projectStatusResult(vm, evaluated.status(), false, false);
        Value metrics(Value::Object{});
        metrics.set("particleSize", double(evaluated.value().particleSize));
        metrics.set("particleMass", double(evaluated.value().particleMass));
        metrics.set("smoothingRadius", double(evaluated.value().smoothingRadius));
        return script::projectStatusResult(vm, Status::success(), true, true, std::move(metrics));
    });
    cls.addFunc("volumeDefaults", [vm](Fluids*) {
        return script::projectStatusResult(vm, Status::success(), true, true, encodeVolumeFluid(VolumeFluidSnapshot{}));
    });
    cls.addFunc("newVolumeSimulator", [vm](Fluids*, ssq::Object object) {
        auto decoded = read(object);
        if (!decoded) return script::projectStatusResult(vm, decoded.status(), false, false);
        auto created = VolumeFluid::create(decoded.value().settings);
        if (!created) return script::projectStatusResult(vm, created.status(), false, false);
        auto owned    = std::move(created).takeValue();
        auto restored = owned->restore(decoded.value());
        if (!restored) return script::projectStatusResult(vm, restored.status(), false, false);
        auto instance = script::makeOwnedSquirrelInstance<VolumeFluid>(vm, std::move(owned));
        if (!instance) return script::projectStatusResult(vm, instance.status(), false, false);
        auto result = script::projectStatusResult(vm, Status::success(), true, true);
        result.set("value", std::move(instance).takeValue());
        result.set("ownership", std::string("owned"));
        return result;
    });
}

}  // namespace eve::fluids
