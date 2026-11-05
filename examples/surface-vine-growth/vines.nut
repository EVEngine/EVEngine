// Surface-crawling vine tips: stick to World3D geometry, perturb, and fork.
// Returns a table of simulation helpers; the caller owns tips / paths / RNG.

function vineCreateSystem(settings) {
    local s = settings;
    local tips = [];
    local branches = []; // each: { points=[], polyline=null, glow=null, alive=true, depth=0 }
    local seed = s.seed;
    local nextForkBudget = s.maxTips;

    function rand01() {
        seed = (seed * 16807) % 2147483647;
        return seed.tofloat() / 2147483647.0;
    }

    function vlen(x, y, z) {
        return sqrt(x * x + y * y + z * z);
    }

    function vnorm(x, y, z) {
        local L = vlen(x, y, z);
        if (L < 1.0e-6) return [0.0, 1.0, 0.0];
        return [x / L, y / L, z / L];
    }

    function projectTangent(dx, dy, dz, nx, ny, nz) {
        local dn = dx * nx + dy * ny + dz * nz;
        return vnorm(dx - nx * dn, dy - ny * dn, dz - nz * dn);
    }

    function rotateAround(dx, dy, dz, ax, ay, az, angle) {
        local c = cos(angle);
        local si = sin(angle);
        local dot = dx * ax + dy * ay + dz * az;
        local cx = ay * dz - az * dy;
        local cy = az * dx - ax * dz;
        local cz = ax * dy - ay * dx;
        return [
            dx * c + cx * si + ax * dot * (1.0 - c),
            dy * c + cy * si + ay * dot * (1.0 - c),
            dz * c + cz * si + az * dot * (1.0 - c)
        ];
    }

    function appendPoint(branch, x, y, z) {
        local pts = branch.points;
        local n = pts.len();
        if (n >= 3) {
            local px = pts[n - 3];
            local py = pts[n - 2];
            local pz = pts[n - 1];
            local dx = x - px;
            local dy = y - py;
            local dz = z - pz;
            if (dx * dx + dy * dy + dz * dz < s.minPointSpacing * s.minPointSpacing) return false;
        }
        pts.append(x);
        pts.append(y);
        pts.append(z);
        branch.dirty = true;
        return true;
    }

    function newBranch(depth, x, y, z) {
        local branch = {
            points = [],
            polyline = null,
            glow = null,
            alive = true,
            depth = depth,
            dirty = true
        };
        appendPoint(branch, x, y, z);
        branches.append(branch);
        return branch;
    }

    function stick(world, x, y, z, nx, ny, nz, lift, reach) {
        local ox = x + nx * lift;
        local oy = y + ny * lift;
        local oz = z + nz * lift;
        local tx = x - nx * reach;
        local ty = y - ny * reach;
        local tz = z - nz * reach;
        world.rayCast(ox, oy, oz, tx, ty, tz);
        if (!world.hasRayHit()) return null;
        return {
            x = world.getRayHitX(),
            y = world.getRayHitY(),
            z = world.getRayHitZ(),
            nx = world.getRayHitNormalX(),
            ny = world.getRayHitNormalY(),
            nz = world.getRayHitNormalZ()
        };
    }

    function spawnBurst(world, originX, originY, originZ) {
        tips.clear();
        branches.clear();
        nextForkBudget = s.maxTips;
        local count = s.rootCount;
        if (count < 7) count = 7;
        if (count > 10) count = 10;
        local stuck = stick(world, originX, originY + 0.4, originZ, 0.0, 1.0, 0.0, 0.05, 2.0);
        local sx = originX;
        local sy = originY;
        local sz = originZ;
        local snx = 0.0;
        local sny = 1.0;
        local snz = 0.0;
        if (stuck != null) {
            sx = stuck.x;
            sy = stuck.y;
            sz = stuck.z;
            snx = stuck.nx;
            sny = stuck.ny;
            snz = stuck.nz;
            local n = vnorm(snx, sny, snz);
            snx = n[0];
            sny = n[1];
            snz = n[2];
            sx += snx * 0.02;
            sy += sny * 0.02;
            sz += snz * 0.02;
        }
        for (local i = 0; i < count; ++i) {
            local yaw = (i.tofloat() / count.tofloat()) * 6.28318530718 + rand01() * 0.18;
            local tangent = projectTangent(cos(yaw), 0.0, sin(yaw), snx, sny, snz);
            if (tangent[0] * tangent[0] + tangent[2] * tangent[2] < 1.0e-4) {
                tangent = projectTangent(1.0, 0.0, 0.0, snx, sny, snz);
            }
            local branch = newBranch(0, sx, sy, sz);
            tips.append({
                alive = true,
                branch = branch,
                x = sx,
                y = sy,
                z = sz,
                nx = snx,
                ny = sny,
                nz = snz,
                dx = tangent[0],
                dy = tangent[1],
                dz = tangent[2],
                traveled = 0.0,
                nextFork = s.forkSpacing * (0.55 + rand01() * 0.7),
                life = s.tipLife * (0.75 + rand01() * 0.5),
                speedScale = 0.85 + rand01() * 0.35,
                yawJitter = (rand01() - 0.5) * s.yawNoise,
                depth = 0
            });
            nextForkBudget -= 1;
        }
        return count;
    }

    function tryFork(parent) {
        if (parent.depth >= s.maxDepth) return;
        if (nextForkBudget <= 0) return;
        if (rand01() > s.forkChance) {
            parent.nextFork += s.forkSpacing * (0.4 + rand01() * 0.5);
            return;
        }
        local angle = s.forkAngle * (0.65 + rand01() * 0.7);
        if (rand01() < 0.5) angle = -angle;
        local rotated = rotateAround(parent.dx, parent.dy, parent.dz, parent.nx, parent.ny, parent.nz, angle);
        local dir = projectTangent(rotated[0], rotated[1], rotated[2], parent.nx, parent.ny, parent.nz);
        local branch = newBranch(parent.depth + 1, parent.x, parent.y, parent.z);
        tips.append({
            alive = true,
            branch = branch,
            x = parent.x,
            y = parent.y,
            z = parent.z,
            nx = parent.nx,
            ny = parent.ny,
            nz = parent.nz,
            dx = dir[0],
            dy = dir[1],
            dz = dir[2],
            traveled = 0.0,
            nextFork = s.forkSpacing * (0.7 + rand01() * 0.8),
            life = parent.life * (0.55 + rand01() * 0.35),
            speedScale = parent.speedScale * (0.75 + rand01() * 0.25),
            yawJitter = (rand01() - 0.5) * s.yawNoise * 1.4,
            depth = parent.depth + 1
        });
        nextForkBudget -= 1;
        // Parent keeps a mild opposite bias so the fork reads clearly.
        local keep = rotateAround(parent.dx, parent.dy, parent.dz, parent.nx, parent.ny, parent.nz, -angle * 0.35);
        local kd = projectTangent(keep[0], keep[1], keep[2], parent.nx, parent.ny, parent.nz);
        parent.dx = kd[0];
        parent.dy = kd[1];
        parent.dz = kd[2];
        parent.nextFork += s.forkSpacing * (0.8 + rand01() * 0.6);
    }

    function stepTip(world, tip, dt) {
        if (!tip.alive) return;
        tip.life -= dt;
        if (tip.life <= 0.0) {
            tip.alive = false;
            tip.branch.alive = false;
            return;
        }

        local n = vnorm(tip.nx, tip.ny, tip.nz);
        tip.nx = n[0];
        tip.ny = n[1];
        tip.nz = n[2];

        local dir = projectTangent(tip.dx, tip.dy, tip.dz, tip.nx, tip.ny, tip.nz);
        local wobble = tip.yawJitter * dt + (rand01() - 0.5) * s.perturb * dt;
        local spun = rotateAround(dir[0], dir[1], dir[2], tip.nx, tip.ny, tip.nz, wobble);
        dir = projectTangent(spun[0], spun[1], spun[2], tip.nx, tip.ny, tip.nz);
        tip.dx = dir[0];
        tip.dy = dir[1];
        tip.dz = dir[2];

        local step = s.speed * tip.speedScale * dt;
        local lift = 0.12;
        local fromX = tip.x + tip.nx * lift;
        local fromY = tip.y + tip.ny * lift;
        local fromZ = tip.z + tip.nz * lift;
        local ahead = step + 0.18;
        world.rayCast(fromX, fromY, fromZ,
                      fromX + tip.dx * ahead, fromY + tip.dy * ahead, fromZ + tip.dz * ahead);
        if (world.hasRayHit() && world.getRayHitFraction() < 0.98) {
            tip.x = world.getRayHitX() + world.getRayHitNormalX() * 0.025;
            tip.y = world.getRayHitY() + world.getRayHitNormalY() * 0.025;
            tip.z = world.getRayHitZ() + world.getRayHitNormalZ() * 0.025;
            tip.nx = world.getRayHitNormalX();
            tip.ny = world.getRayHitNormalY();
            tip.nz = world.getRayHitNormalZ();
            local climbed = projectTangent(tip.dx, tip.dy, tip.dz, tip.nx, tip.ny, tip.nz);
            tip.dx = climbed[0];
            tip.dy = climbed[1];
            tip.dz = climbed[2];
        } else {
            local cx = tip.x + tip.dx * step;
            local cy = tip.y + tip.dy * step;
            local cz = tip.z + tip.dz * step;
            local hit = stick(world, cx, cy, cz, tip.nx, tip.ny, tip.nz, 0.45, 1.35);
            if (hit == null) {
                // Edge wrap: probe slightly ahead and inward toward the previous surface.
                local wx = cx + tip.dx * 0.08 - tip.nx * 0.05;
                local wy = cy + tip.dy * 0.08 - tip.ny * 0.05;
                local wz = cz + tip.dz * 0.08 - tip.nz * 0.05;
                world.rayCast(wx, wy, wz,
                              wx - tip.dx * 0.55 - tip.nx * 0.85,
                              wy - tip.dy * 0.55 - tip.ny * 0.85,
                              wz - tip.dz * 0.55 - tip.nz * 0.85);
                if (world.hasRayHit()) {
                    tip.x = world.getRayHitX() + world.getRayHitNormalX() * 0.025;
                    tip.y = world.getRayHitY() + world.getRayHitNormalY() * 0.025;
                    tip.z = world.getRayHitZ() + world.getRayHitNormalZ() * 0.025;
                    tip.nx = world.getRayHitNormalX();
                    tip.ny = world.getRayHitNormalY();
                    tip.nz = world.getRayHitNormalZ();
                    local wrapped = projectTangent(tip.dx, tip.dy, tip.dz, tip.nx, tip.ny, tip.nz);
                    tip.dx = wrapped[0];
                    tip.dy = wrapped[1];
                    tip.dz = wrapped[2];
                } else {
                    tip.alive = false;
                    tip.branch.alive = false;
                    return;
                }
            } else {
                tip.x = hit.x + hit.nx * 0.025;
                tip.y = hit.y + hit.ny * 0.025;
                tip.z = hit.z + hit.nz * 0.025;
                tip.nx = hit.nx;
                tip.ny = hit.ny;
                tip.nz = hit.nz;
                local stuckDir = projectTangent(tip.dx, tip.dy, tip.dz, tip.nx, tip.ny, tip.nz);
                tip.dx = stuckDir[0];
                tip.dy = stuckDir[1];
                tip.dz = stuckDir[2];
            }
        }

        appendPoint(tip.branch, tip.x, tip.y, tip.z);
        tip.traveled += step;
        if (tip.traveled >= tip.nextFork) tryFork(tip);
    }

    function update(world, dt) {
        local clamped = dt;
        if (clamped > 0.05) clamped = 0.05;
        if (clamped < 0.0) clamped = 0.0;
        // Snapshot length so forks spawned this frame do not advance until next tick.
        local count = tips.len();
        for (local i = 0; i < count; ++i) stepTip(world, tips[i], clamped);
    }

    function livingTipCount() {
        local n = 0;
        foreach (tip in tips) if (tip.alive) n += 1;
        return n;
    }

    function branchCount() {
        return branches.len();
    }

    function getBranches() {
        return branches;
    }

    function getTips() {
        return tips;
    }

    function getSeed() {
        return seed;
    }

    return {
        spawnBurst = spawnBurst,
        update = update,
        livingTipCount = livingTipCount,
        branchCount = branchCount,
        getBranches = getBranches,
        getTips = getTips,
        getSeed = getSeed,
        rand01 = rand01
    };
}
