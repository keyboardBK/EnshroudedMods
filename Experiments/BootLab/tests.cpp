#include "bootlab.cpp"
#include "test_support.h"
int main(){
    using namespace bootlab;
    test::observer_tests();test::setup();
    const auto before=test::data;
    assert(emf_mod_load(api) && test::commands.size()==3);
    inspect_command(test::world,0,nullptr,nullptr);status(test::world,0,nullptr,nullptr);
    assert(test::data==before);
    auto* d=test::put<ecs::Durability>(20);d->dataStorage[0]=99;d->dataStorage[1]=100;
    auto* dye=test::put<ecs::ColorPaletteItem>(20);dye->paletteItemId.value=8;
    auto* gem=test::put<ecs::WeaponGemSlot>(20);gem->weaponGem.id.value=9;
    const auto enriched=test::data;
    const auto boots=scan(test::world);assert(boots.size()==1);
    assert(boots[0].present==31 && boots[0].durability==99 && boots[0].dye==8 && boots[0].gem==9);
    inspect_command(test::world,0,nullptr,nullptr);assert(test::data==enriched);
    test::kind=EMF_WORLD_CLIENT;assert(scan(test::world).empty());
    std::puts("PASS metadata: no game memory writes, exact component sizes, native field presence, unchanged item/perks/gem/dye/durability, client exclusion");
}
