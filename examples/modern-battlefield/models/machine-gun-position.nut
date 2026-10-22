// Compact timber-revetted MG nest, staggered bags, duckboards and overhead splinter cover.
// The weapon remains a separate asset placed at the origin by the battlefield scene.
export function buildMachineGunPosition(m,a) {
    local dirt=[0.29,0.25,0.17],wood=[0.31,0.23,0.14],edge=[0.43,0.33,0.20],sand=[0.49,0.41,0.27],dark=[0.16,0.18,0.13];
    m.armorBox(0,0.015,0,5.2,0.03,4.4,dirt);
    for(local i=0;i<15;i++) m.armorBox(-1.4+i*0.20,0.055,-0.10,0.18,0.065,3.5,wood);
    foreach(s in [-1,1]) {
        // Triangular earth banks leave the firing and entrance lanes open.
        local v=[[s*1.8,0.03,-1.9],[s*2.65,0.03,-1.9],[s*1.98,0.62,-1.9],
            [s*1.8,0.03,1.9],[s*2.65,0.03,1.9],[s*1.98,0.62,1.9]];
        local faces=s>0?[[0,2,1],[3,4,5],[0,3,5,2],[1,2,5,4],[0,1,4,3]]:
            [[1,2,0],[5,4,3],[2,5,3,0],[4,5,2,1],[3,4,1,0]];
        m.bakeFaces(v,faces,dirt);
        for(local i=0;i<5;i++) {
            local z=-1.45+i*0.72;
            m.armorBox(s*1.85,0.41,z,0.12,0.73,0.68,wood);
            if(i%2==0) m.armorBox(s*1.73,0.47,z,0.12,0.91,0.13,edge);
        }
        foreach(y in [0.20,0.66]) m.armorBox(s*1.76,y,0.0,0.09,0.09,3.6,edge);
        for(local row=0;row<3;row++) for(local i=0;i<4;i++)
            m.filledBag(s*1.98,0.84+row*0.24,-1.18+i*0.75+(row%2)*0.12,0.85,0.25,0.55,sand,1.5708);
        m.armorBox(s*1.5,1.0,1.12,0.16,1.9,0.16,wood);
        m.braceBeam([s*1.50,1.46,1.12],[s*1.04,1.78,1.12],0.09,edge);
        m.armorBox(s*1.4,0.39,-1.64,0.65,0.11,0.65,wood);
    }
    for(local row=0;row<3;row++) for(local i=0;i<6;i++)
        m.filledBag((i-2.5)*0.71+(row%2)*0.20,0.15+row*0.26,1.68,0.84,0.27,0.60,sand);
    m.armorBox(0,1.85,1.10,3.32,0.18,0.19,edge);
    for(local i=0;i<7;i++) m.armorBox(-1.5+i*0.5,1.94,1.12,0.42,0.09,1.0,wood);
    m.armorBox(0,2.00,1.1,3.60,0.035,1.15,dark);
    for(local i=0;i<4;i++) m.filledBag(-1.18+i*0.78,2.15,1.14,0.85,0.25,0.55,sand);
    // Recessed ready-ammunition shelf and spent-link catch tray.
    m.armorBox(-1.55,0.58,-0.5,0.36,0.08,0.80,edge);
    m.armorBox(0.66,0.12,0.2,0.52,0.08,0.62,dark);
    foreach(x in [0.43,0.89]) m.armorBox(x,0.20,0.20,0.04,0.17,0.62,wood);
    m.armorBox(-1.75,1.26,1.65,0.56,0.26,0.07,edge);
    m.stencilGlyphs("MG",-1.75,1.26,1.691,0.034,dark);
    foreach(s in [-1,1]) m.armorBox(s*0.65,0.08,-2.02,0.10,0.12,0.44,edge);
}
