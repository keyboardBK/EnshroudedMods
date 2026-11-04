#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <unordered_set>
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"
#include "storage.h"
#include "legacy_ids.h"

namespace serverboots {
namespace ecs=emf::gen::ecs;
constexpr const char* kId="speedmod_server";
constexpr int64_t kLease=1000000000LL;
const EmfApi* api=nullptr;
std::mutex gate;
Ledger ledger;
speedmod::Settings settings;
std::mt19937_64 rng;
std::filesystem::path directory,ledger_path;
std::string written;
std::atomic<uint32_t> loaded_session{0};
std::atomic<uint16_t> loaded_slot{0};
std::atomic<uint64_t> load_generation{0};
uint64_t seen_load=0;
uint32_t epoch=0;
int64_t next_scan=0,next_discover=0;
bool healthy=false,enabled=true;
uint64_t deleted=0,rolls=0;
std::vector<uint32_t> inventories,players,roots;
std::map<uint32_t,std::vector<std::string>> addresses;
std::map<uint32_t,Observation> observed;
std::unordered_set<uint32_t> legacy;
struct Effect {uint32_t entity=0,pide=0;int percent=0;int64_t at=0;bool retiring=false;};
std::map<uint32_t,Effect> effects;
struct WornNotice {uint32_t player=0,pide=0;uint64_t record=0;int percent=-1;};
WornNotice last_notice;
struct Removal {uint32_t session,inventory,slot,item,pide,count;};
std::vector<Removal> removals,orphans;
struct Renewal {uint32_t session,entity,player,pide;int64_t now;};
std::vector<Renewal> renewals;

bool copy_guarded(void* out,const void* source,size_t count) {
    __try {std::memcpy(out,source,count);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
template<class T> const T* component(EmfWorld* w,uint32_t entity) {
    uint64_t size=0;auto* p=api->get_component(w,T::kHash,entity,&size);
    return p && size==sizeof(T)?static_cast<const T*>(p):nullptr;
}
template<class T> const T* read(void* ctx,const char* name,uint32_t entity) {
    return static_cast<const T*>(api->program_read_other(ctx,name,entity,sizeof(T)));
}
bool info(EmfWorld* w,uint32_t item,std::string& name) {
    EmfItemInfo value{};value.size=sizeof(value);
    if(!api->item_info(w,item,&value))return false;
    value.name[sizeof(value.name)-1]=0;name=value.name;return !name.empty();
}
bool old_shoe(EmfWorld* w,uint32_t item) {
    if(!legacy.count(item))return false;
    std::string name;
    return !info(w,item,name) || name.rfind("SM02_Boot_",0)==0;
}
bool gameplay_boot(EmfWorld* w,uint32_t item) {
    uint64_t size=0;const auto* p=static_cast<const uint8_t*>(api->item_data(w,item,&size));
    if(!p || size!=2088 || p[260]!=17)return false;
    uint32_t flags=0;std::memcpy(&flags,p+1496,4);
    return (flags&2) && !(flags&(1u<<18));
}
std::string root_address(EmfWorld* w,uint32_t entity) {
    std::ostringstream out;
    if(component<ecs::PlayerInput>(w,entity)) {
        if(entity>1023)return {};
        const auto* table=static_cast<const uint8_t*>(api->singleton(w,"BasicPlayerInfos"));
        uint8_t head[16]{};uint32_t held=0;uint64_t account=0;
        if(!table || !copy_guarded(head,table+size_t(entity-1)*0xa0,sizeof(head)))return {};
        std::memcpy(&held,head,4);std::memcpy(&account,head+8,8);
        if(held!=entity || !account)return {};
        out<<"account:"<<std::hex<<account;
    } else {
        const auto* t=component<ecs::CurrentTransform>(w,entity);
        if(!t)return {};
        int64_t xyz[3];std::memcpy(xyz,t,sizeof(xyz));
        out<<"container:"<<(xyz[0]/4194304)<<':'<<(xyz[1]/4194304)<<':'<<(xyz[2]/4194304);
    }
    return out.str();
}
void walk_address(EmfWorld* w,uint32_t entity,const std::string& path,std::set<uint32_t>& visited,int depth=0) {
    if(depth>8 || !entity || !visited.insert(entity).second)return;
    if(component<ecs::Inventory>(w,entity))addresses[entity].push_back(path+"/self");
    const auto* setup=component<ecs::InventorySetup>(w,entity);
    if(!setup)return;
    for(size_t i=0;i<16;++i)if(setup->linksEntities[i].id && setup->linksEntities[i].id!=entity) {
        const auto child=setup->linksEntities[i].id;
        walk_address(w,child,path+"/page:"+std::to_string(i)+":"+std::to_string(int(setup->linksCategories[i])),visited,depth+1);
    }
}
void discover(uint32_t entity,void* opaque) {
    auto* w=static_cast<EmfWorld*>(opaque);
    if(component<ecs::Inventory>(w,entity))inventories.push_back(entity);
    if(component<ecs::PlayerInput>(w,entity) && component<ecs::SlotSelection>(w,entity))players.push_back(entity);
    if(component<ecs::InventorySetup>(w,entity)) {
        const auto* owner=component<ecs::OwnerRelationship>(w,entity);
        if(component<ecs::PlayerInput>(w,entity) || !owner || !owner->ownerEntityId.id || owner->ownerEntityId.id==entity)roots.push_back(entity);
    }
}
std::string location(uint32_t inventory,uint32_t slot) {
    const auto found=addresses.find(inventory);
    if(found==addresses.end() || found->second.size()!=1)return {};
    return found->second[0]+"/slot:"+std::to_string(slot);
}
void program(void* ctx,void*) {
    std::lock_guard<std::mutex> lock(gate);
    for(const auto& r:renewals) {
        if(r.session!=api->server_session() || !healthy || !enabled)continue;
        const auto* h=read<ecs::ImpactHierarchy>(ctx,"ImpactHierarchy",r.entity);
        const auto* life=read<ecs::LifeTime>(ctx,"LifeTime",r.entity);
        if(!h || !life || h->parentImpactId.id!=r.player || h->weaponPideId.id!=r.pide || life->triggeredDestroy)continue;
        auto* writable=static_cast<ecs::LifeTime*>(api->program_write_other(ctx,"LifeTime",r.entity,sizeof(ecs::LifeTime)));
        if(writable) {writable->lifeTime.value=kLease;writable->endOfLife.value=r.now+kLease;}
    }
    renewals.clear();
    for(const auto& r:removals) {
        if(r.session!=api->server_session())continue;
        const auto* inv=read<ecs::Inventory>(ctx,"Inventory",r.inventory);
        if(!inv || r.slot>=8)continue;
        const auto& stack=inv->slots[r.slot];
        if(stack.id.value!=r.item || stack.data.pide.id!=r.pide || stack.data.count!=r.count || !legacy.count(r.item))continue;
        bool failed=false;
        for(auto player:players) {
            const auto* selection=read<ecs::SlotSelection>(ctx,"SlotSelection",player);
            if(!selection || selection->equipment.armourFeet.entityId.id!=r.inventory || selection->equipment.armourFeet.slotIndex!=r.slot)continue;
            auto* event=static_cast<ecs::HandleEquipmentSlotChangedEvent*>(api->emit_event(ctx,"HandleEquipmentSlotChangedEvent",sizeof(ecs::HandleEquipmentSlotChangedEvent)));
            if(!event) {failed=true;break;}
            event->playerEntityId.id=player;event->equipmentSlot=emf::gen::EquipmentSlot::Armour_Feet;
        }
        if(failed)continue;
        auto* writable=static_cast<ecs::Inventory*>(api->program_write_other(ctx,"Inventory",r.inventory,sizeof(ecs::Inventory)));
        if(!writable)continue;
        writable->slots[r.slot]={};
        orphans.push_back(r);++deleted;
        api->log(kId,"Deleted legacy shoe: inventory=%u slot=%u old_item=%08x pide=%u",r.inventory,r.slot,r.item,r.pide);
    }
    removals.clear();
}
bool valid_effect(EmfWorld* w,const Effect& e,uint32_t player) {
    const auto* h=component<ecs::ImpactHierarchy>(w,e.entity);
    return h && h->parentImpactId.id==player && h->weaponPideId.id==e.pide && component<ecs::LifeTime>(w,e.entity);
}
void update_effect(EmfWorld* w,uint32_t player,uint32_t pide,int percent,int64_t now) {
    auto& effect=effects[player];
    if(effect.entity) {
        const bool valid=valid_effect(w,effect,player);
        if(valid && !effect.retiring && effect.pide==pide && effect.percent==percent && percent>0) {
            const auto* life=component<ecs::LifeTime>(w,effect.entity);
            if(life && !life->triggeredDestroy) {renewals.push_back({epoch,effect.entity,player,pide,now});return;}
        }
        if(valid) {
            if(!effect.retiring)effect.retiring=api->destroy(w,effect.entity);
            return; // Wait until native shutdown removed it; never overlap two effects.
        }
        if(!effect.retiring && now-effect.at<kLease)return; // A queued spawn may not exist yet.
        effect={};
    }
    if(percent<=0 || !pide)return;
    uint8_t guid[16]={0,0,0x03,0xb0,0,0x03,0,0x53,0xa3,0,0,0,0,0,0,static_cast<uint8_t>(percent)};
    ecs::ImpactHierarchy hierarchy{};
    hierarchy.parentImpactId.id=player;hierarchy.rootImpactId.id=player;
    hierarchy.weaponPideId.id=pide;hierarchy.permissionCheckId.id=player;
    ecs::LifeTime life{};life.lifeTime.value=kLease;life.endOfLife.value=now+kLease;life.enforceDestroy=true;
    const EmfComponentInit init[]={{ecs::ImpactHierarchy::kHash,sizeof(hierarchy),&hierarchy},{ecs::LifeTime::kHash,sizeof(life),&life}};
    const auto spawned=api->spawn_ex(w,guid,0,0,0,init,2);
    if(spawned)effect={spawned,pide,percent,now,false};
}
void notify_equipped(EmfWorld* w,uint32_t player,uint32_t pide,const Record* record) {
    if(!api->local_player || player!=api->local_player())return;
    if(!healthy || !enabled || !pide || !record) {last_notice={};return;}
    if(record->percent>0) {
        const auto found=effects.find(player);
        if(found==effects.end())return;
        const auto& effect=found->second;
        const auto* life=component<ecs::LifeTime>(w,effect.entity);
        if(effect.retiring || effect.pide!=pide || effect.percent!=record->percent ||
            !valid_effect(w,effect,player) || !life || life->triggeredDestroy)return;
    }
    if(last_notice.player==player && last_notice.pide==pide && last_notice.record==record->number &&
        last_notice.percent==record->percent)return;
    if(!api->notify)return;
    api->notify("SpeedMod: Sprint bonus +%d%%",record->percent);
    last_notice={player,pide,record->number,record->percent};
}
void tick(EmfWorld* w,void*) {
    if(api->world_kind(w)!=EMF_WORLD_SERVER)return;
    std::lock_guard<std::mutex> lock(gate);
    const auto current=api->server_session();const auto now=api->ecs_time_ns(w);
    if(current!=epoch || seen_load!=load_generation.load()) {
        seen_load=load_generation.load();
        epoch=current;next_scan=next_discover=0;healthy=false;effects.clear();renewals.clear();removals.clear();orphans.clear();
        ledger=Ledger{};written.clear();ledger_path.clear();observed.clear();last_notice={};
    }
    if(loaded_session.load()!=current || !loaded_slot.load())return;
    if(ledger_path.empty()) {
        wchar_t filename[32]{};swprintf_s(filename,L"world-%04x.txt",unsigned(loaded_slot.load()));
        ledger_path=directory/L"records"/filename;
        try {
            if(std::filesystem::exists(ledger_path) && std::filesystem::file_size(ledger_path)==0)throw std::runtime_error("Empty existing ledger");
            written=read_file(ledger_path);ledger.load(written);healthy=true;
        }catch(const std::exception& e) {api->log(kId,"Record load failed; effects paused: %s",e.what());}
    }
    // Native inventory cleanup is independent of the random-roll ledger.
    for(const auto& r:orphans)if(r.session==current && r.pide) {
        const auto* state=component<ecs::ItemState>(w,r.pide);
        const auto* inv=component<ecs::Inventory>(w,r.inventory);
        if(state && state->itemId.value==r.item && state->containedInSlotId.entityId.id==r.inventory &&
            state->containedInSlotId.slotIndex==r.slot && (!inv || inv->slots[r.slot].data.pide.id!=r.pide))api->destroy(w,r.pide);
    }
    orphans.clear();
    if(now<next_scan)return;next_scan=now+250000000LL;
    if(now>=next_discover) {
        inventories.clear();players.clear();roots.clear();api->for_each_entity(w,discover,w);next_discover=now+1000000000LL;
    }
    addresses.clear();
    for(auto root:roots) {
        const auto address=root_address(w,root);if(address.empty())continue;
        std::set<uint32_t> visited;walk_address(w,root,address,visited);
    }
    std::vector<Observation> snapshot;observed.clear();
    if(removals.empty())for(auto inventory:inventories) {
        const auto* inv=component<ecs::Inventory>(w,inventory);if(!inv)continue;
        for(uint32_t slot=0;slot<8;++slot) {
            const auto& stack=inv->slots[slot];
            if(old_shoe(w,stack.id.value)) {removals.push_back({current,inventory,slot,stack.id.value,stack.data.pide.id,stack.data.count});continue;}
        }
    }
    std::set<uint32_t> seen;
    for(auto inventory:inventories) {
        const auto* inv=component<ecs::Inventory>(w,inventory);if(!inv)continue;
        for(uint32_t slot=0;slot<8;++slot) {
            const auto& stack=inv->slots[slot];const auto pide=stack.data.pide.id;
            if(stack.data.count!=1 || !pide || seen.count(pide) || !gameplay_boot(w,stack.id.value) || old_shoe(w,stack.id.value))continue;
            const auto* state=component<ecs::ItemState>(w,pide);const auto* used=component<ecs::UsedItem>(w,pide);
            if(!state || !used || state->itemId.value!=stack.id.value || used->itemId.value!=stack.id.value ||
                state->containedInSlotId.entityId.id!=inventory || state->containedInSlotId.slotIndex!=slot)continue;
            const auto* level=component<ecs::Level>(w,pide);const auto* perks=component<ecs::PerkContainerNew>(w,pide);
            Observation o{pide,location(inventory,slot),"",level?int(level->dataStorage[0]):int(state->itemLevelUi),
                speedmod::actual_rarity(perks?perks->availablePerks:0,int(state->itemRarityUi))};
            if(!info(w,stack.id.value,o.name) || !valid(o))continue;
            snapshot.push_back(o);observed[pide]=o;seen.insert(pide);
        }
    }
    if(healthy)try {
        auto candidate=ledger;
        candidate.reconcile(snapshot,[&](int level,int rarity) {++rolls;return speedmod::roll(settings,level,rarity,rng);});
        const auto bytes=candidate.serialize();
        if(bytes!=written) {atomic_save(ledger_path,bytes);written=bytes;}
        ledger=std::move(candidate); // New effects are allowed only after durable storage.
    }catch(const std::exception& e) {healthy=false;renewals.clear();api->log(kId,"Record write failed; effects expire: %s",e.what());}
    std::set<uint32_t> present(players.begin(),players.end());
    if(api->local_player && !present.count(api->local_player()))last_notice={};
    for(auto& pair:effects)if(!present.count(pair.first))update_effect(w,pair.first,0,0,now);
    for(auto player:players) {
        uint32_t pide=0;int percent=0;
        const Record* worn_record=nullptr;
        const auto* selection=component<ecs::SlotSelection>(w,player);
        if(selection && healthy && enabled) {
            const auto slot=selection->equipment.armourFeet;
            const auto* inv=component<ecs::Inventory>(w,slot.entityId.id);
            if(inv && slot.slotIndex<8) {
                pide=inv->slots[slot.slotIndex].data.pide.id;
                const auto* record=ledger.for_entity(pide);
                if(record && observed.count(pide)) {percent=record->percent;worn_record=record;}
            }
        }
        update_effect(w,player,pide,percent,now);
        notify_equipped(w,player,pide,worn_record);
    }
}
void status(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);
    api->command_reply("SpeedMod 0.3: ledger=%s enabled=%d records=%zu rolls=%llu legacy_deleted=%llu. No native tooltip. Live multiplayer unverified.",
        healthy?"ready":"paused",enabled,ledger.size(),static_cast<unsigned long long>(rolls),static_cast<unsigned long long>(deleted));
    for(auto player:players) {
        const auto* w=api->server_world();if(!w)continue;
        const auto* slots=component<ecs::SlotSelection>(const_cast<EmfWorld*>(w),player);if(!slots)continue;
        const auto slot=slots->equipment.armourFeet;const auto* inv=component<ecs::Inventory>(const_cast<EmfWorld*>(w),slot.entityId.id);
        if(!inv || slot.slotIndex>=8)continue;
        const auto* record=ledger.for_entity(inv->slots[slot.slotIndex].data.pide.id);
        if(record)api->command_reply("Player %u: %s +%d%% (server record %llu)",player,record->name.c_str(),record->percent,static_cast<unsigned long long>(record->number));
    }
}
void disable(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);enabled=false;renewals.clear();api->command_reply("Bonuses disabled; existing temporary effects expire within one second.");
}
void enable(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);enabled=true;api->command_reply("Bonuses enabled when the server ledger is ready.");
}
void reload(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);healthy=false;ledger_path.clear();renewals.clear();
    api->command_reply("Server records will be reloaded on the next server tick; invalid files pause bonuses.");
}
bool load_settings() {
    const auto path=(directory/L"config.ini").wstring();
    const auto get=[&](const wchar_t* section,const wchar_t* key,int fallback) {return int(GetPrivateProfileIntW(section,key,fallback,path.c_str()));};
    settings.low=get(L"Levels",L"Low",1);settings.high=get(L"Levels",L"High",45);
    const wchar_t* names[]={L"Common",L"Uncommon",L"Rare",L"Epic",L"Legendary"};
    for(int i=0;i<5;++i) {
        auto& r=settings.rarity[i];r.low_min=get(names[i],L"LowMin",r.low_min);r.low_max=get(names[i],L"LowMax",r.low_max);
        r.high_min=get(names[i],L"HighMin",r.high_min);r.high_max=get(names[i],L"HighMax",r.high_max);
    }
    return settings.valid();
}
void on_load(uint16_t slot,void*) {loaded_slot=slot;loaded_session=api->server_session();++load_generation;}
}
extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* incoming) {
    using namespace serverboots;
    if(!incoming || incoming->version<16 || incoming->size<offsetof(EmfApi,program_write_other)+sizeof(incoming->program_write_other))return false;
    if(!incoming->game_build || !incoming->get_component || !incoming->singleton || !incoming->on_tick || !incoming->on_load ||
        !incoming->server_session || !incoming->ecs_time_ns || !incoming->world_kind || !incoming->for_each_entity ||
        !incoming->item_info || !incoming->item_data || !incoming->spawn_ex || !incoming->destroy || !incoming->emit_event ||
        !incoming->register_program_def || !incoming->program_read_other || !incoming->program_write_other ||
        !incoming->register_command || !incoming->command_reply || !incoming->log || !incoming->server_world)return false;
    EmfGameBuild build{};build.size=sizeof(build);
    // Component and BasicPlayerInfos layouts were audited against this build.
    // This version has no executable patch and never exits the game on a mismatch.
    if(!incoming->game_build(&build) || !build.known || build.number!=1285601 || build.server)return false;
    HMODULE module=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&api),&module) || !GetModuleFileNameW(module,path,32768))return false;
    api=incoming;directory=std::filesystem::path(path).parent_path();
    if(!load_settings()) {api->log(kId,"Invalid config.ini; mod not started.");return false;}
    try {std::random_device seed;rng.seed((uint64_t(seed())<<32)^seed());legacy=legacy_ids();}catch(...) {return false;}
    static const char* reads[]={"Inventory","SlotSelection","ImpactHierarchy","LifeTime",nullptr};
    static const char* writes[]={"Inventory","LifeTime",nullptr};
    static const char* events[]={"HandleEquipmentSlotChangedEvent",nullptr};
    EmfProgramDef def{};def.size=sizeof(def);def.name="speedmod_server_records";def.world_kind=EMF_WORLD_SERVER;
    def.fn=program;def.other_read=reads;def.other_write=writes;def.events_out=events;
    if(!api->register_program_def(&def))return false;
    api->on_load(on_load,nullptr);api->on_tick(tick,nullptr);
    api->register_command("speedmod.status",": server records, worn bonuses and legacy cleanup count",status,nullptr);
    api->register_command("speedmod.disable",": expire temporary sprint bonuses",disable,nullptr);
    api->register_command("speedmod.enable",": enable recorded sprint bonuses",enable,nullptr);
    api->register_command("speedmod.reload",": reload server records after a file repair",reload,nullptr);
    api->log(kId,"0.3 loaded: server name/location records, failed connections reroll, legacy cleanup enabled. No ItemId conversion or heap bootstrap.");
    return true;
}
