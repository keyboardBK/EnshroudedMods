#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "../runtime/boots_logic.h"

namespace serverboots {
struct Observation {
    uint32_t entity=0; // A live handle only. Never serialized.
    std::string location, name;
    int level=0, rarity=-1;
};
struct Record {
    uint64_t number=0;
    std::string location, name;
    int level=0, rarity=-1, percent=0;
};
inline uint64_t checksum(const std::string& text) {
    uint64_t result=14695981039346656037ULL;
    for(unsigned char c:text) {result^=c;result*=1099511628211ULL;}
    return result;
}
inline bool valid(const Observation& o) {
    return o.entity && !o.name.empty() && o.name.size()<=96 && o.location.size()<=1024 &&
        o.level>=1 && o.level<=255 && o.rarity>=0 && o.rarity<=4;
}
class Ledger {
    std::map<uint64_t,Record> records_;
    std::map<uint32_t,uint64_t> live_;
    uint64_t next_=1;
public:
    void forget_live() {live_.clear();}
    size_t size() const {return records_.size();}
    const Record* for_entity(uint32_t entity) const {
        const auto it=live_.find(entity);
        if(it==live_.end())return nullptr;
        const auto r=records_.find(it->second);return r==records_.end()?nullptr:&r->second;
    }
    // Resolve all live moves before saved locations, so a new shoe cannot steal
    // the roll of a shoe moving out of the same slot in this observation batch.
    void reconcile(const std::vector<Observation>& observations,const std::function<int(int,int)>& roll) {
        std::set<uint32_t> entities;
        std::map<std::string,size_t> locations;
        for(const auto& o:observations) {
            if(!valid(o) || !entities.insert(o.entity).second)throw std::invalid_argument("Invalid/duplicate observation");
            if(!o.location.empty())++locations[o.location];
        }
        Ledger candidate=*this;
        std::map<uint32_t,uint64_t> resolved;
        std::set<uint64_t> taken;
        for(const auto& o:observations) {
            const auto old=live_.find(o.entity);
            if(old==live_.end())continue;
            auto& r=candidate.records_.at(old->second);
            if(r.name!=o.name)continue;
            resolved[o.entity]=r.number;taken.insert(r.number);
            r.location=locations[o.location]==1?o.location:"";
            // Upgrading the same live shoe never rerolls it. Its current fields
            // must also be persisted, otherwise the upgrade looks unknown at restart.
            r.level=o.level;r.rarity=o.rarity;
        }
        std::map<std::string,std::vector<uint64_t>> saved;
        for(const auto& pair:candidate.records_)
            if(!pair.second.location.empty() && !taken.count(pair.first))saved[pair.second.location].push_back(pair.first);
        for(const auto& o:observations) {
            if(resolved.count(o.entity))continue;
            uint64_t number=0;
            const auto found=saved.find(o.location);
            if(!o.location.empty() && locations[o.location]==1 && found!=saved.end() && found->second.size()==1) {
                const auto& r=candidate.records_.at(found->second.front());
                if(!taken.count(r.number) && r.name==o.name && r.level==o.level && r.rarity==o.rarity)number=r.number;
            }
            if(found!=saved.end())for(auto old:found->second)
                if(!taken.count(old) && old!=number)candidate.records_.at(old).location.clear();
            if(!number) {
                if(candidate.next_==UINT64_MAX)throw std::overflow_error("Record counter exhausted");
                const int percent=roll(o.level,o.rarity);
                if(percent<0 || percent>100)throw std::runtime_error("Invalid roll");
                number=candidate.next_++;
                candidate.records_[number]={number,"",o.name,o.level,o.rarity,percent};
            }
            auto& r=candidate.records_.at(number);
            r.location=locations[o.location]==1?o.location:"";
            resolved[o.entity]=number;taken.insert(number);
        }
        candidate.live_=std::move(resolved);
        *this=std::move(candidate);
    }
    std::string serialize() const {
        std::ostringstream body;
        body<<next_<<'\n';
        for(const auto& pair:records_) {
            const auto& r=pair.second;
            body<<r.number<<' '<<std::quoted(r.location)<<' '<<std::quoted(r.name)<<' '
                <<r.level<<' '<<r.rarity<<' '<<r.percent<<'\n';
        }
        const auto bytes=body.str();std::ostringstream out;
        out<<"SPEEDMOD_SERVER_1 "<<std::hex<<checksum(bytes)<<'\n'<<bytes;return out.str();
    }
    void load(const std::string& text) {
        if(text.empty()) {*this=Ledger{};return;}
        const auto end=text.find('\n');
        if(end==std::string::npos)throw std::runtime_error("Incomplete ledger header");
        std::istringstream header(text.substr(0,end));std::string magic,extra;uint64_t hash=0;
        const auto body=text.substr(end+1);
        if(!(header>>magic>>std::hex>>hash) || header>>extra || magic!="SPEEDMOD_SERVER_1" || checksum(body)!=hash)
            throw std::runtime_error("Ledger checksum/version mismatch");
        std::istringstream in(body);Ledger parsed;std::string line;
        if(!std::getline(in,line))throw std::runtime_error("Missing record counter");
        std::istringstream first(line);
        if(!(first>>parsed.next_) || first>>extra || !parsed.next_)throw std::runtime_error("Invalid record counter");
        while(std::getline(in,line)) {
            if(line.empty())throw std::runtime_error("Empty record");
            std::istringstream row(line);Record r;
            if(!(row>>r.number>>std::quoted(r.location)>>std::quoted(r.name)>>r.level>>r.rarity>>r.percent) || row>>extra ||
                !r.number || r.number>=parsed.next_ || !valid({1,r.location,r.name,r.level,r.rarity}) ||
                r.percent<0 || r.percent>100 || !parsed.records_.emplace(r.number,r).second)
                throw std::runtime_error("Invalid/duplicate record");
        }
        *this=std::move(parsed);
    }
};
}
