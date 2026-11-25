local function need(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}

// Shared analytic heightfield: adjacent native heightmap meshes agree at their seam.
local function sampleHeight(x,z) {
    local ridge = z < -9.0 ? (-z-9.0)/13.0 : 0.0;
    local h = 0.45 + 0.40*sin(x*0.15)*cos(z*0.18) + 0.18*sin(z*0.48+x*0.09);
    h += ridge*ridge*(3.6+2.0*sin(x*0.11)*sin(x*0.11));
    local pool = 1.0-((x+16.0)*(x+16.0)/70.0+(z+3.0)*(z+3.0)/32.0);
    if(pool<0.0) pool=0.0;
    return h - pool*0.9;
}

export function groundHeight(x,z) { return sampleHeight(x,z); }

export function makeWorld(regions, assets) {
    local groundHeight=sampleHeight;
    local world = { keep=[], objects=[], fauna=[], count=0 };
    local keep = function(v) { world.keep.append(v); return v; };
    local object = function(mesh,x,y,z,sx,sy,sz,color) {
        local o = eve.Renderable3D();
        o.setMesh(mesh); o.setPosition(x,y,z); o.setScale(sx,sy,sz);
        o.setTint(color[0],color[1],color[2],1.0);
        o.setRoughness(0.88); o.setCastShadow(true); o.setReceiveShadow(true);
        world.objects.append(o); return o;
    };
    local sphere = keep(gfx.newMeshSphere(10,7));
    local cube = keep(gfx.newMeshCube(1.0));
    local cylinder = keep(gfx.newMeshCylinder(7,1,true));
    local atlasParams = keep(need(procgen.newParams()));
    atlasParams.setSeed(84031); atlasParams.setSize(256,256); atlasParams.setInt("colors",12);
    local atlas = keep(need(procgen.generateTexture("tex.tree_atlas",atlasParams,gfx)));
    foreach (ri,r in regions) {
        local conf = need(r.binding.configuration());
        local plants = assets[conf.schemes.plants];
        local surface = assets[conf.schemes.surface];
        local hm = keep(need(procgen.newHeightmap(65,89)));
        local pixels = keep(eve.Image().newEmptyImageData(65,89,"RGBA8"));
        for(local z=0;z<89;++z) for(local x=0;x<65;++x) {
            local wx=r.minX+x*0.5, wz=-22.0+z*0.5;
            hm.setHeight(x,z,(groundHeight(wx,wz)+2.0)/12.0);
            local path=abs(wz-(7.0+1.2*sin(wx*0.1)))<1.15;
            local grain=0.87+0.13*sin(wx*2.31+wz*3.73)*sin(wx*2.31+wz*3.73);
            local c=surface.color;
            pixels.setPixel(x,z,(path?0.47:c[0])*grain,(path?0.41:c[1])*grain,(path?0.30:c[2])*grain,1.0);
        }
        local terrain=object(keep(eve.HeightmapTargetModule().newMesh(hm,0.5,12.0)),
            r.minX,-2.0,-22.0,1.0,1.0,1.0,[1.0,1.0,1.0]);
        terrain.setTexture(keep(gfx.newTexture(pixels,false,false)));
        if(surface.water) {
            local water=object(sphere,r.center,0.10,-3.0,8.0,0.09,5.2,[0.08,0.35,0.39]);
            water.setRoughness(0.14); water.setMetallic(0.2);
        }
        local material=keep(gfx.newMaterial());
        material.setAlbedoTexture(atlas); material.setTint(plants.color[0],plants.color[1],plants.color[2],1.0);
        material.setSurfaceMode("masked"); material.setAlphaCutoff(0.30);
        material.setAlphaTechnique("cutoff"); material.setDoubleSided(true);
        material.setRoughness(0.9); material.setCastShadow(true); material.setReceiveShadow(true);
        local variants=[];
        for(local variant=0;variant<2;++variant) {
            local p=keep(need(procgen.newParams())); p.setSeed(4201+ri*37+variant*11);
            p.setString("style","realistic"); p.setString("branchAlgorithm","weberPenn");
            p.setString("leafMode","clusters"); p.setFloat("height",plants.height);
            p.setFloat("crownRadius",plants.crown); p.setFloat("trunkRadius",0.16);
            p.setFloat("foliageStart",plants.foliage); p.setInt("branchLevels",3);
            p.setInt("branchCount",7); p.setFloat("leafDensity",0.8);
            p.setInt("clusterLimit",55); p.setInt("clusterPlanes",6); p.setInt("clusterLeaves",16);
            p.setFloat("droop",ri==1?0.55:0.08);
            variants.append(keep(need(procgen.generateMesh("mesh.tree",p,gfx))));
        }
        // Placement comes from the existing native BiomeRules / SpatialData pipeline.
        local domain=keep(need(procgen.boxVolume(r.minX+1.5,0.0,-20.0,r.maxX-1.5,0.0,14.0)));
        local path=keep(need(procgen.boxVolume(r.minX,0.0,4.8,r.maxX,0.0,10.0)));
        local rules=keep(need(procgen.newBiomeRules()));
        if(!rules.addLayer("canopy",domain,10,plants.density) ||
           !rules.addAsset("canopy","tree",1.0,0.72,1.13,true) || !rules.addExclusion(path))
            throw "Cannot assemble native biome placement rules";
        local points=keep(rules.generate(domain,plants.spacing,conf.seed,0.78));
        if(points==null) throw rules.getError();
        local trees=0;
        for(local i=0;i<points.getCount();++i) {
            local x=points.getX(i), z=points.getZ(i), y=groundHeight(x,z);
            if(surface.water && abs(x-r.center)<8.0 && abs(z+3.0)<5.0) continue;
            local s=points.getScaleY(i);
            local tree=object(variants[i%2],x,y-0.08,z,s,s,s,[1.0,1.0,1.0]);
            tree.setMaterial(material); tree.setYaw(i*137.5); ++trees;
            if(i%2==0) object(sphere,x+1.0,y+0.25,z+0.5,0.75,0.5,0.6,surface.rock);
            // Understory clumps share the native canopy point distribution.
            for(local k=0;k<3;++k) {
                local bx=x+cos(k*2.1)*1.25, bz=z+sin(k*2.1)*1.25;
                object(sphere,bx,groundHeight(bx,bz)+0.15,bz,0.48,0.26,0.42,plants.color);
            }
        }
        foreach(kind in ["animals","monsters"]) {
            local fauna=assets[conf.schemes[kind]];
            for(local i=0;i<fauna.count;++i) {
                local x=r.center-6.0+i*2.8, z=kind=="animals"?2.5+sin(i*2.1+ri)*2.1:-7.5;
                local parts=[];
                local part=function(mesh,dx,dy,dz,sx,sy,sz,c) {
                    local o=object(mesh,x+dx,groundHeight(x,z)+dy,z+dz,sx,sy,sz,c);
                    parts.append({o=o,x=dx,y=dy,z=dz});
                };
                if(kind=="animals") {
                    local small=fauna.species=="beetle";
                    part(sphere,0.0,small?0.37:0.72,0.0,0.8,small?0.25:0.45,0.40,fauna.color);
                    part(sphere,0.68,small?0.40:1.0,0.0,0.30,small?0.18:0.30,0.28,fauna.color);
                    for(local leg=0;leg<(small?6:4);++leg)
                        part(cylinder,small?-0.5+(leg/2)*0.5:(leg<2?-0.45:0.45),small?0.15:0.32,
                            leg%2==0?-0.32:0.32,0.09,small?0.3:0.6,0.09,fauna.color);
                    if(fauna.species=="deer") for(local horn=0;horn<2;++horn)
                        part(cylinder,0.68,1.55,horn==0?-0.15:0.15,0.06,0.75,0.06,[0.36,0.24,0.12]);
                    if(fauna.species=="fox") {
                        part(sphere,-0.9,0.65,0.0,0.55,0.18,0.18,fauna.color);
                        part(cube,0.62,1.30,-0.16,0.12,0.25,0.12,fauna.color);
                        part(cube,0.62,1.30,0.16,0.12,0.25,0.12,fauna.color);
                    }
                } else {
                    part(sphere,0.0,1.3,0.0,0.9,1.1,0.65,fauna.color);
                    part(cube,0.0,2.45,0.0,0.8,0.7,0.7,fauna.color);
                    part(sphere,-1.0,1.15,0.0,0.30,0.85,0.32,fauna.color);
                    part(sphere,1.0,1.15,0.0,0.30,0.85,0.32,fauna.color);
                    part(cube,-0.22,2.5,0.37,0.13,0.10,0.04,[0.95,0.88,0.34]);
                    part(cube,0.22,2.5,0.37,0.13,0.10,0.04,[0.95,0.88,0.34]);
                }
                world.fauna.append({parts=parts,x=x,z=z,phase=i+ri*2,animal=kind=="animals"});
            }
        }
        r.trees <- trees;
        print("BIOME_TOUR_BUILT region="+r.id+" seed="+conf.seed+" trees="+trees+" animals=5 monsters=2\n");
    }
    // Boundary posts mark the actual spatial partition, not just the itinerary.
    for(local boundary=1;boundary<4;++boundary) for(local z=-18.0;z<=14.0;z+=8.0) {
        local x=-64.0+boundary*32.0;
        object(cube,x,groundHeight(x,z)+0.48,z,0.3,0.96,0.3,[0.78,0.74,0.60]);
    }
    world.count=world.objects.len();
    return world;
}
