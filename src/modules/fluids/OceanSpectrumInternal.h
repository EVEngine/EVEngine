#pragma once

#include "fluids/OceanSpectrum.h"

#include <complex>

namespace eve::fluids {
struct OceanSpectrumData {
    struct Band {
        OceanSpectrumBand                 settings;
        std::vector<std::complex<double>> coefficients;
    };
    OceanSpectrumSettings settings;
    std::vector<Band>     bands;
    double                variance = 0.0;
};
}  // namespace eve::fluids
