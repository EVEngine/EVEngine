// Four-turn concertina coil: two twisted strands, crossed barbs and braced posts.
export function buildBarbedWire(m,a) {
    local wire=[0.26,0.31,0.31],bright=[0.43,0.46,0.42],iron=[0.17,0.22,0.21];
    local previous=[null,null];
    for(local i=0;i<=96;i++) {
        local t=i/96.0,ang=t*25.132741;
        local x=-2.25+t*4.5,y=0.65+sin(ang)*0.48,z=cos(ang)*0.48;
        for(local s=0;s<2;s++) {
            local twist=ang*3+s*3.14159265;
            local p=[x+sin(twist)*0.016,y+cos(twist)*0.016,z];
            if(previous[s]!=null) m.roundRod(previous[s],p,0.012,wire);
            previous[s]=p;
        }
        if(i%8==0) {
            local radial=[0.0,sin(ang),cos(ang)];
            foreach(s in [-1,1]) {
                m.roundRod([x-0.12,y-s*radial[1]*0.11,z-s*radial[2]*0.11],
                    [x+0.12,y+s*radial[1]*0.11,z+s*radial[2]*0.11],0.012,bright);
            }
            m.roundTube(x,y,z,0.032,0.047,iron,"body",1.5708);
        }
    }
    foreach(x in [-2.35,0.0,2.35]) {
        m.armorBox(x,0.04,0,0.30,0.08,0.30,iron);
        m.armorBox(x,0.69,0,0.065,1.30,0.065,iron);
        m.armorBox(x+0.035,0.69,0.025,0.065,1.30,0.035,wire);
        foreach(y in [0.25,0.65,1.04]) {
            m.roundTube(x,y,0.06,0.035,0.03,bright);
            m.roundRod([x-0.08,y,0.06],[x+0.08,y,0.06],0.012,bright);
        }
        m.roundRod([x,0.92,0],[x,0.08,-0.85],0.014,wire);
        m.armorBox(x,0.04,-0.85,0.18,0.08,0.25,iron);
        foreach(s in [-1,1]) m.roundTube(x+s*0.10,0.088,0.08,0.025,0.015,bright,"body",0,1.5708);
    }
    // Two tension wires keep the coil's opening and upper edge connected.
    foreach(y in [0.22,1.02]) m.roundRod([-2.35,y,0],[2.35,y,0],0.012,wire);
}
