#pragma once
#include "observer.h"
namespace bootlab {
inline int formula_percent(int level,int rarity) {
    if(level<1 || level>255 || rarity<0 || rarity>4) throw std::invalid_argument("Invalid boot level/rarity");
    const auto range=speedmod::Settings{}.range(level,rarity);
    return (range.min+range.max+1)/2; // nearest integer midpoint; no random roll
}
struct Contribution {
    float baseline=0,delta=0,last=0;
    bool attached=false,conflict=false;
    bool plan(float current,int percent,float& next) {
        if(!std::isfinite(current) || current<0 || current>100 || percent<0 || percent>100 || conflict)return false;
        if(attached && std::fabs(current-last)>0.00001f) { conflict=true; return false; }
        const float base=attached ? baseline : current;
        const float extra=8.0f*static_cast<float>(percent)/100.0f;
        next=base+extra; return std::isfinite(next) && next<=100;
    }
    void committed(float previous,float next,int percent) {
        if(!attached)baseline=previous;
        delta=8.0f*static_cast<float>(percent)/100.0f; last=next; attached=percent!=0;
        if(!attached) { baseline=0;delta=0;last=0; }
    }
};
}
