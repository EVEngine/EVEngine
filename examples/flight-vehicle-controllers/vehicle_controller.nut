// VehicleController — game-facing wrapper around eve.Vehicle() wheel mobility.
// Owns definition registration, seat input, fixed-step update order, and XZ pose.
// Physics authority stays in vehicle + Box2D; this class only maps controls and pose.

class VehicleController {
    vehicles = null;
    world = null;
    car = null;
    defId = "controller.car";
    ready = false;
    lastThrottle = 0.0;
    lastSteer = 0.0;
    lastBrake = 0.0;

    function clampf(v, lo, hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    function ensureModules() {
        if (vehicles == null) vehicles = eve.Vehicle();
    }

    /** @brief Bind a Box2D world and register a wheel-car definition.
     *  World XY maps to render XZ (Y-up). Call before spawn. */
    function setup(physicsWorld, definitionId = "controller.car") {
        ensureModules();
        world = physicsWorld;
        defId = definitionId;
        local defs = @"[{""id"":""" + defId + @""",""category"":""car"",""mobility"":""wheel"",""maxSpeed"":28,""accel"":14,""turnRate"":95,""radius"":1.1,""maxHealth"":100}]";
        if (vehicles.registerVehiclesFromJson(defs) < 1)
            throw "VehicleController: definition registration failed for " + defId;
        ready = true;
        return this;
    }

    function spawn(x, z, headingDeg = 0.0, faction = "player") {
        if (!ready) throw "VehicleController: call setup() before spawn()";
        if (car != null) {
            if (vehicles.hasPhysics(car)) vehicles.detachPhysics(car);
            car = null;
        }
        car = vehicles.newVehicle(defId, x, z, headingDeg, faction);
        if (car == null) throw "VehicleController: newVehicle failed";
        if (!vehicles.attachPhysics2D(car, world))
            throw "VehicleController: attachPhysics2D failed";
        setDrive(0.0, 0.0, 1.0);
        return this;
    }

    function setDrive(throttle, steer, brake = 0.0) {
        lastThrottle = clampf(throttle, -1.0, 1.0);
        lastSteer = clampf(steer, -1.0, 1.0);
        lastBrake = clampf(brake, 0.0, 1.0);
    }

    /** @brief Read WASD / arrows / Space into throttle, steer, brake. */
    function sampleKeyboard() {
        local throttle = 0.0;
        local steer = 0.0;
        local brake = 0.0;
        if (keyboard.isDown("w") || keyboard.isDown("up")) throttle = 1.0;
        if (keyboard.isDown("s") || keyboard.isDown("down")) throttle = -0.45;
        if (keyboard.isDown("a") || keyboard.isDown("left")) steer = -1.0;
        if (keyboard.isDown("d") || keyboard.isDown("right")) steer = 1.0;
        if (keyboard.isDown("space") || keyboard.isDown("SPACE")) brake = 1.0;
        setDrive(throttle, steer, brake);
        return { throttle = lastThrottle, steer = lastSteer, brake = lastBrake };
    }

    function park() {
        setDrive(0.0, 0.0, 1.0);
    }

    /** @brief Advance vehicle then physics with the same dt (required order). */
    function step(dt) {
        if (car == null) return;
        vehicles.setInput(car, lastThrottle, lastSteer, lastBrake, false);
        vehicles.update(dt);
        world.update(dt);
        vehicles.clearEvents();
    }

    function pose() {
        if (car == null) return { x = 0.0, y = 0.0, z = 0.0, yaw = 0.0, speed = 0.0 };
        local headingDeg = vehicles.getHeading(car);
        local headingRad = headingDeg * 3.141592653589793 / 180.0;
        return {
            x = vehicles.getX(car),
            y = 0.0,
            z = vehicles.getY(car),
            yaw = -headingRad,
            headingRad = headingRad,
            speed = vehicles.getSpeed(car),
            headingDeg = headingDeg
        };
    }

    function reset(x, z, headingDeg = 0.0) {
        if (car == null) {
            spawn(x, z, headingDeg);
            return;
        }
        vehicles.clearOrders(car);
        setDrive(0.0, 0.0, 1.0);
        vehicles.update(2.1);
        vehicles.detachPhysics(car);
        vehicles.setPosition(car, x, z);
        vehicles.setHeading(car, headingDeg);
        if (!vehicles.attachPhysics2D(car, world))
            throw "VehicleController: reset attachPhysics2D failed";
        setDrive(0.0, 0.0, 1.0);
    }

    function close() {
        if (car != null && vehicles != null && vehicles.hasPhysics(car))
            vehicles.detachPhysics(car);
        car = null;
        ready = false;
    }
}
