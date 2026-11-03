#define BOOTLAB_ID "speedmod_metadata"
#include "observer.h"
namespace bootlab {
void status(EmfWorld*,int,const char* const*,void*) {
    api->command_reply("METADATA: read-only. Inspects native level/perks/durability/dye/gem fields. No verified free saved field; no writes to padding, perks, durability, gems or save files.");
}
void inspect_command(EmfWorld* world,int,const char* const*,void*) {
    if(!world || api->world_kind(world)!=EMF_WORLD_SERVER) {api->command_reply("Host a world first.");return;}
    for(const auto& b:scan(world)) {
        api->command_reply("pide=%u item=%08x inv=%u slot=%u level=%d rarity=%d unlocked=%d presence=%x durability=%d/%d dye=%08x gem=%08x",
            b.pide,b.item,b.inventory,b.slot,b.level,b.rarity,b.unlocked,b.present,b.durability,b.max_durability,b.dye,b.gem);
    }
}
}
extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* candidate) {
    using namespace bootlab;
    if(!supported(candidate))return false;
    api=candidate;
    api->register_command("speedmod_metadata.status",": metadata investigation status",status,nullptr);
    api->register_command("speedmod_metadata.inspect",": inspect loaded shoe fields without mutation",inspect_command,nullptr);
    api->register_command("speedmod_metadata.capture","<label>: read-only field snapshot",capture,nullptr);
    api->log(id,"Read-only metadata experiment; no EML data, item IDs or save fields changed.");
    return true;
}
