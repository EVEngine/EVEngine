// Deterministic native equivalent of the package WaveGenerator sample.
// fixedTime is injected by the simulation owner, so pause/replay never observes wall time.
function fluidWavePosition(originalPosition, amplitude, frequency, fixedTime) {
    return [originalPosition[0] + sin(fixedTime * frequency) * amplitude,
            originalPosition[1], originalPosition[2]];
}
