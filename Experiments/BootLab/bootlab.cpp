#define BOOTLAB_ID "speedmod_formula"
#include "observer.h"
#include "formula.h"
namespace bootlab {
struct Request { uint32_t session=0,player=0,inventory=0,slot=0,pide=0,item=0; int percent=0; bool cleanup=false; };
inline std::mutex gate;
inline Request pending;
inline Contribution contribution;
inline bool apply_enabled=false;
inline uint32_t epoch=0;
inline uint32_t owner=0;
inline int latest_percent=0;
inline std::vector<uint32_t> players;
void find_player(uint32_t e,void* opaque) {
    auto* world=static_cast<EmfWorld*>(opaque);
    if(component<ecs::PlayerInput>(world,e) && component<ecs::SlotSelection>(world,e)) players.push_back(e);
}
bool old_runtime() {
    if(!api->mod_info)return false;
    EmfModInfo info{};info.size=sizeof(info);
    return api->mod_info("speedmod_boots",&info) && (info.status==EMF_MOD_LOADED || info.status==EMF_MOD_PENDING);
}
void queue_cleanup(uint32_t current) {
    apply_enabled=false;
    if(contribution.attached && owner)pending={current,owner,0,0,0,0,0,true};
}
void tick(EmfWorld* world,void*) {
    if(api->world_kind(world)!=EMF_WORLD_SERVER)return;
    std::lock_guard<std::mutex> lock(gate);
    const auto current=api->server_session();
    if(current!=epoch) {epoch=current;pending={};contribution={};owner=0;apply_enabled=false;}
    players.clear();api->for_each_entity(world,find_player,world);
    pending={};latest_percent=0;
    // Only an isolated, locally hosted single-player test; never a joined client or guest.
    if(players.size()!=1 || players[0]!=api->local_player() || old_runtime()) {queue_cleanup(current);return;}
    const auto player=players[0];
    const auto* selection=component<ecs::SlotSelection>(world,player);
    const auto slot=selection->equipment.armourFeet;
    Boot boot;
    const bool worn=inspect(world,slot.entityId.id,slot.slotIndex,boot);
    if(worn) {
        EmfItemInfo info{};info.size=sizeof(info);
        if(!api->item_info || !api->item_info(world,boot.item,&info) || std::strncmp(info.name,"SM02_",5)==0) {
            queue_cleanup(current);return; // Do not stack with saved variants.
        }
        latest_percent=formula_percent(boot.level,boot.rarity);
    }
    uint64_t size=0;
    const auto* base=static_cast<const ecs::Movement*>(api->get_template_component(world,ecs::Movement::kHash,player,&size));
    if(!base || size!=sizeof(*base) || std::fabs(base->dataStorage[5]-8.0f)>0.00001f) {
        queue_cleanup(current);return;
    }
    if(!apply_enabled || !worn) {queue_cleanup(current);return;}
    pending={current,player,worn?boot.inventory:0,worn?boot.slot:0,worn?boot.pide:0,worn?boot.item:0,
        apply_enabled?latest_percent:0};
}
template<class T> const T* read(void* ctx,const char* name,uint32_t entity) {
    return static_cast<const T*>(api->program_read_other(ctx,name,entity,sizeof(T)));
}
void update(void* ctx,void*) {
    std::lock_guard<std::mutex> lock(gate);
    const auto r=pending;
    if(!r.player || r.session!=api->server_session() || (!contribution.attached && r.percent==0))return;
    const auto* slots=read<ecs::SlotSelection>(ctx,"SlotSelection",r.player);
    if(!slots)return;
    const auto slot=slots->equipment.armourFeet;
    if(r.cleanup) {
        if(r.player!=owner)return;
    } else if(r.pide) {
        if(slot.entityId.id!=r.inventory || slot.slotIndex!=r.slot || r.slot>=8)return;
        const auto* inv=read<ecs::Inventory>(ctx,"Inventory",r.inventory);
        if(!inv || inv->slots[r.slot].id.value!=r.item || inv->slots[r.slot].data.pide.id!=r.pide)return;
        const auto* state=read<ecs::ItemState>(ctx,"ItemState",r.pide);
        const auto* used=read<ecs::UsedItem>(ctx,"UsedItem",r.pide);
        const auto* level=read<ecs::Level>(ctx,"Level",r.pide);
        const auto* perks=read<ecs::PerkContainerNew>(ctx,"PerkContainerNew",r.pide);
        const int actual_level=level?level->dataStorage[0]:(state?state->itemLevelUi:0);
        const int actual_rarity=state?speedmod::actual_rarity(perks?perks->availablePerks:0,static_cast<int>(state->itemRarityUi)):-1;
        if(!state || !used || used->itemId.value!=r.item || actual_level<1 || actual_level>255 || actual_rarity<0 || actual_rarity>4 || state->itemId.value!=r.item ||
           state->containedInSlotId.entityId.id!=r.inventory || state->containedInSlotId.slotIndex!=r.slot ||
           formula_percent(actual_level,actual_rarity)!=latest_percent)return;
    } else if(slot.entityId.id) {
        const auto* inv=read<ecs::Inventory>(ctx,"Inventory",slot.entityId.id);
        if(!inv || slot.slotIndex>=8 || inv->slots[slot.slotIndex].id.value)return;
    }
    const auto* movement=read<ecs::Movement>(ctx,"Movement",r.player);
    if(!movement || movement->rootId.value!=2517049208u || movement->storageOffset!=0 || movement->storageCount!=18)return;
    float next=0;const float before=movement->dataStorage[5];
    const int wanted=apply_enabled?r.percent:0;
    const bool already_conflicted=contribution.conflict;
    if(!contribution.plan(before,wanted,next)) {
        if(contribution.conflict && !already_conflicted) {
            apply_enabled=false;
            api->log(id,"Movement changed outside this experiment. Paused without overwriting it. Reload the TEST character before another apply.");
        }
        return;
    }
    if(std::fabs(next-before)<0.000001f)return;
    auto* writable=static_cast<ecs::Movement*>(api->program_write_other(ctx,"Movement",r.player,sizeof(ecs::Movement)));
    if(!writable || std::fabs(writable->dataStorage[5]-before)>0.000001f)return;
    writable->dataStorage[5]=next;
    contribution.committed(before,next,wanted);
    owner=contribution.attached?r.player:0;
}
void status(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);
    api->command_reply("FORMULA: preview +%d%%, apply=%d, delta=%.3f, conflict=%d. Native tooltip and multiplayer NOT implemented.",
        latest_percent,apply_enabled,contribution.delta,contribution.conflict);
}
void apply(EmfWorld* world,int argc,const char* const* argv,void*) {
    if(!world || api->world_kind(world)!=EMF_WORLD_SERVER)return;
    std::lock_guard<std::mutex> lock(gate);
    if(argc!=2 || std::strcmp(argv[1],"test-character")!=0 || epoch!=api->server_session() ||
       players.size()!=1 || players[0]!=api->local_player() || old_runtime() || contribution.conflict) {
        api->command_reply("Refused. Use apply test-character ONLY on an isolated test character after a preview tick. Conflicts require reload.");return;
    }
    apply_enabled=true;api->command_reply("Experimental local Movement write enabled. Run disable before saving/exiting. Live save behaviour is UNVERIFIED.");
}
void disable(EmfWorld*,int,const char* const*,void*) {
    std::lock_guard<std::mutex> lock(gate);
    apply_enabled=false;api->command_reply("Preview only. Known contribution will be removed at the next validated program callback; conflicts require a TEST character reload.");
}
}
extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* candidate) {
    using namespace bootlab;
    if(!supported(candidate) || !candidate->on_tick || !candidate->local_player || !candidate->get_template_component ||
       !candidate->item_info || !candidate->register_program_def || !candidate->program_read_other ||
       !candidate->program_write_other)return false;
    api=candidate;
    static const char* reads[]={"Movement","SlotSelection","Inventory","ItemState","UsedItem","Level","PerkContainerNew",nullptr};
    static const char* writes[]={"Movement",nullptr};
    EmfProgramDef def{};def.size=sizeof(def);def.name="speedmod_formula_test";def.world_kind=EMF_WORLD_SERVER;
    def.fn=update;def.other_read=reads;def.other_write=writes;
    if(!api->register_program_def(&def))return false;
    api->register_command("speedmod_formula.status",": preview and contribution state",status,nullptr);
    api->register_command("speedmod_formula.capture","<label>: read-only boot snapshot",capture,nullptr);
    api->register_command("speedmod_formula.apply","test-character: opt in to local Movement experiment",apply,nullptr);
    api->register_command("speedmod_formula.disable",": stop writes and undo known contribution",disable,nullptr);
    api->on_tick(tick,nullptr);api->log(id,"Formula experiment: PREVIEW ONLY by default. No EML or item/save edits.");
    return true;
}
