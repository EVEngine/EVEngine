
            function checked(r) { if (!r.ok) throw "ocean render operation failed"; return r.hasValue ? r.value : null; }
            local sb = eve.OceanSpectrumBuilder(); local s = checked(sb.settings());
            s.significantWaveHeight = 0.8;
            s.bands = [{resolution=16,length=64.0,minimumWavelength=8.0,maximumWavelength=64.0}];
            checked(sb.configure(s)); local sea = checked(sb.build());
            local wb = eve.OceanWakeBuilder(); local ws = checked(wb.settings());
            ws.resolution=16; ws.length=32.0; ws.absorptionWidth=8.0;
            checked(wb.configure(ws)); local wake = checked(wb.build());
            local gb = eve.OceanGpuSpectrumBuilder();
            local gpu = checked(gb.build(sea,testGraphics(),testCompute()));
            local token = checked(checked(gpu.frameBuilder(0,60)).submit());
            local rb = eve.OceanRendererBuilder(); local rs = checked(rb.settings());
            rs.segments=32; rs.rings=16; rs.outerRadius=128.0;
            checked(rb.configure(rs)); render <- checked(rb.build(testGraphics()));
            rs.backend="cpu"; checked(rb.configure(rs)); cpuRender <- checked(rb.build(testGraphics()));
            local placement = {center=[0.0,0.0],origin=[0.0,0.0,0.0]};
            publication <- checked(render.gpuFrameBuilder(gpu,token,wake,placement));
            local frame = checked(checked(sea.frameBuilder(0,60)).build());
            cpuPublication <- checked(cpuRender.frameBuilder(frame,wake,placement));
            checked(publication.publish());
            checked(cpuPublication.publish());
            token = checked(checked(gpu.frameBuilder(1,60)).submit());
            staleRejected <- !publication.publish().ok;
            retainedPublication <- checked(render.gpuFrameBuilder(gpu,token,null,placement));
            local disposable = checked(gb.build(sea,testGraphics(),testCompute()));
            local pending = checked(disposable.frameBuilder(2,60));
            disposable=null; collectgarbage();
            checked(pending.submit());
            gpu=null; token=null; sea=null; frame=null; wake=null;
            collectgarbage();
            checked(cpuPublication.publish());
            function drawGpu() { checked(render.draw()); }
            function drawCpu() { checked(cpuRender.draw()); }
        