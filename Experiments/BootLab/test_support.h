#pragma once
#include <cassert>
#include <cstdarg>
#include <utility>
namespace test {
using namespace bootlab;
inline std::map<std::pair<uint32_t,uint32_t>,std::vector<uint8_t>> data;
inline std::map<uint32_t,std::vector<uint8_t>> items;
inline std::vector<uint32_t> entities{1,10};
inline uint32_t epoch = 1;
inline uint8_t kind = EMF_WORLD_SERVER;
inline EmfWorld* world = reinterpret_cast<EmfWorld*>(0x100);
inline int64_t now = 1000000000LL;
inline bool refuse_write = false;
inline std::string reply_text;
inline std::vector<std::string> commands;
template<class T> T* put(uint32_t entity, const T& value = {}) {
    auto& bytes = data[{T::kHash,entity}]; bytes.resize(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T)); return reinterpret_cast<T*>(bytes.data());
}
inline void* get(EmfWorld*,uint32_t hash,uint32_t entity,uint64_t* size) {
    auto found = data.find({hash,entity}); if (found == data.end()) return nullptr;
    *size = found->second.size(); return found->second.data();
}
inline bool build(EmfGameBuild* b) { b->known=true; b->server=false; b->number=1283531; strcpy_s(b->sha256,game_sha); return true; }
inline void log(const char*,const char*,...) {}
inline void reply(const char* fmt,...) {
    char text[2048]{}; va_list args; va_start(args,fmt); vsnprintf_s(text,sizeof(text),_TRUNCATE,fmt,args); va_end(args);
    reply_text = text;
}
inline uint32_t session() { return epoch; }
inline uint8_t world_kind(EmfWorld*) { return kind; }
inline int64_t time(EmfWorld*) { return now; }
inline uint32_t local_player() { return 1; }
inline void enumerate(EmfWorld*,EmfEntityCallback cb,void* user) { for (auto e:entities) cb(e,user); }
inline void* item(EmfWorld*,uint32_t id_,uint64_t* size) {
    auto it=items.find(id_); if(it==items.end())return nullptr; *size=it->second.size();return it->second.data();
}
inline bool command(const char* name,const char*,EmfCommandCallback,void*) { commands.push_back(name); return true; }
inline void on_tick(EmfWorldCallback,void*) {}
inline bool register_program(const EmfProgramDef*) {return true;}
inline void* program_read(void*,const char* name,uint32_t entity,uint32_t expected) {
    const uint32_t hash = std::strcmp(name,"Movement")==0 ? ecs::Movement::kHash : 0;
    uint64_t size = 0; void* result = get(world,hash,entity,&size); return size==expected ? result:nullptr;
}
inline void* program_write(void* ctx,const char* name,uint32_t entity,uint32_t expected) {
    return refuse_write ? nullptr : program_read(ctx,name,entity,expected);
}
inline bool item_info(EmfWorld*,uint32_t id_,EmfItemInfo* out) {out->item_id=id_;strcpy_s(out->name,"VanillaBoot");return true;}
inline void* template_component(EmfWorld*,uint32_t hash,uint32_t,uint64_t* size) { return get(world,hash,99,size); }
inline bool mod_info(const char*,EmfModInfo*) { return false; }
inline void setup() {
    data.clear(); items.clear(); commands.clear(); epoch=1; kind=EMF_WORLD_SERVER; now=1000000000LL; refuse_write=false;
    static EmfApi a{}; a.version=16; a.size=sizeof(a); a.game_build=build; a.get_component=get;
    a.world_kind=world_kind; a.server_session=session; a.ecs_time_ns=time; a.for_each_entity=enumerate;
    a.item_data=item; a.register_command=command; a.log=log; a.command_reply=reply; a.on_tick=on_tick;
    a.item_info=item_info;
    a.local_player=local_player; a.register_program_def=register_program; a.program_read_other=program_read;
    a.program_write_other=program_write; a.get_template_component=template_component; a.mod_info=mod_info;
    api=&a;
    items[100].resize(2088); items[100][260]=17; items[100][1496]=2;
    put<ecs::PlayerInput>(1);
    auto* slots=put<ecs::SlotSelection>(1); slots->equipment.armourFeet.entityId.id=10; slots->equipment.armourFeet.slotIndex=0;
    auto* inv=put<ecs::Inventory>(10); inv->slots[0].id.value=100; inv->slots[0].data.count=1; inv->slots[0].data.pide.id=20;
    auto* state=put<ecs::ItemState>(20); state->itemId.value=100; state->itemLevelUi=45;
    state->itemRarityUi=emf::gen::ItemRarity(4); state->containedInSlotId.entityId.id=10;
    put<ecs::UsedItem>(20)->itemId.value=100;
    put<ecs::Level>(20)->dataStorage[0]=45;
    auto* perks=put<ecs::PerkContainerNew>(20); perks->availablePerks=5; perks->unlockedPerks=2;
}
inline void observer_tests() {
    setup(); Boot b; assert(inspect(world,10,0,b) && b.level==45 && b.rarity==4);
    auto moved=b; moved.pide=777; moved.inventory=55; moved.session=9;
    assert(fingerprint(b)==fingerprint(moved)); // Two distinct but identical shoes collide.
    moved.durability=8; moved.present|=4; assert(fingerprint(b)!=fingerprint(moved));
    assert(!inspect(world,10,8,b)); kind=EMF_WORLD_CLIENT; assert(!inspect(world,10,0,b));
    kind=EMF_WORLD_SERVER; put<ecs::UsedItem>(20)->itemId.value=101; assert(!inspect(world,10,0,b));
    assert(!label_valid("../bad") && !label_valid("a/b") && label_valid("before_save"));
    assert(supported(api)); api=nullptr;
}
}
