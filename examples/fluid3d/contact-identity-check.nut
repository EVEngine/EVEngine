// Opt-in check for Fluid3DContactEventDispatcher identity data.
function verifyVolumeFluidContactIdentity() {
    local definition = checked(fluids.volumeDefaults());
    local particle = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    particle.position = [0.0, 1.25, 0.0];
    particle.actorGroup = 23;
    definition.particles = [particle];
    local collider = checked(fluids.volumeColliderDefaults());
    collider.label = 9;
    definition.colliders = [collider];
    local source = checked(fluids.newVolumeSimulator(definition));
    checked(source.step(1.0 / 120.0, 1));
    local contacts = checked(source.contacts());
    if (contacts.len() == 0 || contacts[0].colliderLabel != 9 ||
        contacts[0].particleIndex != 0 || contacts[0].actorGroup != 23)
        throw "Contact dispatcher identity is incomplete";
    local tracker = eve.VolumeFluidContactTracker();
    local entered = checked(tracker.advance(source, 0.01));
    if (entered.len() != 1 || entered[0].type != 0)
        throw "Contact enter event is incorrect";
    local stayed = checked(tracker.advance(source, 0.01));
    if (stayed.len() != 1 || stayed[0].type != 1)
        throw "Contact stay event is incorrect";
    source.clear();
    local exited = checked(tracker.advance(source, 0.01));
    if (exited.len() != 1 || exited[0].type != 2)
        throw "Contact exit event is incorrect";
    return true;
}
