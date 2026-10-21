#include "fuc_config.hpp"

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mhfu::fuc_config {
namespace {

constexpr std::array<Choice, 2> kOnOff{{{"Off", 0x00u}, {"On", 0x01u}}};
constexpr std::array<Choice, 3> kRegions{{{"ULJM05500", 0x00u}, {"ULUS10391", 0x01u}, {"ULES01213", 0x02u}}};
// The field of view's high halfword, 0x3F5F to 0x3F8F, stored as 5F 3F.
constexpr std::array<Choice, 4> kFieldOfView{
    {{"Default", 0x5F3Fu}, {"Minor", 0x6F3Fu}, {"Moderate", 0x7F3Fu}, {"Extreme", 0x8F3Fu}}};
constexpr std::array<Choice, 5> kCameraHeights{
    {{"1 (lowest)", 0x00u}, {"2", 0x01u}, {"3 (middle)", 0x02u}, {"4 (default)", 0x03u}, {"5 (highest)", 0x04u}}};
constexpr std::array<Choice, 3> kMinimap{{{"Default", 0x00u}, {"75%", 0x01u}, {"50%", 0x02u}}};

const std::array<Option, 11> kOptions{{
    {"File Replacer", 0x10u, 1u, kOnOff,
        "On: FUComplete loads the replacement files in FUCDAT/NATIVEPSP, as its file replacer mods need. Off "
        "(default): the game's own files. FUComplete's menu can also turn it on and off in the game."},
    {"Save Region", 0x11u, 1u, kRegions,
        "Which game's save FUComplete loads. It stays as it is here: another region loads no character from "
        "your save. Change it with FUComplete's own tool if you move a save from another region.",
        true},
    {"True Raw/Ele/Status", 0x12u, 1u, kOnOff,
        "On: weapons show their true attack, element and status, without the weapon class's multiplier. Only "
        "the numbers shown change. Off (default): the game's numbers."},
    {"Dos Audio", 0x13u, 1u, kOnOff,
        "On: the jungle, desert, swamp, snowy mountains, volcano and tower battle themes are those of Monster "
        "Hunter Dos. Off (default): the game's own."},
    {"Field of View", 0x14u, 2u, kFieldOfView,
        "How wide the camera sees: Default 0.872, Minor 0.935, Moderate 0.997, Extreme 1.120."},
    {"Starting Camera Height", 0x16u, 1u, kCameraHeights,
        "Where the camera's height starts in a quest, from 1, looking at the ground, to 5, looking at the sky. 4 "
        "is the game's."},
    {"Minimap Size", 0x17u, 1u, kMinimap,
        "The minimap's size. At 75% and 50% the loading screen between areas is smaller too."},
    {"Hunting Horn Tweak", 0x18u, 1u, kOnOff,
        "On: hunting horn notes clear only when the horn is sheathed, as in Frontier; the left and right swings "
        "do more damage and stun (31 and 18, up from 27 and 15); and the hilt jab cuts. Off (default): the game's "
        "horn."},
    {"Early Kill Lao/Shen", 0x19u, 1u, kOnOff,
        "On: Lao-Shan Lung, Ash Lao-Shan Lung and Shen Gaoren in the Fortress can be killed before area 5. With "
        "unpatched players, a patched one has to enter the area first. Off (default): the game's thresholds."},
    {"Guild Hall Full Chest", 0x1Au, 1u, kOnOff,
        "On, offline only: the Guild Hall's item box can change equipment, through FUComplete. Pokee's Change "
        "Equipment in the Guild Hall does the same, online too, and steps aside while this is on."},
    {"Guild Hall Drink Buff", 0x1Bu, 1u, kOnOff,
        "On: seated at a table in the Guild Hall, ○ gives a drink: +10 health per Felyne chef working in your "
        "kitchen, up to +50, and +25 stamina for 1 to 4 chefs, +50 for 5. Off (default): nothing."},
}};

} // namespace

std::span<const Option> options() {
    return kOptions;
}

std::filesystem::path config_path(const std::filesystem::path &memory_stick) {
    return memory_stick / "PSP" / "SAVEDATA" / "FUCDAT" / "CONFIG.BIN";
}

std::optional<std::vector<std::uint8_t>> read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    for (const Option &option : kOptions)
        if (option.offset + option.bytes > bytes.size()) return std::nullopt;
    return bytes;
}

std::uint16_t value(const std::vector<std::uint8_t> &bytes, const Option &option) {
    if (option.offset + option.bytes > bytes.size()) return 0u;
    if (option.bytes == 1u) return bytes[option.offset];
    return static_cast<std::uint16_t>(bytes[option.offset] << 8u | bytes[option.offset + 1u]);
}

int choice(const std::vector<std::uint8_t> &bytes, const Option &option) {
    const std::uint16_t current = value(bytes, option);
    for (std::size_t i = 0; i < option.choices.size(); ++i)
        if (option.choices[i].value == current) return static_cast<int>(i);
    return -1;
}

void set(std::vector<std::uint8_t> &bytes, const Option &option, std::size_t index) {
    if (index >= option.choices.size() || option.offset + option.bytes > bytes.size()) return;
    const std::uint16_t chosen = option.choices[index].value;
    if (option.bytes == 1u) {
        bytes[option.offset] = static_cast<std::uint8_t>(chosen);
    } else {
        bytes[option.offset] = static_cast<std::uint8_t>(chosen >> 8u);
        bytes[option.offset + 1u] = static_cast<std::uint8_t>(chosen);
    }
}

bool write(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
    std::filesystem::path temporary = path;
    temporary += ".part";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) std::filesystem::remove(temporary, ec);
    return !ec;
}

} // namespace mhfu::fuc_config
