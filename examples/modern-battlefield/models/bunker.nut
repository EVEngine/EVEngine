// Hollow concrete pillbox with three bevelled embrasures and a protected rear entry.
export function buildBunker(m,a) {
    local concrete=[0.39,0.41,0.38],edge=[0.49,0.49,0.43],joint=[0.24,0.27,0.25],metal=[0.18,0.23,0.21],sand=[0.50,0.42,0.28];
    m.armorBox(0,0.08,0,5.25,0.16,4.50,concrete);
    m.armorBox(0,0.18,0,3.8,0.09,3.5,joint);
    foreach(s in [-1,1]) {
        m.armorBox(s*2.04,1.30,0,0.66,2.34,3.85,concrete);
        m.armorBox(s*1.97,1.30,1.80,0.80,2.34,0.62,concrete);
        m.armorBox(s*1.40,1.24,-1.80,1.48,2.22,0.58,concrete);
        for(local i=0;i<3;i++) m.armorBox(s*2.38,1.28,-1.1+i*1.1,0.009,2.16,0.012,joint);
        m.armorBox(s*2.386,0.61,0.0,0.012,0.012,3.62,joint);
        m.armorBox(s*2.386,1.56,0.0,0.012,0.012,3.62,joint);
    }
    m.armorBox(0,0.66,1.81,3.35,0.98,0.62,concrete);
    m.armorBox(0,2.06,1.81,3.35,0.88,0.62,concrete);
    foreach(x in [-0.53,0.53]) m.armorBox(x,1.40,1.81,0.16,0.53,0.62,concrete);
    foreach(x in [-1.06,0.0,1.06]) {
        local w=0.45,b=1.17,t=1.63,zz=2.125,iw=0.31,ib=1.26,it=1.54,iz=1.49;
        m.bakeFaces([[x-w,b,zz],[x+w,b,zz],[x+w,t,zz],[x-w,t,zz],
            [x-iw,ib,iz],[x+iw,ib,iz],[x+iw,it,iz],[x-iw,it,iz]],
            [[0,1,5,4],[1,2,6,5],[2,3,7,6],[3,0,4,7]],joint);
        m.armorBox(x,1.16,2.15,0.96,0.09,0.22,edge);
    }
    m.armorBox(0,2.18,-1.80,1.2,0.35,0.58,concrete);
    // Offset blast wall leaves a walk-in passage instead of a painted-on door.
    m.armorBox(-0.55,1.05,-2.75,2.7,1.82,0.33,concrete);
    m.armorBox(0.48,1.04,-2.13,0.90,1.65,0.10,metal,"body",-0.65);
    m.armorBox(0.20,1.0,-2.48,0.09,0.10,0.20,edge);
    foreach(z in [-2.05,-2.42,-2.8]) m.armorBox(0.55,0.10,z,1.35,0.20,0.36,edge);
    m.armorBox(0,1.86,-2.12,0.42,0.12,0.10,metal);
    m.armorBox(0,1.85,-2.18,0.30,0.06,0.015,[0.70,0.67,0.48]);
    m.hullLoft([[-2.15,2.30,2.36,2.73],[-1.85,2.65,2.35,2.90],[1.85,2.65,2.35,2.90],[2.20,2.30,2.36,2.73]],concrete,"body",0.28);
    foreach(s in [-1,1]) for(local i=0;i<3;i++) m.filledBag(s*2.04,3.02,-0.95+i*0.80,0.90,0.27,0.56,sand,1.5708);
    for(local i=0;i<4;i++) m.filledBag(-1.3+i*0.87,3.02,1.62,0.95,0.27,0.56,sand);
    m.roundTube(1.25,3.09,-1.10,0.17,0.70,metal,"body",0,1.5708);
    for(local i=0;i<4;i++) m.roundTube(1.25,3.15+i*0.08,-1.1,0.25,0.035,joint,"body",0,1.5708);
    m.armorBox(1.25,3.48,-1.1,0.57,0.07,0.57,metal);
    m.stencilGlyphs("B",-1.95,1.95,2.12,0.04,edge);
    foreach(s in [-1,1]) {
        m.braceBeam([s*1.85,0.4,2.125],[s*1.72,0.76,2.125],0.012,joint);
        m.braceBeam([s*1.72,0.76,2.125],[s*1.86,0.96,2.125],0.010,joint);
    }
}
