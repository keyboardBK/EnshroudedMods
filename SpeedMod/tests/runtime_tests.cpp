#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include "../runtime/boots_runtime.cpp"

namespace test {
std::map<std::pair<uint32_t,uint32_t>, std::vector<uint8_t>> components;
std::map<uint32_t, std::vector<uint8_t>> items;
std::map<uint32_t,int> chosen_rarity;
std::vector<uint32_t> players;
std::vector<uint32_t> removed;
EmfWorld* world = reinterpret_cast<EmfWorld*>(0x100);
EmfWorld* client = reinterpret_cast<EmfWorld*>(0x200);
uint32_t current_session = 1, next_entity = 1000, build_number = 1283531;
int64_t clock_ns = 1000000000;
int spawned = 0;
bool delayed_spawn = false, fail_spawn = false;

void log(const char*,const char*,...) {}
void reply(const char*,...) {}
void on_tick(EmfWorldCallback,void*) {}
bool command(const char*,const char*,EmfCommandCallback,void*) { return true; }
uint8_t kind(EmfWorld* w) { return w==world ? EMF_WORLD_SERVER : EMF_WORLD_CLIENT; }
EmfWorld* server() { return world; }
uint32_t get_session() { return current_session; }
int64_t now(EmfWorld*) { return clock_ns; }
bool build(EmfGameBuild* b) { b->known=1;b->number=build_number;return true; }
void* get_component(EmfWorld*,uint32_t hash,uint32_t entity,uint64_t* size) {
    const auto it=components.find({hash,entity});
    if(it==components.end()) return nullptr;
    *size=it->second.size();return it->second.data();
}
void* item_data(EmfWorld*,uint32_t id,uint64_t* size) {
    auto it=items.find(id);if(it==items.end())return nullptr;
    *size=it->second.size();return it->second.data();
}
void enumerate(EmfWorld*,EmfEntityCallback cb,void* ctx) { for(auto p:players)cb(p,ctx); }
template<class T> T* put(uint32_t id,const T& data={}) {
    auto& bytes=components[{T::kHash,id}];bytes.resize(sizeof(T));
    std::memcpy(bytes.data(),&data,sizeof(T));return reinterpret_cast<T*>(bytes.data());
}
void expire(uint32_t id) {
    components.erase({ecs::ImpactHierarchy::kHash,id});components.erase({ecs::LifeTime::kHash,id});chosen_rarity.erase(id);
}
bool destroy(EmfWorld*,uint32_t id) { removed.push_back(id);expire(id);return true; }
uint32_t spawn(EmfWorld*,const uint8_t* guid,double,double,double,const EmfComponentInit* initial,uint32_t count) {
    if(fail_spawn)return 0;
    ++spawned;uint32_t id=next_entity++;
    assert(guid[15]>=1 && guid[15]<=5);chosen_rarity[id]=guid[15]-1;
    if(!delayed_spawn) for(uint32_t i=0;i<count;++i) {
        auto& bytes=components[{initial[i].hash,id}];bytes.resize(initial[i].size);
        std::memcpy(bytes.data(),initial[i].data,initial[i].size);
    }
    return id;
}
EmfApi fake_api() {
    EmfApi a{};a.size=sizeof(a);a.log=log;a.command_reply=reply;a.on_tick=on_tick;a.register_command=command;
    a.game_build=build;a.server_session=get_session;a.world_kind=kind;a.server_world=server;a.ecs_time_ns=now;
    a.get_component=get_component;a.item_data=item_data;a.for_each_entity=enumerate;a.destroy=destroy;a.spawn_ex=spawn;
    return a;
}
void setup_player(uint32_t player,uint32_t item,uint32_t pide,int rarity) {
    players.push_back(player);put<ecs::PlayerInput>(player);
    auto* selection=put<ecs::SlotSelection>(player);selection->equipment.armourFeet.entityId.id=100+player;
    auto* inv=put<ecs::Inventory>(100+player);inv->slots[0].id.value=item;inv->slots[0].data.count=1;inv->slots[0].data.pide.id=pide;
    auto* state=put<ecs::ItemState>(pide);state->itemRarityUi=static_cast<emf::gen::ItemRarity>(0); // stale UI says Common
    auto* perks=put<ecs::PerkContainerNew>(pide);perks->availablePerks=static_cast<uint8_t>(rarity+1);perks->unlockedPerks=0;
    items[item].resize(2088);items[item][260]=17;
}
void reset() {
    components.clear();items.clear();chosen_rarity.clear();players.clear();removed.clear();effects.clear();
    session=0;next_poll=next_discover=0;clock_ns=1000000000;current_session++;spawned=0;next_entity=1000;
    delayed_spawn=fail_spawn=false;spawn_warning=joined_warning=false;
}
void step(int64_t elapsed=100000000) { clock_ns+=elapsed;tick(world,nullptr); }
}

int main() {
    using namespace test;
    auto a=fake_api();assert(emf_mod_load(&a));
    for(int rarity=0;rarity<5;++rarity) {
        reset();setup_player(1,42,400,rarity);step();
        assert(spawned==1 && chosen_rarity.begin()->second==rarity);
        // Buying no upgrades must still use the full actual rarity bonus.
        for(int i=0;i<30;++i)step();
        assert(spawned==1 && effects.size()==1);
    }
    reset();setup_player(1,42,400,0);setup_player(2,42,401,4);step();
    assert(spawned==2 && effects[0].boot.rarity==0 && effects[1].boot.rarity==4);
    // Same original item definition, different actual loot rarities.
    component<ecs::PerkContainerNew>(world,400)->availablePerks=4;step();
    assert(spawned==3 && effects[0].boot.rarity==3 && removed.size()==1);
    component<ecs::Inventory>(world,101)->slots[0].data.count=0;step();
    assert(effects[0].entity==0 && removed.size()==2);
    reset();setup_player(1,42,400,4);uint64_t vanity=1ULL<<18;
    std::memcpy(items[42].data()+1496,&vanity,8);step();assert(spawned==0);
    reset();setup_player(1,42,400,4);items[42][260]=16;step();assert(spawned==0);
    reset();setup_player(1,42,400,4);component<ecs::SlotSelection>(world,1)->equipment.armourFeet.slotIndex=9;
    step();assert(spawned==0);
    reset();setup_player(1,42,400,4);delayed_spawn=true;step();
    for(int i=0;i<8;++i)step();assert(spawned==1);
    component<ecs::PerkContainerNew>(world,400)->availablePerks=1;step();
    assert(spawned==1); // swapping while the old effect is still pending cannot overlap it
    reset();setup_player(1,42,400,4);step();uint32_t effect=effects[0].entity;
    auto* life=component<ecs::LifeTime>(world,effect);assert(life->endOfLife.value==clock_ns+speedmod::kLeaseNs);
    // Disabled callback: the native lifetime watchdog can expire the effect.
    clock_ns+=speedmod::kLeaseNs+1;expire(effect);tick(world,nullptr);assert(spawned==2);
    current_session++;components.clear();players.clear();step();assert(effects.empty());
    reset();setup_player(1,42,400,4);tick(client,nullptr);assert(spawned==0);
    reset();setup_player(1,42,400,4);step();components.erase({ecs::PlayerInput::kHash,1});step();assert(effects.empty());
    assert(speedmod::actual_rarity(0,4)==4);assert(speedmod::actual_rarity(0,-1)==-1);
    build_number=999;assert(!emf_mod_load(&a));
    std::puts("PASS: 5 rarities, same-item different drops, upgrade independence, no stacking, swap/unequip, cosmetics, wrong slot, invalid slot, pending spawn, watchdog respawn, session cleanup, client exclusion, disconnect, unknown build.");
}
