#include <cassert>
#include <cstdio>
#include <map>
#include <utility>
#include "../runtime/boots_runtime.cpp"
namespace test {
std::map<std::pair<uint32_t,uint32_t>,std::vector<uint8_t>> data;
std::map<uint32_t,std::vector<uint8_t>> items;
EmfWorld* world=reinterpret_cast<EmfWorld*>(0x100);
uint32_t epoch=1;int64_t now=1000000000LL;uint8_t kind=EMF_WORLD_SERVER;
bool refuse_write=false,refuse_event=false;int events=0;
std::vector<uint32_t> entities{1,10};
void log(const char*,const char*,...) {}
void reply(const char*,...) {}
void* registry(EmfWorld*,const char*) {return world;}
void* program_registry(void*,const char*) {return world;}
uint32_t get_session(){return epoch;}
uint8_t get_kind(EmfWorld*){return kind;}
int64_t time(EmfWorld*){return now;}
void* get(EmfWorld*,uint32_t hash,uint32_t entity,uint64_t* size) {
    auto it=data.find({hash,entity});if(it==data.end())return nullptr;
    *size=it->second.size();return it->second.data();
}
template<class T> T* put(uint32_t entity,const T& value={}) {
    auto& d=data[{T::kHash,entity}];d.resize(sizeof(T));std::memcpy(d.data(),&value,sizeof(T));return reinterpret_cast<T*>(d.data());
}
template<class T> T* at(uint32_t entity) {uint64_t size=0;return static_cast<T*>(get(world,T::kHash,entity,&size));}
uint32_t hash(const char* name) {
    if(!std::strcmp(name,"Inventory"))return ecs::Inventory::kHash;
    if(!std::strcmp(name,"UsedItem"))return ecs::UsedItem::kHash;
    if(!std::strcmp(name,"ItemState"))return ecs::ItemState::kHash;
    if(!std::strcmp(name,"Level"))return ecs::Level::kHash;
    if(!std::strcmp(name,"PerkContainerNew"))return ecs::PerkContainerNew::kHash;
    if(!std::strcmp(name,"SlotSelection"))return ecs::SlotSelection::kHash;
    assert(false);return 0;
}
void* read(void*,const char* name,uint32_t id,uint32_t expected) {
    uint64_t size=0;void* p=get(world,hash(name),id,&size);return size==expected?p:nullptr;
}
void* write(void* ctx,const char* name,uint32_t id,uint32_t expected) {
    return refuse_write?nullptr:read(ctx,name,id,expected);
}
void* emit(void*,const char*,uint32_t size) {
    static ecs::HandleEquipmentSlotChangedEvent event{};
    assert(size==sizeof(event));if(refuse_event)return nullptr;++events;return &event;
}
void enumerate(EmfWorld*,EmfEntityCallback cb,void* ctx) {for(auto e:entities)cb(e,ctx);}
void* item_data(EmfWorld*,uint32_t id,uint64_t* size) {
    auto it=items.find(id);if(it==items.end())return nullptr;*size=it->second.size();return it->second.data();
}
void list_items(EmfWorld*,EmfItemCallback cb,void* ctx) {
    for(uint32_t b=0;b<=100;++b) {
        EmfItemInfo i{};i.item_id=1000+b;std::snprintf(i.name,sizeof(i.name),"SM02_Boot_%08x_P%03u",100u,b);cb(&i,ctx);
    }
}
void setup() {
    data.clear();items.clear();queue.clear();queued_players.clear();catalog.clear();variants.clear();inventories.clear();players.clear();
    ready=false;warned=false;session=0;next_poll=next_discover=0;epoch=1;now=1000000000LL;kind=EMF_WORLD_SERVER;
    restore_mode=false;changed=0;refuse_write=refuse_event=false;events=0;rng.seed(12345);settings={};
    static EmfApi a{};a.log=log;a.command_reply=reply;a.singleton=registry;a.program_singleton=program_registry;
    a.server_session=get_session;a.world_kind=get_kind;a.ecs_time_ns=time;a.get_component=get;
    a.program_read_other=read;a.program_write_other=write;a.emit_event=emit;a.for_each_entity=enumerate;
    a.item_data=item_data;a.for_each_item=list_items;api=&a;
    for(uint32_t i=1000;i<=1100;++i) {auto& d=items[i];d.resize(2088);d[260]=17;d[1496]=2;}
    items[100]=items[1000];
    put<ecs::PlayerInput>(1);auto* ss=put<ecs::SlotSelection>(1);ss->equipment.armourFeet.entityId.id=10;ss->equipment.armourFeet.slotIndex=0;
    auto* inv=put<ecs::Inventory>(10);inv->slots[0].id.value=100;inv->slots[0].data.count=1;inv->slots[0].data.pide.id=20;
    auto* state=put<ecs::ItemState>(20);state->itemId.value=100;state->itemLevelUi=45;state->itemRarityUi=emf::gen::ItemRarity(4);
    state->containedInSlotId.entityId.id=10;state->containedInSlotId.slotIndex=0;
    put<ecs::UsedItem>(20)->itemId.value=100;put<ecs::Level>(20)->dataStorage[0]=45;
    auto* perks=put<ecs::PerkContainerNew>(20);perks->availablePerks=5;perks->unlockedPerks=2;
}
void scan(){tick(world,nullptr);}
void apply(){apply_changes(reinterpret_cast<void*>(1),nullptr);}
void advance(){now+=1000000000LL;scan();apply();}
}
int main() {
    speedmod::Settings s;assert(s.valid());
    assert(s.range(1,0).min==1 && s.range(1,0).max==5);
    assert(s.range(45,0).min==10 && s.range(45,0).max==15);
    assert(s.range(1,4).min==15 && s.range(1,4).max==20);
    assert(s.range(45,4).min==20 && s.range(45,4).max==100);
    assert(s.range(999,4).max==100 && s.range(-1,0).min==1);
    s.rarity[0].low_min=101;assert(!s.valid());s={};s.high=1;assert(!s.valid());s={};
    std::mt19937_64 random(5);bool minimum=false,maximum=false;
    for(int i=0;i<10000;++i) {int r=speedmod::roll(s,45,4,random);assert(r>=20&&r<=100);minimum|=r==20;maximum|=r==100;}
    assert(minimum&&maximum);
    for(int rank=0;rank<5;++rank)for(int lv=1;lv<=60;++lv) {auto r=s.range(lv,rank);assert(r.min>=0&&r.max<=100&&r.min<=r.max);}
    assert(speedmod::actual_rarity(5,0)==4 && speedmod::actual_rarity(0,3)==3 && speedmod::actual_rarity(0,9)==-1);
    using namespace test;
    setup();scan();assert(queue.size()==1);apply();
    const auto rolled=at<ecs::UsedItem>(20)->itemId.value;
    assert(rolled>=1020&&rolled<=1100 && changed==1 && events==1);
    assert(at<ecs::Inventory>(10)->slots[0].id.value==rolled && at<ecs::ItemState>(20)->itemId.value==rolled);
    assert(at<ecs::Inventory>(10)->slots[0].data.pide.id==20 && at<ecs::Level>(20)->dataStorage[0]==45);
    assert(at<ecs::PerkContainerNew>(20)->unlockedPerks==2);
    // Re-equip, storage/trade, configuration edits, and a new world session do not reroll a saved variant ID.
    at<ecs::SlotSelection>(1)->equipment.armourFeet.entityId.id=0;settings.rarity[4]={0,0,0,0};advance();assert(changed==1);
    auto transferred=*at<ecs::Inventory>(10);put<ecs::Inventory>(30,transferred);at<ecs::Inventory>(10)->slots[0]={};
    at<ecs::ItemState>(20)->containedInSlotId.entityId.id=30;entities.push_back(30);advance();assert(changed==1);
    ++epoch;advance();assert(changed==1 && at<ecs::Inventory>(30)->slots[0].id.value==rolled);
    restore(nullptr,0,nullptr,nullptr);advance();assert(changed==2 && at<ecs::UsedItem>(20)->itemId.value==100);advance();assert(changed==2);
    setup();scan();++epoch;apply();assert(changed==0); // old-session queued entity IDs
    setup();scan();at<ecs::Inventory>(10)->slots[0].data.pide.id=99;apply();assert(changed==0);
    setup();scan();at<ecs::Level>(20)->dataStorage[0]=1;apply();assert(changed==0);
    setup();scan();refuse_write=true;apply();assert(changed==0 && at<ecs::UsedItem>(20)->itemId.value==100);
    setup();scan();refuse_event=true;apply();assert(changed==0);
    setup();kind=EMF_WORLD_CLIENT;scan();assert(queue.empty());
    setup();items.erase(1000);scan();assert(queue.empty()&&!ready); // incomplete catalog fails closed
    setup();scan();restore_mode=true;apply();assert(changed==0); // mode change cancels old batch
    setup();at<ecs::PerkContainerNew>(20)->availablePerks=1;scan();apply();
    assert(at<ecs::UsedItem>(20)->itemId.value>=1010 && at<ecs::UsedItem>(20)->itemId.value<=1015);
    std::puts("PASS: ranges/RNG, persistent IDs, PIDE preservation, equipment event, restore, stale requests, safe writes, client exclusion, fail-closed catalog");
}
