#pragma once

namespace eve::physics {
class World3D;
void registerPhysicsCapabilities();
void registerCameraObstructionWorld(World3D* world);
void unregisterCameraObstructionWorld(World3D* world);
}
