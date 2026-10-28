// Slatted shipping crate with separate boards, braces, corner irons and nail heads.
export function buildWoodenCrate(m,a) {
    local planks=[[0.36,0.24,0.12],[0.43,0.30,0.16],[0.49,0.35,0.19]],trim=[0.51,0.36,0.19];
    local iron=[0.19,0.22,0.20],grain=[0.23,0.15,0.08],shadow=[0.12,0.085,0.045];
    m.armorBox(0,0.50,0,1.18,0.92,0.92,shadow);
    foreach(s in [-1,1]) {
        for(local i=0;i<5;i++) {
            local x=-0.50+i*0.25;
            m.armorBox(x,0.50,s*0.48,0.235,0.93,0.065,planks[i%3]);
            for(local k=0;k<3;k++) {
                local xx=x-0.075+k*0.055;
                m.braceBeam([xx,0.22+(i%2)*0.10,s*0.516],[xx+0.008,0.73-(k%2)*0.08,s*0.516],0.007,grain);
            }
        }
        foreach(x in [-0.57,0.57]) {
            m.armorBox(x,0.5,s*0.54,0.13,1.02,0.075,trim);
            foreach(y in [0.10,0.91]) m.roundTube(x,y,s*0.582,0.019,0.014,iron);
        }
        foreach(y in [0.10,0.91]) m.armorBox(0,y,s*0.54,1.18,0.12,0.075,trim);
        m.braceBeam([-0.49,0.19,s*0.57],[0.49,0.83,s*0.57],0.095,trim);
        for(local row=0;row<4;row++) {
            m.armorBox(s*0.64,0.15+row*0.235,0,0.065,0.22,0.93,planks[(row+1)%3]);
            m.armorBox(s*0.675,0.17+row*0.235,-0.13,0.008,0.009,0.48,grain);
        }
        m.armorBox(s*0.69,0.61,0,0.035,0.17,0.40,shadow);
        m.braceBeam([s*0.72,0.64,-0.15],[s*0.77,0.50,-0.11],0.045,iron);
        m.braceBeam([s*0.77,0.50,-0.11],[s*0.77,0.50,0.11],0.045,iron);
        m.braceBeam([s*0.77,0.50,0.11],[s*0.72,0.64,0.15],0.045,iron);
        foreach(z in [-0.43,0.43]) foreach(y in [0.13,0.88]) {
            m.armorBox(s*0.69,y,z,0.035,0.18,0.17,iron);
            m.roundTube(s*0.715,y,z,0.022,0.016,[0.38,0.39,0.33],"body",1.5708);
        }
    }
    for(local i=0;i<5;i++) {
        m.armorBox(-0.5+i*0.25,1.00,0,0.235,0.08,1.03,planks[(i+2)%3]);
        m.armorBox(-0.52+i*0.25,1.043,0.1,0.008,0.008,0.65,grain);
    }
    foreach(z in [-0.35,0.35]) m.armorBox(0,1.07,z,1.32,0.075,0.12,trim);
    foreach(x in [-0.45,0.45]) m.armorBox(x,0.025,0,0.13,0.05,1.0,trim);
}
