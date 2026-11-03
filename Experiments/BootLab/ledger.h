#pragma once
#include "observer.h"
namespace bootlab {
struct IdentityEvidence {
    bool restart=false, transfer=false, distinguish_duplicates=false;
    bool verified() const {return restart && transfer && distinguish_duplicates;}
};
struct PersistentKey {std::string world,item;IdentityEvidence evidence;};
struct Entry {int level=0,rarity=0,bonus=0;};
inline bool key_part(const std::string& key) {
    if(key.empty() || key.size()>128)return false;
    for(auto c:key)if(!(c>='0'&&c<='9')&&!(c>='a'&&c<='z')&&!(c>='A'&&c<='Z')&&c!='-'&&c!='_'&&c!=':')return false;
    return true;
}
class Ledger {
    std::map<std::pair<std::string,std::string>,Entry> entries_;
public:
    const Entry& get_or_roll(const PersistentKey& key,int level,int rarity,std::mt19937_64& rng) {
        if(!key.evidence.verified() || !key_part(key.world) || !key_part(key.item))
            throw std::invalid_argument("No verified permanent item identity. Session PIDE/fingerprint MUST NOT be used.");
        if(level<1 || level>255 || rarity<0 || rarity>4)throw std::invalid_argument("Invalid item");
        const auto id_=std::make_pair(key.world,key.item);
        const auto it=entries_.find(id_);
        if(it!=entries_.end())return it->second; // level changes NEVER reroll
        return entries_.emplace(id_,Entry{level,rarity,speedmod::roll(speedmod::Settings{},level,rarity,rng)}).first->second;
    }
    size_t size()const{return entries_.size();}
    std::string serialize()const {
        std::ostringstream out;out<<"BOOTLEDGER1\n";
        for(const auto& pair:entries_)out<<pair.first.first<<' '<<pair.first.second<<' '<<pair.second.level<<' '
            <<pair.second.rarity<<' '<<pair.second.bonus<<'\n';
        return out.str();
    }
    void load(const std::string& text) {
        std::istringstream in(text);std::string line;
        if(!std::getline(in,line) || line!="BOOTLEDGER1")throw std::runtime_error("Unknown/corrupt ledger; do not reroll");
        std::map<std::pair<std::string,std::string>,Entry> parsed;
        while(std::getline(in,line)) {
            if(line.empty())continue;
            std::istringstream row(line);std::string world,item,extra;Entry e;
            if(!(row>>world>>item>>e.level>>e.rarity>>e.bonus) || row>>extra || !key_part(world) || !key_part(item) ||
                e.level<1 || e.level>255 || e.rarity<0 || e.rarity>4 || e.bonus<0 || e.bonus>100 ||
                !parsed.emplace(std::make_pair(world,item),e).second)throw std::runtime_error("Corrupt/duplicate ledger entry; do not reroll");
        }
        entries_=std::move(parsed); // Parse first; failure leaves all existing entries intact.
    }
    void save(const std::filesystem::path& path)const {
        if(!path.is_absolute() || !std::filesystem::is_directory(path.parent_path()))
            throw std::invalid_argument("Use an existing absolute per-world sidecar directory");
        const auto suffix=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        auto temporary=path;temporary+=L".tmp-"+suffix;
        if(std::filesystem::exists(temporary))throw std::runtime_error("Temporary file exists");
        try {
            const auto bytes=serialize();
            HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create ledger temporary file");
            DWORD written=0;
            const bool ok=bytes.size()<=MAXDWORD && WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) &&
                written==bytes.size() && FlushFileBuffers(file);
            CloseHandle(file);
            if(!ok)throw std::runtime_error("Ledger write failed");
            if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Ledger commit failed");
        }catch(...){std::error_code error;std::filesystem::remove(temporary,error);throw;}
    }
};
}
