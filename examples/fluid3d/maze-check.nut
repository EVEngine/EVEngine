// Opt-in FluidMaze acceptance. Loading this file does not run it.
function verifyVolumeFluidMaze() {
    paused = true;

    local walls = fluidMazeWalls();
    local colliders = fluidMazeColliders(walls,0.0);
    fluidMazeUpdateColliderPoses(walls,colliders,0.45);
    local expected = fluidMazePoint(walls[5].center,0.45);
    local actual = colliders[5].center;
    if (fabs(actual[0]-expected[0])>0.00001 || fabs(actual[1]-expected[1])>0.00001 ||
        fabs(colliders[5].rotation[2]-sin(0.225))>0.00001)
        throw "Maze collider group transform mismatch";

    local collisionDefinition = checked(fluids.volumeDefaults());
    collisionDefinition.settings.gravity = [0.0,0.0,0.0];
    collisionDefinition.settings.minimum = [-2.0,-1.0,-0.5];
    collisionDefinition.settings.maximum = [2.0,3.0,0.5];
    collisionDefinition.settings.iterations = 1;
    local collisionParticle = checked(fluids.volumeEmissionDefaults()).description.prototype;
    collisionParticle.position = expected;
    collisionParticle.material = clone collisionParticle.material;
    collisionParticle.material.cohesion = 0.0;
    collisionParticle.material.viscosity = 0.0;
    collisionDefinition.particles = [collisionParticle];
    collisionDefinition.colliders = [colliders[5]];
    local collisionSolver = checked(fluids.newVolumeSimulator(collisionDefinition));
    checked(collisionSolver.step(1.0/120.0,1));
    local contacts = checked(collisionSolver.contacts());
    if (contacts.len()!=1 || contacts[0].colliderLabel!=walls[5].label)
        throw "Rotated maze wall did not produce labelled contact";

    local triggerDefinition = checked(fluids.volumeDefaults());
    triggerDefinition.settings.gravity = [0.0,0.0,0.0];
    triggerDefinition.settings.minimum = [-2.0,-1.0,-0.5];
    triggerDefinition.settings.maximum = [2.0,3.0,0.5];
    local triggerPrototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
    triggerPrototype.material = clone triggerPrototype.material;
    triggerPrototype.material.cohesion = 0.0;
    triggerPrototype.material.viscosity = 0.0;
    local triggerParticles = [];
    foreach (position in [[0.82,2.02,0.0],[-0.82,1.24,0.0],[0.0,0.65,0.0]]) {
        local particle = clone triggerPrototype;
        particle.material = clone triggerPrototype.material;
        particle.position = position;
        triggerParticles.append(particle);
    }
    triggerDefinition.particles = triggerParticles;
    local triggerScene = {solver=checked(fluids.newVolumeSimulator(triggerDefinition)),angle=0.0,
        finished={},colored={}};
    local first = fluidMazeApplyTriggers(triggerScene);
    if (first.color0!=1 || first.color1!=1 || first.finish!=1 ||
        first.coloredTotal!=2 || first.finishedTotal!=1)
        throw "Maze color/finish trigger counts mismatch";
    local colored = checked(triggerScene.solver.snapshot()).particles;
    triggerScene.angle = 1.4;
    local second = fluidMazeApplyTriggers(triggerScene);
    local preserved = checked(triggerScene.solver.snapshot()).particles;
    for (local channel=0;channel<4;++channel)
        if (preserved[0].color[channel]!=colored[0].color[channel] ||
            preserved[1].color[channel]!=colored[1].color[channel])
            throw "Maze contamination did not persist outside trigger";
    if (preserved[0].color[0]<0.9 || preserved[1].color[2]<0.9)
        throw "Maze contamination color mismatch";
    if (second.finishedTotal < 1 || second.coloredTotal < 2)
        throw "Maze cumulative score lost prior particle identities";

    rebuild(15);
    maze.angularSpeed = 0.7;
    local peakContacts = 0;
    for (local step = 0; step < 180; ++step) {
        maze.angle += maze.angularSpeed/120.0;
        if (step==90) maze.angularSpeed=-0.7;
        fluidMazeUpdateColliderPoses(maze.walls,maze.colliders,maze.angle);
        checked(solver.setColliders(maze.colliders));
        checked(solver.step(1.0/120.0,1));
        local contactCount = checked(solver.contacts()).len();
        if (contactCount>peakContacts) peakContacts=contactCount;
        fluidMazeApplyTriggers(maze,solver);
    }
    if (peakContacts<=0 || solver.getParticleCount()!=18)
        throw "Interactive maze did not retain bounded fluid/collider contacts";
    dirty = true;
    return "VOLUME_FLUID_MAZE_PASS walls="+walls.len()+" transformedContact="+
        contacts.len()+" colored="+first.coloredTotal+" finished="+first.finishedTotal+
        " persistent="+second.coloredTotal+" sceneFinished="+maze.finished.len()+
        " scenePeakContacts="+peakContacts;
}
