#pragma once
#include <array>
#include <cstdint>
namespace speedmod {
constexpr int64_t kLeaseNs = 1000000000LL;
constexpr int64_t kPollNs = 100000000LL;
constexpr int64_t kDiscoverNs = 1000000000LL;
constexpr std::array<float, 5> kDefaultPercent{5, 10, 15, 20, 30};
// availablePerks is rarity + 1, NOT the number of purchased upgrades.
inline int actual_rarity(int available_perks, int ui_rarity) {
    if (available_perks >= 1 && available_perks <= 5) return available_perks - 1;
    if (ui_rarity >= 0 && ui_rarity < 5) return ui_rarity;
    return -1;
}
struct Boot {
    uint32_t item = 0;
    uint32_t pide = 0;
    int rarity = -1;
    bool operator==(const Boot& b) const { return item == b.item && pide == b.pide && rarity == b.rarity; }
    bool operator!=(const Boot& b) const { return !(*this == b); }
    bool valid() const { return item && pide && rarity >= 0 && rarity < 5; }
};
struct Effect {
    uint32_t player = 0;
    Boot boot{};
    uint32_t entity = 0;
    int64_t spawned_at = 0;
};
}
