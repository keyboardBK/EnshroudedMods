#define BOOTLAB_ID "bootlab_abi_smoke"
#include "observer.h"
#include "test_support.h"
namespace smoke {
struct Command {std::string name;EmfCommandCallback fn;void* user;};
std::vector<Command> commands;
bool add(const char* name,const char*,EmfCommandCallback fn,void* user){commands.push_back({name,fn,user});return true;}
}
int main(int argc,char** argv){
    using namespace bootlab;
    assert(argc==2);test::setup();
    auto candidate=*api;candidate.register_command=smoke::add;
    HMODULE dll=LoadLibraryA(argv[1]);assert(dll);
    using Load=bool(*)(const EmfApi*);
    auto load=reinterpret_cast<Load>(GetProcAddress(dll,"emf_mod_load"));assert(load);
    auto invalid=candidate;invalid.size=8;assert(!load(&invalid));assert(smoke::commands.empty());
    invalid=candidate;invalid.version=1;assert(!load(&invalid));assert(smoke::commands.empty());
    const auto before=test::data;
    assert(load(&candidate));assert(!smoke::commands.empty());
    bool captured=false;
    for(const auto& command:smoke::commands){
        if(command.name.size()>=8 && command.name.substr(command.name.size()-8)==".capture"){
            const std::string label="smoke_"+std::to_string(GetTickCount64());
            const char* args[]={command.name.c_str(),label.c_str()};
            command.fn(test::world,2,args,command.user);
            assert(test::reply_text.find("Captured 1 boots")!=std::string::npos);captured=true;
            const char* bad[]={command.name.c_str(),"../escape"};
            command.fn(test::world,2,bad,command.user);
            assert(test::reply_text.find("Use capture")!=std::string::npos);
        }
    }
    assert(captured && before==test::data);
    FreeLibrary(dll);
    std::puts("PASS actual DLL ABI: load/export, version/size refusal, command registration, read-only native capture, traversal refusal");
}
