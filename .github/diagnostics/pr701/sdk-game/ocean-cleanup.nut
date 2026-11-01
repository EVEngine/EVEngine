
            local hullConfig={origin=[0,0,0],volumes=[{center=[0,0,0],rotation=[0,0,0,1],
                halfExtent=[1,1,1],bevels=[]}]};
            local hullPublication=checked(render.hullMaskBuilder(hullConfig));
            hullConfig.volumes[0].halfExtent[0]=-1;
            local invalidHull=checked(render.hullMaskBuilder(hullConfig));
            if(invalidHull.publish().ok) throw "Invalid hull accepted";
            hullConfig.unknown <- true;
            if(render.hullMaskBuilder(hullConfig).ok) throw "Unknown hull field accepted";
            hullConfig=null; invalidHull=null;
            render=null; cpuRender=null; publication=null; collectgarbage();
            checked(hullPublication.publish());
            hullPublication=null; collectgarbage();
            checked(retainedPublication.publish());
            checked(cpuPublication.publish());
            retainedPublication=null; cpuPublication=null; collectgarbage();
        