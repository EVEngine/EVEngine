// FlyingController — sky flight on Body3D with thrust + WingModel lift/drag.
// Authority is Box3D; this class maps pitch/roll/yaw/throttle into forces/torques.
// Chord/normal are rotated from body quaternion each step (sampleWingForce path).

class FlyingController {
    world = null;
    body = null;
    wing = null;
    wind = null;
    ready = false;
    throttle = 0.0;
    pitchCmd = 0.0;
    rollCmd = 0.0;
    yawCmd = 0.0;
    thrustForce = 900.0;
    pitchTorque = 900.0;
    rollTorque = 1200.0;
    yawTorque = 600.0;
    wingArea = 8.0;
    maxSpeed = 55.0;
    spawnX = 0.0;
    spawnY = 18.0;
    spawnZ = 0.0;

    function clampf(v, lo, hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    function checked(result, context) {
        if (!result.ok) throw context + ": " + result.status.summary;
        return "value" in result ? result.value : null;
    }

    /** @brief Rotate a local vector by body quaternion (qx,qy,qz,qw). */
    function rotateLocal(qx, qy, qz, qw, vx, vy, vz) {
        local ix = qw * vx + qy * vz - qz * vy;
        local iy = qw * vy + qz * vx - qx * vz;
        local iz = qw * vz + qx * vy - qy * vx;
        local iw = -qx * vx - qy * vy - qz * vz;
        return {
            x = ix * qw + iw * (-qx) + iy * (-qz) - iz * (-qy),
            y = iy * qw + iw * (-qy) + iz * (-qx) - ix * (-qz),
            z = iz * qw + iw * (-qz) + ix * (-qy) - iy * (-qx)
        };
    }

    function setup(physicsWorld3D) {
        world = physicsWorld3D;
        wing = eve.WingModel();
        wing.area = wingArea;
        wing.density = 1.225;
        wing.aspectRatio = 7.5;
        wing.efficiency = 0.85;
        wing.stallAngle = 0.35;
        wind = eve.ForceVector();
        ready = true;
        return this;
    }

    function spawn(x, y, z) {
        if (!ready) throw "FlyingController: call setup() before spawn()";
        spawnX = x; spawnY = y; spawnZ = z;
        if (body != null) {
            // Leave the old body in place; create a fresh craft for a clean velocity state.
            body = null;
        }
        body = world.newBody("dynamic", x, y, z);
        body.newBoxShape(3.6, 0.55, 4.8, 1.0, 0.2, 0.05);
        body.setLinearDamping(0.35);
        body.setAngularDamping(0.55);
        body.setGravityScale(1.0);
        // Nose along +X, level flight.
        body.setRotation(0.0, 0.0, 0.0, 1.0);
        body.setLinearVelocity(28.0, 0.0, 0.0);
        body.setAngularVelocity(0.0, 0.0, 0.0);
        setControls(0.55, 0.0, 0.0, 0.0);
        return this;
    }

    function setControls(throttleIn, pitchIn, rollIn, yawIn) {
        throttle = clampf(throttleIn, 0.0, 1.0);
        pitchCmd = clampf(pitchIn, -1.0, 1.0);
        rollCmd = clampf(rollIn, -1.0, 1.0);
        yawCmd = clampf(yawIn, -1.0, 1.0);
    }

    /** @brief WASD throttle/yaw, arrows pitch/roll, Q/E fine yaw. */
    function sampleKeyboard() {
        local thr = throttle;
        local pitch = 0.0;
        local roll = 0.0;
        local yaw = 0.0;
        if (keyboard.isDown("w")) thr = clampf(thr + 0.02, 0.0, 1.0);
        if (keyboard.isDown("s")) thr = clampf(thr - 0.02, 0.0, 1.0);
        if (keyboard.isDown("up")) pitch = 1.0;
        if (keyboard.isDown("down")) pitch = -1.0;
        if (keyboard.isDown("left") || keyboard.isDown("a")) roll = -1.0;
        if (keyboard.isDown("right") || keyboard.isDown("d")) roll = 1.0;
        if (keyboard.isDown("q")) yaw = -1.0;
        if (keyboard.isDown("e")) yaw = 1.0;
        setControls(thr, pitch, roll, yaw);
        return { throttle = throttle, pitch = pitchCmd, roll = rollCmd, yaw = yawCmd };
    }

    function cruiseHold() {
        // Gentle auto-level cruise while the player drives the car.
        local pitch = 0.0;
        if (body != null) {
            local alt = body.getY();
            if (alt < 14.0) pitch = 0.45;
            else if (alt > 30.0) pitch = -0.25;
        }
        setControls(0.62, pitch, 0.0, 0.08);
    }

    function applyAeroAndThrust() {
        local qx = body.getRotX();
        local qy = body.getRotY();
        local qz = body.getRotZ();
        local qw = body.getRotW();
        local forward = rotateLocal(qx, qy, qz, qw, 1.0, 0.0, 0.0);
        local up = rotateLocal(qx, qy, qz, qw, 0.0, 1.0, 0.0);
        local right = rotateLocal(qx, qy, qz, qw, 0.0, 0.0, 1.0);

        // Thrust along body forward.
        local thrust = throttle * thrustForce;
        body.applyForce(forward.x * thrust, forward.y * thrust, forward.z * thrust);

        // Control torques in body axes → world.
        local tx = right.x * pitchCmd * pitchTorque + up.x * yawCmd * yawTorque + forward.x * rollCmd * rollTorque;
        local ty = right.y * pitchCmd * pitchTorque + up.y * yawCmd * yawTorque + forward.y * rollCmd * rollTorque;
        local tz = right.z * pitchCmd * pitchTorque + up.z * yawCmd * yawTorque + forward.z * rollCmd * rollTorque;
        body.applyTorque(tx, ty, tz);

        // Wing lift/drag from relative airspeed (body velocity − wind).
        local chord = eve.ForceVector();
        chord.x = forward.x; chord.y = forward.y; chord.z = forward.z;
        local normal = eve.ForceVector();
        normal.x = up.x; normal.y = up.y; normal.z = up.z;
        wing.chord = chord;
        wing.normal = normal;
        local air = eve.ForceVector();
        air.x = body.getLinearVelocityX() - wind.x;
        air.y = body.getLinearVelocityY() - wind.y;
        air.z = body.getLinearVelocityZ() - wind.z;
        local sample = eve.sampleWingForce(wing, air);
        if (sample.ok) {
            local f = sample.value.force;
            checked(eve.applyForceSI3D(body, f.x, f.y, f.z, body.getX(), body.getY(), body.getZ()),
                "FlyingController wing force");
        }
    }

    /** @brief Apply controls/aero then step the 3D world once. */
    function step(dt) {
        if (body == null) return;
        applyAeroAndThrust();
        world.update(dt);
        // Soft floor — bounce back into the sky if the craft kisses the ground.
        if (body.getY() < 2.5) {
            body.setPosition(body.getX(), 2.5, body.getZ());
            local vy = body.getLinearVelocityY();
            if (vy < 0.0) body.setLinearVelocity(body.getLinearVelocityX(), -vy * 0.35, body.getLinearVelocityZ());
        }
        // Arcade speed clamp so thrust + lift cannot run away.
        local vx = body.getLinearVelocityX();
        local vy = body.getLinearVelocityY();
        local vz = body.getLinearVelocityZ();
        local speed = sqrt(vx * vx + vy * vy + vz * vz);
        if (speed > maxSpeed && speed > 0.001) {
            local s = maxSpeed / speed;
            body.setLinearVelocity(vx * s, vy * s, vz * s);
        }
    }

    function pose() {
        if (body == null)
            return { x = 0.0, y = 0.0, z = 0.0, yaw = 0.0, pitch = 0.0, roll = 0.0, speed = 0.0, altitude = 0.0 };
        local qx = body.getRotX();
        local qy = body.getRotY();
        local qz = body.getRotZ();
        local qw = body.getRotW();
        local vx = body.getLinearVelocityX();
        local vy = body.getLinearVelocityY();
        local vz = body.getLinearVelocityZ();
        local yaw = atan2(2.0 * (qw * qy + qx * qz), 1.0 - 2.0 * (qx * qx + qy * qy));
        local pitch = asin(clampf(2.0 * (qw * qx - qy * qz), -1.0, 1.0));
        local roll = atan2(2.0 * (qw * qz + qx * qy), 1.0 - 2.0 * (qx * qx + qz * qz));
        return {
            x = body.getX(),
            y = body.getY(),
            z = body.getZ(),
            yaw = yaw,
            pitch = pitch,
            roll = roll,
            speed = sqrt(vx * vx + vy * vy + vz * vz),
            altitude = body.getY()
        };
    }

    function reset(x = null, y = null, z = null) {
        local sx = x == null ? spawnX : x;
        local sy = y == null ? spawnY : y;
        local sz = z == null ? spawnZ : z;
        spawn(sx, sy, sz);
    }

    function close() {
        body = null;
        ready = false;
    }
}
