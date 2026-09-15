// Opt-in contract check for Fluid3D's normalized ThicknessClip convention.
local localSurface = fluids.newSurfaceRenderer(32, 32);
local configured = localSurface.configureSurface(1.0, 1.2, 0.03, 2);
if (!configured.ok) throw "Fluid3D thickness cutoff was rejected";
if (localSurface.configureSurface(1.0, -0.001, 0.03, 2).ok)
    throw "Negative thickness cutoff was accepted";
if (localSurface.configureSurface(1.0, 5.001, 0.03, 2).ok)
    throw "Oversized thickness cutoff was accepted";
::thicknessCutoffSettingsPass <- true;
