#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "emf/emf_api.h"
#include "emf/generated/components.hpp"
#include "../../SpeedMod/runtime/boots_logic.h"

namespace bootlab {
namespace ecs = emf::gen::ecs;
inline const EmfApi* api = nullptr;
inline constexpr const char* id = BOOTLAB_ID;
inline const uint64_t process_run = GetTickCount64();
inline constexpr const char* game_sha = "0403981fd2e8ad18bd8c5698dff03c8d505b5a46677832c496db9c645513f2a6";
template<class T> const T* component(EmfWorld* world, uint32_t entity) {
    uint64_t size = 0;
    const auto* value = api->get_component(world, T::kHash, entity, &size);
    return value && size == sizeof(T) ? static_cast<const T*>(value) : nullptr;
}
inline bool supported(const EmfApi* candidate) {
    if (!candidate || candidate->version < 16 ||
        candidate->size < offsetof(EmfApi, program_write_other) + sizeof(candidate->program_write_other) ||
        !candidate->game_build || !candidate->get_component || !candidate->world_kind ||
        !candidate->server_session || !candidate->for_each_entity || !candidate->item_data ||
        !candidate->register_command || !candidate->log || !candidate->command_reply) return false;
    EmfGameBuild build{}; build.size = sizeof(build);
    return candidate->game_build(&build) && build.known && !build.server &&
        build.number == 1283531 && std::strcmp(build.sha256, game_sha) == 0;
}
inline bool gameplay_boot(EmfWorld* world, uint32_t item) {
    uint64_t size = 0;
    const auto* bytes = static_cast<const uint8_t*>(api->item_data(world, item, &size));
    if (!bytes || size != 2088 || bytes[260] != 17) return false;
    uint32_t flags = 0; std::memcpy(&flags, bytes + 1496, sizeof(flags));
    return (flags & (1u << 1)) && !(flags & (1u << 18));
}
struct Boot {
    uint32_t session = 0, inventory = 0, slot = 0, pide = 0, item = 0;
    int level = 0, rarity = -1, unlocked = 0;
    uint32_t present = 0, dye = 0, gem = 0;
    int32_t durability = 0, max_durability = 0;
};
inline bool inspect(EmfWorld* world, uint32_t inventory, uint32_t slot, Boot& output) {
    if (!world || api->world_kind(world) != EMF_WORLD_SERVER || slot >= 8) return false;
    const auto* inv = component<ecs::Inventory>(world, inventory);
    if (!inv) return false;
    const auto& stack = inv->slots[slot];
    if (stack.data.count != 1 || !stack.data.pide.id || !gameplay_boot(world, stack.id.value)) return false;
    const auto* state = component<ecs::ItemState>(world, stack.data.pide.id);
    const auto* used = component<ecs::UsedItem>(world, stack.data.pide.id);
    if (!state || !used || state->itemId.value != stack.id.value || used->itemId.value != stack.id.value ||
        state->containedInSlotId.entityId.id != inventory || state->containedInSlotId.slotIndex != slot) return false;
    Boot result{};
    result.session = api->server_session(); result.inventory = inventory; result.slot = slot;
    result.pide = stack.data.pide.id; result.item = stack.id.value;
    const auto* level = component<ecs::Level>(world, result.pide);
    result.level = level ? level->dataStorage[0] : state->itemLevelUi;
    const auto* perks = component<ecs::PerkContainerNew>(world, result.pide);
    result.rarity = speedmod::actual_rarity(perks ? perks->availablePerks : 0, static_cast<int>(state->itemRarityUi));
    result.unlocked = perks ? perks->unlockedPerks : 0;
    if (level) result.present |= 1;
    if (perks) result.present |= 2;
    if (const auto* d = component<ecs::Durability>(world, result.pide)) {
        result.present |= 4; result.durability = d->dataStorage[0]; result.max_durability = d->dataStorage[1];
    }
    if (const auto* d = component<ecs::ColorPaletteItem>(world, result.pide)) {
        result.present |= 8; result.dye = d->paletteItemId.value;
    }
    if (const auto* d = component<ecs::WeaponGemSlot>(world, result.pide)) {
        result.present |= 16; result.gem = d->weaponGem.id.value;
    }
    if (result.level < 1 || result.level > 255 || result.rarity < 0 || result.rarity > 4) return false;
    output = result; return true;
}
inline std::string fingerprint(const Boot& boot) {
    // An observation signature, NEVER a persistent item identity.
    std::ostringstream out;
    out << std::hex << boot.item << ':' << boot.present << ':' << boot.level << ':' << boot.rarity << ':'
        << boot.unlocked << ':' << boot.dye << ':' << boot.gem << ':' << boot.durability << ':' << boot.max_durability;
    return out.str();
}
inline std::filesystem::path module_dir() {
    HMODULE module = nullptr;
    wchar_t path[32768]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&module_dir), &module) || !GetModuleFileNameW(module, path, 32768))
        throw std::runtime_error("Cannot locate this experiment");
    return std::filesystem::path(path).parent_path();
}
inline bool label_valid(const std::string& label) {
    if (label.empty() || label.size() > 32) return false;
    for (const auto c : label) if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
        !(c >= '0' && c <= '9') && c != '-' && c != '_') return false;
    return true;
}
inline void visit_inventory(uint32_t entity, void* context) {
    auto* pair = static_cast<std::pair<EmfWorld*, std::vector<Boot>*>*>(context);
    for (uint32_t slot = 0; slot < 8; ++slot) {
        Boot boot;
        if (inspect(pair->first, entity, slot, boot)) pair->second->push_back(boot);
    }
}
inline std::vector<Boot> scan(EmfWorld* world) {
    std::vector<Boot> result;
    if (world && api->world_kind(world) == EMF_WORLD_SERVER) {
        std::pair<EmfWorld*, std::vector<Boot>*> context{world, &result};
        api->for_each_entity(world, visit_inventory, &context);
    }
    return result;
}
inline void capture(EmfWorld* world, int argc, const char* const* argv, void*) {
    if (!world || api->world_kind(world) != EMF_WORLD_SERVER) { api->command_reply("Host a world first."); return; }
    if (argc != 2 || !label_valid(argv[1])) { api->command_reply("Use capture <label>: 1-32 letters, digits, - or _."); return; }
    try {
        const auto observations = scan(world);
        const auto folder = module_dir() / "observations";
        std::filesystem::create_directories(folder);
        const auto path = folder / (std::string(argv[1]) + ".csv");
        // Never overwrite evidence from a different session.
        if (std::filesystem::exists(path)) throw std::runtime_error("Label already exists; use another label.");
        std::ofstream out(path, std::ios::binary);
        out << "process,run,session,inventory,slot,pide,item,level,rarity,present,unlocked,dye,gem,durability,max_durability,fingerprint\n";
        for (const auto& b : observations)
            out << GetCurrentProcessId() << ',' << process_run << ',' << b.session << ',' << b.inventory << ',' << b.slot << ',' << b.pide << ',' << b.item << ','
                << b.level << ',' << b.rarity << ',' << b.present << ',' << b.unlocked << ',' << b.dye << ','
                << b.gem << ',' << b.durability << ',' << b.max_durability << ',' << fingerprint(b) << '\n';
        out.flush();
        if (!out) throw std::runtime_error("Capture write failed");
        api->command_reply("Captured %zu boots to this mod's observations/%s.csv. Session IDs and fingerprints are NOT permanent keys.",
            observations.size(), argv[1]);
    } catch (const std::exception& e) { api->command_reply("Capture failed: %s", e.what()); }
}
}
