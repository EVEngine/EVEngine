local localSurface = fluids.newSurfaceRenderer(17, 17);
local configured = localSurface.configureSurfaceBlend(5, 10);
if (!configured.ok) throw "default surface blend rejected";
if (localSurface.configureSurfaceBlend(-1, 10).ok) throw "negative blend factor accepted";
if (localSurface.configureSurfaceBlend(5, 11).ok) throw "out-of-range blend factor accepted";
if (!localSurface.configureParticleBlend(2, 0, false).ok) throw "particle blend rejected";
if (localSurface.configureParticleBlend(11, 0, false).ok) throw "particle source factor accepted";
if (localSurface.configureParticleBlend(2, -1, true).ok) throw "particle destination factor accepted";
::surfaceBlendSettingsPass <- true;
