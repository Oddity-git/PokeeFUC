#pragma once

// Saves through Android's document picker (#17). A folder the player picks
// is a tree of content:// documents, not a path, so the save screen works on
// local copies: an import copies the save folders it can find in the picked
// folder into a staging folder and checks them there as on any platform; an
// export or a backup is written to a staging folder and then copied into the
// picked one. Only in the Android app (MHFU_ANDROID_APP).

#include <filesystem>
#include <optional>
#include <string>

namespace mhfu::android {

struct PickedImport {
    std::filesystem::path staged; // the local copy to look for saves in
    std::string error;            // empty: the copy worked
};
// Asks for a folder, then copies the save folders (those holding a PARAM.SFO)
// found in it into <staging>/<its name>/SAVEDATA, where find_saves() looks:
// the folder itself, its SAVEDATA and PSP folders and two levels of other
// folders, so a memory stick's root, PPSSPP's PSP folder or an export all
// work. `staging` is emptied first. Nothing when the player cancels.
[[nodiscard]] std::optional<PickedImport> pick_saves_to_import(const std::filesystem::path &staging);

struct PickedExport {
    std::string where; // the picked folder, for the player
    std::string error; // empty: the copy worked
};
// Asks for a folder and copies `local` into it as a folder of the same name,
// with everything in it. Nothing when the player cancels.
[[nodiscard]] std::optional<PickedExport> pick_folder_and_copy(const std::filesystem::path &local);

// A readable name for a picked tree, for showing beside the sync setting.
[[nodiscard]] std::string describe_tree_uri(const std::string &tree_uri);

// Save sync: the picked tree holds the files of this game's save folder
// (PARAM.SFO and its data) at its top level. `read_sync_folder` copies them
// into `staging`, where a local save folder can be checked and hashed;
// `write_sync_folder` writes the files of `local` into the tree, replacing
// those of the same name and leaving other files alone.
bool read_sync_folder(const std::string &tree_uri, const std::filesystem::path &staging, std::string &error);
bool write_sync_folder(const std::filesystem::path &local, const std::string &tree_uri, std::string &error);

} // namespace mhfu::android
