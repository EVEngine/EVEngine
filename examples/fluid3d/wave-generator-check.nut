dofile("wave-support.nut");

local original = [1.0, 2.0, 3.0];
local quarter = fluidWavePosition(original, 0.5, 2.0, 3.14159265359 / 4.0);
if (fabs(quarter[0] - 1.5) > 0.00001 || quarter[1] != 2.0 || quarter[2] != 3.0)
    throw "WaveGenerator position mismatch";
local replay = fluidWavePosition(original, 0.5, 2.0, 3.14159265359 / 4.0);
if (replay[0] != quarter[0] || replay[1] != quarter[1] || replay[2] != quarter[2])
    throw "WaveGenerator replay mismatch";
rebuild(11);
advanceAttachment(3.14159265359 / 2.0);
local integrated = checked(solver.snapshot());
if (fabs(integrated.colliders[0].center[0] - 0.3) > 0.00001)
    throw "WaveGenerator moving collider integration mismatch";
return "VOLUME_WAVE_GENERATOR_PASS";
