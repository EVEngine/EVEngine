# physics：补充绑定参考

本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、
初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。
`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。
参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。

## Body

- `applyAngularImpulse(impulse: float)` → `void`；[绑定实现 L507](../../../../src/modules/physics/Physics.cpp#L507)。
- `applyForceAt(fx: float, fy: float, x: float, y: float)` → `void`；[绑定实现 L505](../../../../src/modules/physics/Physics.cpp#L505)。
- `getAngle()` → `float`；[绑定实现 L494](../../../../src/modules/physics/Physics.cpp#L494)。
- `getAngularVelocity()` → `float`；[绑定实现 L503](../../../../src/modules/physics/Physics.cpp#L503)。
- `getLinearSpeed()` → `float`；[绑定实现 L498](../../../../src/modules/physics/Physics.cpp#L498)。
- `getLinearVelocityX()` → `float`；[绑定实现 L496](../../../../src/modules/physics/Physics.cpp#L496)。
- `getLinearVelocityY()` → `float`；[绑定实现 L497](../../../../src/modules/physics/Physics.cpp#L497)。
- `getType()` → `string`；[绑定实现 L509](../../../../src/modules/physics/Physics.cpp#L509)。
- `getWorldCenterY()` → `float`；[绑定实现 L501](../../../../src/modules/physics/Physics.cpp#L501)。
- `getY()` → `float`；[绑定实现 L492](../../../../src/modules/physics/Physics.cpp#L492)。
- `isActive()` → `bool`；[绑定实现 L513](../../../../src/modules/physics/Physics.cpp#L513)。
- `isAwake()` → `bool`；[绑定实现 L517](../../../../src/modules/physics/Physics.cpp#L517)。
- `isBullet()` → `bool`；[绑定实现 L515](../../../../src/modules/physics/Physics.cpp#L515)。
- `isFixedRotation()` → `bool`；[绑定实现 L511](../../../../src/modules/physics/Physics.cpp#L511)。
- `newCircleFixture(radius: float, density: float, friction: float, restitution: float)` → `Fixture`；[绑定实现 L520](../../../../src/modules/physics/Physics.cpp#L520)。
- `newRectangleFixtureAt(width: float, height: float, offsetX: float, offsetY: float, density: float, friction: float, restitution: float)` → `Fixture`；[绑定实现 L519](../../../../src/modules/physics/Physics.cpp#L519)。
- `setActive(active: bool)` → `void`；[绑定实现 L512](../../../../src/modules/physics/Physics.cpp#L512)。
- `setAngle(radians: float)` → `void`；[绑定实现 L493](../../../../src/modules/physics/Physics.cpp#L493)。
- `setAngularVelocity(omega: float)` → `void`；[绑定实现 L502](../../../../src/modules/physics/Physics.cpp#L502)。
- `setAwake(awake: bool)` → `void`；[绑定实现 L516](../../../../src/modules/physics/Physics.cpp#L516)。
- `setLinearVelocity(vx: float, vy: float)` → `void`；[绑定实现 L495](../../../../src/modules/physics/Physics.cpp#L495)。
- `setPosition(x: float, y: float)` → `void`；[绑定实现 L490](../../../../src/modules/physics/Physics.cpp#L490)。
- `setType(bodyType: string)` → `void`；[绑定实现 L508](../../../../src/modules/physics/Physics.cpp#L508)。

## Body3D

- `applyAngularImpulse(ix: float, iy: float, iz: float)` → `void`；[绑定实现 L619](../../../../src/modules/physics/Physics.cpp#L619)。
- `applyForceAt(fx: float, fy: float, fz: float, x: float, y: float, z: float)` → `void`；[绑定实现 L615](../../../../src/modules/physics/Physics.cpp#L615)。
- `getAngularDamping()` → `float`；[绑定实现 L630](../../../../src/modules/physics/Physics.cpp#L630)。
- `getAngularVelocityX()` → `float`；[绑定实现 L605](../../../../src/modules/physics/Physics.cpp#L605)。
- `getAngularVelocityY()` → `float`；[绑定实现 L606](../../../../src/modules/physics/Physics.cpp#L606)。
- `getAngularVelocityZ()` → `float`；[绑定实现 L607](../../../../src/modules/physics/Physics.cpp#L607)。
- `getGravityScale()` → `float`；[绑定实现 L632](../../../../src/modules/physics/Physics.cpp#L632)。
- `getInertiaXY()` → `float`；[绑定实现 L650](../../../../src/modules/physics/Physics.cpp#L650)。
- `getInertiaXZ()` → `float`；[绑定实现 L651](../../../../src/modules/physics/Physics.cpp#L651)。
- `getInertiaYY()` → `float`；[绑定实现 L648](../../../../src/modules/physics/Physics.cpp#L648)。
- `getInertiaYZ()` → `float`；[绑定实现 L652](../../../../src/modules/physics/Physics.cpp#L652)。
- `getInertiaZZ()` → `float`；[绑定实现 L649](../../../../src/modules/physics/Physics.cpp#L649)。
- `getLinearDamping()` → `float`；[绑定实现 L628](../../../../src/modules/physics/Physics.cpp#L628)。
- `getLinearVelocityX()` → `float`；[绑定实现 L600](../../../../src/modules/physics/Physics.cpp#L600)。
- `getLinearVelocityY()` → `float`；[绑定实现 L601](../../../../src/modules/physics/Physics.cpp#L601)。
- `getLinearVelocityZ()` → `float`；[绑定实现 L602](../../../../src/modules/physics/Physics.cpp#L602)。
- `getLocalCenterY()` → `float`；[绑定实现 L654](../../../../src/modules/physics/Physics.cpp#L654)。
- `getLocalCenterZ()` → `float`；[绑定实现 L655](../../../../src/modules/physics/Physics.cpp#L655)。
- `getRotW()` → `float`；[绑定实现 L598](../../../../src/modules/physics/Physics.cpp#L598)。
- `getRotY()` → `float`；[绑定实现 L596](../../../../src/modules/physics/Physics.cpp#L596)。
- `getRotZ()` → `float`；[绑定实现 L597](../../../../src/modules/physics/Physics.cpp#L597)。
- `getSleepThreshold()` → `float`；[绑定实现 L636](../../../../src/modules/physics/Physics.cpp#L636)。
- `getType()` → `string`；[绑定实现 L660](../../../../src/modules/physics/Physics.cpp#L660)。
- `getWorldCenterY()` → `float`；[绑定实现 L657](../../../../src/modules/physics/Physics.cpp#L657)。
- `getWorldCenterZ()` → `float`；[绑定实现 L658](../../../../src/modules/physics/Physics.cpp#L658)。
- `getY()` → `float`；[绑定实现 L592](../../../../src/modules/physics/Physics.cpp#L592)。
- `getZ()` → `float`；[绑定实现 L593](../../../../src/modules/physics/Physics.cpp#L593)。
- `isActive()` → `bool`；[绑定实现 L664](../../../../src/modules/physics/Physics.cpp#L664)。
- `isAngularXLocked()` → `bool`；[绑定实现 L641](../../../../src/modules/physics/Physics.cpp#L641)。
- `isAngularYLocked()` → `bool`；[绑定实现 L642](../../../../src/modules/physics/Physics.cpp#L642)。
- `isAngularZLocked()` → `bool`；[绑定实现 L643](../../../../src/modules/physics/Physics.cpp#L643)。
- `isAwake()` → `bool`；[绑定实现 L668](../../../../src/modules/physics/Physics.cpp#L668)。
- `isBullet()` → `bool`；[绑定实现 L666](../../../../src/modules/physics/Physics.cpp#L666)。
- `isFixedRotation()` → `bool`；[绑定实现 L662](../../../../src/modules/physics/Physics.cpp#L662)。
- `isLinearXLocked()` → `bool`；[绑定实现 L638](../../../../src/modules/physics/Physics.cpp#L638)。
- `isLinearYLocked()` → `bool`；[绑定实现 L639](../../../../src/modules/physics/Physics.cpp#L639)。
- `isLinearZLocked()` → `bool`；[绑定实现 L640](../../../../src/modules/physics/Physics.cpp#L640)。
- `isSleepEnabled()` → `bool`；[绑定实现 L634](../../../../src/modules/physics/Physics.cpp#L634)。
- `setActive(active: bool)` → `void`；[绑定实现 L663](../../../../src/modules/physics/Physics.cpp#L663)。
- `setAngularVelocity(wx: float, wy: float, wz: float)` → `void`；[绑定实现 L604](../../../../src/modules/physics/Physics.cpp#L604)。
- `setAwake(awake: bool)` → `void`；[绑定实现 L667](../../../../src/modules/physics/Physics.cpp#L667)。
- `setLinearVelocity(vx: float, vy: float, vz: float)` → `void`；[绑定实现 L599](../../../../src/modules/physics/Physics.cpp#L599)。
- `setPosition(x: float, y: float, z: float)` → `void`；[绑定实现 L590](../../../../src/modules/physics/Physics.cpp#L590)。
- `setSleepThreshold(threshold: float)` → `void`；[绑定实现 L635](../../../../src/modules/physics/Physics.cpp#L635)。
- `setType(bodyType: string)` → `void`；[绑定实现 L659](../../../../src/modules/physics/Physics.cpp#L659)。

## DistanceField3D

- `didMoverHitWall()` → `bool`；[绑定实现 L485](../../../../src/modules/physics/Physics.cpp#L485)。
- `getCollisionX()` → `float`；[绑定实现 L463](../../../../src/modules/physics/Physics.cpp#L463)。
- `getCollisionZ()` → `float`；[绑定实现 L465](../../../../src/modules/physics/Physics.cpp#L465)。
- `getDepth()` → `int`；[绑定实现 L437](../../../../src/modules/physics/Physics.cpp#L437)。
- `getMoverGroundSnap()` → `float`；[绑定实现 L459](../../../../src/modules/physics/Physics.cpp#L459)。
- `getMoverNormalY()` → `float`；[绑定实现 L480](../../../../src/modules/physics/Physics.cpp#L480)。
- `getMoverNormalZ()` → `float`；[绑定实现 L481](../../../../src/modules/physics/Physics.cpp#L481)。
- `getMoverSkinWidth()` → `float`；[绑定实现 L457](../../../../src/modules/physics/Physics.cpp#L457)。
- `getMoverStepHeight()` → `float`；[绑定实现 L461](../../../../src/modules/physics/Physics.cpp#L461)。
- `getNormalY()` → `float`；[绑定实现 L447](../../../../src/modules/physics/Physics.cpp#L447)。
- `getNormalZ()` → `float`；[绑定实现 L448](../../../../src/modules/physics/Physics.cpp#L448)。
- `getOriginY()` → `float`；[绑定实现 L441](../../../../src/modules/physics/Physics.cpp#L441)。
- `getOriginZ()` → `float`；[绑定实现 L442](../../../../src/modules/physics/Physics.cpp#L442)。
- `getOutsideDistance()` → `float`；[绑定实现 L443](../../../../src/modules/physics/Physics.cpp#L443)。
- `getPenetrationDepth()` → `float`；[绑定实现 L472](../../../../src/modules/physics/Physics.cpp#L472)。
- `getShapeContactX()` → `float`；[绑定实现 L469](../../../../src/modules/physics/Physics.cpp#L469)。
- `getShapeContactZ()` → `float`；[绑定实现 L471](../../../../src/modules/physics/Physics.cpp#L471)。
- `getSurfaceX()` → `float`；[绑定实现 L466](../../../../src/modules/physics/Physics.cpp#L466)。
- `getSurfaceZ()` → `float`；[绑定实现 L468](../../../../src/modules/physics/Physics.cpp#L468)。
- `sample(x: float, y: float, z: float)` → `float`；[绑定实现 L434](../../../../src/modules/physics/Physics.cpp#L434)。
- `sampleNormal(x: float, y: float, z: float)` → `void`；[绑定实现 L445](../../../../src/modules/physics/Physics.cpp#L445)。

## Fixture

- `getBodyId()` → `int`；[绑定实现 L845](../../../../src/modules/physics/Physics.cpp#L845)。
- `getCategoryBits()` → `int`；[绑定实现 L840](../../../../src/modules/physics/Physics.cpp#L840)。
- `getDensity()` → `float`；[绑定实现 L836](../../../../src/modules/physics/Physics.cpp#L836)。
- `getFriction()` → `float`；[绑定实现 L832](../../../../src/modules/physics/Physics.cpp#L832)。
- `getGroupIndex()` → `int`；[绑定实现 L844](../../../../src/modules/physics/Physics.cpp#L844)。
- `getMaskBits()` → `int`；[绑定实现 L842](../../../../src/modules/physics/Physics.cpp#L842)。
- `getRestitution()` → `float`；[绑定实现 L834](../../../../src/modules/physics/Physics.cpp#L834)。
- `getTag()` → `string`；[绑定实现 L838](../../../../src/modules/physics/Physics.cpp#L838)。
- `isSensor()` → `bool`；[绑定实现 L830](../../../../src/modules/physics/Physics.cpp#L830)。
- `setDensity(density: float)` → `void`；[绑定实现 L835](../../../../src/modules/physics/Physics.cpp#L835)。

## Fluid2D

- `clearBounds()` → `void`；[绑定实现 L1076](../../../../src/modules/physics/Physics.cpp#L1076)。
- `getCapacity()` → `int`；[绑定实现 L1084](../../../../src/modules/physics/Physics.cpp#L1084)。
- `getGravityX()` → `float`；[绑定实现 L1061](../../../../src/modules/physics/Physics.cpp#L1061)。
- `getGravityY()` → `float`；[绑定实现 L1062](../../../../src/modules/physics/Physics.cpp#L1062)。
- `getIterations()` → `int`；[绑定实现 L1074](../../../../src/modules/physics/Physics.cpp#L1074)。
- `getNearPressureStiffness()` → `float`；[绑定实现 L1070](../../../../src/modules/physics/Physics.cpp#L1070)。
- `getParticleCount()` → `int`；[绑定实现 L1085](../../../../src/modules/physics/Physics.cpp#L1085)。
- `getParticleSize()` → `float`；[绑定实现 L1082](../../../../src/modules/physics/Physics.cpp#L1082)。
- `getParticleVx(index: int)` → `float`；[绑定实现 L1088](../../../../src/modules/physics/Physics.cpp#L1088)。
- `getParticleVy(index: int)` → `float`；[绑定实现 L1089](../../../../src/modules/physics/Physics.cpp#L1089)。
- `getParticleY(index: int)` → `float`；[绑定实现 L1087](../../../../src/modules/physics/Physics.cpp#L1087)。
- `getPressureStiffness()` → `float`；[绑定实现 L1068](../../../../src/modules/physics/Physics.cpp#L1068)。
- `getRestDensity()` → `float`；[绑定实现 L1066](../../../../src/modules/physics/Physics.cpp#L1066)。
- `getSmoothingRadius()` → `float`；[绑定实现 L1064](../../../../src/modules/physics/Physics.cpp#L1064)。
- `getViscosity()` → `float`；[绑定实现 L1072](../../../../src/modules/physics/Physics.cpp#L1072)。
- `setNearPressureStiffness(k: float)` → `void`；[绑定实现 L1069](../../../../src/modules/physics/Physics.cpp#L1069)。

## Joint2D

- `getBodyAId()` → `int`；[绑定实现 L527](../../../../src/modules/physics/Physics.cpp#L527)。
- `getBodyBId()` → `int`；[绑定实现 L528](../../../../src/modules/physics/Physics.cpp#L528)。
- `getCollideConnected()` → `bool`；[绑定实现 L530](../../../../src/modules/physics/Physics.cpp#L530)。
- `getDistanceDampingRatio()` → `float`；[绑定实现 L535](../../../../src/modules/physics/Physics.cpp#L535)。
- `getDistanceFrequency()` → `float`；[绑定实现 L534](../../../../src/modules/physics/Physics.cpp#L534)。
- `getDistanceLength()` → `float`；[绑定实现 L532](../../../../src/modules/physics/Physics.cpp#L532)。
- `getGearJoint1()` → `Joint2D`；[绑定实现 L560](../../../../src/modules/physics/Physics.cpp#L560)。
- `getGearJoint2()` → `Joint2D`；[绑定实现 L561](../../../../src/modules/physics/Physics.cpp#L561)。
- `getGearRatio()` → `float`；[绑定实现 L559](../../../../src/modules/physics/Physics.cpp#L559)。
- `getMotorAngularOffset()` → `float`；[绑定实现 L557](../../../../src/modules/physics/Physics.cpp#L557)。
- `getMotorLinearOffsetX()` → `float`；[绑定实现 L555](../../../../src/modules/physics/Physics.cpp#L555)。
- `getMotorLinearOffsetY()` → `float`；[绑定实现 L556](../../../../src/modules/physics/Physics.cpp#L556)。
- `getRevoluteAngle()` → `float`；[绑定实现 L538](../../../../src/modules/physics/Physics.cpp#L538)。
- `getRevoluteMotorTorque()` → `float`；[绑定实现 L540](../../../../src/modules/physics/Physics.cpp#L540)。
- `getRevoluteSpeed()` → `float`；[绑定实现 L539](../../../../src/modules/physics/Physics.cpp#L539)。
- `getWeldDampingRatio()` → `float`；[绑定实现 L547](../../../../src/modules/physics/Physics.cpp#L547)。
- `getWeldFrequency()` → `float`；[绑定实现 L546](../../../../src/modules/physics/Physics.cpp#L546)。
- `getWheelSpeed()` → `float`；[绑定实现 L551](../../../../src/modules/physics/Physics.cpp#L551)。
- `getWheelTranslation()` → `float`；[绑定实现 L550](../../../../src/modules/physics/Physics.cpp#L550)。
- `isValid()` → `bool`；[绑定实现 L563](../../../../src/modules/physics/Physics.cpp#L563)。
- `setDistanceLength(lengthPixels: float)` → `void`；[绑定实现 L531](../../../../src/modules/physics/Physics.cpp#L531)。
- `setMotorAngularOffset(radians: float)` → `void`；[绑定实现 L553](../../../../src/modules/physics/Physics.cpp#L553)。
- `setMotorLimits(maxForcePixels: float, maxTorque: float)` → `void`；[绑定实现 L554](../../../../src/modules/physics/Physics.cpp#L554)。
- `setMotorLinearOffset(xPixels: float, yPixels: float)` → `void`；[绑定实现 L552](../../../../src/modules/physics/Physics.cpp#L552)。
- `setWheelMotor(enabled: bool, speed: float, maxTorque: float)` → `void`；[绑定实现 L549](../../../../src/modules/physics/Physics.cpp#L549)。
- `setWheelSpring(frequencyHz: float, dampingRatio: float)` → `void`；[绑定实现 L548](../../../../src/modules/physics/Physics.cpp#L548)。

## Joint3D

- `getAngularSeparation()` → `float`；[绑定实现 L750](../../../../src/modules/physics/Physics.cpp#L750)。
- `getBodyAId()` → `int`；[绑定实现 L735](../../../../src/modules/physics/Physics.cpp#L735)。
- `getBodyBId()` → `int`；[绑定实现 L736](../../../../src/modules/physics/Physics.cpp#L736)。
- `getCollideConnected()` → `bool`；[绑定实现 L738](../../../../src/modules/physics/Physics.cpp#L738)。
- `getConstraintForceX()` → `float`；[绑定实现 L743](../../../../src/modules/physics/Physics.cpp#L743)。
- `getConstraintForceY()` → `float`；[绑定实现 L744](../../../../src/modules/physics/Physics.cpp#L744)。
- `getConstraintForceZ()` → `float`；[绑定实现 L745](../../../../src/modules/physics/Physics.cpp#L745)。
- `getConstraintTorqueX()` → `float`；[绑定实现 L746](../../../../src/modules/physics/Physics.cpp#L746)。
- `getConstraintTorqueY()` → `float`；[绑定实现 L747](../../../../src/modules/physics/Physics.cpp#L747)。
- `getConstraintTorqueZ()` → `float`；[绑定实现 L748](../../../../src/modules/physics/Physics.cpp#L748)。
- `getDistanceLength()` → `float`；[绑定实现 L752](../../../../src/modules/physics/Physics.cpp#L752)。
- `getForceThreshold()` → `float`；[绑定实现 L740](../../../../src/modules/physics/Physics.cpp#L740)。
- `getLinearSeparation()` → `float`；[绑定实现 L749](../../../../src/modules/physics/Physics.cpp#L749)。
- `getMotorAngularVelocityX()` → `float`；[绑定实现 L796](../../../../src/modules/physics/Physics.cpp#L796)。
- `getMotorAngularVelocityY()` → `float`；[绑定实现 L797](../../../../src/modules/physics/Physics.cpp#L797)。
- `getMotorAngularVelocityZ()` → `float`；[绑定实现 L798](../../../../src/modules/physics/Physics.cpp#L798)。
- `getMotorLinearVelocityX()` → `float`；[绑定实现 L793](../../../../src/modules/physics/Physics.cpp#L793)。
- `getMotorLinearVelocityY()` → `float`；[绑定实现 L794](../../../../src/modules/physics/Physics.cpp#L794)。
- `getMotorLinearVelocityZ()` → `float`；[绑定实现 L795](../../../../src/modules/physics/Physics.cpp#L795)。
- `getParallelDampingRatio()` → `float`；[绑定实现 L801](../../../../src/modules/physics/Physics.cpp#L801)。
- `getParallelHertz()` → `float`；[绑定实现 L800](../../../../src/modules/physics/Physics.cpp#L800)。
- `getParallelMaxTorque()` → `float`；[绑定实现 L802](../../../../src/modules/physics/Physics.cpp#L802)。
- `getRevoluteAngle()` → `float`；[绑定实现 L760](../../../../src/modules/physics/Physics.cpp#L760)。
- `getRevoluteMotorTorque()` → `float`；[绑定实现 L761](../../../../src/modules/physics/Physics.cpp#L761)。
- `getSphericalConeAngle()` → `float`；[绑定实现 L771](../../../../src/modules/physics/Physics.cpp#L771)。
- `getSphericalTwistAngle()` → `float`；[绑定实现 L772](../../../../src/modules/physics/Physics.cpp#L772)。
- `getTorqueThreshold()` → `float`；[绑定实现 L742](../../../../src/modules/physics/Physics.cpp#L742)。
- `getWeldAngularDampingRatio()` → `float`；[绑定实现 L787](../../../../src/modules/physics/Physics.cpp#L787)。
- `getWeldAngularHertz()` → `float`；[绑定实现 L786](../../../../src/modules/physics/Physics.cpp#L786)。
- `getWeldLinearDampingRatio()` → `float`；[绑定实现 L785](../../../../src/modules/physics/Physics.cpp#L785)。
- `getWeldLinearHertz()` → `float`；[绑定实现 L784](../../../../src/modules/physics/Physics.cpp#L784)。
- `setDistanceLength(length: float)` → `void`；[绑定实现 L751](../../../../src/modules/physics/Physics.cpp#L751)。
- `setMotorAngularSpring(hertz: float, dampingRatio: float, maxTorque: float)` → `void`；[绑定实现 L792](../../../../src/modules/physics/Physics.cpp#L792)。
- `setMotorAngularVelocity(wx: float, wy: float, wz: float)` → `void`；[绑定实现 L789](../../../../src/modules/physics/Physics.cpp#L789)。
- `setMotorLinearSpring(hertz: float, dampingRatio: float, maxForce: float)` → `void`；[绑定实现 L791](../../../../src/modules/physics/Physics.cpp#L791)。
- `setMotorLinearVelocity(vx: float, vy: float, vz: float)` → `void`；[绑定实现 L788](../../../../src/modules/physics/Physics.cpp#L788)。
- `setMotorVelocityLimits(maxForce: float, maxTorque: float)` → `void`；[绑定实现 L790](../../../../src/modules/physics/Physics.cpp#L790)。
- `setParallelSpring(hertz: float, dampingRatio: float, maxTorque: float)` → `void`；[绑定实现 L799](../../../../src/modules/physics/Physics.cpp#L799)。

## Mechanism2D

- `clearDrive()` → `void`；[绑定实现 L579](../../../../src/modules/physics/Physics.cpp#L579)。
- `getCrankPinJoint()` → `Joint2D`；[绑定实现 L572](../../../../src/modules/physics/Physics.cpp#L572)。
- `getRatchetDirection()` → `int`；[绑定实现 L582](../../../../src/modules/physics/Physics.cpp#L582)。
- `getRatchetEngagementTorque()` → `float`；[绑定实现 L584](../../../../src/modules/physics/Physics.cpp#L584)。
- `getSliderJoint()` → `Joint2D`；[绑定实现 L574](../../../../src/modules/physics/Physics.cpp#L574)。
- `getSliderPinJoint()` → `Joint2D`；[绑定实现 L573](../../../../src/modules/physics/Physics.cpp#L573)。
- `isDriveEnabled()` → `bool`；[绑定实现 L580](../../../../src/modules/physics/Physics.cpp#L580)。
- `isValid()` → `bool`；[绑定实现 L570](../../../../src/modules/physics/Physics.cpp#L570)。
- `setRatchetDirection(direction: int)` → `void`；[绑定实现 L581](../../../../src/modules/physics/Physics.cpp#L581)。
- `setRatchetEngagementTorque(torque: float)` → `void`；[绑定实现 L583](../../../../src/modules/physics/Physics.cpp#L583)。

## Mechanism3D

- `clearDrive()` → `void`；[绑定实现 L819](../../../../src/modules/physics/Physics.cpp#L819)。
- `getCrankPinJoint()` → `Joint3D`；[绑定实现 L812](../../../../src/modules/physics/Physics.cpp#L812)。
- `getRatchetDirection()` → `int`；[绑定实现 L822](../../../../src/modules/physics/Physics.cpp#L822)。
- `getRatchetEngagementTorque()` → `float`；[绑定实现 L824](../../../../src/modules/physics/Physics.cpp#L824)。
- `getSliderJoint()` → `Joint3D`；[绑定实现 L814](../../../../src/modules/physics/Physics.cpp#L814)。
- `getSliderPinJoint()` → `Joint3D`；[绑定实现 L813](../../../../src/modules/physics/Physics.cpp#L813)。
- `isDriveEnabled()` → `bool`；[绑定实现 L820](../../../../src/modules/physics/Physics.cpp#L820)。
- `isValid()` → `bool`；[绑定实现 L810](../../../../src/modules/physics/Physics.cpp#L810)。
- `setRatchetDirection(direction: int)` → `void`；[绑定实现 L821](../../../../src/modules/physics/Physics.cpp#L821)。
- `setRatchetEngagementTorque(torque: float)` → `void`；[绑定实现 L823](../../../../src/modules/physics/Physics.cpp#L823)。

## Shape3D

- `areHitEventsEnabled()` → `bool`；[绑定实现 L856](../../../../src/modules/physics/Physics.cpp#L856)。
- `getBoxDepth()` → `float`；[绑定实现 L947](../../../../src/modules/physics/Physics.cpp#L947)。
- `getBoxHeight()` → `float`；[绑定实现 L946](../../../../src/modules/physics/Physics.cpp#L946)。
- `getCategoryBits()` → `dynamic`；[绑定实现 L985](../../../../src/modules/physics/Physics.cpp#L985)。
- `getDensity()` → `float`；[绑定实现 L981](../../../../src/modules/physics/Physics.cpp#L981)。
- `getFriction()` → `float`；[绑定实现 L963](../../../../src/modules/physics/Physics.cpp#L963)。
- `getFrictionCombineMode()` → `string`；[绑定实现 L977](../../../../src/modules/physics/Physics.cpp#L977)。
- `getGroupIndex()` → `int`；[绑定实现 L995](../../../../src/modules/physics/Physics.cpp#L995)。
- `getHeightFieldCellSizeZ()` → `float`；[绑定实现 L942](../../../../src/modules/physics/Physics.cpp#L942)。
- `getHeightFieldCountZ()` → `int`；[绑定实现 L940](../../../../src/modules/physics/Physics.cpp#L940)。
- `getHeightFieldGlobalMax()` → `float`；[绑定实现 L944](../../../../src/modules/physics/Physics.cpp#L944)。
- `getLocalRotW()` → `float`；[绑定实现 L959](../../../../src/modules/physics/Physics.cpp#L959)。
- `getLocalRotX()` → `float`；[绑定实现 L956](../../../../src/modules/physics/Physics.cpp#L956)。
- `getLocalRotY()` → `float`；[绑定实现 L957](../../../../src/modules/physics/Physics.cpp#L957)。
- `getLocalRotZ()` → `float`；[绑定实现 L958](../../../../src/modules/physics/Physics.cpp#L958)。
- `getLocalX()` → `float`；[绑定实现 L953](../../../../src/modules/physics/Physics.cpp#L953)。
- `getLocalY()` → `float`；[绑定实现 L954](../../../../src/modules/physics/Physics.cpp#L954)。
- `getLocalZ()` → `float`；[绑定实现 L955](../../../../src/modules/physics/Physics.cpp#L955)。
- `getMaskBits()` → `dynamic`；[绑定实现 L991](../../../../src/modules/physics/Physics.cpp#L991)。
- `getMaterialId()` → `int`；[绑定实现 L973](../../../../src/modules/physics/Physics.cpp#L973)。
- `getRestitution()` → `float`；[绑定实现 L965](../../../../src/modules/physics/Physics.cpp#L965)。
- `getRestitutionCombineMode()` → `string`；[绑定实现 L979](../../../../src/modules/physics/Physics.cpp#L979)。
- `getRollingResistance()` → `float`；[绑定实现 L967](../../../../src/modules/physics/Physics.cpp#L967)。
- `getTag()` → `int`；[绑定实现 L854](../../../../src/modules/physics/Physics.cpp#L854)。
- `getTangentVelocityX()` → `float`；[绑定实现 L969](../../../../src/modules/physics/Physics.cpp#L969)。
- `getTangentVelocityY()` → `float`；[绑定实现 L970](../../../../src/modules/physics/Physics.cpp#L970)。
- `getTangentVelocityZ()` → `float`；[绑定实现 L971](../../../../src/modules/physics/Physics.cpp#L971)。
- `getTriangleMeshMaterialCount()` → `int`；[绑定实现 L912](../../../../src/modules/physics/Physics.cpp#L912)。
- `getTriangleMeshMaterialFriction(slot: int)` → `float`；[绑定实现 L915](../../../../src/modules/physics/Physics.cpp#L915)。
- `getTriangleMeshMaterialId(slot: int)` → `int`；[绑定实现 L921](../../../../src/modules/physics/Physics.cpp#L921)。
- `getTriangleMeshMaterialIndex(triangleIndex: int)` → `int`；[绑定实现 L910](../../../../src/modules/physics/Physics.cpp#L910)。
- `getTriangleMeshMaterialRestitution(slot: int)` → `float`；[绑定实现 L917](../../../../src/modules/physics/Physics.cpp#L917)。
- `getTriangleMeshMaterialRollingResistance(slot: int)` → `float`；[绑定实现 L919](../../../../src/modules/physics/Physics.cpp#L919)。
- `isSensor()` → `bool`；[绑定实现 L961](../../../../src/modules/physics/Physics.cpp#L961)。
- `setDensity(density: float)` → `void`；[绑定实现 L980](../../../../src/modules/physics/Physics.cpp#L980)。

## SoftBody3D

- `clearBounds()` → `void`；[绑定实现 L1025](../../../../src/modules/physics/Physics.cpp#L1025)。
- `getCollideWorld()` → `World3D`；[绑定实现 L1037](../../../../src/modules/physics/Physics.cpp#L1037)。
- `getDamping()` → `float`；[绑定实现 L1012](../../../../src/modules/physics/Physics.cpp#L1012)。
- `getGrabIndex()` → `int`；[绑定实现 L1033](../../../../src/modules/physics/Physics.cpp#L1033)。
- `getGravityX()` → `float`；[绑定实现 L1004](../../../../src/modules/physics/Physics.cpp#L1004)。
- `getGravityY()` → `float`；[绑定实现 L1005](../../../../src/modules/physics/Physics.cpp#L1005)。
- `getGravityZ()` → `float`；[绑定实现 L1006](../../../../src/modules/physics/Physics.cpp#L1006)。
- `getIterations()` → `int`；[绑定实现 L1010](../../../../src/modules/physics/Physics.cpp#L1010)。
- `getParticleCount()` → `int`；[绑定实现 L1042](../../../../src/modules/physics/Physics.cpp#L1042)。
- `getParticleMass()` → `float`；[绑定实现 L1016](../../../../src/modules/physics/Physics.cpp#L1016)。
- `getParticleY(index: int)` → `float`；[绑定实现 L1044](../../../../src/modules/physics/Physics.cpp#L1044)。
- `getParticleZ(index: int)` → `float`；[绑定实现 L1045](../../../../src/modules/physics/Physics.cpp#L1045)。
- `getSelfCollision()` → `bool`；[绑定实现 L1023](../../../../src/modules/physics/Physics.cpp#L1023)。
- `isGrabbing()` → `bool`；[绑定实现 L1032](../../../../src/modules/physics/Physics.cpp#L1032)。
- `isPinned(index: int)` → `bool`；[绑定实现 L1028](../../../../src/modules/physics/Physics.cpp#L1028)。
- `setParticlePosition(index: int, x: float, y: float, z: float)` → `void`；[绑定实现 L1046](../../../../src/modules/physics/Physics.cpp#L1046)。

## World

- `destroyBody(body: Body)` → `void`；[绑定实现 L112](../../../../src/modules/physics/Physics.cpp#L112)。
- `getBeginContactBodyAId(index: int)` → `int`；[绑定实现 L127](../../../../src/modules/physics/Physics.cpp#L127)。
- `getBeginContactBodyBId(index: int)` → `int`；[绑定实现 L128](../../../../src/modules/physics/Physics.cpp#L128)。
- `getBeginContactFixtureATag(index: int)` → `string`；[绑定实现 L129](../../../../src/modules/physics/Physics.cpp#L129)。
- `getBeginContactFixtureBTag(index: int)` → `string`；[绑定实现 L130](../../../../src/modules/physics/Physics.cpp#L130)。
- `getEndContactBodyAId(index: int)` → `int`；[绑定实现 L132](../../../../src/modules/physics/Physics.cpp#L132)。
- `getEndContactBodyBId(index: int)` → `int`；[绑定实现 L133](../../../../src/modules/physics/Physics.cpp#L133)。
- `getEndContactFixtureATag(index: int)` → `string`；[绑定实现 L134](../../../../src/modules/physics/Physics.cpp#L134)。
- `getEndContactFixtureBTag(index: int)` → `string`；[绑定实现 L135](../../../../src/modules/physics/Physics.cpp#L135)。
- `getGravityX()` → `float`；[绑定实现 L97](../../../../src/modules/physics/Physics.cpp#L97)。
- `getGravityY()` → `float`；[绑定实现 L98](../../../../src/modules/physics/Physics.cpp#L98)。
- `getImpactBodyAId(index: int)` → `int`；[绑定实现 L137](../../../../src/modules/physics/Physics.cpp#L137)。
- `getImpactBodyBId(index: int)` → `int`；[绑定实现 L138](../../../../src/modules/physics/Physics.cpp#L138)。
- `getImpactCount()` → `int`；[绑定实现 L136](../../../../src/modules/physics/Physics.cpp#L136)。
- `getImpactFixtureATag(index: int)` → `string`；[绑定实现 L139](../../../../src/modules/physics/Physics.cpp#L139)。
- `getImpactFixtureBTag(index: int)` → `string`；[绑定实现 L140](../../../../src/modules/physics/Physics.cpp#L140)。
- `getImpactNormalImpulse(index: int)` → `float`；[绑定实现 L146](../../../../src/modules/physics/Physics.cpp#L146)。
- `getImpactNormalX(index: int)` → `float`；[绑定实现 L143](../../../../src/modules/physics/Physics.cpp#L143)。
- `getImpactNormalY(index: int)` → `float`；[绑定实现 L144](../../../../src/modules/physics/Physics.cpp#L144)。
- `getImpactPointX(index: int)` → `float`；[绑定实现 L141](../../../../src/modules/physics/Physics.cpp#L141)。
- `getImpactPointY(index: int)` → `float`；[绑定实现 L142](../../../../src/modules/physics/Physics.cpp#L142)。
- `getImpactRelativeNormalSpeed(index: int)` → `float`；[绑定实现 L145](../../../../src/modules/physics/Physics.cpp#L145)。
- `getImpactTangentImpulse(index: int)` → `float`；[绑定实现 L147](../../../../src/modules/physics/Physics.cpp#L147)。
- `getMeter()` → `float`；[绑定实现 L100](../../../../src/modules/physics/Physics.cpp#L100)。
- `getRayHitBodyId()` → `int`；[绑定实现 L117](../../../../src/modules/physics/Physics.cpp#L117)。
- `getRayHitFraction()` → `float`；[绑定实现 L122](../../../../src/modules/physics/Physics.cpp#L122)。
- `getRayHitNormalX()` → `float`；[绑定实现 L120](../../../../src/modules/physics/Physics.cpp#L120)。
- `getRayHitNormalY()` → `float`；[绑定实现 L121](../../../../src/modules/physics/Physics.cpp#L121)。
- `getRayHitX()` → `float`；[绑定实现 L118](../../../../src/modules/physics/Physics.cpp#L118)。
- `getRayHitY()` → `float`；[绑定实现 L119](../../../../src/modules/physics/Physics.cpp#L119)。
- `hasRayHit()` → `bool`；[绑定实现 L116](../../../../src/modules/physics/Physics.cpp#L116)。
- `updateFull(dt: float, velocityIterations: int, positionIterations: int)` → `void`；[绑定实现 L95](../../../../src/modules/physics/Physics.cpp#L95)。

## World3D

- `destroyBody(body: Body3D)` → `void`；[绑定实现 L229](../../../../src/modules/physics/Physics.cpp#L229)。
- `getAwakeBodyCount()` → `int`；[绑定实现 L204](../../../../src/modules/physics/Physics.cpp#L204)。
- `getAwakeContactCount()` → `int`；[绑定实现 L205](../../../../src/modules/physics/Physics.cpp#L205)。
- `getBeginContactBodyAId(index: int)` → `int`；[绑定实现 L327](../../../../src/modules/physics/Physics.cpp#L327)。
- `getBeginContactBodyBId(index: int)` → `int`；[绑定实现 L328](../../../../src/modules/physics/Physics.cpp#L328)。
- `getBeginContactShapeAId(index: int)` → `int`；[绑定实现 L329](../../../../src/modules/physics/Physics.cpp#L329)。
- `getBeginContactShapeATag(index: int)` → `int`；[绑定实现 L331](../../../../src/modules/physics/Physics.cpp#L331)。
- `getBeginContactShapeBId(index: int)` → `int`；[绑定实现 L330](../../../../src/modules/physics/Physics.cpp#L330)。
- `getBeginContactShapeBTag(index: int)` → `int`；[绑定实现 L332](../../../../src/modules/physics/Physics.cpp#L332)。
- `getBeginTriggerSensorBodyId(index: int)` → `int`；[绑定实现 L341](../../../../src/modules/physics/Physics.cpp#L341)。
- `getBeginTriggerSensorShapeId(index: int)` → `int`；[绑定实现 L343](../../../../src/modules/physics/Physics.cpp#L343)。
- `getBeginTriggerSensorShapeTag(index: int)` → `int`；[绑定实现 L345](../../../../src/modules/physics/Physics.cpp#L345)。
- `getBeginTriggerVisitorBodyId(index: int)` → `int`；[绑定实现 L342](../../../../src/modules/physics/Physics.cpp#L342)。
- `getBeginTriggerVisitorShapeId(index: int)` → `int`；[绑定实现 L344](../../../../src/modules/physics/Physics.cpp#L344)。
- `getBeginTriggerVisitorShapeTag(index: int)` → `int`；[绑定实现 L346](../../../../src/modules/physics/Physics.cpp#L346)。
- `getBoundsMaxX()` → `float`；[绑定实现 L196](../../../../src/modules/physics/Physics.cpp#L196)。
- `getBoundsMaxY()` → `float`；[绑定实现 L197](../../../../src/modules/physics/Physics.cpp#L197)。
- `getBoundsMaxZ()` → `float`；[绑定实现 L198](../../../../src/modules/physics/Physics.cpp#L198)。
- `getBoundsMinX()` → `float`；[绑定实现 L193](../../../../src/modules/physics/Physics.cpp#L193)。
- `getBoundsMinY()` → `float`；[绑定实现 L194](../../../../src/modules/physics/Physics.cpp#L194)。
- `getBoundsMinZ()` → `float`；[绑定实现 L195](../../../../src/modules/physics/Physics.cpp#L195)。
- `getClosestBodyId()` → `int`；[绑定实现 L271](../../../../src/modules/physics/Physics.cpp#L271)。
- `getClosestNormalX()` → `float`；[绑定实现 L277](../../../../src/modules/physics/Physics.cpp#L277)。
- `getClosestNormalY()` → `float`；[绑定实现 L278](../../../../src/modules/physics/Physics.cpp#L278)。
- `getClosestNormalZ()` → `float`；[绑定实现 L279](../../../../src/modules/physics/Physics.cpp#L279)。
- `getClosestShapeTag()` → `int`；[绑定实现 L273](../../../../src/modules/physics/Physics.cpp#L273)。
- `getClosestY()` → `float`；[绑定实现 L275](../../../../src/modules/physics/Physics.cpp#L275)。
- `getClosestZ()` → `float`；[绑定实现 L276](../../../../src/modules/physics/Physics.cpp#L276)。
- `getContactDampingRatio()` → `float`；[绑定实现 L170](../../../../src/modules/physics/Physics.cpp#L170)。
- `getContactHertz()` → `float`；[绑定实现 L169](../../../../src/modules/physics/Physics.cpp#L169)。
- `getContactPointCount()` → `int`；[绑定实现 L356](../../../../src/modules/physics/Physics.cpp#L356)。
- `getContactPointNormalZ(index: int)` → `float`；[绑定实现 L367](../../../../src/modules/physics/Physics.cpp#L367)。
- `getContactPointShapeTag(index: int)` → `int`；[绑定实现 L358](../../../../src/modules/physics/Physics.cpp#L358)。
- `getContactPointY(index: int)` → `float`；[绑定实现 L363](../../../../src/modules/physics/Physics.cpp#L363)。
- `getContactPointZ(index: int)` → `float`；[绑定实现 L364](../../../../src/modules/physics/Physics.cpp#L364)。
- `getContactPushOutSpeed()` → `float`；[绑定实现 L171](../../../../src/modules/physics/Physics.cpp#L171)。
- `getContactRecycleDistance()` → `float`；[绑定实现 L173](../../../../src/modules/physics/Physics.cpp#L173)。
- `getDynamicTreeHeight()` → `int`；[绑定实现 L208](../../../../src/modules/physics/Physics.cpp#L208)。
- `getEndContactBodyAId(index: int)` → `int`；[绑定实现 L334](../../../../src/modules/physics/Physics.cpp#L334)。
- `getEndContactBodyBId(index: int)` → `int`；[绑定实现 L335](../../../../src/modules/physics/Physics.cpp#L335)。
- `getEndContactShapeAId(index: int)` → `int`；[绑定实现 L336](../../../../src/modules/physics/Physics.cpp#L336)。
- `getEndContactShapeATag(index: int)` → `int`；[绑定实现 L338](../../../../src/modules/physics/Physics.cpp#L338)。
- `getEndContactShapeBId(index: int)` → `int`；[绑定实现 L337](../../../../src/modules/physics/Physics.cpp#L337)。
- `getEndContactShapeBTag(index: int)` → `int`；[绑定实现 L339](../../../../src/modules/physics/Physics.cpp#L339)。
- `getEndTriggerSensorBodyId(index: int)` → `int`；[绑定实现 L348](../../../../src/modules/physics/Physics.cpp#L348)。
- `getEndTriggerSensorShapeId(index: int)` → `int`；[绑定实现 L350](../../../../src/modules/physics/Physics.cpp#L350)。
- `getEndTriggerSensorShapeTag(index: int)` → `int`；[绑定实现 L352](../../../../src/modules/physics/Physics.cpp#L352)。
- `getEndTriggerVisitorBodyId(index: int)` → `int`；[绑定实现 L349](../../../../src/modules/physics/Physics.cpp#L349)。
- `getEndTriggerVisitorShapeId(index: int)` → `int`；[绑定实现 L351](../../../../src/modules/physics/Physics.cpp#L351)。
- `getEndTriggerVisitorShapeTag(index: int)` → `int`；[绑定实现 L353](../../../../src/modules/physics/Physics.cpp#L353)。
- `getExplosionResultCount()` → `int`；[绑定实现 L179](../../../../src/modules/physics/Physics.cpp#L179)。
- `getExplosionResultDeltaAngularVelocityX(index: int)` → `float`；[绑定实现 L187](../../../../src/modules/physics/Physics.cpp#L187)。
- `getExplosionResultDeltaAngularVelocityY(index: int)` → `float`；[绑定实现 L189](../../../../src/modules/physics/Physics.cpp#L189)。
- `getExplosionResultDeltaAngularVelocityZ(index: int)` → `float`；[绑定实现 L191](../../../../src/modules/physics/Physics.cpp#L191)。
- `getExplosionResultDeltaVelocityY(index: int)` → `float`；[绑定实现 L183](../../../../src/modules/physics/Physics.cpp#L183)。
- `getExplosionResultDeltaVelocityZ(index: int)` → `float`；[绑定实现 L185](../../../../src/modules/physics/Physics.cpp#L185)。
- `getGravityX()` → `float`；[绑定实现 L155](../../../../src/modules/physics/Physics.cpp#L155)。
- `getGravityY()` → `float`；[绑定实现 L156](../../../../src/modules/physics/Physics.cpp#L156)。
- `getGravityZ()` → `float`；[绑定实现 L157](../../../../src/modules/physics/Physics.cpp#L157)。
- `getHitBodyAId(index: int)` → `int`；[绑定实现 L374](../../../../src/modules/physics/Physics.cpp#L374)。
- `getHitBodyBId(index: int)` → `int`；[绑定实现 L375](../../../../src/modules/physics/Physics.cpp#L375)。
- `getHitEventThreshold()` → `float`；[绑定实现 L163](../../../../src/modules/physics/Physics.cpp#L163)。
- `getHitNormalY(index: int)` → `float`；[绑定实现 L384](../../../../src/modules/physics/Physics.cpp#L384)。
- `getHitNormalZ(index: int)` → `float`；[绑定实现 L385](../../../../src/modules/physics/Physics.cpp#L385)。
- `getHitPointY(index: int)` → `float`；[绑定实现 L381](../../../../src/modules/physics/Physics.cpp#L381)。
- `getHitPointZ(index: int)` → `float`；[绑定实现 L382](../../../../src/modules/physics/Physics.cpp#L382)。
- `getHitShapeAId(index: int)` → `int`；[绑定实现 L376](../../../../src/modules/physics/Physics.cpp#L376)。
- `getHitShapeATag(index: int)` → `int`；[绑定实现 L378](../../../../src/modules/physics/Physics.cpp#L378)。
- `getHitShapeBId(index: int)` → `int`；[绑定实现 L377](../../../../src/modules/physics/Physics.cpp#L377)。
- `getHitShapeBTag(index: int)` → `int`；[绑定实现 L379](../../../../src/modules/physics/Physics.cpp#L379)。
- `getIslandCount()` → `int`；[绑定实现 L203](../../../../src/modules/physics/Physics.cpp#L203)。
- `getJointCount()` → `int`；[绑定实现 L202](../../../../src/modules/physics/Physics.cpp#L202)。
- `getJointStressBodyAId(index: int)` → `int`；[绑定实现 L390](../../../../src/modules/physics/Physics.cpp#L390)。
- `getJointStressBodyBId(index: int)` → `int`；[绑定实现 L391](../../../../src/modules/physics/Physics.cpp#L391)。
- `getJointStressKind(index: int)` → `int`；[绑定实现 L392](../../../../src/modules/physics/Physics.cpp#L392)。
- `getJointStressTorqueY(index: int)` → `float`；[绑定实现 L397](../../../../src/modules/physics/Physics.cpp#L397)。
- `getJointStressTorqueZ(index: int)` → `float`；[绑定实现 L398](../../../../src/modules/physics/Physics.cpp#L398)。
- `getMaximumLinearSpeed()` → `float`；[绑定实现 L175](../../../../src/modules/physics/Physics.cpp#L175)。
- `getMoverNormalY()` → `float`；[绑定实现 L313](../../../../src/modules/physics/Physics.cpp#L313)。
- `getMoverNormalZ()` → `float`；[绑定实现 L314](../../../../src/modules/physics/Physics.cpp#L314)。
- `getQueryCategoryBits()` → `int`；[绑定实现 L401](../../../../src/modules/physics/Physics.cpp#L401)。
- `getQueryIgnoredBodyId()` → `int`；[绑定实现 L405](../../../../src/modules/physics/Physics.cpp#L405)。
- `getQueryIgnoredShapeId()` → `int`；[绑定实现 L406](../../../../src/modules/physics/Physics.cpp#L406)。
- `getQueryMaskBits()` → `int`；[绑定实现 L402](../../../../src/modules/physics/Physics.cpp#L402)。
- `getRayHitBodyId()` → `int`；[绑定实现 L234](../../../../src/modules/physics/Physics.cpp#L234)。
- `getRayHitFraction()` → `float`；[绑定实现 L245](../../../../src/modules/physics/Physics.cpp#L245)。
- `getRayHitNormalX()` → `float`；[绑定实现 L242](../../../../src/modules/physics/Physics.cpp#L242)。
- `getRayHitNormalY()` → `float`；[绑定实现 L243](../../../../src/modules/physics/Physics.cpp#L243)。
- `getRayHitNormalZ()` → `float`；[绑定实现 L244](../../../../src/modules/physics/Physics.cpp#L244)。
- `getRayHitShapeTag()` → `int`；[绑定实现 L236](../../../../src/modules/physics/Physics.cpp#L236)。
- `getRayHitX()` → `float`；[绑定实现 L239](../../../../src/modules/physics/Physics.cpp#L239)。
- `getRayHitY()` → `float`；[绑定实现 L240](../../../../src/modules/physics/Physics.cpp#L240)。
- `getRayHitZ()` → `float`；[绑定实现 L241](../../../../src/modules/physics/Physics.cpp#L241)。
- `getRayResultNormalY(index: int)` → `float`；[绑定实现 L256](../../../../src/modules/physics/Physics.cpp#L256)。
- `getRayResultNormalZ(index: int)` → `float`；[绑定实现 L257](../../../../src/modules/physics/Physics.cpp#L257)。
- `getRayResultY(index: int)` → `float`；[绑定实现 L253](../../../../src/modules/physics/Physics.cpp#L253)。
- `getRayResultZ(index: int)` → `float`；[绑定实现 L254](../../../../src/modules/physics/Physics.cpp#L254)。
- `getRecycledContactCount()` → `int`；[绑定实现 L206](../../../../src/modules/physics/Physics.cpp#L206)。
- `getRestitutionThreshold()` → `float`；[绑定实现 L161](../../../../src/modules/physics/Physics.cpp#L161)。
- `getShapeCastBodyId()` → `int`；[绑定实现 L282](../../../../src/modules/physics/Physics.cpp#L282)。
- `getShapeCastNormalY()` → `float`；[绑定实现 L291](../../../../src/modules/physics/Physics.cpp#L291)。
- `getShapeCastNormalZ()` → `float`；[绑定实现 L292](../../../../src/modules/physics/Physics.cpp#L292)。
- `getShapeCastResultNormalY(index: int)` → `float`；[绑定实现 L305](../../../../src/modules/physics/Physics.cpp#L305)。
- `getShapeCastResultNormalZ(index: int)` → `float`；[绑定实现 L306](../../../../src/modules/physics/Physics.cpp#L306)。
- `getShapeCastResultY(index: int)` → `float`；[绑定实现 L302](../../../../src/modules/physics/Physics.cpp#L302)。
- `getShapeCastResultZ(index: int)` → `float`；[绑定实现 L303](../../../../src/modules/physics/Physics.cpp#L303)。
- `getShapeCastY()` → `float`；[绑定实现 L288](../../../../src/modules/physics/Physics.cpp#L288)。
- `getShapeCastZ()` → `float`；[绑定实现 L289](../../../../src/modules/physics/Physics.cpp#L289)。
- `getShapeCount()` → `int`；[绑定实现 L200](../../../../src/modules/physics/Physics.cpp#L200)。
- `getStaticTreeHeight()` → `int`；[绑定实现 L207](../../../../src/modules/physics/Physics.cpp#L207)。
- `hasClosestPoint()` → `bool`；[绑定实现 L270](../../../../src/modules/physics/Physics.cpp#L270)。
- `hasRayHit()` → `bool`；[绑定实现 L233](../../../../src/modules/physics/Physics.cpp#L233)。
- `hasShapeCastHit()` → `bool`；[绑定实现 L281](../../../../src/modules/physics/Physics.cpp#L281)。
- `isContinuousCollisionEnabled()` → `bool`；[绑定实现 L159](../../../../src/modules/physics/Physics.cpp#L159)。
- `isWarmStartingEnabled()` → `bool`；[绑定实现 L177](../../../../src/modules/physics/Physics.cpp#L177)。
- `updateFull(dt: float, subStepCount: int)` → `void`；[绑定实现 L153](../../../../src/modules/physics/Physics.cpp#L153)。

## 接收者见绑定源码

- `getMeter()` → `float`；[绑定实现 L1096](../../../../src/modules/physics/Physics.cpp#L1096)。
- `getName()` → `string`；[绑定实现 L1094](../../../../src/modules/physics/Physics.cpp#L1094)。


重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，
再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。
