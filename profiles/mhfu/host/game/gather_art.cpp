#include "game/gather_art.hpp"

#include "title.hpp"

#include <iostream>
#include <iterator>

namespace mhfu::game {
namespace {

// FUComplete has the same code at its own addresses, and its tables lower in
// memory; the build's title picks the table (title.hpp).
constexpr bool kFreedomUniteArt = kTitle == Title::FreedomUnite;

constexpr GatherArtWord kWordsFreedomUnite[] = {
    {0x08869750u, 0x3C0208A6u}, // lui v0,0x08A6: the point lookup
    {0x08869754u, 0x8C840050u}, // lw a0,80(a0): the gather object's base
    {0x08869758u, 0x8C42C584u}, // lw v0,-14972(v0): the offset at 0x08A5C584
    {0x0886975Cu, 0x00051880u}, // sll v1,a1,2: four bytes a point
    {0x0882E10Cu, 0x3C03089Au}, // lui v1,0x089A: the item records
    {0x0882E110u, 0x2463D2BDu}, // addiu v1,v1,-11587: the icon byte at 0x0899D2BD
    {0x0882E168u, 0x3C02089Au}, // lui v0,0x089A
    {0x0882E16Cu, 0x2442D2BCu}, // addiu v0,v0,-11588: the records at 0x0899D2BC
    {0x0882E1A4u, 0x2484AB7Cu}, // addiu a0,a0,-21636: the tints at 0x0893AB7C
    {0x088303C8u, 0x3C030894u}, // lui v1,0x0894: the icon's sprite
    {0x088303D0u, 0x2463ABECu}, // addiu v1,v1,-21524: the sprites at 0x0893ABEC
    {0x0882BE94u, 0x3C020894u}, // lui v0,0x0894: the sprite rectangles
    {0x0882BEA0u, 0x244292C8u}, // addiu v0,v0,-27960: at 0x089392C8
};
constexpr GatherArtWord kWordsFUComplete[] = {
    {0x08869748u, 0x3C0208A5u}, // lui v0,0x08A5: the point lookup
    {0x0886974Cu, 0x8C840050u}, // lw a0,80(a0): the gather object's base
    {0x08869750u, 0x8C427534u}, // lw v0,30004(v0): the offset at 0x08A57534
    {0x08869754u, 0x00051880u}, // sll v1,a1,2: four bytes a point
    {0x0882E350u, 0x3C03089Au}, // lui v1,0x089A: the item records
    {0x0882E354u, 0x2463A23Du}, // addiu v1,v1,-24003: the icon byte at 0x0899A23D
    {0x0882E3ACu, 0x3C02089Au}, // lui v0,0x089A
    {0x0882E3B0u, 0x2442A23Cu}, // addiu v0,v0,-24004: the records at 0x0899A23C
    {0x0882E3E8u, 0x24847FA4u}, // addiu a0,a0,32676: the tints at 0x08937FA4
    {0x0883060Cu, 0x3C030894u}, // lui v1,0x0894: the icon's sprite
    {0x08830614u, 0x24638014u}, // addiu v1,v1,-32748: the sprites at 0x08938014
    {0x0882C0D8u, 0x3C020893u}, // lui v0,0x0893: the sprite rectangles
    {0x0882C0E4u, 0x244266F0u}, // addiu v0,v0,26352: at 0x089366F0
};
static_assert(std::size(kWordsFreedomUnite) == std::size(kWordsFUComplete));
constexpr const auto &kWords = kFreedomUniteArt ? kWordsFreedomUnite : kWordsFUComplete;

constexpr std::uint32_t kGatherObject = kFreedomUniteArt ? 0x08A62D9Cu : 0x08A5DD4Cu;
constexpr std::uint32_t kGatherBase = 80u;
constexpr std::uint32_t kPointOffset = kFreedomUniteArt ? 0x08A5C584u : 0x08A57534u;
constexpr std::uint16_t kListEnd = 0xFFFFu;
constexpr std::uint32_t kMaxPairs = 64u;

constexpr std::uint32_t kItemRecords = kFreedomUniteArt ? 0x0899D2BCu : 0x0899A23Cu;
constexpr std::uint32_t kItemStride = 24u;
constexpr std::uint32_t kItems = 1500u; // 0x0885C83C turns away ids past 1500
constexpr std::uint8_t kNoIcon = 255u;
constexpr std::uint32_t kIconSprites = kFreedomUniteArt ? 0x0893ABECu : 0x08938014u;
constexpr std::uint32_t kIcons = 56u; // the large size's table; the small one follows
constexpr std::uint32_t kSprites = kFreedomUniteArt ? 0x089392C8u : 0x089366F0u;
constexpr std::uint32_t kSpriteStride = 12u;
constexpr std::uint32_t kTints = kFreedomUniteArt ? 0x0893AB7Cu : 0x08937FA4u;
constexpr std::uint8_t kTintCount = 11u;
constexpr std::uint32_t kAtlasSide = 256u;
constexpr std::uint32_t kRam = 0x08800000u;

bool readable(const psprecomp::GuestMemory &memory, std::uint32_t address, std::uint32_t bytes) {
    return address >= kRam && memory.raw_pointer(address, bytes) != nullptr;
}

bool &code_logged() {
    static bool logged = false;
    return logged;
}

} // namespace

std::span<const GatherArtWord> gather_art_words() {
    return kWords;
}

bool gather_art_code_matches(const psprecomp::GuestMemory &memory) {
    for (const GatherArtWord &word : kWords) {
        if (memory.raw_pointer(word.address, 4u) == nullptr) return false;
        const std::uint32_t found = memory.load32(word.address);
        if (found == word.instruction) continue;
        if (!code_logged()) {
            code_logged() = true;
            std::cerr << "[gather] item art code differs at 0x" << std::hex << word.address << ": 0x" << found
                      << ", expected 0x" << word.instruction << std::dec << std::endl;
        }
        return false;
    }
    return true;
}

std::optional<std::vector<GatherDrop>> gather_drops(const psprecomp::GuestMemory &memory, std::uint16_t id) {
    if (!readable(memory, kGatherObject, 4u) || !readable(memory, kPointOffset, 4u)) return std::nullopt;
    const std::uint32_t object = memory.load32(kGatherObject);
    if (!readable(memory, object + kGatherBase, 4u)) return std::nullopt;
    const std::uint32_t base = memory.load32(object + kGatherBase);
    const std::uint32_t slot = base + memory.load32(kPointOffset) + static_cast<std::uint32_t>(id) * 4u;
    if (!readable(memory, slot, 4u)) return std::nullopt;
    std::uint32_t pair = base + memory.load32(slot);
    std::vector<GatherDrop> drops;
    for (std::uint32_t n = 0; n < kMaxPairs; ++n, pair += 4u) {
        if (!readable(memory, pair, 4u)) return std::nullopt;
        const std::uint16_t weight = memory.load16(pair);
        if (weight == kListEnd) return drops;
        drops.push_back({weight, memory.load16(pair + 2u)});
    }
    return std::nullopt;
}

std::optional<std::uint16_t> gather_item(const psprecomp::GuestMemory &memory, std::uint16_t id) {
    const std::optional<std::vector<GatherDrop>> drops = gather_drops(memory, id);
    if (!drops) return std::nullopt;
    std::optional<std::uint16_t> best;
    std::uint16_t heaviest = 0u;
    for (const GatherDrop &drop : *drops) {
        if (drop.weight <= heaviest) continue;
        heaviest = drop.weight;
        best = drop.item;
    }
    return best;
}

std::optional<std::array<std::uint8_t, 2>> item_icon_and_tint(
    const psprecomp::GuestMemory &memory, std::uint16_t item) {
    if (item >= kItems) return std::nullopt;
    const std::uint32_t record = kItemRecords + static_cast<std::uint32_t>(item) * kItemStride;
    if (!readable(memory, record, 3u)) return std::nullopt;
    return std::array<std::uint8_t, 2>{memory.load8(record + 1u), memory.load8(record + 2u)};
}

GatherArt gather_art_for(const psprecomp::GuestMemory &memory, std::uint16_t kind, std::uint16_t id) {
    if (kind == 3u) return GatherArt::of_item(kPickaxeItem);
    if (kind == 4u) return GatherArt::of_item(kBugNetItem);
    const std::optional<std::uint16_t> item = gather_item(memory, id);
    if (!item) return {};
    // Other points list the grey husk as filler, so only a point that mostly
    // gives a husk or a bone is a bone pile.
    if (const std::optional<std::array<std::uint8_t, 2>> look = item_icon_and_tint(memory, *item))
        if ((*look)[0] == kHuskIcon || (*look)[0] == kBoneIcon) return GatherArt::of_item(kBoneItem);
    return GatherArt::of_item(*item);
}

std::optional<ItemArt> item_art(const psprecomp::GuestMemory &memory, std::uint16_t item) {
    if (item >= kItems) return std::nullopt;
    const std::uint32_t record = kItemRecords + static_cast<std::uint32_t>(item) * kItemStride;
    if (!readable(memory, record, 3u)) return std::nullopt;
    return icon_art(memory, memory.load8(record + 1u), memory.load8(record + 2u));
}

std::optional<ItemArt> icon_art(const psprecomp::GuestMemory &memory, std::uint8_t icon, std::uint8_t tint) {
    if (icon == kNoIcon || icon >= kIcons || tint >= kTintCount) return std::nullopt;
    const std::uint32_t entry = kIconSprites + static_cast<std::uint32_t>(icon) * 2u;
    if (!readable(memory, entry, 2u)) return std::nullopt;
    const std::uint32_t sprite = kSprites + static_cast<std::uint32_t>(memory.load16(entry)) * kSpriteStride;
    const std::uint32_t colour = kTints + static_cast<std::uint32_t>(tint) * 4u;
    if (!readable(memory, sprite, kSpriteStride) || !readable(memory, colour, 3u)) return std::nullopt;
    ItemArt art;
    art.u = memory.load16(sprite);
    art.v = memory.load16(sprite + 2u);
    art.width = memory.load16(sprite + 4u);
    art.height = memory.load16(sprite + 6u);
    if (art.width == 0u || art.height == 0u || art.u + art.width > kAtlasSide || art.v + art.height > kAtlasSide)
        return std::nullopt;
    art.red = memory.load8(colour);
    art.green = memory.load8(colour + 1u);
    art.blue = memory.load8(colour + 2u);
    return art;
}

} // namespace mhfu::game
