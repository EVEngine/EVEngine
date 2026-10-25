#include <filesystem>
#include <iostream>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include <stdexcept>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformTrack.h"
#include "physics/backend/bake/ParticleTrack.h"
#include "physics/backend/bake/RigidPoseTrack.h"
#include "physics/backend/bake/script/PhysicsBake.h"
namespace {
struct Files {
    std::filesystem::path directory;
    Files() {
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = std::filesystem::temp_directory_path() /
                             ("eve-bake-script-profile-" + std::to_string(std::random_device{}()));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve script fixture directory");
    }
    ~Files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
void run() {
    using namespace eve::physics;
    Files      files;
    const auto rigid     = (files.directory / "rigid.bake").string();
    const auto deform    = (files.directory / "deform.bake").string();
    const auto particles = (files.directory / "particles.bake").string();
    {
        BakeRecipe recipe;
        recipe.setProducer("script-profile-v1").expect("producer");
        recipe.setBackend("fixture-values").expect("backend");
        recipe.setSource("fixture", "two-frames").expect("source");
        recipe.setParameters("").expect("parameters");
        recipe.setTiming(1.f, 1).expect("timing");
        recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
        eve::PersistentId::Bytes bytes{};
        bytes.back() = 1;
        const eve::PersistentId id(bytes);
        auto                    rigidTrack = RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels,
                                                                     {{0, {{id, {}}}}, {1, {{id, {2, 0, 0, 0, 0, 0, 1}}}}}, recipe)
                              .expect("rigid");
        rigidTrack.save(rigid).expect("publish rigid");
        auto topology = DeformTopology::prepare(DeformPrimitive::Segments, 2, {0, 1}, {}).expect("topology");
        auto deformTrack =
            DeformTrack::prepare({{0, topology, {{0, 0, 0}, {0, 1, 0}}}, {1, topology, {{2, 0, 0}, {2, 1, 0}}}}, recipe)
                .expect("deform");
        deformTrack.save(deform).expect("publish deform");
        BakedParticlePose a;
        a.w = a.h          = 2;
        auto b             = a;
        b.x                = 2;
        auto particleTrack = ParticleTrack::prepare(1, {{id, 1, {{0, a}, {1, b}}}}, recipe).expect("particles");
        particleTrack.save(particles).expect("publish particles");
    }
    ssq::VM vm(2048);
    auto    table = vm.addTable("eve");
    PhysicsBake::expose(table);
    vm.addFunc("rigidPath", [&] { return rigid; });
    vm.addFunc("deformPath", [&] { return deform; });
    vm.addFunc("particlePath", [&] { return particles; });
    vm.addFunc("removeAssets", [&] {
        return std::filesystem::remove(rigid) && std::filesystem::remove(deform) && std::filesystem::remove(particles);
    });
    vm.run(vm.compileSource(R"(
        function value(result) { assert(result.ok && result.hasValue); return result.value; }
        local recipe = eve.BakeRecipe();
        assert(recipe.setProducer("script-profile-v1").ok);
        assert(recipe.setBackend("fixture-values").ok);
        assert(recipe.setSource("fixture", "two-frames").ok);
        assert(recipe.setParameters("").ok);
        assert(recipe.setTiming(1.0,1).ok);
        assert(recipe.setCoordinates("pixels2d").ok);
        local bake = eve.PhysicsBake();
        local rigid = value(bake.loadRigid(rigidPath(),recipe));
        local deform = value(bake.loadDeform(deformPath(),recipe));
        local particles = value(bake.loadParticles(particlePath(),recipe));
        local id = value(rigid.trackId(0));
        local otherRigid = value(rigid.copy());
        local otherDeform = value(deform.prepareInstance());
        local otherParticles = value(particles.prepareInstance());
        assert(rigid.seek(0.5).ok && value(rigid.sample(id)).x == 1.0);
        assert(deform.seek(0.5).ok);
        local snapshot = value(deform.captureSample());
        assert(value(snapshot.point(0)).x == 1.0);
        assert(particles.seek(0.5).ok);
        local particleSnapshot = value(particles.captureSample());
        assert(particleSnapshot.count() == 1);
        assert(value(particleSnapshot.particle(0)).x == 1.0);
        assert(otherRigid.time() == 0.0 && otherDeform.time() == 0.0 && otherParticles.time() == 0.0);
        assert(removeAssets());
        assert(!bake.loadRigid(rigidPath(),recipe).ok);
        rigid = null; deform = null; particles = null; recipe = null; bake = null;
        collectgarbage();
        assert(otherRigid.seek(1.0).ok && value(otherRigid.sample(id)).x == 2.0);
        assert(otherDeform.seek(1.0).ok && value(value(otherDeform.captureSample()).point(0)).x == 2.0);
        assert(otherParticles.seek(1.0).ok && value(otherParticles.captureSample()).count() == 0);
        otherRigid = null; otherDeform = null; otherParticles = null; collectgarbage();
        assert(value(snapshot.point(0)).x == 1.0);
        assert(value(particleSnapshot.particle(0)).x == 1.0);
    )"));
}
}  // namespace
int main() {
    try {
        run();
        std::cout << "physics-bake-script-only: VM persistence, independent players and owned snapshots PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
