// Low wedge hull, shallow gun mount and exceptionally long, unbraked cannon.
export function buildTankDestroyer(m,a) {
    local paint=[0.28,0.35,0.30],edge=[0.40,0.45,0.34],metal=[0.16,0.20,0.19],black=[0.035,0.045,0.04];
    a.armed=true; a.gunY=1.70; a.gunZ=1.0; a.barrel=5.8; a.elevation=0.08;
    m.setJointPivot("turret",0,1.26,0.35); m.setJointPivot("barrel",0,0.44,0.65);
    m.trackedRunningGear(5.8,1.48,6,paint);
    m.hullLoft([[-3.0,1.15,0.50,1.25],[-2.4,1.55,0.45,1.48],[0.2,1.55,0.45,1.6],[3.2,1.3,0.72,1.0]],paint);
    m.hullLoft([[-1.6,1.0,0.0,0.42],[-0.8,1.16,0,0.72],[0.4,0.94,0.0,0.61],[1.2,0.42,0.05,0.35]],paint,"turret",0.28);
    foreach(s in [-1,1]) {
        for(local i=0;i<6;i++) m.armorBox(s*1.75,1.2,-2.4+i*0.92,0.12,0.44,0.84,paint);
        m.armorBox(s*0.9,0.52,0.08,0.55,0.25,0.9,edge,"turret",s*0.38);
        m.armorBox(s*1.08,1.45,2.45,0.35,0.14,0.25,metal);
        m.armorBox(s*1.08,1.47,2.58,0.24,0.08,0.02,[0.8,0.79,0.62]);
        m.armorBox(s*1.05,1.53,-1.65,0.55,0.22,0.85,metal);
        for(local i=0;i<4;i++) m.armorBox(s*1.33,0.38,-1.0+i*0.25,0.16,0.16,0.20,edge,"turret");
        m.braceBeam([s*1.1,1.45,1.4],[s*1.1,1.3,2.8],0.055,metal);
    }
    for(local i=0;i<11;i++) m.armorBox(0,1.50,-2.5+i*0.1,1.5,0.05,0.045,metal);
    m.roundTube(0.32,0.72,-0.45,0.27,0.10,metal,"turret",0,1.5708);
    m.armorBox(-0.4,0.78,0.0,0.23,0.23,0.29,metal,"turret");
    m.armorBox(-0.4,0.81,0.15,0.18,0.12,0.02,[0.08,0.29,0.32],"turret");
    m.roundTube(-0.7,1.05,-0.8,0.017,0.8,metal,"turret",0,1.5708);
    m.profileTube(0,0,0,[[-0.2,0.30],[0.5,0.28],[0.62,0.16],[1.6,0.14],[1.75,0.19],[2.1,0.19],[2.2,0.13],[5.6,0.09],[5.8,0.11]],metal,"barrel");
    foreach(z in [0.7,1.3,3.1,4.3]) m.roundTube(0,0,z,z<2?0.17:0.12,0.065,edge,"barrel");
    m.roundTube(0,0,5.807,0.072,0.012,black,"barrel");
}
