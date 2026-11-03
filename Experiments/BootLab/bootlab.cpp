#define BOOTLAB_ID "speedmod_ledger"
#include "observer.h"
#include "ledger.h"
namespace bootlab {
void status(EmfWorld*,int,const char* const*,void*) {
    api->command_reply("LEDGER: read-only identity probe. No permanent identity provider has passed restart+transfer+duplicate tests. Automatic rolls and speed writes DISABLED.");
}
}
extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* candidate) {
    using namespace bootlab;
    if(!supported(candidate))return false;
    api=candidate;
    api->register_command("speedmod_ledger.status",": persistent identity readiness",status,nullptr);
    api->register_command("speedmod_ledger.capture","<label>: capture loaded boot observations",capture,nullptr);
    api->log(id,"Ledger prototype + read-only capture. No item mutation, no EML. Persistence waits for a verified identity provider.");
    return true;
}
