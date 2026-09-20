#pragma once
#include "common/Result.h"
namespace ssq{class Table;} namespace eve::graphics{class Camera3D;} namespace eve::camera{
/** @brief Device-independent input snapshot for Pcg free-camera movement. */ struct PcgFreeCameraInput{float mouseX=0,mouseY=0,scroll=0,forward=0,right=0,up=0;bool sprint=false,rightPressed=false,rightReleased=false,leftPressed=false,escapePressed=false,focused=true;};
/** @brief Caller-owned six-degree free-camera state ported from Pcg FreeCamera. */ class EVENGINE_API_WORLD PcgFreeCamera{
public:
 /** @brief Configure capture and movement settings atomically. */ [[nodiscard]] Result<void> configure(bool enabled,bool lockCursor,bool holdRight,float lookSpeed,float moveSpeed,float sprintSpeed,bool scrollIncrease,float increase,float roll);
 /** @brief Capture input and initialize pose from a live camera. */ [[nodiscard]] Result<void> capture(graphics::Camera3D* camera);
 /** @brief Release input capture. */ void release()noexcept{captured_=false;}
 /** @brief Process one input snapshot and update the live camera. */ [[nodiscard]] Result<void> update(graphics::Camera3D* camera,const PcgFreeCameraInput& input,float dt);
 /** @brief Change roll and immediately update orientation on the next frame. */ [[nodiscard]] Result<void> setRoll(float roll);
 /** @brief Return capture state for cursor policy. */ bool getCaptured()const noexcept{return captured_;}
 /** @brief Return current sprint speed after scroll changes. */ float getSprintSpeed()const noexcept{return sprintSpeed_;}
 /** @brief Return current yaw in degrees. */ float getYaw()const noexcept{return yaw_;}
 /** @brief Return current pitch in degrees. */ float getPitch()const noexcept{return pitch_;}
 /** @brief Return configured roll in degrees. */ float getRoll()const noexcept{return roll_;}
private: bool enabled_=true,lockCursor_=false,holdRight_=true,scrollIncrease_=true,captured_=false;float lookSpeed_=5,moveSpeed_=5,sprintSpeed_=50,increase_=100,roll_=0,yaw_=0,pitch_=0;
};
/** @brief Register Pcg free-camera bindings. */ void exposePcgFreeCameraBindings(ssq::Table& table);
}