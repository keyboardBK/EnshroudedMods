#pragma once
#include <algorithm>
#include <cmath>
#include <random>
namespace speedmod {
struct Endpoints { int low_min, low_max, high_min, high_max; };
struct Range { int min, max; };
struct Settings {
    int low=1, high=45;
    Endpoints rarity[5]={{1,5,10,15},{5,9,13,36},{8,13,15,58},{12,16,18,79},{15,20,20,100}};
    bool valid() const {
        if(low<1 || high<=low || high>255)return false;
        for(const auto& r:rarity)
            if(r.low_min<0 || r.high_min<0 || r.low_max>100 || r.high_max>100 ||
               r.low_min>r.low_max || r.high_min>r.high_max)return false;
        return true;
    }
    Range range(int level,int rank) const {
        const auto& r=rarity[std::clamp(rank,0,4)];
        const double t=double(std::clamp(level,low,high)-low)/(high-low);
        return {int(std::lround(r.low_min+(r.high_min-r.low_min)*t)),
                int(std::lround(r.low_max+(r.high_max-r.low_max)*t))};
    }
};
inline int actual_rarity(int available,int fallback) {
    return available>=1 && available<=5 ? available-1 : (fallback>=0 && fallback<=4 ? fallback : -1);
}
template<class Rng> int roll(const Settings& settings,int level,int rarity,Rng& rng) {
    const auto r=settings.range(level,rarity);
    return std::uniform_int_distribution<int>(r.min,r.max)(rng);
}
}
