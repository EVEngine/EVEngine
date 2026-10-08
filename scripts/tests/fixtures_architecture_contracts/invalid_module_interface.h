// Missing @cost on an owned-container public API (G-5).
std::vector<std::string> collectIds();

void pump() {
    // Runtime convenience lookup (G-4) and hot-path query (G-3).
    auto* mod = getModInst(eve::gfx, Graphics);
    auto* q = eve::cap::query<ISceneQuery>();
    (void)mod;
    (void)q;
}

void wire() {
    eve::cap::provide<IGreeter>(&g);
    eve::cap::ProviderRef<IGpuTimer>::bind();
}
