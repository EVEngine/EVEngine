config = {
    width = 1280
    height = 860
    title = "Cottage | structural collapse study"
    debug = false
    hotReload = false
    modules = ["keyboard", "physics", "destruction", "destructionFx", "camera", "font", "fluids"]
    dust = {
        fixedHz=24.0 displayHz=12.0 width=640 height=430
        gridWidth=80 gridHeight=54 gridDepth=64 extinction=0.9 radius=1.3
        spacing=0.85 counts=[48,72,128,192]
        lifetime=8.0 updraft=0.035 drag=0.65 vorticity=0.65 spread=0.8
        color=[0.68,0.60,0.46] opacity=0.34
    }
    collapse = {
        output = "cottage"
        playbackAsset = ""
        seed = 42
        fixedHz = 60.0
        seconds = 16.0
        contactTolerance = 0.085
        maximumFragments = 1024
        edgeBreaksPerStep = 8
        groups = [
            {id=0 name="stone foundation" fragments=1 cohesion=80.0 connection=40.0 color=[0.40,0.44,0.43]},
            {id=1 name="timber posts" fragments=1 cohesion=28.0 connection=4.0 color=[0.22,0.14,0.09]},
            {id=2 name="timber beams" fragments=1 cohesion=28.0 connection=4.5 color=[0.29,0.18,0.11]},
            {id=3 name="plaster masonry" fragments=3 cohesion=1.8 connection=1.0 color=[0.88,0.80,0.63]},
            {id=4 name="floor panels" fragments=2 cohesion=16.0 connection=7.0 color=[0.42,0.30,0.19]},
            {id=5 name="roof courses" fragments=1 cohesion=18.0 connection=4.5 color=[0.55,0.16,0.075]},
            {id=6 name="window frames" fragments=1 cohesion=8.0 connection=1.5 color=[0.20,0.13,0.085]},
            {id=7 name="shutters" fragments=1 cohesion=7.0 connection=1.2 color=[0.12,0.29,0.27]},
            {id=8 name="brick chimney" fragments=3 cohesion=5.0 connection=4.0 color=[0.52,0.24,0.16]},
            {id=10 name="stone lintels" fragments=1 cohesion=12.0 connection=2.0 color=[0.73,0.68,0.54]},
            {id=11 name="oak door" fragments=1 cohesion=10.0 connection=1.2 color=[0.35,0.20,0.095]},
            {id=12 name="ridge tiles" fragments=1 cohesion=18.0 connection=4.5 color=[0.39,0.10,0.05]},
            {id=20 name="lawn" fragments=1 cohesion=999.0 connection=999.0 color=[0.26,0.36,0.27]},
            {id=21 name="gravel" fragments=1 cohesion=999.0 connection=999.0 color=[0.57,0.59,0.52]},
            {id=22 name="foundation apron" fragments=1 cohesion=999.0 connection=999.0 color=[0.31,0.35,0.32]}
        ]
        impacts = [
            {label="LOCAL WALL FAILURE" time=3.0 x=3.4 y=1.8 z=2.8 radius=2.5 strain=5.0 impulse=3.0 dx=0.65 dy=0.12 dz=0.8},
            {label="ROOF BEARING FAILURE" time=4.8 x=0.0 y=6.4 z=3.0 radius=6.2 strain=25.0 impulse=3.5 dx=0.3 dy=0.1 dz=1.0},
            {label="FRAME COLLAPSE" time=8.0 x=0.0 y=2.8 z=0.0 radius=8.0 strain=80.0 impulse=2.5 dx=0.1 dy=-0.5 dz=0.2},
            {label="GLOBAL COLLAPSE" time=11.0 x=0.0 y=1.5 z=0.0 radius=9.0 strain=120.0 impulse=2.0 dx=0.0 dy=-0.8 dz=0.1}
        ]
    }
};
