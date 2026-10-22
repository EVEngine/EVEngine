// Cast reinforced bollard: tapered chamfered shaft, lifting eye and warning paint.
export function buildConcretePost(m,a) {
    local concrete=[0.43,0.44,0.40],edge=[0.52,0.52,0.46],pit=[0.28,0.30,0.28],iron=[0.24,0.25,0.21];
    local v=[],f=[];
    foreach(r in [[0.04,0.50],[0.21,0.50],[0.28,0.34],[1.58,0.26],[1.66,0.22]]) {
        local y=r[0],w=r[1],b=w*0.24;
        foreach(p in [[-w+b,-w],[w-b,-w],[w,-w+b],[w,w-b],[w-b,w],[-w+b,w],[-w,w-b],[-w,-w+b]])
            v.push([p[0],y,p[1]]);
    }
    f.push([0,1,2,3,4,5,6,7]);f.push([39,38,37,36,35,34,33,32]);
    for(local row=0;row<4;row++) for(local k=0;k<8;k++) {
        local n=(k+1)%8;f.push([row*8+k,(row+1)*8+k,(row+1)*8+n,row*8+n]);
    }
    m.bakeFaces(v,f,concrete);
    foreach(s in [-1,1]) {
        m.armorBox(0,0.98,s*0.303,0.46,0.28,0.018,[0.11,0.13,0.12],"body",0,-s*0.0615);
        for(local i=0;i<3;i++) {
            local x=-0.18+i*0.15,z=s*0.315;
            m.bakeFaces([[x-0.045,0.85,z+s*0.008],[x+0.02,0.85,z+s*0.008],
                [x+0.11,1.11,z-s*0.008],[x+0.045,1.11,z-s*0.008]],[[0,1,2,3],[3,2,1,0]],[0.70,0.56,0.23]);
        }
        m.braceBeam([s*0.14,0.30,s*0.341],[s*0.14,1.56,s*0.263],0.007,pit);
        foreach(x in [-0.35,0.35]) {
            m.roundTube(x,0.22,s*0.35,0.042,0.018,iron,"body",0,1.5708);
            m.roundTube(x,0.234,s*0.35,0.024,0.02,edge,"body",0,1.5708);
        }
    }
    for(local i=0;i<16;i++) {
        local y=0.34+(i%8)*0.15,x=sin(i*2.39)*0.18,z=0.34-(y-0.28)*0.0615;
        if(y>0.80&&y<1.15) continue;
        m.roundTube(x,y,z+0.002,0.007+(i%3)*0.003,0.005,pit);
    }
    local previous=null;
    for(local i=0;i<=12;i++) {
        local t=i*3.14159265/12,p=[cos(t)*0.10,1.66+sin(t)*0.15,0.0];
        if(previous!=null) m.roundRod(previous,p,0.014,iron);
        previous=p;
    }
    m.bakeFaces([[0.34,0.20,0.43],[0.45,0.20,0.35],[0.36,0.29,0.35]],[[0,1,2],[2,1,0]],pit);
}
