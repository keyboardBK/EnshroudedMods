#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"
#include "boots_logic.h"
#include "resource_heap.h"
namespace {
namespace ecs=emf::gen::ecs;
constexpr const char* kId="speedmod_boots";
constexpr const char* kRegistry="g38_items::ItemRegistry";
const EmfApi* api=nullptr;
speedmod::Settings settings;
std::mt19937_64 rng;
struct Variant { uint32_t base; int bonus; };
std::unordered_map<uint32_t,Variant> variants;
std::unordered_map<uint32_t,std::array<uint32_t,101>> catalog;
std::vector<uint32_t> inventories, players;
struct Change {
    uint32_t session, inventory, slot, old_id, pide, new_id;
    int level, rarity, bonus;
    void* registry;
    bool restoring;
};
std::vector<Change> queue;
std::vector<uint32_t> queued_players;
std::mutex queue_mutex;
std::atomic<bool> restore_mode{false};
std::atomic<uint64_t> changed{0};
uint32_t session=0;
int64_t next_poll=0,next_discover=0;
bool ready=false, warned=false;

template<class T> T* component(EmfWorld* w,uint32_t entity) {
    uint64_t size=0;void* p=api->get_component(w,T::kHash,entity,&size);
    return p && size==sizeof(T) ? static_cast<T*>(p) : nullptr;
}
template<class T> const T* read(void* ctx,const char* name,uint32_t entity) {
    return static_cast<const T*>(api->program_read_other(ctx,name,entity,sizeof(T)));
}
template<class T> T* write(void* ctx,const char* name,uint32_t entity) {
    return static_cast<T*>(api->program_write_other(ctx,name,entity,sizeof(T)));
}
bool gameplay_boot(EmfWorld* w,uint32_t id) {
    uint64_t size=0;const auto* p=static_cast<const uint8_t*>(api->item_data(w,id,&size));
    if(!p || size!=2088 || p[260]!=17)return false;
    uint32_t flags=0;std::memcpy(&flags,p+1496,4);
    return (flags&(1u<<18))==0 && (flags&(1u<<1))!=0;
}
void item_visit(const EmfItemInfo* item,void* opaque) {
    auto* w=static_cast<EmfWorld*>(opaque);
    unsigned base=0;int bonus=-1,used=0;
    if(sscanf_s(item->name,"SM02_Boot_%8x_P%3d%n",&base,&bonus,&used)==2 &&
       used==23 && item->name[used]=='\0' && bonus>=0 && bonus<=100 && gameplay_boot(w,item->item_id)) {
        catalog[base][bonus]=item->item_id;
        variants[item->item_id]={base,bonus};
    }
}
bool build_catalog(EmfWorld* w) {
    catalog.clear();variants.clear();api->for_each_item(w,item_visit,w);
    if(catalog.empty())return false;
    for(const auto& entry:catalog) {
        if(!gameplay_boot(w,entry.first))return false;
        for(const auto id:entry.second)if(!id)return false;
    }
    api->log(kId,"Catalog: %zu boots, %zu persistent variants",catalog.size(),variants.size());return true;
}
void discover(uint32_t entity,void* opaque) {
    auto* w=static_cast<EmfWorld*>(opaque);
    if(component<ecs::Inventory>(w,entity))inventories.push_back(entity);
    if(component<ecs::PlayerInput>(w,entity) && component<ecs::SlotSelection>(w,entity))players.push_back(entity);
}
void apply_changes(void* ctx,void*) {
    std::vector<Change> batch;std::vector<uint32_t> current_players;
    {std::lock_guard<std::mutex> lock(queue_mutex);batch.swap(queue);current_players=queued_players;}
    for(const auto& c:batch) {
        if(!c.registry || c.session!=api->server_session() || c.registry!=api->program_singleton(ctx,kRegistry) ||
           c.restoring!=restore_mode.load())continue;
        const auto* inv=read<ecs::Inventory>(ctx,"Inventory",c.inventory);
        const auto* used=read<ecs::UsedItem>(ctx,"UsedItem",c.pide);
        const auto* state=read<ecs::ItemState>(ctx,"ItemState",c.pide);
        if(!inv || !used || !state || c.slot>=8)continue;
        const auto& stack=inv->slots[c.slot];
        if(stack.id.value!=c.old_id || stack.data.pide.id!=c.pide || stack.data.count!=1 ||
           used->itemId.value!=c.old_id || state->itemId.value!=c.old_id ||
           state->containedInSlotId.entityId.id!=c.inventory || state->containedInSlotId.slotIndex!=c.slot)continue;
        if(!c.restoring) {
            const auto* level=read<ecs::Level>(ctx,"Level",c.pide);
            const auto* perks=read<ecs::PerkContainerNew>(ctx,"PerkContainerNew",c.pide);
            const int actual_level=level ? int(level->dataStorage[0]) : state->itemLevelUi;
            if(actual_level!=c.level || speedmod::actual_rarity(perks?perks->availablePerks:0,int(state->itemRarityUi))!=c.rarity)continue;
        }
        // Reserve equipment notification before any mutation. Rebuild the native effect on a worn shoe.
        bool event_failed=false;
        for(const auto player:current_players) {
            const auto* slots=read<ecs::SlotSelection>(ctx,"SlotSelection",player);
            if(!slots || slots->equipment.armourFeet.entityId.id!=c.inventory || slots->equipment.armourFeet.slotIndex!=c.slot)continue;
            auto* event=static_cast<ecs::HandleEquipmentSlotChangedEvent*>(api->emit_event(ctx,"HandleEquipmentSlotChangedEvent",sizeof(ecs::HandleEquipmentSlotChangedEvent)));
            if(!event) { event_failed=true;break; }
            event->playerEntityId.id=player;event->equipmentSlot=emf::gen::EquipmentSlot::Armour_Feet;
        }
        if(event_failed)continue;
        auto* wi=write<ecs::Inventory>(ctx,"Inventory",c.inventory);
        auto* wu=write<ecs::UsedItem>(ctx,"UsedItem",c.pide);
        auto* ws=write<ecs::ItemState>(ctx,"ItemState",c.pide);
        if(!wi || !wu || !ws)continue;
        // Keep the SAME PIDE: level, rarity, upgrades, gems, dye, ownership and durability are untouched.
        wi->slots[c.slot].id.value=c.new_id;wu->itemId.value=c.new_id;ws->itemId.value=c.new_id;
        ++changed;
        api->log(kId,"%s inventory=%u slot=%u pide=%u item=%08x -> %08x level=%d rarity=%d bonus=+%d%%",
                 c.restoring?"Restored":"Rolled",c.inventory,c.slot,c.pide,c.old_id,c.new_id,c.level,c.rarity,c.bonus);
    }
}
void tick(EmfWorld* w,void*) {
    if(api->world_kind(w)!=EMF_WORLD_SERVER)return;
    const uint32_t epoch=api->server_session();const int64_t now=api->ecs_time_ns(w);
    if(epoch!=session || now<next_poll-250000000LL) {
        std::lock_guard<std::mutex> lock(queue_mutex);
        queue.clear();queued_players.clear();inventories.clear();players.clear();
        session=epoch;next_poll=next_discover=0;ready=false;warned=false;
    }
    if(now<next_poll)return;next_poll=now+250000000LL;
    if(!ready) {
        ready=build_catalog(w);
        if(!ready) {
            if(!warned)api->log(kId,"Missing/incomplete 0.2 EML variants; refusing item conversion.");
            warned=true;next_poll=now+5000000000LL;return;
        }
    }
    if(now>=next_discover) {
        inventories.clear();players.clear();api->for_each_entity(w,discover,w);next_discover=now+1000000000LL;
    }
    std::lock_guard<std::mutex> lock(queue_mutex);
    // Do not overwrite a batch still awaiting the next safe program callback.
    if(!queue.empty())return;
    queued_players=players;
    std::unordered_set<uint32_t> seen;
    const bool restoring=restore_mode.load();
    for(const auto entity:inventories) {
        const auto* inv=component<ecs::Inventory>(w,entity);if(!inv)continue;
        for(uint32_t slot=0;slot<8 && queue.size()<256;++slot) {
            const auto& s=inv->slots[slot];
            if(!s.data.pide.id || s.data.count!=1 || seen.count(s.data.pide.id))continue;
            const auto variant=variants.find(s.id.value);const auto base=catalog.find(s.id.value);
            if(restoring ? variant==variants.end() : (variant!=variants.end() || base==catalog.end()))continue;
            const auto* state=component<ecs::ItemState>(w,s.data.pide.id);
            const auto* used=component<ecs::UsedItem>(w,s.data.pide.id);
            const auto* level=component<ecs::Level>(w,s.data.pide.id);
            const auto* perks=component<ecs::PerkContainerNew>(w,s.data.pide.id);
            if(!state || !used || state->itemId.value!=s.id.value || used->itemId.value!=s.id.value)continue;
            const int lv=level?int(level->dataStorage[0]):state->itemLevelUi;
            const int rarity=speedmod::actual_rarity(perks?perks->availablePerks:0,int(state->itemRarityUi));
            if(!restoring && (lv<1 || lv>255 || rarity<0))continue;
            const int bonus=restoring?variant->second.bonus:speedmod::roll(settings,lv,rarity,rng);
            const uint32_t target=restoring?variant->second.base:base->second[bonus];
            queue.push_back({epoch,entity,slot,s.id.value,s.data.pide.id,target,lv,rarity,bonus,api->singleton(w,kRegistry),restoring});
            seen.insert(s.data.pide.id);
        }
    }
}
void status(EmfWorld*,int,const char* const*,void*) {
    api->command_reply("SpeedMod 0.2.1: %s, conversions=%llu. Existing variants never reroll. Live save/multiplayer tests pending.",restore_mode.load()?"RESTORE":"ROLL",static_cast<unsigned long long>(changed.load()));
}
void restore(EmfWorld*,int,const char* const*,void*) {
    restore_mode=true;
    api->command_reply("RESTORE mode: converting LOADED shoes to original IDs. Stops random rolls until restart. Keep EML data until ALL offline characters/chests are restored and saved.");
}
bool load_settings() {
    HMODULE module=nullptr;wchar_t path[MAX_PATH]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&api),&module) ||
       !GetModuleFileNameW(module,path,MAX_PATH))return false;
    auto* slash=wcsrchr(path,L'\\');if(!slash)return false;wcscpy_s(slash+1,MAX_PATH-size_t(slash+1-path),L"config.ini");
    if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return false;
    const auto get=[&](const wchar_t* section,const wchar_t* key,int fallback) {return int(GetPrivateProfileIntW(section,key,fallback,path));};
    settings.low=get(L"Levels",L"Low",1);settings.high=get(L"Levels",L"High",45);
    const wchar_t* names[]={L"Common",L"Uncommon",L"Rare",L"Epic",L"Legendary"};
    for(int i=0;i<5;++i) {
        auto& r=settings.rarity[i];r.low_min=get(names[i],L"LowMin",r.low_min);r.low_max=get(names[i],L"LowMax",r.low_max);
        r.high_min=get(names[i],L"HighMin",r.high_min);r.high_max=get(names[i],L"HighMax",r.high_max);
    }
    return settings.valid();
}
bool prepare_resource_heap(const EmfGameBuild& build) {
    // This is a process-memory patch only. Never change the executable on disk.
    // Verify the early chain bootstrap ran against the exact supported executable.
    if(build.server || std::strcmp(build.sha256,"0403981fd2e8ad18bd8c5698dff03c8d505b5a46677832c496db9c645513f2a6")!=0) {
        api->log(kId,"Resource heap fix: unsupported executable; no code changed.");return false;
    }
    auto* code=reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr))+speedmod::kHeapInitRva;
    MEMORY_BASIC_INFORMATION arena{};
    if(!VirtualQuery(reinterpret_cast<void*>(0x20000000000ULL),&arena,sizeof(arena)) ||
       !speedmod::heap_instruction_matches(code,true) ||
       (arena.State==MEM_COMMIT && arena.RegionSize<speedmod::kResourceHeapBytes)) {
        api->log(kId,"Resource heap bootstrap missing/late. Add speedmod_bootstrap.dll BEFORE speedmod_eml.dll in dinput8-chain.ini.");return false;
    }
    api->log(kId,"Verified startup bootstrap: resource heap 768 MiB (shared original UI icons).");return true;
}
}
extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* incoming) {
    if(!incoming || incoming->version<16 || incoming->size<offsetof(EmfApi,program_write_other)+sizeof(incoming->program_write_other) ||
       !incoming->game_build || !incoming->program_write_other || !incoming->program_read_other || !incoming->register_program_def)return false;
    EmfGameBuild build{};build.size=sizeof(build);
    if(!incoming->game_build(&build) || !build.known || build.number!=1283531)return false;
    api=incoming;
    if(!prepare_resource_heap(build))return false;
    if(!load_settings()) {api->log(kId,"Missing/invalid config.ini; refusing conversions");return false;}
    try {std::random_device seed;rng.seed((uint64_t(seed())<<32)^seed());}catch(...) {return false;}
    static const char* reads[]={"Inventory","UsedItem","ItemState","Level","PerkContainerNew","SlotSelection",nullptr};
    static const char* writes[]={"Inventory","UsedItem","ItemState",nullptr};
    static const char* singletons[]={kRegistry,nullptr};
    static const char* events[]={"HandleEquipmentSlotChangedEvent",nullptr};
    EmfProgramDef def{};def.size=sizeof(def);def.name="speedmod_boot_rolls";def.world_kind=EMF_WORLD_SERVER;
    def.fn=apply_changes;def.singletons=singletons;def.other_read=reads;def.other_write=writes;def.events_out=events;
    if(!api->register_program_def(&def))return false;
    api->on_tick(tick,nullptr);
    api->register_command("speedmod_boots.status",": show roll mode",status,nullptr);
    api->register_command("speedmod_boots.restore",": restore LOADED shoes before uninstall",restore,nullptr);
    api->log(kId,"Loaded 0.2.1: persistent level/rarity rolls. Requires matching EML data on ALL peers.");return true;
}
