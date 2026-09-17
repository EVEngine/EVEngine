// Opt-in setup/runtime contract probe for Fluid3D sphere/cube distribution shapes.
function runShapeDistributionCheck() {
    local sphere=checked(fluids.volumeEmissionFromSphere(0.2,0.1,true));
    local cube=checked(fluids.volumeEmissionFromCube(0.2,0.3,0.4,0.1,false));
    local edge=checked(fluids.volumeEmissionFromEdge(0.5,0.1,30.0));
    local disk=checked(fluids.volumeEmissionFromDisk(0.2,0.1,true));
    if(sphere.version!=11||cube.version!=11||sphere.description.randomVelocity!=0.0||sphere.description.distribution.len()==0||
       cube.description.distribution.len()!=60||edge.description.distribution.len()!=5||
       disk.description.distribution.len()==0)
        throw "Analytic distribution shape mismatch";
    local point=sphere.description.distribution[0];
    if(point.direction.len()!=3)throw "Distribution direction missing";
    local definition=checked(fluids.volumeDefaults());definition.settings.gravity=[0.0,0.0,0.0];
    local testSolver=checked(fluids.newVolumeSimulator(definition));
    sphere.description.origin=[0.0,1.0,0.0];sphere.description.speed=2.0;
    if(checked(testSolver.emitBurst(sphere,1))!=1||testSolver.getParticleCount()!=1)
        throw "Sphere distribution burst failed";
    if(edge.description.distribution[3].direction[1]>-0.999||
       fabs(disk.description.distribution[0].position[0])>0.201)
        throw "Edge radial velocity or disk edge emission mismatch";
    local randomNozzle=checked(fluids.volumeEmissionDefaults());randomNozzle.description.origin=[0.2,1.0,0.0];
    randomNozzle.description.direction=[0.0,0.0,1.0];randomNozzle.description.speed=2.0;
    randomNozzle.description.randomVelocity=1.0;randomNozzle.description.seed=77;
    if(checked(testSolver.emitBurst(randomNozzle,1))!=1)throw "Random velocity burst failed";
    local randomParticle=checked(testSolver.snapshot()).particles[1];
    local speed=sqrt(randomParticle.velocity[0]*randomParticle.velocity[0]+randomParticle.velocity[1]*randomParticle.velocity[1]+randomParticle.velocity[2]*randomParticle.velocity[2]);
    if(fabs(speed-2.0)>0.0001||randomParticle.velocity[2]>1.999)throw "Fluid3D randomVelocity blend mismatch";
    print("VOLUME_SHAPE_DISTRIBUTION_PASS sphere="+sphere.description.distribution.len()+
          " cube="+cube.description.distribution.len()+" edge="+edge.description.distribution.len()+
          " disk="+disk.description.distribution.len()+"\n");
}

runShapeDistributionCheck();
