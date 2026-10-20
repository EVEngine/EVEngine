// The dev asset is authored in metres, Y-up, +X bow, waterline Y=0.
// Keep the source model/materials in their shared asset directory.
function oceanLoadType055() {
    if (!fs.mountExternalReadOnly("../../assets/models/type055", "type055"))
        throw "Ocean Voyage requires the repository assets/models/type055 directory";
    local data=model3d.newModelDataFromFile("type055/type055.obj");
    if (data==null || data.getMeshCount()==0) throw "Type 055 model contains no meshes";
    local parts=[];
    local paint={hull=[0.18,0.23,0.27],fittings=[0.28,0.33,0.37],
                 dark=[0.025,0.045,0.06],markings=[0.68,0.70,0.65]};
    for(local i=0;i<data.getMeshCount();++i) {
        local r=model3d.createRenderable(gfx,data,i);
        if(r==null) throw "Type 055 mesh creation failed";
        local name=data.getMaterialName(data.getMaterialIndex(i));
        if(!(name in paint)) throw "Unexpected Type 055 material: "+name;
        local color=paint[name], material=r.getMaterial();
        if(material==null) throw "Type 055 material missing";
        // Imported renderables use their Material, overriding Renderable3D tint/roughness.
        material.setTint(color[0],color[1],color[2],1.0);
        material.setRoughness(0.85);
        material.setMetallic(0.0);
        parts.append(r);
    }
    return {data=data,parts=parts};
}

// Conservative dry volumes from type055.scad's hull stations (length, half-beam, deck).
// The visual OBJ remains authoritative; these deliberately inset proxies only hide
// interior water. They do not supply collision geometry or change buoyancy queries.
function oceanType055Masks() {
    local stations=[[-90.0,7.5,5.0],[-78.0,9.6,5.2],[-50.0,10.0,5.5],
                    [-15.0,10.0,5.8],[25.0,9.4,6.2],[53.0,7.5,6.8],
                    [72.0,4.3,7.6],[86.0,1.4,8.2],[90.0,0.12,8.4]];
    local masks=[];
    for(local i=0;i<stations.len()-1;++i) {
        local a=stations[i], b=stations[i+1];
        local slope=(b[1]-a[1])/(b[0]-a[0]), intercept=a[1]-slope*a[0];
        local width=a[1]<b[1] ? a[1] : b[1];
        local deck=a[2]<b[2] ? a[2] : b[2];
        local highDeck=a[2]>b[2] ? a[2] : b[2];
        local lower=0.36*width/3.4, upper=0.22*width/(highDeck-0.4);
        local planes=[[0.0,-1.0,0.0,2.98],[0.0,1.0,0.0,deck-0.02],
                      [0.0,0.0,-1.0,-a[0]],[0.0,0.0,1.0,b[0]]];
        foreach(side in [-1.0,1.0]) {
            planes.append([side,-lower,-0.42*slope,0.42*intercept+3.0*lower-0.02]);
            planes.append([side,-upper,-0.78*slope,0.78*intercept-0.4*upper-0.02]);
        }
        foreach(p in planes) {
            local length=sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
            for(local k=0;k<4;++k) p[k]/=length;
        }
        masks.append(planes);
    }
    return masks;
}
