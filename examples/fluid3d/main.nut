persist solver = null;
persist surface = null;
persist pixels = null;
persist imageModule = null;
persist texture = null;
persist accumulator = 0.0;
persist paused = true;
persist dirty = true;
persist displayAccumulator = 0.0;
persist droppedTime = 0.0;
persist failure = "";
persist keys = {};
persist mode = 0;
persist labelFont = null;
persist emitter = null;
persist nozzle = null;
persist simulationTime = 0.0;
persist wheelWorld = null;
persist wheelBody = null;
persist wheelCoupling = null;
persist wheelTexture = null;
persist wheelContacts = 0;
persist simulationMs = 0.0;
persist reconstructionMs = 0.0;
persist uploadMs = 0.0;
persist slowWorkCount = 0;
persist thermalRules = [];
persist viscosityColors = [];
persist attachmentCollider = null;
persist foamGenerator = null;
persist foamPool = null;
persist foamCompositeMs = 0.0;
persist sdfPose = null;
persist overlapQueries = [];
persist overlapCounts = [];
persist maze = null;
persist bucket = null;
persist bottle = null;
persist karman = null;
persist refractionBackground = null;
persist obstacleTexture = null;

dofile("maze-support.nut");
dofile("bucket-support.nut");
dofile("bottle-support.nut");
dofile("karman-support.nut");
dofile("wave-support.nut");

function updateOverlapTriggers(t) {
    overlapQueries[0].center = [-0.38 + 0.24 * sin(t), 0.72, 0.0];
    overlapQueries[1].center = [0.38 + 0.24 * cos(t * 0.8), 0.72, 0.0];
    overlapCounts = checked(solver.applyQueryColors(overlapQueries,
        [[1.0, 0.08, 0.04, 1.0], [1.0, 0.85, 0.04, 1.0]],
        0.04, 0.75, 0.9, 1.0, 1024));
}

function advanceAttachment(t) {
    attachmentCollider.center = fluidWavePosition([0.0, 0.5, 0.0], 0.3, 1.0, t);
    local angle = 0.35 * sin(t);
    attachmentCollider.rotation = [0.0, 0.0, sin(angle * 0.5), cos(angle * 0.5)];
    checked(solver.setColliders([attachmentCollider]));
    checked(solver.step(1.0 / 60.0, 2));
    checked(solver.applySolidColor(0.95, 0.65, 0.2, 1.0));
}

// Stop sustained overload before submitting more fluid work. This cannot
// interrupt an in-flight GPU command or recover a lost device.
function checkWorkBudget(milliseconds) {
    slowWorkCount = milliseconds > 50.0 ? slowWorkCount + 1 : 0;
    if (milliseconds > 250.0 || slowWorkCount >= 3)
        throw "Fluid work budget exceeded: " + format("%.1f ms", milliseconds);
}

function movingPose(t) {
    local halfAngle = 0.25 * sin(2.0 * t);
    local c = 0.70710678118 * cos(halfAngle);
    local s = 0.70710678118 * sin(halfAngle);
    return { position = [0.35 * sin(t), 1.7, 0.0], rotation = [c, s, s, c] };
}

function drawMillStructure(angle, foreground) {
    local wood = foreground ? 0.72 : 0.30;
    local green = foreground ? 0.43 : 0.20;
    local blue = foreground ? 0.16 : 0.10;
    if (!foreground) {
        // Static frame and tail race. These are presentation geometry only;
        // the fluid solver still sees the same two bounded paddle colliders.
        gfx.drawTexturedRectRotated(wheelTexture,318.0,496.0,28.0,274.0,-18.0,0.24,0.20,0.16,1.0);
        gfx.drawTexturedRectRotated(wheelTexture,642.0,496.0,28.0,274.0,18.0,0.24,0.20,0.16,1.0);
        gfx.drawTexturedRectRotated(wheelTexture,480.0,565.0,390.0,22.0,0.0,0.20,0.25,0.28,1.0);
        gfx.drawTexturedRectRotated(wheelTexture,650.0,248.0,250.0,14.0,-6.0,0.16,0.19,0.21,1.0);
    }
    // A segmented rim gives the original mill silhouette without adding rigid
    // bodies or fluid colliders. Four visible paddles match the two crossbars.
    for(local i=0;i<24;++i) {
        local a=angle+i*15.0;
        local r=a*3.14159265359/180.0;
        gfx.drawTexturedRectRotated(wheelTexture,480.0+cos(r)*82.0,360.0-sin(r)*82.0,
            24.0,10.0,-a+90.0,wood,green,blue,foreground ? 0.96 : 0.55);
    }
    if(foreground) {
        gfx.drawTexturedRectRotated(wheelTexture,480.0,360.0,161.7,18.0,angle,0.68,0.40,0.14,0.92);
        gfx.drawTexturedRectRotated(wheelTexture,480.0,360.0,161.7,18.0,angle+90.0,0.68,0.40,0.14,0.92);
        gfx.drawTexturedRect(wheelTexture,468.0,348.0,24.0,24.0,0.28,0.18,0.10,1.0);
    }
}

function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

function advanceWheelJet(dt) {
    return checked(emitter.advance(solver,nozzle,dt,90.0,4,0.2));
}

function mazeStateValid() {
    return maze != null && "walls" in maze && typeof maze.walls == "array" &&
        maze.walls.len() > 0 && "center" in maze.walls[0] &&
        typeof maze.walls[0].center == "array" && maze.walls[0].center.len() >= 2 &&
        "half" in maze.walls[0] && typeof maze.walls[0].half == "array" &&
        maze.walls[0].half.len() >= 2;
}

function bucketStateValid() {
    return bucket != null && "walls" in bucket && typeof bucket.walls == "array" &&
        bucket.walls.len() > 0 && "center" in bucket.walls[0] &&
        typeof bucket.walls[0].center == "array" && bucket.walls[0].center.len() >= 2 &&
        "half" in bucket.walls[0] && typeof bucket.walls[0].half == "array" &&
        bucket.walls[0].half.len() >= 2;
}

function bottleStateValid() {
    return bottle != null && "pose" in bottle && typeof bottle.pose == "table" &&
        "position" in bottle.pose && typeof bottle.pose.position == "array" && bottle.pose.position.len() == 3;
}

function pressed(key) {
    local now = keyboard.isDown(key);
    local previous = key in keys ? keys[key] : false;
    keys[key] <- now;
    return now && !previous;
}

function rebuild(selected) {
    wheelCoupling = null;
    wheelBody = null;
    if(wheelWorld != null) {wheelWorld.destroy();wheelWorld=null;}
    wheelContacts = 0;
    mode = selected;
    thermalRules = [];
    viscosityColors = [];
    attachmentCollider = null;
    foamGenerator = null;
    foamPool = null;
    foamCompositeMs = 0.0;
    sdfPose = null;
    overlapQueries = [];
    overlapCounts = [];
    maze = null;
    bucket = null;
    bottle = null;
    karman = null;
    if(surface != null) {
        checked(surface.configureSurface(1.0,0.0,0.03,2));
        checked(surface.configureSurfaceBlurRadius(0.02));
        checked(surface.configureSurfaceDownsample(1));
        checked(surface.configureThicknessDownsample(1));
        checked(surface.configureAnisotropy(mode == 3));
        if(mode == 3) checked(surface.configureSurfaceDownsample(2));
        checked(surface.configureMaterial(true,0.8,0.0,0.55,0.75,0.35));
        checked(surface.configureReflection(true));
        checked(surface.configureFoam(true,1));
        checked(surface.configureColors(0.05,0.32,0.72,0.55,0.72,1.0));
        checked(surface.configureRefraction(1.0,5.0,0.01,1));
        checked(surface.configureRefractionEnabled(true));
        if(mode == 5) surface.setCameraXYZ(0.0, 4.0, 0.1, 0.0, 1.3, 0.0, 48.0);
        else surface.setCameraXYZ(3.4, 3.0, 5.0, 0.0, 0.9, 0.0, 48.0);
    }
    local definition = checked(fluids.volumeDefaults());
    definition.settings.capacity = 2048;
    definition.settings.spacing = 0.12;
    definition.settings.minimum = [-1.2, 0.0, -0.75];
    definition.settings.maximum = [1.2, 2.4, 0.75];
    // Interactive default: two stable 1/120 s substeps and two PBF iterations.
    // The lab runs the CPU reference solver, so extra quality passes are opt-in
    // per scenario instead of consuming the presentation budget in every mode.
    definition.settings.iterations = 2;
    local defaultParticle = checked(fluids.volumeEmissionDefaults()).description.prototype;
    for (local z = 0; z < (mode >= 4 && mode != 14 ? 0 : 8); ++z)
        for (local y = 0; y < 10; ++y)
            for (local x = 0; x < 8; ++x) {
                local particle = clone defaultParticle;
                local material = clone defaultParticle.material;
                if (mode == 1) { material.viscosity = 70.0; material.yieldStress = 5.0; }
                if (mode == 2) { material.phase = 1; material.buoyancy = -0.4; material.drag = 1.0; }
                if (mode == 3) { material.phase = 2; material.cohesion = 0.0; }
                particle.position = [mode == 14 ? -0.42 + x * 0.12 : -1.0 + x * 0.12,
                    0.12 + y * 0.12, -0.42 + z * 0.12];
                if (mode == 2) particle.color = [0.55, 0.58, 0.62, 0.35];
                particle.material = material;
                definition.particles.append(particle);
            }
    if(mode == 7) {
        definition.settings.spacing = 0.1;
        definition.settings.minimum = [-0.3,0.0,-0.3];
        definition.settings.maximum = [0.3,1.4,0.3];
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for(local z=0;z<5;++z) for(local y=0;y<10;++y) for(local x=0;x<5;++x) {
            local p=clone prototype;
            p.material=clone prototype.material;
            p.material.density=y<5 ? 700.0 : 1300.0;
            p.material.cohesion=0.0;p.material.viscosity=0.05;
            p.position=[-0.2+x*0.1+0.015*sin(y+z),0.071+y*0.1+0.015*sin(x+z),-0.2+z*0.1+0.015*cos(x+y)];
            p.color=y<5 ? [0.1,0.5,1.0,1.0] : [1.0,0.2,0.08,1.0];
            definition.particles.append(p);
        }
        surface.setCameraXYZ(1.5,1.6,2.4,0.0,0.5,0.0,48.0);
    }
    if (mode == 9) {
        definition.settings.spacing = 0.1;
        definition.settings.minimum = [-0.6, 0.0, -0.3];
        definition.settings.maximum = [0.6, 1.2, 0.3];
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for (local z = 0; z < 5; ++z) for (local y = 0; y < 5; ++y) for (local x = 0; x < 10; ++x) {
            local p = clone prototype;
            p.material = clone prototype.material;
            p.material.diffusion = 20.0;
            p.material.cohesion = x < 5 ? 0.5 : 2.0;
            p.material.viscosity = x < 5 ? 2.0 : 50.0;
            p.data = [p.material.viscosity, p.material.cohesion, 0.0, 0.0];
            p.position = [-0.45 + x * 0.1, 0.1 + y * 0.1, -0.2 + z * 0.1];
            p.velocity = [x < 5 ? 0.4 : -0.4, 0.0, 0.0];
            p.color = x < 5 ? [1.0, 0.1, 0.05, 1.0] : [0.05, 0.2, 1.0, 1.0];
            definition.particles.append(p);
        }
        surface.setCameraXYZ(0.0, 1.4, 2.1, 0.0, 0.25, 0.0, 48.0);
    }
    solver = checked(fluids.newVolumeSimulator(definition));
    emitter = eve.VolumeFluidJetEmitter();
    if (mode == 14) {
        overlapQueries = [
            {type=1,center=[-0.38,0.72,0.0],size=[0.65,0.85,1.1],rotation=[0.0,0.0,0.0,1.0],contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761},
            {type=1,center=[0.38,0.72,0.0],size=[0.65,0.85,1.1],rotation=[0.0,0.0,0.258819,0.965926],contactOffset=0.0,maxDistance=0.0,phaseMask=1,collisionFilter=4294901761}
        ];
        updateOverlapTriggers(0.0);
        surface.setCameraXYZ(2.7,2.1,3.8,0.0,0.65,0.0,46.0);
    }
    if (mode == 10) {
        local thermal = checked(fluids.volumeDefaults());
        thermal.settings.capacity = 128;
        thermal.settings.spacing = 0.1;
        thermal.settings.minimum = [-1.0, 0.0, -0.4];
        thermal.settings.maximum = [1.0, 1.2, 0.4];
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for (local side = 0; side < 2; ++side)
            for (local z = 0; z < 4; ++z) for (local y = 0; y < 3; ++y) for (local x = 0; x < 4; ++x) {
                local p = clone prototype;
                p.material = clone prototype.material;
                p.material.viscosity = 5.0; p.material.cohesion = 1.0; p.material.diffusion = 8.0;
                p.position = [(side == 0 ? -0.45 : 0.45) - 0.15 + x * 0.1, 0.34 + y * 0.1, -0.15 + z * 0.1];
                p.data = [5.0, 1.0, 0.0, 0.0];
                thermal.particles.append(p);
            }
        solver = checked(fluids.newVolumeSimulator(thermal));
        local colliders = [];
        for (local side = 0; side < 2; ++side) {
            local plate = checked(fluids.volumeColliderDefaults());
            plate.label = side + 1; plate.shape = 1;
            plate.center = [side == 0 ? -0.45 : 0.45, 0.25, 0.0];
            plate.halfExtent = [0.35, 0.05, 0.3];
            colliders.append(plate);
            local rule = checked(fluids.volumeThermalRuleDefaults());
            rule.colliderLabel = plate.label; rule.rate = side == 0 ? -2.0 : 2.0;
            thermalRules.append(rule);
        }
        checked(solver.setColliders(colliders));
        viscosityColors = [
            {viscosity = 0.05, color = [1.0, 0.12, 0.02, 1.0]},
            {viscosity = 5.0, color = [1.0, 0.85, 0.15, 1.0]},
            {viscosity = 10.0, color = [0.08, 0.35, 1.0, 1.0]}
        ];
        checked(solver.applyMaterialChannelsWithColors(viscosityColors));
        surface.setCameraXYZ(0.0, 1.4, 3.0, 0.0, 0.35, 0.0, 48.0);
    }
    if (mode == 11) {
        local definition = checked(fluids.volumeDefaults());
        definition.settings.capacity = 32;
        definition.settings.spacing = 0.1;
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for (local z = 0; z < 4; ++z) for (local x = 0; x < 4; ++x) {
            local p = clone prototype;
            p.position = [-0.15 + x * 0.1, 0.59, -0.15 + z * 0.1];
            definition.particles.append(p);
        }
        solver = checked(fluids.newVolumeSimulator(definition));
        attachmentCollider = checked(fluids.volumeColliderDefaults());
        attachmentCollider.label = 31; attachmentCollider.shape = 1;
        attachmentCollider.center = [0.0, 0.5, 0.0];
        attachmentCollider.halfExtent = [0.4, 0.05, 0.4];
        attachmentCollider.solidify = true;
        checked(solver.setColliders([attachmentCollider]));
        surface.setCameraXYZ(1.0, 1.4, 2.4, 0.0, 0.55, 0.0, 48.0);
    }
    if (mode == 12) {
        checked(surface.configureFoam(true,2));
        local foamDefinition = checked(fluids.volumeDefaults());
        foamDefinition.settings.capacity = 256;
        foamDefinition.settings.spacing = 0.1;
        foamDefinition.settings.gravity = [0.0, 0.0, 0.0];
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for (local z=0; z<8; ++z) for (local y=0; y<3; ++y) for (local x=0; x<8; ++x) {
            local p = clone prototype;
            p.material = clone prototype.material;
            p.material.viscosity = 0.05;
            p.position = [-0.35+x*0.1, 0.7+y*0.1, -0.35+z*0.1];
            p.velocity = [-2.0*p.position[2], 0.0, 2.0*p.position[0]];
            p.color = [0.05, 0.3, 0.7, 1.0];
            foamDefinition.particles.append(p);
        }
        solver = checked(fluids.newVolumeSimulator(foamDefinition));
        foamPool = eve.VolumeFluidDiffuse();
        local poolState = checked(foamPool.snapshot());
        poolState.capacity = 512;
        checked(foamPool.restore(poolState));
        foamGenerator = eve.VolumeFluidFoam();
        local foamState = checked(foamGenerator.snapshot());
        foamState.settings.rate = 400.0;
        foamState.settings.vorticityThreshold = 0.5;
        // Native field calibration: surface sites are about 520-700 kg/m3 in
        // this bounded vortex, while the interior reaches about 1010 kg/m3.
        foamState.settings.densityThreshold = 700.0;
        foamState.settings.randomness = 0.1;
        foamState.settings.maxPerStep = 8;
        checked(foamGenerator.restore(foamState));
        checked(foamGenerator.advance(solver, foamPool, 0.02));
        surface.setCameraXYZ(0.0, 2.0, 2.0, 0.0, 0.8, 0.0, 42.0);
    }
    if (mode == 13) {
        local sdfDefinition = checked(fluids.volumeDefaults());
        sdfDefinition.settings.capacity = 512;
        sdfDefinition.settings.spacing = 0.1;
        sdfDefinition.settings.minimum = [-1.0, 0.0, -0.65];
        sdfDefinition.settings.maximum = [1.0, 2.2, 0.65];
        sdfDefinition.settings.iterations = 3;
        local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
        for (local z = 0; z < 6; ++z)
            for (local y = 0; y < 6; ++y)
                for (local x = 0; x < 8; ++x) {
                    local p = clone prototype;
                    p.material = clone prototype.material;
                    p.material.viscosity = 1.0;
                    p.position = [-0.38 + x * 0.1, 1.25 + y * 0.1, -0.25 + z * 0.1];
                    p.color = [0.04, 0.38, 0.9, 1.0];
                    sdfDefinition.particles.append(p);
                }
        solver = checked(fluids.newVolumeSimulator(sdfDefinition));
        local obstacle = checked(fluids.volumeSdfColliderDefaults());
        obstacle.label = 51;
        obstacle.position = [0.0, 0.72, 0.0];
        obstacle.scale = 0.85;
        obstacle.friction = 0.05;
        checked(solver.setSdfColliders([obstacle]));
        sdfPose = checked(fluids.volumeSdfPoseDefaults());
        sdfPose.label = obstacle.label;
        sdfPose.position = obstacle.position;
        sdfPose.scale = obstacle.scale;
        surface.setCameraXYZ(2.6, 2.2, 3.8, 0.0, 0.85, 0.0, 46.0);
    }
    if (mode == 15) {
        maze = fluidMazeScene();
        solver = maze.solver;
        delete maze.solver;
        surface.setCameraXYZ(0.0,1.2,5.0,0.0,1.2,0.0,48.0);
    }
    if (mode == 16) {
        bucket=fluidBucketScene();
        solver=bucket.solver;delete bucket.solver;
        surface.setCameraXYZ(0.0,1.1,5.0,0.0,1.1,0.0,48.0);
    }
    if (mode == 17) {
        bottle=fluidBottleScene();
        solver=bottle.solver;delete bottle.solver;
        checked(surface.configureSurface(1.15,0.008,0.025,2));
        checked(surface.configureMaterial(true,0.92,0.05,0.62,0.32,0.55));
        checked(surface.configureColors(0.48,0.16,0.025,0.95,0.72,0.30));
        checked(surface.configureRefraction(0.9,3.5,0.012,2));
        surface.setCameraXYZ(0.0,1.35,4.2,0.0,0.95,0.0,43.0);
    }
    if(mode == 18) {
        karman=fluidKarmanScene();solver=karman.solver;delete karman.solver;
        surface.setCameraXYZ(0.0,1.65,3.4,0.0,0.78,0.0,42.0);
    }
    nozzle = checked(fluids.volumeEmissionDefaults());
    nozzle.description.origin = [-0.65, 1.6, 0.0];
    nozzle.description.direction = [1.0, 0.2, 0.0];
    nozzle.description.extent = [0.12, 0.12, 0.12];
    nozzle.description.speed = 1.5;
    nozzle.description.prototype.life = 1.5;
    if(mode == 16) {
        nozzle.description.origin=[0.0,2.05,0.0];nozzle.description.direction=[0.0,-1.0,0.0];
        nozzle.description.extent=[0.0,0.0,0.0];nozzle.description.speed=1.2;
        nozzle.description.prototype.life=1.8;
    }
    if(mode == 8) {
        // FluidMill is a sustained emitter scene. Cap active storage and lower
        // only the render targets; physics spacing and the shared SSF algorithm
        // stay unchanged.
        definition.settings.capacity=384;
        definition.settings.spacing=0.1;
        definition.settings.spacing=0.08;
        checked(surface.configureSurfaceDownsample(2));
        checked(surface.configureThicknessDownsample(4));
        surface.setCameraXYZ(0.0,1.0,4.0,0.0,1.0,0.0,48.0);
        wheelWorld=physics.newWorld3D(0.0,0.0,0.0,false);
        local anchor=wheelWorld.newBody("static",0.0,1.0,0.0);
        wheelBody=wheelWorld.newBody("dynamic",0.0,1.0,0.0);
        wheelBody.newBoxShape(0.8,0.1,0.2,1000.0,0.5,0.0);
        wheelBody.newBoxShape(0.1,0.8,0.2,1000.0,0.5,0.0);
        if(wheelWorld.newRevoluteJoint(anchor,wheelBody,0.0,1.0,0.0,0.0,0.0,1.0,false)==null)
            throw "Wheel hinge creation failed";
        wheelCoupling=eve.VolumeFluidCoupling();
        local paddle=checked(fluids.volumeColliderDefaults());
        paddle.shape=1;paddle.center=[0.0,0.0,0.0];paddle.halfExtent=[0.4,0.05,0.1];paddle.friction=0.5;
        paddle.label=1;checked(wheelCoupling.attach(wheelBody,paddle));
        paddle.label=2;paddle.rotation=[0.0,0.0,0.70710678118,0.70710678118];
        checked(wheelCoupling.attach(wheelBody,paddle));
        nozzle.description.origin=[0.25,2.0,0.0];nozzle.description.direction=[0.0,-1.0,0.0];
        nozzle.description.speed=2.0;nozzle.description.extent=[0.05,0.05,0.05];
        // The original FluidMill uses a sustained particle emitter. A bounded
        // rate controller forms a continuous sheet while capping each step.
        emitter=eve.VolumeFluidEmitter();
    }
    if(mode == 5) {
        local model = model3d.newModelDataFromFile("assets/emission-ring.obj");
        local shape = checked(fluids.volumeEmissionFromModel(model, 0, 1.0, 1.0, 1.0, definition.settings.spacing));
        shape.description.origin = [0.0, 1.3, 0.0];
        shape.description.direction = [0.0, 0.0, 1.0];
        shape.description.speed = 0.0;
        checked(solver.emitBurst(shape, shape.description.distribution.len()));
    }
    accumulator = 0.0;
    simulationTime = 0.0;
    dirty = true;
    droppedTime = 0.0;
    failure = "";
    simulationMs = 0.0;
    reconstructionMs = 0.0;
    uploadMs = 0.0;
    slowWorkCount = 0;
    paused = true;
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.04, 0.065, 1.0);
    surface = fluids.newSurfaceRenderer(384, 288);
    // Build the existing fluids SSF backend before per-frame timing starts.
    checked(surface.prepare());
    surface.setCameraXYZ(3.4, 3.0, 5.0, 0.0, 0.9, 0.0, 48.0);
    imageModule = eve.Image();
    pixels = imageModule.newEmptyImageData(384, 288, "RGBA8");
    refractionBackground=imageModule.newEmptyImageData(384,288,"RGBA8");
    // Sparse high-contrast grid: fewer than 10k script/native calls at startup,
    // while still making normal-driven refraction displacement obvious.
    for(local x=0;x<384;x+=24) for(local y=0;y<288;++y)
        refractionBackground.setPixel(x,y,0.22,0.48,0.72,1.0);
    for(local y=0;y<288;y+=24) for(local x=0;x<384;++x)
        refractionBackground.setPixel(x,y,0.42,0.24,0.16,1.0);
    texture = gfx.newTexture(pixels, false, false);
    local white=imageModule.newEmptyImageData(1,1,"RGBA8");white.setPixel(0,0,1.0,1.0,1.0,1.0);
    wheelTexture=gfx.newTexture(white,false,false);
    local obstaclePixels=imageModule.newEmptyImageData(32,32,"RGBA8");
    for(local y=0;y<32;++y)for(local x=0;x<32;++x) {
        local dx=x-15.5;local dy=y-15.5;
        if(dx*dx+dy*dy<=240.25)obstaclePixels.setPixel(x,y,0.72,0.35,0.12,1.0);
    }
    obstacleTexture=gfx.newTexture(obstaclePixels,false,false);
    local fontData = eve.Font().newFontDataFromFile("fonts/DejaVuSans-Bold.ttf", 18);
    labelFont = gfx.newFont(fontData, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789|:.-");
    gfx.setFont(labelFont);
    rebuild(0);
};

eve_update = function(dt) {
    if (mode == 15 && !mazeStateValid()) rebuild(15);
    if (mode == 16 && !bucketStateValid()) rebuild(16);
    if (mode == 17 && !bottleStateValid()) rebuild(17);
    if (pressed("space")) paused = !paused;
    if (pressed("r")) rebuild(mode);
    for (local i = 0; i < 9; ++i) if (pressed((i + 1).tostring())) rebuild(i);
    if (pressed("0")) rebuild(9);
    if (pressed("t")) rebuild(10);
    if (pressed("s")) rebuild(11);
    if (pressed("f")) rebuild(12);
    if (mode != 16 && mode != 17 && pressed("d")) rebuild(13);
    if (pressed("q")) rebuild(14);
    if (pressed("m")) rebuild(15);
    if (pressed("b")) rebuild(16);
    if (pressed("w")) rebuild(17);
    if (pressed("k")) rebuild(18);
    // Bound work per presentation frame. Overload slows simulation time instead
    // of accumulating an unbounded catch-up queue; show discarded wall time.
    local presentationInterval = mode == 2 ? 1.0 / 15.0 : 1.0 / 30.0;
    displayAccumulator += dt;
    if (displayAccumulator > presentationInterval) displayAccumulator = presentationInterval;
    try {
        local frameWorkMs = 0.0;
        local simulatedThisFrame = false;
        // Alternate expensive CPU simulation and synchronous SSF readback.
        // `dirty` means the latest simulated state still needs presenting;
        // never let another simulation step starve that reconstruction.
        if (!paused && failure == "" && !dirty) {
            accumulator += dt;
            if (accumulator >= 1.0 / 60.0) {
                local simulationStart = clock();
                if(mode == 8) {
                    wheelContacts+=checked(wheelCoupling.step(wheelWorld,solver,1.0/60.0,2));
                    wheelWorld.update(1.0/60.0);
                    advanceWheelJet(1.0/60.0);
                } else if (mode == 10) {
                    checked(solver.stepWithThermalContacts(1.0 / 60.0, 4, thermalRules));
                    checked(solver.applyMaterialChannelsWithColors(viscosityColors));
                } else if (mode == 11) advanceAttachment(simulationTime);
                else if (mode == 13) {
                    sdfPose.position = [0.2 * sin(simulationTime), 0.72, 0.0];
                    sdfPose.velocity = [0.2 * cos(simulationTime), 0.0, 0.0];
                    checked(solver.updateSdfColliderPoses([sdfPose]));
                    checked(solver.step(1.0 / 60.0, 2));
                }
                else if (mode == 15) {
                    if (keyboard.isDown("a")) maze.angularSpeed += 5.0 / 60.0;
                    if (keyboard.isDown("d")) maze.angularSpeed -= 5.0 / 60.0;
                    maze.angularSpeed *= pow(0.8,1.0/60.0);
                    if (fabs(maze.angularSpeed) < 0.0001) maze.angularSpeed = 0.0;
                    maze.angle += maze.angularSpeed / 60.0;
                    if (maze.angularSpeed != 0.0) {
                        fluidMazeUpdateColliderPoses(maze.walls,maze.colliders,maze.angle);
                        checked(solver.setColliders(maze.colliders));
                    }
                    checked(solver.step(1.0/60.0,2));
                    fluidMazeApplyTriggers(maze,solver);
                }
                else if(mode == 16) {
                    local target=keyboard.isDown("d") ? 1.45 : 0.0;
                    local limit=(target>bucket.angle ? 0.872664626 : 1.745329252)/60.0;
                    local delta=target-bucket.angle;
                    if(delta>limit)delta=limit;else if(delta < -limit)delta=-limit;
                    if(fabs(delta)>0.000001) {
                        bucket.angle+=delta;
                        fluidBucketUpdateColliderPoses(bucket.walls,bucket.colliders,bucket.angle);
                        checked(solver.setColliders(bucket.colliders));
                    }
                    checked(solver.step(1.0/60.0,2));
                    bucket.emitted+=checked(emitter.advance(solver,nozzle,1.0/60.0,2,0.25));
                    if(solver.getParticleCount()>bucket.peakActive)bucket.peakActive=solver.getParticleCount();
                }
                else if(mode == 17) {
                    local target=keyboard.isDown("d") ? 2.05 : 0.0;
                    local maxSpeed=target>bottle.angle ? 0.785398163 : 1.570796327;
                    local delta=target-bottle.angle; local limit=maxSpeed/60.0;
                    if(delta>limit)delta=limit;else if(delta < -limit)delta=-limit;
                    if(fabs(delta)>0.000001) {
                        bottle.angle+=delta;bottle.pose=fluidBottlePose(bottle.angle,delta*60.0);
                        checked(solver.updateSdfColliderPoses([bottle.pose]));
                    }
                    checked(solver.step(1.0/60.0,2));
                }
                else if(mode == 18) {
                    checked(solver.step(1.0/60.0,2));
                    local contacts=checked(solver.contacts()).len();
                    if(contacts>karman.peakContacts)karman.peakContacts=contacts;
                    if((simulationTime*60.0).tointeger()%6==0)fluidKarmanSampleWake(karman,solver);
                }
                else checked(solver.step(1.0 / 60.0, 2));
                if (mode == 14) updateOverlapTriggers(simulationTime);
                if (mode == 9) checked(solver.applyMaterialChannels());
                if (mode == 12) {
                    checked(foamPool.advance(solver, 1.0/60.0, 4));
                    checked(foamGenerator.advance(solver, foamPool, 1.0/60.0));
                }
                if (mode == 4) checked(emitter.advance(solver, nozzle, 1.0 / 60.0, 16, 0.5));
                if (mode == 6) checked(emitter.advanceMoving(solver, nozzle,
                    movingPose(simulationTime), movingPose(simulationTime + 1.0 / 60.0),
                    1.0 / 60.0, 16, 0.5, 1.0));
                simulationMs = (clock() - simulationStart) * 1000.0;
                frameWorkMs = simulationMs;
                simulatedThisFrame = true;
                simulationTime += 1.0 / 60.0;
                accumulator -= 1.0 / 60.0;
                if (accumulator >= 1.0 / 60.0) {
                    droppedTime += accumulator;
                    accumulator = 0.0;
                }
                dirty = true;
                // Do not add a synchronous reconstruction to an already
                // expensive simulation frame.
                if (simulationMs > 250.0)
                    throw "Fluid simulation exceeded 250 ms budget";
            }
        } else {
            accumulator = 0.0;
        }
        // A paused/static frame reuses its texture. Reconstruction/readback is
        // currently synchronous, so submit it at most 30 times per second.
        if (dirty && !simulatedThisFrame && displayAccumulator >= presentationInterval && failure == "") {
            local reconstructionStart = clock();
            local presented = false;
            if (mode == 2) checked(surface.renderGasVolume(solver, 5.0));
            else if (mode == 12 || mode == 17) surface.renderVolume(solver);
            else {
                checked(surface.renderVolumeColorToTexture(solver, gfx, texture));
                presented = true;
            }
            if (mode == 12) {
                local foamStart = clock();
                checked(surface.compositeDiffuse(foamPool, 0.035, 0.9, 0.5));
                foamCompositeMs = (clock()-foamStart)*1000.0;
            }
            if(mode == 17) checked(surface.compositeConfiguredSceneRefraction(refractionBackground));
            reconstructionMs = (clock() - reconstructionStart) * 1000.0;
            local uploadStart = clock();
            if (!presented) checked(surface.copyToTexture(gfx, texture));
            uploadMs = (clock() - uploadStart) * 1000.0;
            frameWorkMs += reconstructionMs + uploadMs;
            dirty = false;
            displayAccumulator = 0.0;
        }
        if (frameWorkMs > 0.0) checkWorkBudget(frameWorkMs);
    } catch (error) {
        failure = error.tostring();
        paused = true;
        print("VOLUME_FLUID_STOPPED: " + failure + "\n");
    }
};

eve_render = function() {
    gfx.clear();
    if(mode == 15 && mazeStateValid()) {
        foreach(wall in maze.walls) {
            local center=fluidMazePoint(wall.center,maze.angle);
            gfx.drawTexturedRectRotated(wheelTexture,480.0+center[0]*161.0,
                360.0-(center[1]-1.2)*161.0,wall.half[0]*322.0,wall.half[1]*322.0,
                -maze.angle*180.0/3.14159265359,0.22,0.28,0.34,1.0);
        }
    }
    if(mode == 16 && bucketStateValid()) {
        foreach(wall in bucket.walls) {
            local center=fluidBucketPoint(wall.center,bucket.angle);
            gfx.drawTexturedRectRotated(wheelTexture,480.0+center[0]*161.0,
                360.0-(center[1]-1.1)*161.0,wall.half[0]*322.0,wall.half[1]*322.0,
                -bucket.angle*180.0/3.14159265359,0.30,0.36,0.42,1.0);
        }
    }
    if(mode == 8 && wheelBody != null) {
        local angle=-2.0*atan2(wheelBody.getRotZ(),wheelBody.getRotW())*180.0/3.14159265359;
        drawMillStructure(angle,false);
    }
    gfx.drawTexturedRect(texture, 0.0, 0.0, 960.0, 720.0, 1.0, 1.0, 1.0, 1.0);
    if(mode == 8 && wheelBody != null) {
        local angle=-2.0*atan2(wheelBody.getRotZ(),wheelBody.getRotW())*180.0/3.14159265359;
        drawMillStructure(angle,true);
    }
    if(mode == 18)gfx.drawTexturedRect(obstacleTexture,440.0,325.0,70.0,70.0,1.0,1.0,1.0,1.0);
    if(mode == 17 && bottleStateValid()) {
        local body=fluidBottlePoint([0.0,0.68,0.0],bottle.angle);
        local neck=fluidBottlePoint([0.0,1.62,0.0],bottle.angle);
        gfx.drawTexturedRectRotated(wheelTexture,480.0+body[0]*183.0,360.0-(body[1]-0.95)*183.0,
            201.0,250.0,-bottle.angle*180.0/3.14159265359,0.42,0.65,0.76,0.18);
        gfx.drawTexturedRectRotated(wheelTexture,480.0+neck[0]*183.0,360.0-(neck[1]-0.95)*183.0,
            67.0,118.0,-bottle.angle*180.0/3.14159265359,0.48,0.72,0.82,0.22);
    }
    gfx.print("FLUID3D LAB", 24.0, 20.0, 0.8, 0.9, 1.0, 1.0, 1.4);
    gfx.print((paused ? "Paused" : "Running") + " | particles: " + solver.getParticleCount() +
        " | dropped seconds: " + format("%.2f", droppedTime), 24.0, 58.0, 0.8, 0.9, 1.0, 1.0, 1.0);
    gfx.print(format("Last work ms | Sim %.1f | Surface %.1f | Upload %.1f",
        simulationMs, reconstructionMs, uploadMs), 24.0, 90.0, 0.8, 0.9, 1.0, 1.0, 1.0);
    if (failure != "") gfx.print("Simulation stopped after error. See log.", 24.0, 125.0, 1.0, 0.4, 0.3, 1.0, 1.0);
    gfx.print("1 Water 2 Viscous 3 Smoke 4 Granular 5 Jet 6 Mesh 7 Moving", 24.0, 650.0, 0.7, 0.8, 0.9, 1.0, 1.0);
    gfx.print("8 Multiphase 9 Wheel 0 Mixing T Thermal S Solid F Foam D SDF Q Query M Maze B Bucket W Bottle K Wake", 24.0, 680.0, 0.7, 0.8, 0.9, 1.0, 0.72);
    if (mode == 10) gfx.print("Left: heating | Right: cooling | Color follows viscosity", 24.0, 150.0, 0.9, 0.8, 0.5, 1.0, 1.0);
    if (mode == 11) gfx.print("S Solid attachment: follows moving collider", 24.0, 150.0, 0.9, 0.8, 0.5, 1.0, 1.0);
    if (mode == 12) gfx.print("Foam: "+foamPool.getParticleCount()+" | Composite ms: "+format("%.2f",foamCompositeMs),
        24.0, 150.0, 0.9, 0.9, 1.0, 1.0, 1.0);
    if (mode == 13) gfx.print("D Moving SDF obstacle: transform-only updates, existing SSF surface", 24.0, 150.0, 0.9, 0.8, 0.5, 1.0, 1.0);
    if (mode == 14) gfx.print("Q Overlap triggers | red: "+overlapCounts[0]+" | yellow: "+overlapCounts[1],
        24.0,150.0,0.9,0.8,0.5,1.0,1.0);
    if (mode == 15) gfx.print("M FluidMaze | A/D rotate | completed: "+maze.finished.len()+
        " | contaminated: "+maze.colored.len(),24.0,150.0,0.9,0.8,0.5,1.0,1.0);
    if(mode == 16)gfx.print("B FaucetAndBucket | hold D to pour | emitted: "+bucket.emitted+
        " | active: "+solver.getParticleCount()+" | peak: "+bucket.peakActive,
        24.0,150.0,0.9,0.8,0.5,1.0,1.0);
    if(mode == 17)gfx.print("W WhiskeyBottle | hold D to pour | pose-only SDF | scene refraction",
        24.0,150.0,0.9,0.8,0.5,1.0,1.0);
    if(mode == 18)gfx.print("K FluidKarmanVortex | sphere wake | contacts: "+karman.peakContacts+
        " | peak curl: "+format("%.2f",karman.peakWakeCurl),24.0,150.0,0.9,0.8,0.5,1.0,1.0);
    if (mode == 2) gfx.print("Smoke: gas-only blurred thickness, CPU bounded path", 24.0, 150.0, 0.9, 0.8, 0.5, 1.0, 1.0);
    if(mode == 8) gfx.print("Wheel rad per sec: "+format("%.3f",wheelBody.getAngularVelocityZ())+
        " | contacts: "+wheelContacts,24.0,125.0,0.9,0.7,0.4,1.0,1.0);
};
