// FUComplete's CONFIG.BIN as the menu reads and writes it (fuc_config.hpp): the
// options' places and values, a field of view of two bytes, files too short
// or missing, and a write that keeps every other byte. No game data: the file
// is made here.
#include "fuc_config.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

namespace fc = mhfu::fuc_config;

const fc::Option *find(std::string_view label) {
    for (const fc::Option &option : fc::options())
        if (label == option.label) return &option;
    return nullptr;
}

// The file as FUComplete 1.4.0 ships it: its version, then every option off,
// Save Region ULUS10391, the default field of view and camera height 4.
std::vector<std::uint8_t> shipped() {
    std::vector<std::uint8_t> bytes(fc::kFileBytes, 0u);
    const std::string_view version = "1.4.0 ENG";
    for (std::size_t i = 0; i < version.size(); ++i) bytes[i] = static_cast<std::uint8_t>(version[i]);
    bytes[0x11] = 0x01u;
    bytes[0x14] = 0x5Fu;
    bytes[0x15] = 0x3Fu;
    bytes[0x16] = 0x03u;
    return bytes;
}

void test_options() {
    check(fc::options().size() == 11u, "eleven options");
    std::uint32_t previous = 0u;
    for (const fc::Option &option : fc::options()) {
        check(option.offset >= 0x10u && option.offset + option.bytes <= 0x1Cu, "each option is in the option bytes");
        check(option.offset >= previous, "in the file's order, none overlapping");
        previous = option.offset + option.bytes;
        check(!option.choices.empty() && option.description != nullptr, "each has choices and a description");
    }
    const fc::Option *region = find("Save Region");
    check(region != nullptr && region->locked, "Save Region is shown but locked");
    for (const fc::Option &option : fc::options())
        check(!option.locked || &option == region, "and it is the only locked one");
}

void test_values() {
    const std::vector<std::uint8_t> bytes = shipped();
    const fc::Option *view = find("Field of View");
    const fc::Option *height = find("Starting Camera Height");
    const fc::Option *drink = find("Guild Hall Drink Buff");
    const fc::Option *region = find("Save Region");
    check(view != nullptr && height != nullptr && drink != nullptr && region != nullptr, "the options are found");
    check(fc::value(bytes, *view) == 0x5F3Fu && fc::choice(bytes, *view) == 0, "the default field of view");
    check(fc::choice(bytes, *height) == 3, "camera height 4");
    check(fc::choice(bytes, *drink) == 0, "the drink buff off");
    check(std::string_view(region->choices[static_cast<std::size_t>(fc::choice(bytes, *region))].label) == "ULUS10391",
        "Save Region ULUS10391");

    std::vector<std::uint8_t> changed = bytes;
    fc::set(changed, *view, 3u);
    check(changed[0x14] == 0x8Fu && changed[0x15] == 0x3Fu && fc::choice(changed, *view) == 3,
        "Extreme writes both bytes of the field of view, high byte first");
    fc::set(changed, *drink, 1u);
    check(changed[0x1B] == 0x01u, "the drink buff on");
    for (std::size_t i = 0; i < changed.size(); ++i)
        if (i != 0x14u && i != 0x15u && i != 0x1Bu) check(changed[i] == bytes[i], "no other byte changes");
    fc::set(changed, *drink, 9u);
    check(changed[0x1B] == 0x01u, "a choice out of range changes nothing");
    changed[0x17] = 0x7Fu;
    check(fc::choice(changed, *find("Minimap Size")) == -1, "a value no choice has reads as none of them");
}

void test_files() {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pokee_fuc_config_tests";
    std::filesystem::remove_all(dir);
    const std::filesystem::path path = fc::config_path(dir / "ms0");
    check(path == dir / "ms0" / "PSP" / "SAVEDATA" / "FUCDAT" / "CONFIG.BIN", "the file is in FUCDAT");
    check(!fc::read(path).has_value(), "a missing file reads as none");
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream out(path, std::ios::binary);
        out << "1.4.0 ENG";
    }
    check(!fc::read(path).has_value(), "a file too short for the options reads as none");
    std::vector<std::uint8_t> bytes = shipped();
    check(fc::write(path, bytes), "the file is written");
    check(!std::filesystem::exists(path.string() + ".part"), "through a temporary file that is gone");
    const std::optional<std::vector<std::uint8_t>> back = fc::read(path);
    check(back.has_value() && *back == bytes, "and reads back as written");
    std::filesystem::remove_all(dir);
}

} // namespace

int main() {
    test_options();
    test_values();
    test_files();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "fuc config: ok\n";
    return 0;
}
