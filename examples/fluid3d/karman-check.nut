// Opt-in FluidKarmanVortex acceptance. Loading this file is inert.
function verifyVolumeFluidKarmanVortex() {
    paused=true;rebuild(18);
    local positive=0;local negative=0;local peak=0.0;local peakContacts=0;
    for(local frame=0;frame<180;++frame) {
        checked(solver.step(1.0/120.0,1));
        if(frame%12==11) {
            local wake=fluidKarmanSampleWake(karman,solver);
            positive+=wake.positive;negative+=wake.negative;if(wake.peak>peak)peak=wake.peak;
            local contacts=checked(solver.contacts()).len();if(contacts>peakContacts)peakContacts=contacts;
        }
    }
    local upstream=checked(solver.sampleField([[-0.95,0.765,0.0]]))[0];
    if(peakContacts<=0 || positive<=0 || negative<=0 || peak<0.1 ||
        peak<=fabs(upstream.vorticity[2]))
        throw "Karman wake validation failed contacts="+peakContacts+" positive="+positive+
            " negative="+negative+" peak="+peak+" upstream="+fabs(upstream.vorticity[2]);
    karman.peakContacts=peakContacts;karman.peakWakeCurl=peak;dirty=true;
    return "VOLUME_FLUID_KARMAN_PASS particles="+solver.getParticleCount()+" contacts="+
        peakContacts+" positive="+positive+" negative="+negative+" peakCurl="+format("%.3f",peak)+
        " upstreamCurl="+format("%.3f",fabs(upstream.vorticity[2]));
}
