// Wide open gun emplacement: horseshoe earth bank, timber revetment and ammunition niches.
// Clear rear lane and central turntable remain available for an independently placed gun.
export function buildArtilleryPosition(m,a) {
    local dirt=[0.29,0.25,0.17],wood=[0.31,0.23,0.14],edge=[0.43,0.34,0.21],sand=[0.51,0.43,0.28],pad=[0.36,0.37,0.31],dark=[0.14,0.18,0.14];
    m.roundTube(0,0.01,0,4.8,0.04,dirt,"body",0,1.5708);
    m.roundTube(0,0.055,0,2.60,0.065,pad,"body",0,1.5708);
    for(local i=0;i<16;i++) {
        local t=i*0.392699;
        m.armorBox(sin(t)*2.31,0.092,cos(t)*2.31,0.035,0.006,0.22,[0.65,0.62,0.44],"body",t);
    }
    for(local i=0;i<14;i++) {
        local ang=-2.35+(i+0.5)*4.7/14,lo=ang-2.35/14,hi=ang+2.35/14;
        local v=[];
        foreach(t in [lo,hi]) foreach(q in [[3.65,0.035],[4.9,0.035],[4.06,1.04]])
            v.push([sin(t)*q[0],q[1],cos(t)*q[0]]);
        m.bakeFaces(v,[[1,2,0],[5,4,3],[2,5,3,0],[4,5,2,1],[3,4,1,0]],dirt);
        for(local row=0;row<4;row++) m.armorBox(sin(ang)*3.70,0.18+row*0.22,cos(ang)*3.70,
            0.10,0.20,1.31,wood,"body",ang+1.5708);
        m.roundTube(sin(lo)*3.60,0.54,cos(lo)*3.60,0.065,1.02,edge,"body",0,1.5708);
        for(local row=0;row<2;row++) {
            local t=ang+row*0.05;
            m.filledBag(sin(t)*4.03,1.12+row*0.25,cos(t)*4.03,1.30,0.27,0.62,sand,t);
        }
    }
    // Rear supply approach and drainage/duckboard strips.
    for(local i=0;i<9;i++) m.armorBox(-0.86+i*0.215,0.065,-3.78,0.19,0.055,2.35,wood);
    foreach(s in [-1,1]) {
        m.armorBox(s*1.15,0.05,-3.9,0.11,0.08,2.2,dark);
        // Built-in recess with overhead shelf, supporting posts and ready-round sockets.
        m.armorBox(s*3.48,0.53,-0.65,0.10,0.90,1.26,wood);
        foreach(z in [-1.25,-0.05]) m.armorBox(s*3.18,0.54,z,0.62,0.92,0.10,edge);
        foreach(y in [0.18,1.02]) m.armorBox(s*3.18,y,-0.65,0.72,0.10,1.35,edge);
        for(local i=0;i<3;i++) {
            local z=-1.03+i*0.37;
            m.profileTube(s*3.12,0.25,z,[[0,0.065],[0.34,0.065],[0.47,0.01]],[0.40,0.43,0.24],"body",0,-1.5708);
            m.roundTube(s*3.12,0.28,z,0.074,0.065,[0.52,0.41,0.20],"body",0,1.5708);
        }
        m.armorBox(s*2.85,0.24,-2.25,0.78,0.16,0.65,wood);
    }
    m.armorBox(2.4,0.96,-2.85,0.10,1.80,0.10,wood);
    m.armorBox(2.4,1.72,-2.80,0.68,0.29,0.065,edge);
    m.stencilGlyphs("AP",2.4,1.73,-2.762,0.027,dark);
}
