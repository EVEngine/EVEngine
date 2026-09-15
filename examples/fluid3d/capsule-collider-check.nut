// Opt-in Fluid3D-compatible analytic capsule collision check.
function verifyVolumeFluidCapsuleCollider() {
    local definition=checked(fluids.volumeDefaults());
    definition.settings.gravity=[0.0,0.0,0.0];
    definition.settings.minimum=[-1.0,-1.0,-1.0];
    definition.settings.maximum=[1.0,1.0,1.0];
    local solver=checked(fluids.newVolumeSimulator(definition));
    local collider=checked(fluids.volumeColliderDefaults());
    collider.label=81;collider.shape=2;collider.center=[0.0,0.0,0.0];
    collider.radius=0.1;collider.halfExtent=[0.1,0.3,0.1];
    collider.rotation=[0.0,0.0,0.70710678,0.70710678];
    collider.angularVelocity=[0.0,0.0,4.0];collider.friction=1.0;
    checked(solver.setColliders([collider]));
    local particle=checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position=[0.15,0.11,0.0];particle.material.cohesion=0.0;particle.material.viscosity=0.0;
    checked(solver.emit([particle]));checked(solver.step(1.0/120.0,1));
    local contacts=checked(solver.contacts());local state=checked(solver.snapshot());
    if(contacts.len()!=1||contacts[0].colliderLabel!=81||contacts[0].normal[1]<0.99||
       state.particles[0].position[1]<0.1499||state.colliders[0].shape!=2)
        throw "Rotated capsule projection or codec mismatch";
    return true;
}
