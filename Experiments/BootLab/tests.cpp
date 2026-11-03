#include "bootlab.cpp"
#include "test_support.h"
namespace test {
bool info(EmfWorld*,uint32_t,EmfItemInfo* value){strcpy_s(value->name,"VanillaBoot");return true;}
void* formula_read(void*,const char* name,uint32_t entity,uint32_t expected) {
    uint32_t h=0;
    if(!std::strcmp(name,"Movement"))h=ecs::Movement::kHash;
    if(!std::strcmp(name,"SlotSelection"))h=ecs::SlotSelection::kHash;
    if(!std::strcmp(name,"Inventory"))h=ecs::Inventory::kHash;
    if(!std::strcmp(name,"ItemState"))h=ecs::ItemState::kHash;
    if(!std::strcmp(name,"UsedItem"))h=ecs::UsedItem::kHash;
    if(!std::strcmp(name,"Level"))h=ecs::Level::kHash;
    if(!std::strcmp(name,"PerkContainerNew"))h=ecs::PerkContainerNew::kHash;
    uint64_t size=0;void* p=get(world,h,entity,&size);return size==expected?p:nullptr;
}
void* formula_write(void* ctx,const char* name,uint32_t entity,uint32_t size){
    return refuse_write?nullptr:formula_read(ctx,name,entity,size);
}
void formula_setup(){
    setup();auto* mutable_api=const_cast<EmfApi*>(api);mutable_api->item_info=info;
    mutable_api->program_read_other=formula_read;mutable_api->program_write_other=formula_write;
    ecs::Movement m{};m.rootId.value=2517049208u;m.storageCount=18;m.dataStorage[5]=8.48f;
    put<ecs::Movement>(1,m);m.dataStorage[5]=8;put<ecs::Movement>(99,m);
    contribution={};pending={};epoch=0;owner=0;apply_enabled=false;entities={1,10};
}
}
int main(){
    using namespace bootlab;
    test::observer_tests();
    assert(formula_percent(1,0)==3 && formula_percent(1,4)==18 && formula_percent(45,4)==60);
    Contribution c;float next=0;
    assert(c.plan(8.48f,20,next) && std::fabs(next-10.08f)<0.00001f);c.committed(8.48f,next,20);
    for(int i=0;i<1000;++i){assert(c.plan(10.08f,20,next));assert(std::fabs(next-10.08f)<0.00001f);}
    assert(c.plan(10.08f,0,next) && std::fabs(next-8.48f)<0.00001f);
    assert(!c.plan(11.0f,20,next) && c.conflict); // preserve unknown external changes
    test::formula_setup();auto before=test::data;
    tick(test::world,nullptr);update(nullptr,nullptr);assert(before==test::data); // default no writes
    const char* args[]={"apply","test-character"};apply(test::world,2,args,nullptr);tick(test::world,nullptr);update(nullptr,nullptr);
    auto* m=const_cast<ecs::Movement*>(component<ecs::Movement>(test::world,1));
    assert(std::fabs(m->dataStorage[5]-13.28f)<0.00001f && contribution.attached);
    for(int i=0;i<100;++i){tick(test::world,nullptr);update(nullptr,nullptr);}
    assert(std::fabs(m->dataStorage[5]-13.28f)<0.00001f);
    disable(test::world,0,nullptr,nullptr);tick(test::world,nullptr);update(nullptr,nullptr);
    assert(std::fabs(m->dataStorage[5]-8.48f)<0.00001f && !contribution.attached);
    test::formula_setup();tick(test::world,nullptr);apply(test::world,2,args,nullptr);tick(test::world,nullptr);update(nullptr,nullptr);
    test::put<ecs::PlayerInput>(2);test::put<ecs::SlotSelection>(2);test::entities.push_back(2);
    tick(test::world,nullptr);update(nullptr,nullptr);
    assert(!contribution.attached && !apply_enabled); // Stop and undo when another player joins.
    test::formula_setup();tick(test::world,nullptr);apply(test::world,2,args,nullptr);tick(test::world,nullptr);update(nullptr,nullptr);
    test::put<ecs::SlotSelection>(1)->equipment.armourFeet={};tick(test::world,nullptr);update(nullptr,nullptr);
    assert(!contribution.attached); // Barefoot returns to native speed.
    test::formula_setup();tick(test::world,nullptr);apply(test::world,2,args,nullptr);tick(test::world,nullptr);
    test::refuse_write=true;before=test::data;update(nullptr,nullptr);assert(test::data==before && !contribution.attached);
    test::refuse_write=false;++test::epoch;update(nullptr,nullptr);assert(!contribution.attached);
    test::formula_setup();tick(test::world,nullptr);apply(test::world,2,args,nullptr);tick(test::world,nullptr);
    test::put<ecs::Inventory>(10)->slots[0].id.value=777;before=test::data;update(nullptr,nullptr);assert(before==test::data);
    std::puts("PASS formula: ranges, default read-only, native-existing bonus, no accumulation, disable, stale session/item, refused writes, external conflict");
}
