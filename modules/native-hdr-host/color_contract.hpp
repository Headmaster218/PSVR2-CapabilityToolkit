#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace native_hdr {
inline double PqFromNits(double n) {
    if (!std::isfinite(n) || n < 0 || n > 10000) throw std::invalid_argument("invalid absolute luminance");
    constexpr double m1=2610.0/16384.0, m2=2523.0/32.0, c1=3424.0/4096.0, c2=2413.0/128.0, c3=2392.0/128.0;
    const double p=std::pow(n/10000.0,m1);
    return std::pow((c1+c2*p)/(1+c3*p),m2);
}
inline std::array<double,3> Relative709ToPq2020(std::array<double,3> v, double referenceWhiteNits) {
    if (!std::isfinite(referenceWhiteNits) || referenceWhiteNits<=0) throw std::invalid_argument("invalid reference white");
    for (double x:v) if (!std::isfinite(x) || x<0) throw std::invalid_argument("invalid relative linear input");
    std::array<double,3> n{{
        (0.6274038959*v[0]+0.3292830384*v[1]+0.0433130657*v[2])*referenceWhiteNits,
        (0.0690972894*v[0]+0.9195403951*v[1]+0.0113623155*v[2])*referenceWhiteNits,
        (0.0163914389*v[0]+0.0880133079*v[1]+0.8955952532*v[2])*referenceWhiteNits}};
    for (double& x:n) x=PqFromNits(x);
    return n;
}
}
