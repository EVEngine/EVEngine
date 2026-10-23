// Low silhouette, split trails, angular shield and long high-velocity barrel.
export function buildAntiTankGun(m,a) {
    local green=[0.32,0.38,0.25],edge=[0.43,0.47,0.31],metal=[0.18,0.21,0.19],black=[0.055,0.065,0.06];
    a.armed=true; a.gunY=1.13; a.gunZ=0.32; a.barrel=4.15;
    m.setJointPivot("turret",0,0.8,0); m.setJointPivot("barrel",0,0.33,0.32);
    m.roundTube(0,0.62,0,0.16,2.65,metal,"body",1.5708);
    foreach(s in [-1,1]) {
        m.roundTube(s*1.3,0.61,0,0.59,0.32,black,"body",1.5708);
        m.roundTube(s*1.48,0.61,0,0.37,0.05,green,"body",1.5708);
        m.roundTube(s*1.52,0.61,0,0.14,0.10,metal,"body",1.5708);
        for(local i=0;i<12;i++) {
            local t=i*0.5236;
            m.armorBox(s*1.3,0.61+sin(t)*0.59,cos(t)*0.59,0.34,0.10,0.19,metal,"body",0,t,0);
            if(i%2==0) m.roundTube(s*1.52,0.61+sin(t)*0.25,cos(t)*0.25,0.035,0.03,metal,"body",1.5708);
        }
        m.braceBeam([s*0.2,0.66,-0.2],[s*1.35,0.12,-3.0],0.21,green);
        m.armorBox(s*1.35,0.13,-3.0,0.55,0.24,0.28,metal);
        m.armorBox(s*1.32,0.35,-2.7,0.07,0.38,0.07,edge);
        m.armorBox(s*0.67,0.31,0.38,1.0,1.35,0.12,green,"turret",0,0,s*0.15);
        m.armorBox(s*0.67,0.95,0.38,0.94,0.08,0.15,edge,"turret");
    }
    m.armorBox(0,0.12,0.38,0.42,0.75,0.16,green,"turret");
    m.armorBox(0,-0.29,0.38,2.1,0.25,0.10,green,"turret");
    m.armorBox(0,0.28,-0.2,0.45,0.45,0.9,metal,"turret");
    m.roundTube(-0.46,0.28,-0.35,0.22,0.04,edge,"turret",1.5708);
    for(local i=0;i<4;i++) {
        local t=i*1.5708;
        m.braceBeam([-0.49,0.28,-0.35],[-0.49,0.28+sin(t)*0.19,-0.35+cos(t)*0.19],0.035,metal,"turret");
    }
    m.armorBox(-0.34,0.61,-0.2,0.10,0.13,0.45,metal,"turret");
    m.profileTube(0,0,0,[[-0.9,0.20],[-0.2,0.20],[0.0,0.16],[0.6,0.15],[3.8,0.09],[4.15,0.09]],metal,"barrel");
    m.armorBox(0,0,4.0,0.31,0.26,0.40,green,"barrel");
    foreach(s in [-1,1]) for(local i=0;i<3;i++) m.armorBox(s*0.16,0,3.88+i*0.1,0.012,0.14,0.05,black,"barrel");
    m.roundTube(0,0,4.205,0.07,0.012,black,"barrel");
    m.roundTube(0.18,-0.19,0.7,0.08,1.6,edge,"barrel");
}
