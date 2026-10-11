#include "zeroerr/unittest.h"
#include "camera/PcgFreeCamera.h"
#include "graphics/RenderSystem3D.h"
#include <cmath>
#include <limits>
using namespace eve::camera;using eve::graphics::Camera3D;
TEST_CASE("camera.pcgFree.captureMoveSprintScrollAndRelease"){
 auto*c=Camera3D::createCamera();c->setEye(0,0,0);c->setTarget(0,0,1);PcgFreeCamera f;REQUIRE(f.configure(true,true,true,5,5,50,true,100,15).ok());PcgFreeCameraInput i;i.rightPressed=true;i.forward=1;i.sprint=true;i.scroll=500;REQUIRE(f.update(c,i,1).ok());CHECK(f.getCaptured());CHECK_EQ(f.getSprintSpeed(),150.f);CHECK_GT(c->getEyeZ(),149.f);CHECK_EQ(f.getRoll(),15.f);i={};i.rightReleased=true;REQUIRE(f.update(c,i,.1f).ok());CHECK(!f.getCaptured());
}
TEST_CASE("camera.pcgFree.focusAndInvalidInputAreSafe"){
 auto*c=Camera3D::createCamera();c->setEye(1,2,3);c->setTarget(1,2,4);PcgFreeCamera f;REQUIRE(f.capture(c).ok());PcgFreeCameraInput i;i.focused=false;REQUIRE(f.update(c,i,.1f).ok());CHECK(!f.getCaptured());float x=c->getEyeX();i.mouseX=std::numeric_limits<float>::quiet_NaN();CHECK(!f.update(c,i,.1f).ok());CHECK_EQ(c->getEyeX(),x);CHECK(!f.configure(true,false,true,5,10,5,true,100,0).ok());
}