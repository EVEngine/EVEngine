// Native composition tests. Run through the engine after the normal scene has initialized.
function verifyNativeHouses() {
    local checks=0;
    local check=function(value,message) { if(!value) throw "HOUSE_TEST: "+message; ++checks; };
    foreach(house in nativeHouses) {
        local s=house.settings, layout=house.layout, g=house.grid;
        check(g.getWidth()==s.width && g.getHeight()==s.depth,"footprint dimensions");
        check(layout.getRoofStyle()==s.roof,"native roof contract");
        check(house.appearance.counts.walls==2*(s.width+s.depth)*s.floors,"four complete facades");
        check(house.appearance.counts.doors==1,"one entrance");
        local faces={}; local doors=[]; local windows=[];
        for(local i=0;i<layout.getInstanceCount();++i) {
            local id=layout.getInstanceComponentId(i);
            if(["wall","window","door"].find(id)==null) continue;
            local x=layout.getInstanceX(i), z=layout.getInstanceY(i), f=layout.getInstanceZ(i), r=layout.getInstanceRotationDeg(i);
            local key=format("%d/%d/%d/%d",x,z,f,r);
            check(!(key in faces),"duplicate facade face"); faces[key] <- id;
            if(id=="door") doors.append([x,z,f,r]);
            if(id=="window") windows.append([x,z,f,r]);
        }
        foreach(d in doors) foreach(w in windows)
            if(d[2]==w[2] && d[3]==w[3]) check(abs(d[0]-w[0])+abs(d[1]-w[1])>1,"window beside entrance");
        foreach(role,mesh in house.appearance.meshes) {
            check(mesh.getVertexCount()>0 && mesh.getIndexCount()%3==0,"nonempty triangles");
            for(local i=0;i<mesh.getVertexCount();++i) {
                local x=mesh.getPositionX(i), y=mesh.getPositionY(i), z=mesh.getPositionZ(i);
                local nx=mesh.getNormalX(i), ny=mesh.getNormalY(i), nz=mesh.getNormalZ(i);
                check(x==x && y==y && z==z && abs(x)<100 && abs(y)<100 && abs(z)<100,"finite bounds");
                check(abs(nx*nx+ny*ny+nz*nz-1.0)<0.001,"unit normal");
            }
            for(local i=0;i<mesh.getIndexCount();i+=3) {
                local a=mesh.getIndex(i), b=mesh.getIndex(i+1), c=mesh.getIndex(i+2), n=mesh.getVertexCount();
                check(a>=0 && b>=0 && c>=0 && a<n && b<n && c<n,"triangle index bounds");
                local ux=mesh.getPositionX(b)-mesh.getPositionX(a), uy=mesh.getPositionY(b)-mesh.getPositionY(a), uz=mesh.getPositionZ(b)-mesh.getPositionZ(a);
                local vx=mesh.getPositionX(c)-mesh.getPositionX(a), vy=mesh.getPositionY(c)-mesh.getPositionY(a), vz=mesh.getPositionZ(c)-mesh.getPositionZ(a);
                local cx=uy*vz-uz*vy, cy=uz*vx-ux*vz, cz=ux*vy-uy*vx;
                check(cx*cx+cy*cy+cz*cz>0.0000000001,"nondegenerate triangle");
            }
        }
        local restored=nativeHouseKit.newLayout(); houseNeed(restored.fromJson(layout.toJson()));
        local restoredGrid=houseNeed(procgen.newGrid(1,1)); houseNeed(restored.writeFootprintGrid(restoredGrid));
        for(local z=0;z<g.getHeight();++z) for(local x=0;x<g.getWidth();++x) {
            check(g.getCell(x,z)==restoredGrid.getCell(x,z),"restored occupancy");
            check(nativeHouseOccupies(house,x+houseLot.x,z+houseLot.z)==(g.getCell(x,z)!=0),"collision projection");
        }
    }
    // Former Python widths 224 and 320 map to 7 and 10 native cells.
    foreach(width in [3,6,7,10]) {
        local s=clone riversideHousePresets[0]; s.width=width;
        local a=prepareNativeHouse(nativeHouseKit,s), b=prepareNativeHouse(nativeHouseKit,s);
        check(a.layout.toJson()==b.layout.toJson(),"seed repeatability");
        foreach(role,mesh in a.appearance.meshes)
            check(mesh.getIndexCount()==b.appearance.meshes[role].getIndexCount(),"repeatable mesh groups");
        check(a.appearance.counts.doors==1,"entrance across sizes");
    }
    local previous=nativeHouses[activeNativeHouse].layout.toJson();
    foreach(field,value in {width=0,depth=8,floors=4,roof="hip",palette="unknown"}) {
        local s=clone riversideHousePresets[0]; s[field]=value;
        local rejected=false;
        try { prepareNativeHouse(nativeHouseKit,s); } catch(error) { rejected=true; }
        check(rejected,"invalid recipe rejected: "+field);
        check(nativeHouses[activeNativeHouse].layout.toJson()==previous,"failure leaves published layout intact");
    }
    check(!nativeHouseOccupies(nativeHouses[2],houseLot.x+5,houseLot.z+1),"smaller house frees lot cell");
    return "HD2D_NATIVE_HOUSE_PASS checks="+checks+" presets="+nativeHouses.len();
}
