#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"
#include "boots_logic.h"

namespace {
namespace ecs = emf::gen::ecs;
constexpr const char* kId = "speedmod_boots";
constexpr uint32_t kSupportedBuild = 1283531;
// Windows GUID memory order, matching the EML helper.
constexpr uint8_t kTemplates[5][16] = {
    {0xd0,0x29,0xb6,0x4f,0xbc,0x15,0xc1,0x5f,0xa1,0xe6,0xf9,0xbc,0xa8,0x73,0xbf,0x01},
    {0xd0,0x29,0xb6,0x4f,0xbc,0x15,0xc1,0x5f,0xa1,0xe6,0xf9,0xbc,0xa8,0x73,0xbf,0x02},
    {0xd0,0x29,0xb6,0x4f,0xbc,0x15,0xc1,0x5f,0xa1,0xe6,0xf9,0xbc,0xa8,0x73,0xbf,0x03},
    {0xd0,0x29,0xb6,0x4f,0xbc,0x15,0xc1,0x5f,0xa1,0xe6,0xf9,0xbc,0xa8,0x73,0xbf,0x04},
    {0xd0,0x29,0xb6,0x4f,0xbc,0x15,0xc1,0x5f,0xa1,0xe6,0xf9,0xbc,0xa8,0x73,0xbf,0x05},
};
const EmfApi* api = nullptr;
std::vector<speedmod::Effect> effects;
uint32_t session = 0;
int64_t next_poll = 0, next_discover = 0;
bool spawn_warning = false, joined_warning = false;

template<class T> T* component(EmfWorld* world, uint32_t entity) {
    uint64_t size = 0;
    void* p = api->get_component(world, T::kHash, entity, &size);
    return p && size == sizeof(T) ? static_cast<T*>(p) : nullptr;
}

bool gameplay_boot(EmfWorld* world, uint32_t item) {
    uint64_t size = 0;
    const auto* p = static_cast<const uint8_t*>(api->item_data(world, item, &size));
    // Offsets and IsVanityItem bit checked against local build reflection.
    if (!p || size != 2088 || p[260] != 17) return false;
    uint64_t flags = 0;
    std::memcpy(&flags, p + 1496, sizeof(flags));
    return (flags & (1ULL << 18)) == 0;
}

speedmod::Boot equipped_boot(EmfWorld* world, uint32_t player) {
    auto* slots = component<ecs::SlotSelection>(world, player);
    if (!slots) return {};
    const auto slot = slots->equipment.armourFeet;
    if (!slot.entityId.id || slot.entityId.id == UINT32_MAX) return {};
    const uint32_t index = slot.slotIndex;
    if (index >= 8) return {};
    auto* inv = component<ecs::Inventory>(world, slot.entityId.id);
    if (!inv) return {};
    const auto& stack = inv->slots[index];
    if (!stack.id.value || !stack.data.count || !stack.data.pide.id || !gameplay_boot(world, stack.id.value)) return {};
    auto* state = component<ecs::ItemState>(world, stack.data.pide.id);
    auto* perks = component<ecs::PerkContainerNew>(world, stack.data.pide.id);
    const int rarity = speedmod::actual_rarity(perks ? perks->availablePerks : 0,
                                               state ? static_cast<int>(state->itemRarityUi) : -1);
    return {stack.id.value, stack.data.pide.id, rarity};
}

void discover_player(uint32_t entity, void* opaque) {
    auto* world = static_cast<EmfWorld*>(opaque);
    if (!component<ecs::PlayerInput>(world, entity) || !component<ecs::SlotSelection>(world, entity)) return;
    auto it = std::find_if(effects.begin(), effects.end(), [entity](const auto& e) { return e.player == entity; });
    if (it == effects.end()) effects.push_back({entity});
}

bool valid_effect(EmfWorld* world, uint32_t entity, uint32_t player) {
    auto* hierarchy = component<ecs::ImpactHierarchy>(world, entity);
    return hierarchy && hierarchy->parentImpactId.id == player && component<ecs::LifeTime>(world, entity);
}

bool clear_effect(EmfWorld* world, speedmod::Effect& effect, int64_t now) {
    if (effect.entity) {
        if (valid_effect(world, effect.entity, effect.player)) {
            if (!api->destroy(world, effect.entity)) return false;
        } else if (now - effect.spawned_at < speedmod::kLeaseNs) {
            return false; // cancellation waits for a queued spawn, never overlaps it
        }
    }
    effect.entity = 0;
    effect.boot = {};
    return true;
}

void tick(EmfWorld* world, void*) {
    if (api->world_kind(world) != EMF_WORLD_SERVER) {
        if (!api->server_world() && !joined_warning) {
            api->log(kId, "Joined-client movement prediction is unverified. This alpha applies effects on the host only.");
            joined_warning = true;
        }
        return;
    }
    const uint32_t now_session = api->server_session();
    const int64_t now = api->ecs_time_ns(world);
    if (session != now_session || now < next_poll - speedmod::kPollNs) {
        effects.clear();
        session = now_session;
        next_poll = next_discover = 0;
        spawn_warning = false;
    }
    if (now < next_poll) return;
    next_poll = now + speedmod::kPollNs;
    if (now >= next_discover) {
        api->for_each_entity(world, discover_player, world);
        next_discover = now + speedmod::kDiscoverNs;
    }
    for (auto& effect : effects) {
        if (!component<ecs::PlayerInput>(world, effect.player)) {
            if (!clear_effect(world, effect, now)) continue;
            effect.player = 0;
            continue;
        }
        const auto boot = equipped_boot(world, effect.player);
        if (effect.boot != boot && !clear_effect(world, effect, now)) continue;
        if (!boot.valid()) continue;
        if (effect.entity) {
            if (valid_effect(world, effect.entity, effect.player)) {
                auto* life = component<ecs::LifeTime>(world, effect.entity);
                if (!life->triggeredDestroy) {
                    life->lifeTime.value = speedmod::kLeaseNs;
                    life->endOfLife.value = now + speedmod::kLeaseNs;
                    continue;
                }
            } else if (now - effect.spawned_at < speedmod::kLeaseNs) {
                continue; // a spawn may be queued; never spawn duplicates
            }
            if (!clear_effect(world, effect, now)) continue;
        }
        ecs::ImpactHierarchy hierarchy{};
        hierarchy.parentImpactId.id = effect.player;
        hierarchy.rootImpactId.id = effect.player;
        hierarchy.weaponPideId.id = boot.pide;
        hierarchy.permissionCheckId.id = effect.player;
        ecs::LifeTime life{};
        life.lifeTime.value = speedmod::kLeaseNs;
        life.endOfLife.value = now + speedmod::kLeaseNs;
        life.enforceDestroy = true;
        const EmfComponentInit initializers[] = {
            {ecs::ImpactHierarchy::kHash, sizeof(hierarchy), &hierarchy},
            {ecs::LifeTime::kHash, sizeof(life), &life},
        };
        const uint32_t spawned = api->spawn_ex(world, kTemplates[boot.rarity], 0, 0, 0, initializers, 2);
        if (spawned) {
            effect.entity = spawned;
            effect.boot = boot;
            effect.spawned_at = now;
            api->log(kId, "Player %u: item %08x, instance %u, rarity %d, effect %u", effect.player, boot.item, boot.pide, boot.rarity, spawned);
        } else if (!spawn_warning) {
            api->log(kId, "Cannot spawn bonus effect. Ensure the SpeedMod EML patch ran before loading the world.");
            spawn_warning = true;
        }
    }
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const auto& e) { return e.player == 0; }), effects.end());
}

void status(EmfWorld*, int, const char* const*, void*) {
    api->command_reply("SpeedMod alpha: native effects from actual worn rarity. Live movement and multiplayer are unverified.");
    for (const auto& effect : effects)
        api->command_reply("Player %u: rarity %d, instance %u, effect %u", effect.player, effect.boot.rarity, effect.boot.pide, effect.entity);
}
}

extern "C" __declspec(dllexport) bool emf_mod_load(const EmfApi* incoming) {
    if (!incoming || incoming->size < offsetof(EmfApi, game_build) + sizeof(incoming->game_build) ||
        !incoming->game_build || !incoming->spawn_ex || !incoming->item_data || !incoming->server_session) return false;
    EmfGameBuild build{};
    build.size = sizeof(build);
    if (!incoming->game_build(&build) || !build.known || build.number != kSupportedBuild) {
        incoming->log(kId, "Unsupported build. Reflected against 1283531; refusing to touch ECS data.");
        return false;
    }
    api = incoming;
    api->on_tick(tick, nullptr);
    api->register_command("speedmod_boots.status", ": show equipped-rarity effects", status, nullptr);
    api->log(kId, "Loaded alpha for build 1283531. Requires SpeedMod EML resources. No inventory or save edits.");
    return true;
}
