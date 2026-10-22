// CPU geometry shared by built-in material preview models. No optional module
// is accessed until upload(); normals and UVs include recessed/beveled surfaces.
class MaterialPreviewGeometry {
    static function build(kind, segments = 128) {
        if (segments < 16 || segments > 256) throw "Preview tessellation must be between 16 and 256";
        local data = {positions=[], normals=[], uvs=[], indices=[]};
        if (kind == "torus") {
            local profile = [];
            for (local j=0; j<=64; ++j) {
                local a = 2.0 * PI * j / 64.0;
                profile.append([0.72 + 0.29*cos(a), 0.29*sin(a)]);
            }
            lathe(data, profile, segments, 0.0, 0.0);
        } else if (kind == "shaderball") {
            local profile = [];
            for (local j=0; j<=192; ++j) {
                local y = -1.0 + 1.82*j/192.0;
                local r = sqrt(1.0-y*y);
                foreach (band in [[0.18,0.065,0.10],[-0.46,0.035,0.055]]) {
                    local t = fabs(y-band[0])/band[1];
                    if (t < 1.0) {
                        local bevel = t < 0.58 ? 1.0 : (1.0-t)/0.42;
                        bevel = bevel*bevel*(3.0-2.0*bevel);
                        r -= band[2]*bevel;
                    }
                }
                profile.append([r,y]);
            }
            // Beveled cap, annular inset, and central disk.
            foreach (point in [[0.55,0.84],[0.46,0.84],[0.44,0.81],[0.12,0.81],[0.10,0.83],[0.0,0.83]])
                profile.append(point);
            lathe(data, profile, segments, 0.42, 0.10);
            // Separate turned pedestal, with rounded lips and a recessed band.
            local pedestal = [[0.0,-1.38],[0.65,-1.38],[0.70,-1.36],[0.73,-1.32],
                          [0.73,-1.25],[0.71,-1.23],[0.66,-1.23],[0.65,-1.20],
                          [0.70,-1.18],[0.70,-1.14],[0.67,-1.11],[0.30,-1.11],
                          [0.26,-1.07],[0.24,-0.94],[0.0,-0.94]];
            lathe(data, pedestal, segments, 0.0, 0.0);
        } else throw "Unknown preview geometry: " + kind;
        return data;
    }

    static function lathe(data, profile, segments, tilt, lift) {
        local first = data.positions.len()/3;
        local ct=cos(tilt), st=sin(tilt);
        for (local j=0; j<profile.len(); ++j) {
            local previous=profile[j>0 ? j-1 : j];
            local next=profile[j+1<profile.len() ? j+1 : j];
            local dr=next[0]-previous[0], dy=next[1]-previous[1];
            local length=sqrt(dr*dr+dy*dy);
            if (length < 0.000001) throw "Degenerate preview profile";
            for (local i=0; i<=segments; ++i) {
                local angle=2.0*PI*i/segments;
                local ca=cos(angle), sa=sin(angle);
                local x=profile[j][0]*ca, y=profile[j][1], z=profile[j][0]*sa;
                data.positions.extend([ct*x-st*y, st*x+ct*y+lift, z]);
                local nx=dy*ca/length, ny=-dr/length, nz=dy*sa/length;
                data.normals.extend([ct*nx-st*ny, st*nx+ct*ny, nz]);
                data.uvs.extend([i.tofloat()/segments, j.tofloat()/(profile.len()-1)]);
                if (j+1<profile.len() && i<segments) {
                    local a=first+j*(segments+1)+i, b=a+segments+1;
                    data.indices.extend([a,b,a+1,a+1,b,b+1]);
                }
            }
        }
    }

    static function upload(data) {
        return gfx.newMeshFromArrays(data.positions,data.normals,data.uvs,data.positions.len()/3,
                                     data.indices,data.indices.len());
    }

    // Cubemap order +X,-X,+Y,-Y,+Z,-Z; v increases down the face image.
    static function faceDirection(face,u,v) {
        local directions = [[1.0,-v,-u],[-1.0,-v,u],[u,1.0,v],
                            [u,-1.0,-v],[u,-v,1.0],[-u,-v,-1.0]];
        if (face<0 || face>=6) throw "Skybox face must be between 0 and 5";
        return directions[face];
    }

    static function skyFace(face) {
        local data={positions=[],normals=[],uvs=[0.0,0.0,1.0,0.0,1.0,1.0,0.0,1.0],indices=[0,1,2,0,2,3]};
        foreach (uv in [[-1.0,-1.0],[1.0,-1.0],[1.0,1.0],[-1.0,1.0]]) {
            local d=faceDirection(face,uv[0],uv[1]);
            data.positions.extend([d[0]*40.0,d[1]*40.0,d[2]*40.0]);
            data.normals.extend([-d[0],-d[1],-d[2]]);
        }
        return upload(data);
    }

    static function skyColor(kind,direction) {
        local x=direction[0], y=direction[1], z=direction[2];
        local n=sqrt(x*x+y*y+z*z); x/=n; y/=n; z/=n;
        if (kind=="studio") {
            local panel=(fabs(x)>0.75 && y>0.02 && y<0.65) || (z< -0.82 && fabs(x)<0.36 && y>0.0);
            local top=y>0.65;
            local v=panel ? 0.95 : (top ? 0.48 : 0.13+0.07*(y+1.0));
            local blue=v*(x>0.0 ? 0.90 : 1.07);
            return [v,v*(x>0.0 ? 0.96 : 1.0),blue>1.0 ? 1.0 : blue];
        }
        local t=(y+1.0)*0.5;
        local low=kind=="sunset" ? [0.38,0.17,0.13] : [0.27,0.33,0.22];
        local horizon=kind=="sunset" ? [0.95,0.55,0.28] : [0.75,0.85,0.95];
        local zenith=kind=="sunset" ? [0.12,0.18,0.38] : [0.12,0.37,0.72];
        local a=y<0.0 ? low : horizon, b=y<0.0 ? horizon : zenith;
        local blend=y<0.0 ? t*2.0 : y;
        local cloud=y>0.08 ? pow((sin(x*17.0+z*9.0)*sin(z*13.0-y*11.0)+1.0)*0.5,5.0)*0.24 : 0.0;
        local sun=pow((x*0.48+y*0.36-z*0.8+1.0)*0.5,180.0)*0.7;
        local color=[];
        for (local i=0;i<3;++i) {
            local c=a[i]*(1.0-blend)+b[i]*blend+cloud+sun;
            color.append(c>1.0 ? 1.0 : c);
        }
        return color;
    }
}
