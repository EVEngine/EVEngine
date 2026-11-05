// Surface-crawling vine tips: stick to World3D geometry, climb walls, and fork.
// Returns a table of simulation helpers; the caller owns tips / paths / RNG.

function vineCreateSystem(settings) {
    local s = settings;
    local tips = [];
    local branches = []; // each: { points=[], polyline=null, glow=null, alive=true, depth=0 }
    local seed = s.seed;
    local nextForkBudget = s.maxTips;
    local maxTipY = 0.0;
    local wallTipFrames = 0;
    local climbTransfers = 0;

    function rand01() {
        seed = (seed * 16807) % 2147483647;
        return seed.tofloat() / 2147483647.0;
    }

    function vlen(x, y, z) {
        return sqrt(x * x + y * y + z * z);
    }

    function vnorm(x, y, z) {
        local L = vlen(x, y, z);
        if (L < 1.0e-5) return null;
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

    // Pick a stable travel direction on a surface. Prefer the incoming tangent;
    // if that collapses (head-on wall hit), climb uphill along the face.
    function surfaceTravelDir(nx, ny, nz, preferDx, preferDy, preferDz) {
        local projected = projectTangent(preferDx, preferDy, preferDz, nx, ny, nz);
        if (projected != null) return projected;
        local uphill = projectTangent(0.0, 1.0, 0.0, nx, ny, nz);
        if (uphill != null) return uphill;
        local alongZ = projectTangent(0.0, 0.0, 1.0, nx, ny, nz);
        if (alongZ != null) return alongZ;
        local alongX = projectTangent(1.0, 0.0, 0.0, nx, ny, nz);
        if (alongX != null) return alongX;
        return [0.0, 1.0, 0.0];
    }

    function isWallNormal(nx, ny, nz) {
        return fabs(ny) < 0.55;
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

    function adoptSurface(tip, hx, hy, hz, nx, ny, nz, preferDx, preferDy, preferDz, countClimb) {
        local n = vnorm(nx, ny, nz);
        if (n == null) return false;
        local wasFloor = !isWallNormal(tip.nx, tip.ny, tip.nz);
        local nx2 = n[0];
        local ny2 = n[1];
        local nz2 = n[2];
        local nxPos = hx + nx2 * 0.03;
        local nyPos = hy + ny2 * 0.03;
        local nzPos = hz + nz2 * 0.03;
        // If the mount jumps too far, drop a corner sample so the ribbon stays on geometry.
        // Never accept a large free-space jump — keep mounts local to avoid air chords.
        local jump = vlen(nxPos - tip.x, nyPos - tip.y, nzPos - tip.z);
        if (jump > 0.55) return false;
        if (fabs(nyPos - tip.y) > 0.45 && isWallNormal(nx2, ny2, nz2)) {
            // Prefer climbing the current face instead of teleporting onto a higher ledge.
            nyPos = tip.y + ny2 * 0.03;
            jump = vlen(nxPos - tip.x, nyPos - tip.y, nzPos - tip.z);
            if (jump > 0.55) return false;
        }
        tip.nx = nx2;
        tip.ny = ny2;
        tip.nz = nz2;
        tip.x = nxPos;
        tip.y = nyPos;
        tip.z = nzPos;
        local preferY = preferDy;
        // Floor -> wall: bias strongly up the face so vines visibly climb.
        if (countClimb && wasFloor && isWallNormal(tip.nx, tip.ny, tip.nz)) {
            preferY = preferDy + 2.4;
            climbTransfers += 1;
        }
        local dir = surfaceTravelDir(tip.nx, tip.ny, tip.nz, preferDx, preferY, preferDz);
        tip.dx = dir[0];
        tip.dy = dir[1];
        tip.dz = dir[2];
        return true;
    }

    // Fan of near-field horizontal probes at tip height — mount the foot, then climb.
    function seekWall(world, tip, step) {
        if (isWallNormal(tip.nx, tip.ny, tip.nz)) return false;
        local reach = step + 0.55;
        local dirs = [
            [tip.dx, tip.dz],
            [tip.dx * 0.866 + tip.dz * 0.5, tip.dz * 0.866 - tip.dx * 0.5],
            [tip.dx * 0.866 - tip.dz * 0.5, tip.dz * 0.866 + tip.dx * 0.5],
            [-tip.dz, tip.dx],
            [tip.dz, -tip.dx]
        ];
        local lifts = [0.10, 0.24, 0.42];
        foreach (liftY in lifts) {
            foreach (d in dirs) {
                local ox = tip.x;
                local oy = tip.y + liftY;
                local oz = tip.z;
                world.rayCast(ox, oy, oz, ox + d[0] * reach, oy, oz + d[1] * reach);
                if (!world.hasRayHit()) continue;
                local dist = world.getRayHitFraction() * reach;
                if (dist > 0.55) continue;
                local nx = world.getRayHitNormalX();
                local ny = world.getRayHitNormalY();
                local nz = world.getRayHitNormalZ();
                if (fabs(ny) >= 0.55) continue;
                local hx = world.getRayHitX();
                local hy = world.getRayHitY();
                local hz = world.getRayHitZ();
                // Remount at tip height so the ribbon meets the wall at the foot.
                world.rayCast(tip.x, tip.y + 0.06, tip.z, hx - nx * 0.15, tip.y + 0.06, hz - nz * 0.15);
                if (world.hasRayHit() && fabs(world.getRayHitNormalY()) < 0.55) {
                    hx = world.getRayHitX();
                    hy = world.getRayHitY();
                    hz = world.getRayHitZ();
                    nx = world.getRayHitNormalX();
                    ny = world.getRayHitNormalY();
                    nz = world.getRayHitNormalZ();
                } else {
                    hy = tip.y;
                }
                return adoptSurface(tip, hx, hy, hz, nx, ny, nz, d[0], 2.5, d[1], true);
            }
        }
        return false;
    }

    function spawnBurst(world, originX, originY, originZ) {
        tips.clear();
        branches.clear();
        nextForkBudget = s.maxTips;
        maxTipY = originY;
        wallTipFrames = 0;
        climbTransfers = 0;
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
            local n = vnorm(stuck.nx, stuck.ny, stuck.nz);
            if (n != null) {
                snx = n[0];
                sny = n[1];
                snz = n[2];
            }
            sx += snx * 0.02;
            sy += sny * 0.02;
            sz += snz * 0.02;
        }
        // Aim a few roots at house (-X) and mountain (+X/+Z) so climb is not left to chance.
        local aim = [
            [-1.0, 0.0, 0.0],
            [-0.8, 0.0, 0.4],
            [1.0, 0.0, 0.15],
            [0.7, 0.0, 0.55],
            [0.0, 0.0, -1.0],
            [0.15, 0.0, 1.0],
            [-0.55, 0.0, -0.7],
            [0.55, 0.0, -0.65]
        ];
        for (local i = 0; i < count; ++i) {
            local yaw = (i.tofloat() / count.tofloat()) * 6.28318530718 + rand01() * 0.12;
            local wantX = cos(yaw);
            local wantZ = sin(yaw);
            if (i < aim.len()) {
                wantX = aim[i][0];
                wantZ = aim[i][2];
            }
            local tangent = projectTangent(wantX, 0.0, wantZ, snx, sny, snz);
            if (tangent == null) tangent = projectTangent(1.0, 0.0, 0.0, snx, sny, snz);
            if (tangent == null) tangent = [1.0, 0.0, 0.0];
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
                nextFork = s.forkSpacing * (0.7 + rand01() * 0.6),
                life = s.tipLife * (0.85 + rand01() * 0.4),
                speedScale = 0.9 + rand01() * 0.3,
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
        // Delay forking until the tip has actually traveled onto geometry.
        if (parent.traveled < s.forkSpacing * 0.85) {
            parent.nextFork = s.forkSpacing * 0.9;
            return;
        }
        if (rand01() > s.forkChance) {
            parent.nextFork += s.forkSpacing * (0.45 + rand01() * 0.55);
            return;
        }
        local angle = s.forkAngle * (0.65 + rand01() * 0.7);
        if (rand01() < 0.5) angle = -angle;
        local rotated = rotateAround(parent.dx, parent.dy, parent.dz, parent.nx, parent.ny, parent.nz, angle);
        local dir = surfaceTravelDir(parent.nx, parent.ny, parent.nz, rotated[0], rotated[1], rotated[2]);
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
            nextFork = s.forkSpacing * (0.8 + rand01() * 0.7),
            life = parent.life * (0.6 + rand01() * 0.35),
            speedScale = parent.speedScale * (0.8 + rand01() * 0.2),
            yawJitter = (rand01() - 0.5) * s.yawNoise * 1.3,
            depth = parent.depth + 1
        });
        nextForkBudget -= 1;
        local keep = rotateAround(parent.dx, parent.dy, parent.dz, parent.nx, parent.ny, parent.nz, -angle * 0.35);
        local kd = surfaceTravelDir(parent.nx, parent.ny, parent.nz, keep[0], keep[1], keep[2]);
        parent.dx = kd[0];
        parent.dy = kd[1];
        parent.dz = kd[2];
        parent.nextFork += s.forkSpacing * (0.9 + rand01() * 0.5);
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
        if (n == null) {
            tip.alive = false;
            tip.branch.alive = false;
            return;
        }
        tip.nx = n[0];
        tip.ny = n[1];
        tip.nz = n[2];

        local dir = surfaceTravelDir(tip.nx, tip.ny, tip.nz, tip.dx, tip.dy, tip.dz);
        // On walls, damp yaw noise and keep a mild uphill bias so vines keep ascending.
        local wobbleScale = isWallNormal(tip.nx, tip.ny, tip.nz) ? 0.35 : 1.0;
        local wobble = (tip.yawJitter * dt + (rand01() - 0.5) * s.perturb * dt) * wobbleScale;
        local spun = rotateAround(dir[0], dir[1], dir[2], tip.nx, tip.ny, tip.nz, wobble);
        dir = surfaceTravelDir(tip.nx, tip.ny, tip.nz, spun[0], spun[1], spun[2]);
        if (isWallNormal(tip.nx, tip.ny, tip.nz)) {
            local uphill = projectTangent(0.0, 1.0, 0.0, tip.nx, tip.ny, tip.nz);
            // Horizontal axis along the wall so vines crawl across the face, not only up a crease.
            local across = projectTangent(-tip.nz, 0.0, tip.nx, tip.nx, tip.ny, tip.nz);
            local ax = 0.0;
            local ay = 0.0;
            local az = 0.0;
            if (uphill != null) {
                ax += uphill[0] * 0.7;
                ay += uphill[1] * 0.7;
                az += uphill[2] * 0.7;
            }
            if (across != null) {
                local side = (tip.yawJitter >= 0.0) ? 0.45 : -0.45;
                ax += across[0] * side;
                ay += across[1] * side;
                az += across[2] * side;
            }
            local blended = vnorm(dir[0] + ax, dir[1] + ay, dir[2] + az);
            if (blended != null) dir = blended;
            else if (uphill != null) dir = uphill;
        }
        tip.dx = dir[0];
        tip.dy = dir[1];
        tip.dz = dir[2];

        local step = s.speed * tip.speedScale * dt;

        // 1) Seek nearby walls while still on floors/berms.
        if (seekWall(world, tip, step)) {
            appendPoint(tip.branch, tip.x, tip.y, tip.z);
            tip.traveled += step;
            if (tip.y > maxTipY) maxTipY = tip.y;
            if (isWallNormal(tip.nx, tip.ny, tip.nz)) wallTipFrames += 1;
            if (tip.traveled >= tip.nextFork) tryFork(tip);
            return;
        }

        // 2) Forward probe along the current surface tangent (catches walls/ledges).
        local lift = 0.08;
        local fromX = tip.x + tip.nx * lift;
        local fromY = tip.y + tip.ny * lift;
        local fromZ = tip.z + tip.nz * lift;
        local ahead = step + 0.28;
        world.rayCast(fromX, fromY, fromZ,
                      fromX + tip.dx * ahead, fromY + tip.dy * ahead, fromZ + tip.dz * ahead);
        if (world.hasRayHit() && world.getRayHitFraction() < 0.97) {
            adoptSurface(tip,
                world.getRayHitX(), world.getRayHitY(), world.getRayHitZ(),
                world.getRayHitNormalX(), world.getRayHitNormalY(), world.getRayHitNormalZ(),
                tip.dx, tip.dy, tip.dz, true);
        } else {
            // 3) Free move on the current tangent, then stick back to the surface.
            local cx = tip.x + tip.dx * step;
            local cy = tip.y + tip.dy * step;
            local cz = tip.z + tip.dz * step;
            local hit = stick(world, cx, cy, cz, tip.nx, tip.ny, tip.nz, 0.28, 0.85);
            // Reject far stick hits (e.g. roof/terrace → distant ground) that draw air chords.
            if (hit != null) {
                local jump = vlen(hit.x - tip.x, hit.y - tip.y, hit.z - tip.z);
                if (jump > step * 3.5 + 0.22) hit = null;
            }
            if (hit != null) {
                adoptSurface(tip, hit.x, hit.y, hit.z, hit.nx, hit.ny, hit.nz, tip.dx, tip.dy, tip.dz, false);
            } else {
                // 4) Edge wrap: look over the lip onto the adjacent face (down the side, not to far ground).
                local wrapped = false;
                local probes = [
                    [cx + tip.dx * 0.1 - tip.nx * 0.02, cy + tip.dy * 0.1 - tip.ny * 0.02, cz + tip.dz * 0.1 - tip.nz * 0.02,
                     -tip.dx * 0.15 - tip.nx * 0.9, -tip.dy * 0.15 - tip.ny * 0.9, -tip.dz * 0.15 - tip.nz * 0.9],
                    [cx + tip.dx * 0.18, cy + tip.dy * 0.18 - 0.05, cz + tip.dz * 0.18,
                     -tip.nx * 0.7, -0.85, -tip.nz * 0.7],
                    [cx + tip.dx * 0.05, cy - 0.08, cz + tip.dz * 0.05,
                     tip.dx * 0.4, -0.7, tip.dz * 0.4]
                ];
                foreach (p in probes) {
                    world.rayCast(p[0], p[1], p[2], p[0] + p[3], p[1] + p[4], p[2] + p[5]);
                    if (!world.hasRayHit()) continue;
                    local jx = world.getRayHitX() - tip.x;
                    local jy = world.getRayHitY() - tip.y;
                    local jz = world.getRayHitZ() - tip.z;
                    if (vlen(jx, jy, jz) > 0.85) continue;
                    adoptSurface(tip,
                        world.getRayHitX(), world.getRayHitY(), world.getRayHitZ(),
                        world.getRayHitNormalX(), world.getRayHitNormalY(), world.getRayHitNormalZ(),
                        tip.dx, tip.dy + 0.5, tip.dz, true);
                    wrapped = true;
                    break;
                }
                if (!wrapped) {
                    tip.alive = false;
                    tip.branch.alive = false;
                    return;
                }
            }
        }

        appendPoint(tip.branch, tip.x, tip.y, tip.z);
        tip.traveled += step;
        if (tip.y > maxTipY) maxTipY = tip.y;
        if (isWallNormal(tip.nx, tip.ny, tip.nz)) wallTipFrames += 1;
        if (tip.traveled >= tip.nextFork) tryFork(tip);
    }

    function update(world, dt) {
        local clamped = dt;
        if (clamped > 0.05) clamped = 0.05;
        if (clamped < 0.0) clamped = 0.0;
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

    function getMaxTipY() {
        return maxTipY;
    }

    function getClimbTransfers() {
        return climbTransfers;
    }

    function getWallTipFrames() {
        return wallTipFrames;
    }

    return {
        spawnBurst = spawnBurst,
        update = update,
        livingTipCount = livingTipCount,
        branchCount = branchCount,
        getBranches = getBranches,
        getTips = getTips,
        getSeed = getSeed,
        getMaxTipY = getMaxTipY,
        getClimbTransfers = getClimbTransfers,
        getWallTipFrames = getWallTipFrames,
        rand01 = rand01
    };
}
