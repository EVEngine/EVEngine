// Czech-style anti-tank obstacle: three open I-sections and riveted centre gussets.
export function buildHedgehog(m,a) {
    local iron=[0.17,0.21,0.20],plate=[0.25,0.28,0.24],rivet=[0.38,0.37,0.28];
    m.structuralIbeam(0,1.11,0,2.80,1.5708,-0.7854,0,iron);
    m.structuralIbeam(0,1.11,0,2.80,1.5708,0.7854,0,iron);
    m.structuralIbeam(0,1.11,0,2.80,0,0,0,iron);
    foreach(s in [-1,1]) {
        m.armorBox(0,1.11,s*0.22,0.58,0.58,0.07,plate,"body",0,0,0.7854);
        foreach(x in [-0.18,0.18]) foreach(y in [-0.18,0.18])
            m.roundTube(x,1.11+y,s*0.27,0.045,0.035,rivet);
        m.roundTube(0,1.11,s*0.275,0.065,0.04,rivet);
        m.armorBox(s*0.60,1.72,0.19,0.23,0.055,0.02,[0.42,0.38,0.24],"body",0,0,s*0.7854);
    }
}
