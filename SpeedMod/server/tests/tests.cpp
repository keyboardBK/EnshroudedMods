#include "../runtime.cpp"
#include <cassert>
#include <cstdarg>

namespace test {
using namespace serverboots;
void ledger_tests() {
    Ledger a;int calls=0;
    const auto roll=[&](int,int) {return ++calls;};
    a.reconcile({{11,"account:A/page:0/slot:1","Same boot",45,4},{12,"chest:1/page:0/slot:2","Same boot",45,4}},roll);
    assert(calls==2 && a.for_entity(11)->percent==1 && a.for_entity(12)->percent==2);
    const auto first=a.for_entity(11)->number;
    // A new shoe occupies the old location while the known shoe moves. Ordering must not steal its roll.
    a.reconcile({{13,"account:A/page:0/slot:1","Same boot",45,4},{11,"chest:2/page:0/slot:3","Same boot",45,4},
        {12,"chest:1/page:0/slot:2","Same boot",45,4}},roll);
    assert(calls==3 && a.for_entity(11)->number==first && a.for_entity(13)->number!=first);
    // Trade and upgrade: live identity preserves the roll; the latest location/fields survive a restart.
    a.reconcile({{11,"account:B/page:1/slot:4","Same boot",46,4},{12,"chest:1/page:0/slot:2","Same boot",45,4},
        {13,"account:A/page:0/slot:1","Same boot",45,4}},roll);
    const auto saved=a.serialize();Ledger restarted;restarted.load(saved);
    restarted.reconcile({{900,"account:B/page:1/slot:4","Same boot",46,4},{901,"chest:1/page:0/slot:2","Same boot",45,4}},roll);
    assert(calls==3 && restarted.for_entity(900)->number==first && restarted.for_entity(901)->percent==2);
    // A renamed/mismatching or imported item rerolls once, then stays stable across repeated scans.
    restarted.reconcile({{902,"account:B/page:1/slot:4","Different boot",46,4},{903,"account:C/page:0/slot:1","Imported",10,2}},roll);
    assert(calls==5);
    for(int n=0;n<100;++n)restarted.reconcile({{902,"account:B/page:1/slot:4","Different boot",46,4},{903,"account:C/page:0/slot:1","Imported",10,2}},roll);
    assert(calls==5);
    // Unknown or non-unique locations get distinct rolls, retained during this session.
    Ledger ambiguous;
    ambiguous.reconcile({{1,"same","Boot",1,0},{2,"same","Boot",1,0},{3,"","Boot",1,0}},roll);
    const auto one=ambiguous.for_entity(1)->number,two=ambiguous.for_entity(2)->number;
    assert(one!=two);
    ambiguous.reconcile({{1,"same","Boot",1,0},{2,"same","Boot",1,0},{3,"","Boot",1,0}},roll);
    assert(calls==8);
    const auto unchanged=ambiguous.serialize();
    bool failed=false;try {ambiguous.reconcile({{1,"same","Boot",1,0},{1,"other","Boot",1,0}},roll);}catch(...) {failed=true;}
    assert(failed && ambiguous.serialize()==unchanged);
    failed=false;try {ambiguous.load(unchanged+"truncated");}catch(...) {failed=true;}
    assert(failed && ambiguous.serialize()==unchanged);
    assert(unchanged.find("SPEEDMOD_SERVER_1")==0);
    speedmod::Settings s;assert(s.range(1,0).min==1 && s.range(45,4).max==100);
}

std::map<std::pair<uint32_t,uint32_t>,std::vector<uint8_t>> data;
std::map<uint32_t,std::string> names;
std::vector<uint32_t> ids;
uint32_t session=1,created=1000;uint8_t kind=EMF_WORLD_SERVER;
int64_t clock_ns=1000000000LL;
int spawns=0,destroys=0;bool write_refused=false;
EmfWorld* world=reinterpret_cast<EmfWorld*>(1);
EmfProgramCallback registered=nullptr;
std::string reply;
std::vector<std::string> notifications;
uint32_t local_player_id=1;
template<class T> T* put(uint32_t entity,const T& t={}) {
    auto& bytes=data[{T::kHash,entity}];bytes.resize(sizeof(T));std::memcpy(bytes.data(),&t,sizeof(T));
    return reinterpret_cast<T*>(bytes.data());
}
void* get(EmfWorld*,uint32_t hash,uint32_t entity,uint64_t* size) {
    const auto found=data.find({hash,entity});if(found==data.end())return nullptr;
    if(size)*size=found->second.size();return found->second.data();
}
uint8_t world_kind(EmfWorld*) {return kind;}
uint32_t server_session() {return session;}
int64_t now(EmfWorld*) {return clock_ns;}
void enumerate(EmfWorld*,EmfEntityCallback cb,void* opaque) {for(auto entity:ids)cb(entity,opaque);}
EmfWorld* server_world() {return world;}
uint32_t local_player() {return local_player_id;}
void* singleton(EmfWorld*,const char*) {return nullptr;}
bool item_info(EmfWorld*,uint32_t id,EmfItemInfo* out) {
    const auto found=names.find(id);if(found==names.end())return false;
    strcpy_s(out->name,found->second.c_str());out->item_id=id;return true;
}
void* item_data(EmfWorld*,uint32_t,uint64_t*) {return nullptr;}
void log(const char*,const char*,...) {}
void command_reply(const char* format,...) {
    char buffer[2048];va_list args;va_start(args,format);vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,format,args);va_end(args);reply=buffer;
}
void notify(const char* format,...) {
    char buffer[2048];va_list args;va_start(args,format);vsnprintf_s(buffer,sizeof(buffer),_TRUNCATE,format,args);va_end(args);
    notifications.emplace_back(buffer);
}
bool command(const char*,const char*,EmfCommandCallback,void*) {return true;}
void subscribe_tick(EmfWorldCallback,void*) {}
void subscribe_load(EmfSaveCallback,void*) {}
bool game_build(EmfGameBuild* out) {out->known=true;out->number=1285601;return true;}
bool register_program(const EmfProgramDef* def) {registered=def->fn;return true;}
void* event(void*,const char*,uint32_t) {static ecs::HandleEquipmentSlotChangedEvent e;return &e;}
void* program_read(void*,const char* name,uint32_t entity,uint32_t size) {
    uint32_t hash=0;
    if(!std::strcmp(name,"Inventory"))hash=ecs::Inventory::kHash;
    if(!std::strcmp(name,"SlotSelection"))hash=ecs::SlotSelection::kHash;
    if(!std::strcmp(name,"ImpactHierarchy"))hash=ecs::ImpactHierarchy::kHash;
    if(!std::strcmp(name,"LifeTime"))hash=ecs::LifeTime::kHash;
    uint64_t actual=0;auto* value=get(world,hash,entity,&actual);return actual==size?value:nullptr;
}
void* program_write(void* ctx,const char* name,uint32_t entity,uint32_t size) {
    return write_refused?nullptr:program_read(ctx,name,entity,size);
}
uint32_t spawn(EmfWorld*,const uint8_t* guid,double,double,double,const EmfComponentInit* init,uint32_t count) {
    assert(guid[3]==0xb0 && guid[6]==0 && guid[7]==0x53 && guid[15]>=1 && guid[15]<=100 && count==2);
    const auto entity=++created;++spawns;
    for(uint32_t i=0;i<count;++i) {
        auto& bytes=data[{init[i].hash,entity}];bytes.resize(init[i].size);std::memcpy(bytes.data(),init[i].data,init[i].size);
    }
    return entity;
}
bool destroy(EmfWorld*,uint32_t entity) {
    ++destroys;
    for(auto it=data.begin();it!=data.end();)if(it->first.second==entity)it=data.erase(it);else ++it;
    return true;
}
EmfApi fake_api() {
    EmfApi a{};a.version=16;a.size=sizeof(a);a.log=log;a.game_build=game_build;
    a.get_component=get;a.singleton=singleton;a.world_kind=world_kind;a.server_session=server_session;
    a.ecs_time_ns=now;a.on_tick=subscribe_tick;a.on_load=subscribe_load;a.for_each_entity=enumerate;
    a.server_world=server_world;a.item_info=item_info;a.item_data=item_data;a.spawn_ex=spawn;a.destroy=destroy;
    a.emit_event=event;a.register_program_def=register_program;a.program_read_other=program_read;
    a.program_write_other=program_write;a.register_command=command;a.command_reply=command_reply;
    a.local_player=local_player;a.notify=notify;return a;
}
void notification_tests() {
    effects.clear();renewals.clear();last_notice={};notifications.clear();enabled=true;healthy=true;epoch=session;
    Record first{1,"slot:0","Boot",45,4,17};
    notify_equipped(world,1,50,&first);assert(notifications.empty()); // No effect, no success notice.
    update_effect(world,1,50,17,clock_ns);
    notify_equipped(world,2,50,&first);assert(notifications.empty()); // A guest cannot notify the host.
    local_player_id=0;notify_equipped(world,1,50,&first);assert(notifications.empty());
    local_player_id=1;notify_equipped(world,1,50,&first);
    assert(notifications.size()==1 && notifications.back()=="SpeedMod: Sprint bonus +17%");
    for(int i=0;i<100;++i) {update_effect(world,1,50,17,clock_ns);notify_equipped(world,1,50,&first);}
    assert(notifications.size()==1); // Polling and lease renewal never repeat the notice.
    notify_equipped(world,1,0,nullptr); // Unequip then put the same shoe on again.
    notify_equipped(world,1,50,&first);assert(notifications.size()==2);
    Record second{2,"slot:1","Boot",45,4,17};
    update_effect(world,1,51,17,clock_ns);notify_equipped(world,1,51,&second);
    assert(notifications.size()==2); // The old effect is still retiring.
    update_effect(world,1,51,17,clock_ns);notify_equipped(world,1,51,&second);
    assert(notifications.size()==3); // A different shoe with the same percentage is still a new equip.
    // Native effect replacement/expiry is not a new equipment change.
    destroy(world,effects[1].entity);clock_ns+=kLease;
    update_effect(world,1,51,17,clock_ns);notify_equipped(world,1,51,&second);assert(notifications.size()==3);
    Record rerolled=second;rerolled.number=3;
    notify_equipped(world,1,51,&rerolled);assert(notifications.size()==4);
    healthy=false;notify_equipped(world,1,51,&rerolled);assert(notifications.size()==4);
    healthy=true;enabled=false;notify_equipped(world,1,51,&rerolled);assert(notifications.size()==4);
    enabled=true;last_notice={};notify_equipped(world,1,51,&rerolled);assert(notifications.size()==5);
    // A valid zero roll needs no effect and is shown as zero rather than a stale previous value.
    Record zero=second;zero.number=4;zero.percent=0;
    notify_equipped(world,1,60,&zero);assert(notifications.size()==6 && notifications.back()=="SpeedMod: Sprint bonus +0%");
    const_cast<EmfApi*>(api)->notify=nullptr;last_notice={};notify_equipped(world,1,60,&zero);assert(notifications.size()==6);
    const_cast<EmfApi*>(api)->notify=notify;
}
void runtime_tests() {
    static auto a=fake_api();assert(!emf_mod_load(nullptr));EmfApi short_api{};short_api.size=4;assert(!emf_mod_load(&short_api));
    assert(emf_mod_load(&a));assert(registered);
    // Targeted cleanup removes a legacy stack, including in a chest; stale requests and
    // a current vanilla item with a numerically colliding old ID must be preserved.
    const auto old=*legacy.begin();names[old]="SM02_Boot_12345678_P020";
    auto* inv=put<ecs::Inventory>(10);inv->slots[3].id.value=old;inv->slots[3].data.count=1;inv->slots[3].data.pide.id=20;
    assert(old_shoe(world,old));names[old]="VanillaBoot";assert(!old_shoe(world,old));names.erase(old);assert(old_shoe(world,old));
    removals={{1,10,3,old,20,1}};registered(nullptr,nullptr);assert(inv->slots[3].id.value==0 && deleted==1);
    inv->slots[3].id.value=777;inv->slots[3].data.pide.id=21;inv->slots[3].data.count=1;
    removals={{1,10,3,old,20,1}};registered(nullptr,nullptr);assert(inv->slots[3].id.value==777 && deleted==1);
    inv->slots[3].id.value=old;inv->slots[3].data.pide.id=20;removals={{0,10,3,old,20,1}};
    registered(nullptr,nullptr);assert(inv->slots[3].id.value==old);
    removals={{1,10,3,old,20,1}};write_refused=true;registered(nullptr,nullptr);write_refused=false;assert(inv->slots[3].id.value==old);
    // Leases do not stack on repeated updates; change waits for native removal.
    epoch=1;healthy=true;enabled=true;effects.clear();renewals.clear();
    update_effect(world,1,20,25,clock_ns);assert(spawns==1);
    for(int i=0;i<20;++i) {update_effect(world,1,20,25,clock_ns);registered(nullptr,nullptr);}
    assert(spawns==1 && component<ecs::LifeTime>(world,created)->endOfLife.value==clock_ns+kLease);
    update_effect(world,1,21,30,clock_ns);assert(destroys==1 && spawns==1);
    update_effect(world,1,21,30,clock_ns);assert(spawns==2);
    disable(world,0,nullptr,nullptr);assert(!enabled && renewals.empty());
    update_effect(world,1,0,0,clock_ns);assert(destroys==2);
    update_effect(world,1,0,0,clock_ns);assert(effects[1].entity==0);
    // Session/world slot gating never writes another world's records.
    const auto temp=std::filesystem::absolute("build")/("server-tests-"+std::to_string(GetCurrentProcessId()));
    assert(temp.parent_path()==std::filesystem::absolute("build"));
    directory=temp;epoch=0;session=8;loaded_session=0;loaded_slot=1;ids.clear();
    tick(world,nullptr);assert(ledger_path.empty());
    on_load(0x101,nullptr);tick(world,nullptr);assert(healthy && ledger_path.filename()==L"world-0101.txt");
    const auto old_path=ledger_path;assert(std::filesystem::exists(old_path));
    ++session;tick(world,nullptr);assert(ledger_path.empty());
    on_load(1,nullptr);tick(world,nullptr);assert(ledger_path.filename()==L"world-0001.txt");
    const auto second=ledger_path;
    // Atomic disk round trip, checksum rejection, and last-good backup.
    Ledger durable;durable.reconcile({{11,"chest:0/slot:0","Boot",10,2}},[](int,int) {return 14;});
    atomic_save(second,durable.serialize());Ledger restored;restored.load(read_file(second));
    restored.reconcile({{999,"chest:0/slot:0","Boot",10,2}},[](int,int)->int {throw std::runtime_error("Unexpected reroll");});
    assert(restored.for_entity(999)->percent==14 && std::filesystem::exists(second.wstring()+L".bak"));
    // Cleanup exact files only; never traverse user's save directories.
    std::filesystem::remove(old_path);std::filesystem::remove(second);std::filesystem::remove(second.wstring()+L".bak");
    std::filesystem::remove(second.parent_path());std::filesystem::remove(temp);
    HMODULE dll=LoadLibraryW(L"build\\speedmod_server.dll");assert(dll);
    const auto load=reinterpret_cast<EmfModLoadFn>(GetProcAddress(dll,"emf_mod_load"));assert(load && !load(nullptr) && !load(&short_api));FreeLibrary(dll);
}
}
int main() {
    test::ledger_tests();test::runtime_tests();test::notification_tests();
    std::puts("PASS server records: moves/trades, duplicate names, restart remap, failed connections reroll once, durable storage, session isolation, targeted legacy cleanup, native effect leases, DLL ABI; equipment notices: host only, no polling/lease spam, re-equip, same-percent swap, reroll, unavailable effects and zero bonus");
}
