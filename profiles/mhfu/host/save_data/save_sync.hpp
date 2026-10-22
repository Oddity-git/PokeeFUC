#pragma once

// Keeping one save on two installations through a folder both can reach: a
// Google Drive (or Nextcloud, Syncthing, ...) folder on a phone and a synced
// folder on a computer. The folder holds the files of the game's save folder
// (PARAM.SFO and its data), so it can be imported like any save.
//
// Nothing here draws or knows a platform: a folder is a local path. The
// Android app stages the picked document tree into such a path and writes it
// back; the menu does the same with a chosen folder on a computer.

#include "save_data/save_transfer.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mhfu::savedata::sync {

// What the two copies say, given the hash each had at the last sync.
enum class Decision {
    NoLocal,   // no save on this device
    NoRemote,  // nothing in the folder yet
    InSync,    // both hold the same bytes
    Upload,    // only this device changed since the last sync
    Download,  // only the folder changed
    FirstSync, // no last sync: the two differ, and they cannot be compared
    Conflict,  // both changed since the last sync
};

[[nodiscard]] Decision decide(
    std::string_view local_hash, std::string_view remote_hash, std::string_view last_hash);

// The files the folder's save is compared by: every regular file at its top
// level, by name and bytes. Empty when the folder is missing or empty.
[[nodiscard]] std::string hash_folder(const std::filesystem::path &folder);

// Copies the regular files at the top level of `from` into `to`, creating
// `to` when needed and replacing files of the same name through a temporary
// file. Other files already in `to` are left alone.
bool copy_folder(const std::filesystem::path &from, const std::filesystem::path &to, std::string &error);

// Words for the player: "This device has a newer save", and so on.
[[nodiscard]] const char *decision_text(Decision decision);

// The save folder this game reads and writes on a memory stick: the first of
// its game-data folders (not the downloaded quests or install data) that
// holds a PARAM.SFO, in this install's save region. Empty when there is none.
[[nodiscard]] std::filesystem::path game_save_folder(const std::filesystem::path &memory_stick);

} // namespace mhfu::savedata::sync
