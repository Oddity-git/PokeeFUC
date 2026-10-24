#pragma once

// Armor model numbers from the USA Freedom Unite executable (ULUS-10391).
// Layered armor uses them to decide which model a chosen piece draws.
// The lookup that pointed an equipment mod at the worn piece, the file bases
// and the inner wear, was the Portable 3rd trace and is not here.
//
// Each armor part has a 40-byte record per id. The first two u16 are the
// model number for a male and for a female hunter. The byte at +4 says who
// can wear it (bit 0 male, bit 1 female, bit 2 blademaster, bit 3 gunner).
//
// Pure functions over a Ram, so the unit tests run them on a buffer.

#include "title.hpp"

#include <cstdint>
#include <optional>

namespace mhfu::game {

class Ram;

enum class Sex : std::uint8_t { Male = 0, Female = 1 };
[[nodiscard]] const char *sex_name(Sex sex);

inline constexpr std::uint32_t kArmorRecord = 40u;
// FUComplete keeps the armor tables 0x2C24 lower (its lookup, 0x088517A8).
inline constexpr std::uint32_t kChestData = kTitle == Title::FreedomUnite ? 0x08967794u : 0x08964B70u;
inline constexpr std::uint32_t kArmsData = kTitle == Title::FreedomUnite ? 0x0896B934u : 0x08968D10u;
inline constexpr std::uint32_t kWaistData = kTitle == Title::FreedomUnite ? 0x0896F96Cu : 0x0896CD48u;
inline constexpr std::uint32_t kLegsData = kTitle == Title::FreedomUnite ? 0x08973954u : 0x08970D30u;
inline constexpr std::uint32_t kHeadData = kTitle == Title::FreedomUnite ? 0x08963374u : 0x08960750u;

// The table of an armor part (kind 0 to 4), or 0 for other kinds.
[[nodiscard]] std::uint32_t armor_data(std::uint8_t kind);
// The model number of an armor piece for a hunter of this sex, as its record
// gives it; nothing when the record is not in memory.
[[nodiscard]] std::optional<std::uint16_t> armor_model(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex);
// Whether a hunter of this sex can wear the piece (its record's byte at +4).
[[nodiscard]] bool wearable_by(const Ram &ram, std::uint8_t kind, std::uint16_t id, Sex sex);

} // namespace mhfu::game
