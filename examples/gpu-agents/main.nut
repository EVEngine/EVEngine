// GPU Agents demo: register a fish school + petal cloud and step the World.

function eve_init() {
    ::gpuAgents <- eve.GpuAgents();
    ::world <- gpuAgents.newWorld();
    world.bakeEmptyObstacles(-12.0, -4.0, -12.0, 24, 16, 24, 1.0);
    world.carveSphere(0.0, 0.0, 0.0, 2.5);
    world.setWaterCurrent(0.15, 0.0, 0.05);
    world.setWind(0.4, 0.1, 0.0);
    world.setVerticalBounds(0.0, 30.0);
    world.initFlatSurface(-16.0, 0.0, -16.0, 32.0, 48);

    ::school <- gpuAgents.newBackend(0, 96);
    gpuAgents.spawnCloud(school, 64, 0.0, 1.0, 7.0, 2.5);
    gpuAgents.registerBackend(world, "school", school);

    ::petals <- gpuAgents.newBackend(3, 128);
    gpuAgents.spawnCloud(petals, 80, 0.0, 12.0, 0.0, 3.0);
    gpuAgents.registerBackend(world, "petals", petals);

    ::life <- gpuAgents.newBackend(1, 48);
    gpuAgents.spawnCloud(life, 24, 0.0, 0.0, 0.0, 5.0);
    gpuAgents.registerBackend(world, "life", life);

    ::acc <- 0.0;
    print("gpu-agents: school=" + school.aliveCount() +
          " petals=" + petals.aliveCount() +
          " life=" + life.aliveCount());
}

function eve_update(dt) {
    world.stepAll(dt);
    acc += dt;
    if (acc > 2.0) {
        acc = 0.0;
        print("gpu-agents tick schoolInst=" + school.instanceCount() +
              " petalInst=" + petals.instanceCount() +
              " lifeInst=" + life.instanceCount());
    }
}
