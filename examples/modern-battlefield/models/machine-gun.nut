// Belt-fed heavy machine gun on a traversing tripod mount.
export function buildMachineGun(m,a) {
    local metal=[0.15,0.18,0.18],edge=[0.26,0.29,0.26],green=[0.30,0.36,0.24],black=[0.035,0.04,0.04],brass=[0.55,0.44,0.20];
    a.armed=true; a.gunY=1.12; a.gunZ=0.0; a.barrel=1.50; a.elevation=0.05;
    m.setJointPivot("turret",0,0.85,0); m.setJointPivot("barrel",0,0.27,0);
    foreach(t in [0.0,2.0944,4.1888]) {
        local x=sin(t)*0.72,z=cos(t)*0.72;
        m.braceBeam([0,0.85,0],[x,0.08,z],0.07,green);
        m.armorBox(x,0.045,z,0.19,0.08,0.17,metal);
        m.braceBeam([x*0.75,0.25,z*0.75],[0,0.42,0],0.035,edge);
    }
    m.roundTube(0,0.73,0,0.10,0.36,metal,"body",0,1.5708);
    m.armorBox(0,0.09,0,0.30,0.25,0.32,green,"turret");
    m.roundTube(0.18,0.26,0,0.12,0.045,edge,"turret",1.5708);
    m.armorBox(0,0,-0.34,0.20,0.22,0.70,metal,"barrel");
    m.armorBox(0,0.13,-0.32,0.23,0.05,0.62,edge,"barrel");
    for(local i=0;i<9;i++) m.armorBox(0,0.17,-0.58+i*0.065,0.16,0.03,0.025,black,"barrel");
    foreach(s in [-1,1]) {
        m.braceBeam([s*0.09,-0.04,-0.67],[s*0.14,-0.22,-0.82],0.055,black,"barrel");
        m.armorBox(s*0.115,0,-0.3,0.025,0.07,0.40,edge,"barrel");
    }
    m.profileTube(0,0,0,[[0,0.09],[0.22,0.09],[0.3,0.065],[1.25,0.065],[1.32,0.095],[1.50,0.095]],metal,"barrel");
    m.roundTube(0,0,1.508,0.045,0.012,black,"barrel");
    for(local i=0;i<9;i++) foreach(s in [-1,1]) m.armorBox(s*0.066,0,0.35+i*0.09,0.012,0.035,0.045,black,"barrel");
    m.armorBox(0,0.12,1.17,0.025,0.17,0.055,edge,"barrel");
    m.armorBox(0,0.15,-0.12,0.07,0.13,0.07,edge,"barrel");
    m.armorBox(-0.32,-0.16,-0.25,0.36,0.38,0.48,green,"barrel");
    m.armorBox(-0.32,0.04,-0.25,0.39,0.04,0.51,edge,"barrel");
    m.braceBeam([-0.43,0.10,-0.25],[-0.23,0.10,-0.25],0.025,black,"barrel");
    for(local i=0;i<7;i++) {
        m.roundTube(-0.13-i*0.045,0.05-i*0.017,-0.03,0.018,0.12,brass,"barrel");
        m.armorBox(-0.13-i*0.045,0.05-i*0.017,-0.03,0.035,0.018,0.03,metal,"barrel");
    }
}
