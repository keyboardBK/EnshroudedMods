#include "bootlab.cpp"
#include "test_support.h"
int main(){
    using namespace bootlab;
    test::observer_tests();
    test::setup();auto before=test::data;
    assert(emf_mod_load(api));status(test::world,0,nullptr,nullptr);
    assert(before==test::data && test::commands.size()==2);
    std::mt19937_64 rng(123);Ledger ledger;
    bool refused=false;
    try{ledger.get_or_roll({"world","pide-20",{}},45,4,rng);}catch(const std::invalid_argument&){refused=true;}
    assert(refused && ledger.size()==0);
    PersistentKey a{"world-A","test-fixture-unique-A",{true,true,true}};
    const int bonus=ledger.get_or_roll(a,45,4,rng).bonus;assert(bonus>=20&&bonus<=100);
    assert(ledger.get_or_roll(a,1,0,rng).bonus==bonus && ledger.size()==1);
    auto other=a;other.item="test-fixture-unique-B";ledger.get_or_roll(other,1,0,rng);assert(ledger.size()==2);
    auto other_world=a;other_world.world="world-B";ledger.get_or_roll(other_world,1,4,rng);assert(ledger.size()==3);
    auto text=ledger.serialize();Ledger after;after.load(text);
    assert(after.get_or_roll(a,30,2,rng).bonus==bonus && after.serialize()==text);
    for(const auto* bad:{"","BOOTLEDGER2\n","BOOTLEDGER1\nw i 45 4 101\n","BOOTLEDGER1\nw i 45 4 30\nw i 1 0 3\n","BOOTLEDGER1\nw i 45 4 30 extra\n"}) {
        refused=false;try{after.load(bad);}catch(const std::runtime_error&){refused=true;}
        assert(refused && after.serialize()==text);
    }
    const auto folder=std::filesystem::absolute("build")/("ledger-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directory(folder);
    const auto path=folder/"world.txt";ledger.save(path);ledger.save(path); // repeat replacement
    std::ifstream in(path,std::ios::binary);std::string disk((std::istreambuf_iterator<char>(in)),{});assert(disk==text);
    const auto denied=folder/"directory-not-a-file";std::filesystem::create_directory(denied);
    refused=false;try{ledger.save(denied);}catch(const std::runtime_error&){refused=true;}
    assert(refused && std::filesystem::is_directory(denied));
    std::puts("PASS ledger: stable-key gate, one roll, duplicate distinction, world namespace, restart serialization, corruption refusal, atomic disk replace, read-only runtime");
}
