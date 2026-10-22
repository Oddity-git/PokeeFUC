// Save sync: the hash of a save folder, copying it into a sync folder, the
// choice between two copies, and finding this game's save folder. No game
// data: the folders are made here.
#include "save_data/param_sfo.hpp"
#include "save_data/save_sync.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace mhfu::savedata::sync;

int failures{};

void check(bool condition, const char *message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// PARAM.SFO naming one data file, as the game writes it.
std::string param_sfo() {
    mhfu::savedata::ParamSfo sfo;
    const std::string list = "MHP2NDG.BIN";
    std::vector<std::uint8_t> names(list.begin(), list.end());
    names.push_back(0u);
    names.push_back(0u);
    sfo.set_binary("SAVEDATA_FILE_LIST", std::move(names), 32u);
    const std::vector<std::uint8_t> bytes = sfo.serialize();
    return std::string(bytes.begin(), bytes.end());
}

void test_decision() {
    check(decide("", "b", "") == Decision::NoLocal, "no save here");
    check(decide("a", "", "") == Decision::NoRemote, "nothing in the folder");
    check(decide("a", "a", "") == Decision::InSync, "the same save is in sync without a last hash");
    check(decide("a", "b", "") == Decision::FirstSync, "two different saves and no last sync");
    check(decide("b", "a", "a") == Decision::Upload, "only this device changed");
    check(decide("a", "b", "a") == Decision::Download, "only the folder changed");
    check(decide("b", "c", "a") == Decision::Conflict, "both changed");
    check(decide("a", "a", "b") == Decision::InSync, "equal beats a stale last hash");
}

void test_hash_and_copy(const fs::path &root) {
    const fs::path local = root / "local";
    const fs::path remote = root / "remote";
    std::error_code ec;
    fs::remove_all(root / "local", ec);
    fs::remove_all(root / "remote", ec);
    check(hash_folder(local).empty(), "a missing folder has no hash");
    fs::create_directories(local);
    check(hash_folder(local).empty(), "an empty folder has no hash");
    write(local / "PARAM.SFO", param_sfo());
    write(local / "MHP2NDG.BIN", "save one");
    const std::string first = hash_folder(local);
    check(first.size() == 64u, "the hash is a SHA-256");
    check(hash_folder(local) == first, "the same files hash the same");
    write(local / "MHP2NDG.BIN", "save two");
    check(hash_folder(local) != first, "a changed file changes the hash");

    std::string error;
    check(copy_folder(local, remote, error), "the save copies into the sync folder");
    check(hash_folder(remote) == hash_folder(local), "and hashes the same");
    check(fs::is_regular_file(remote / "PARAM.SFO"), "with its files");
    write(remote / "other.txt", "left alone");
    write(local / "MHP2NDG.BIN", "save three");
    check(copy_folder(local, remote, error), "a second copy replaces the save");
    check(hash_folder(remote) == hash_folder(local), "an extra file in the folder is not part of the save");
    check(fs::exists(remote / "other.txt"), "the copy leaves other files alone");
    check(!copy_folder(root / "missing", remote, error) && !error.empty(), "a missing source is refused");
}

void test_game_save_folder(const fs::path &root) {
    const fs::path stick = root / "stick";
    std::error_code ec;
    fs::remove_all(stick, ec);
    check(game_save_folder(stick).empty(), "a stick with no save has no game save folder");
    write(stick / "PSP" / "SAVEDATA" / "ULUS10391QST" / "PARAM.SFO", "sfo");
    write(stick / "PSP" / "SAVEDATA" / "ULUS10391DAT" / "PARAM.SFO", "sfo");
    write(stick / "PSP" / "SAVEDATA" / "ULUS10391" / "PARAM.SFO", "sfo");
    check(game_save_folder(stick) == stick / "PSP" / "SAVEDATA" / "ULUS10391", "the game data folder, not the quests");
    fs::remove(stick / "PSP" / "SAVEDATA" / "ULUS10391" / "PARAM.SFO", ec);
    check(game_save_folder(stick).empty(), "a folder without PARAM.SFO is not a save");
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "pokee_save_sync_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    test_decision();
    test_hash_and_copy(root);
    test_game_save_folder(root);
    fs::remove_all(root, ec);
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "save sync tests passed\n";
    return 0;
}
