#include "zeroerr/unittest.h"
#include "camera/CameraController.h"
#include "camera/PcgCarCameraSetup.h"
#include "graphics/RenderSystem3D.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"
#include <limits>
using namespace eve::camera; using eve::graphics::Camera3D; using namespace eve::scene;
TEST_CASE("camera.pcgCar.appliesExactDefaultsAndInput") {
 Scene* scene=Scene::create(); scene->beginBuild(); scene->beginNode("root"); scene->addNode("car"); scene->end(); REQUIRE(scene->mountBuildAs("pcg-car-test"));
 SceneNodeRef car("pcg-car-test","car"); auto* camera=Camera3D::createCamera(); CameraController controller; PcgCarCameraSetup setup; PcgCarCameraProfile profile;
 REQUIRE(setup.configure(profile).ok()); REQUIRE(setup.apply(&controller,camera,&car,15.f,10.f).ok());
 CHECK_EQ(controller.getMode(),"orbit"); CHECK_EQ(controller.getRadius(),6.f); CHECK_EQ(controller.getMinimumRadius(),.6f); CHECK_EQ(controller.getMaximumRadius(),20.f);
 REQUIRE(setup.update(&controller,1.f,100.f,1.f,.25f,false,0.f).ok()); CHECK_EQ(setup.getYaw(),19.f); CHECK_EQ(setup.getPitch(),-80.f); CHECK_EQ(controller.getRadius(),.6f);
}
TEST_CASE("camera.pcgCar.rejectsBadProfilesWithoutMutation") {
 PcgCarCameraSetup setup; PcgCarCameraProfile profile; REQUIRE(setup.configure(profile).ok()); profile.minimumDistance=30.f; CHECK(!setup.configure(profile).ok());
 CameraController controller; REQUIRE(controller.setRadiusLimits(1.f,10.f).ok()); controller.setRadius(5.f); CHECK(!controller.setRadiusLimits(8.f,2.f).ok()); CHECK_EQ(controller.getRadius(),5.f); CHECK_EQ(controller.getMinimumRadius(),1.f);
 CHECK(!setup.update(nullptr,0,0,0,.1f,false,0).ok()); CHECK(!setup.update(&controller,std::numeric_limits<float>::quiet_NaN(),0,0,.1f,false,0).ok());
}
TEST_CASE("camera.pcgCar.rearLockUsesInjectedTargetYaw") {
 PcgCarCameraProfile profile; profile.lockToRearOfTarget=true; profile.rotationDamping=2.f; PcgCarCameraSetup setup; REQUIRE(setup.configure(profile).ok());
 Scene* scene=Scene::create(); scene->beginBuild(); scene->beginNode("root"); scene->addNode("car"); scene->end(); REQUIRE(scene->mountBuildAs("pcg-car-rear-test")); SceneNodeRef car("pcg-car-rear-test","car");
 auto* camera=Camera3D::createCamera(); CameraController controller; REQUIRE(setup.apply(&controller,camera,&car,0,0).ok()); REQUIRE(setup.update(&controller,0,0,0,.5f,false,90).ok()); CHECK_GT(setup.getYaw(),50.f); CHECK_LT(setup.getYaw(),90.f);
}
