#include "ui/save_screen.hpp"

#include "ui/file_browser.hpp"
#include "ui/layer.hpp"
#include "ui/widgets.hpp"

#include "gpu/vulkan_renderer.hpp"
#include "install/user_data.hpp"
#include "platform/utf8_path.hpp"
#include "settings/settings.hpp"
#include "save_data/save_sync.hpp"
#include "save_data/save_transfer.hpp"
#include "ui/ui.hpp"
#include "pokee_version.hpp"
#if defined(MHFU_ANDROID_APP)
#include "platform/android_documents.hpp"
#include "platform/android_jni.hpp"
#endif

#include "imgui.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace mhfu::ui {
namespace {

namespace fs = std::filesystem;
namespace sd = mhfu::savedata;

enum class Stage {
    Closed,
    ChooseImport,
    Review,
    Imported,
    ChooseExport,
    Exported,
    Backup,
    ChooseBackup,
    ConfirmBackup,
    BackedUp,
    Reminder,
    Sync,
    ChooseSyncFolder,
};

// What the sync screen knows about the two copies. Refreshed when the screen
// opens or after an action, not every frame: hashing and the Android copy
// are not free.
struct SyncView {
    fs::path local;
    fs::path remote;
    std::string local_hash;
    std::string remote_hash;
    sd::FolderSummary local_summary;
    sd::FolderSummary remote_summary;
    sd::sync::Decision decision{sd::sync::Decision::NoLocal};
    std::string error;
    std::string note;
    bool downloaded{}; // the last action brought the folder's save here: restart to load it
    bool ready{};
};

struct Found {
    sd::SaveCheck check;
    sd::FolderSummary incoming;
    sd::FolderSummary current;
};

struct Imported {
    std::string name;
    sd::ImportResult result;
};

struct State {
    Stage stage{Stage::Closed};
    std::unique_ptr<FileBrowser> browser;
    fs::path last_folder; // where the browser opens next time
    fs::path picked;
    std::vector<Found> found;
    std::size_t other_games{};
    std::vector<Imported> imported;
    fs::path backup_dir;
    sd::ExportResult exported;
    fs::path backup_target; // the folder chosen for a backup
    std::vector<std::string> backup_conflicts;
    sd::BackupResult backed_up;
    bool review_backed_up{};     // a backup was made from the import's review
    bool reminding{};            // the screens run as the backup reminder, before the game
    std::string reminder_reason; // why the reminder shows; empty: a new release
    bool focus{};                // focus the first row of a new stage
    bool restart{};              // the player chose to restart
    bool focus_row{};            // focus the Import row when the screen closes
    SyncView sync_view;
    bool sync_refresh{true}; // compare the two saves again
    bool sync_conflict{};    // both changed: show the two choices
};

State &state() {
    static State s;
    return s;
}

float px(float value) {
    return std::round(value * Layer::get().scale());
}

fs::path savedata_root() {
    return sd::memory_stick() / "PSP" / "SAVEDATA";
}

std::string utf8(const fs::path &path) {
    return install::path_to_utf8(path);
}

// The default place for backups, beside the rest of Pokee's data.
fs::path backups_directory() {
    try {
        return install::user_data_directory() / "save-backups";
    } catch (const std::exception &) {
        return sd::memory_stick().parent_path() / "save-backups";
    }
}

// The Flatpak reads the player's folders but writes only to its own data
// directory and to Downloads, so an export or a backup elsewhere fails there.
void sandbox_note() {
    if (std::getenv("FLATPAK_ID") == nullptr) return;
    ImGui::Indent(px(16.0f));
    paragraph("The Flatpak can read your folders but write only to its own data folder and to Downloads. Pick "
              "Downloads or the backups folder; Open the backups folder shows where that is.",
        colors::kTextDim);
    ImGui::Unindent(px(16.0f));
}

std::string names_text(const std::vector<std::string> &names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i)
        text += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + sd::save_label(names[i]);
    return text;
}

std::string describe(const sd::FolderSummary &summary) {
    if (!summary.exists) return "None";
    return "Saved " + sd::timestamp_for_display(summary.modified) + ", " + human_size(summary.bytes);
}

void go(Stage stage) {
    State &s = state();
    s.stage = stage;
    s.focus = true;
    ImGui::SetScrollY(0.0f);
}

void close() {
    State &s = state();
    s.stage = Stage::Closed;
    s.browser.reset();
    s.found.clear();
    s.focus_row = !s.reminding;
}

#if defined(MHFU_ANDROID_APP)
fs::path transfer_folder(const char *name) {
    return sd::memory_stick().parent_path() / "transfer" / name;
}
#endif

void open_browser(Stage stage);

std::string sync_folder_name() {
    const std::string &folder = settings::current().save_sync_folder;
    if (folder.empty()) return "";
#if defined(MHFU_ANDROID_APP)
    return android::describe_tree_uri(folder);
#else
    return utf8(fs::path(folder));
#endif
}

// The two saves compared: this device's game save, and the copy in the sync
// folder. On Android the folder is a document tree, so it is copied into a
// local staging folder first.
void refresh_sync() {
    State &s = state();
    const std::string note = s.sync_view.note; // an action's result survives the refresh
    const bool downloaded = s.sync_view.downloaded;
    SyncView view;
    view.local = sd::sync::game_save_folder(sd::memory_stick());
    if (!view.local.empty()) {
        view.local_hash = sd::sync::hash_folder(view.local);
        view.local_summary = sd::summarize_folder(view.local);
    }
    const std::string folder = settings::current().save_sync_folder;
    if (!folder.empty()) {
#if defined(MHFU_ANDROID_APP)
        const fs::path staging = transfer_folder("sync");
        std::error_code ec;
        fs::remove_all(staging, ec);
        fs::create_directories(staging, ec);
        if (!android::read_sync_folder(folder, staging, view.error)) {
            view.error = "Cannot read the sync folder: " + view.error + ".";
        } else {
            view.remote = staging;
        }
#else
        view.remote = fs::path(folder);
        std::error_code ec;
        fs::create_directories(view.remote, ec);
#endif
        if (view.remote.empty() || view.error.empty()) {
            view.remote_hash = sd::sync::hash_folder(view.remote);
            if (!view.remote_hash.empty()) view.remote_summary = sd::summarize_folder(view.remote);
        }
    }
    view.decision = sd::sync::decide(view.local_hash, view.remote_hash, settings::current().save_sync_hash);
    view.note = note;
    view.downloaded = downloaded;
    view.ready = true;
    s.sync_view = std::move(view);
}

bool upload_sync() {
    State &s = state();
    SyncView &view = s.sync_view;
    settings::Settings &settings = settings::current();
    if (view.local.empty()) {
        view.error = "This device has no save to upload yet.";
        return false;
    }
    const auto now = std::chrono::system_clock::now();
    // The folder's earlier save is kept before it is replaced.
    if (!view.remote.empty() && !view.remote_hash.empty()) {
        std::error_code ec;
        const fs::path backup = backups_directory() / ("sync-" + sd::timestamp_for_path(now));
        std::string error;
        if (sd::sync::copy_folder(view.remote, backup, error)) {
            std::cout << "[saves] the sync folder's earlier save is in " << utf8(backup) << "\n";
        } else {
            view.error = "The save was not uploaded: the folder's earlier save could not be kept (" + error + ").";
            return false;
        }
    }
    std::string error;
#if defined(MHFU_ANDROID_APP)
    const bool ok = android::write_sync_folder(view.local, settings.save_sync_folder, error);
#else
    const bool ok = sd::sync::copy_folder(view.local, view.remote, error);
#endif
    if (!ok) {
        view.error = "The save was not uploaded: " + error + ".";
        return false;
    }
    settings.save_sync_hash = sd::sync::hash_folder(view.local);
    settings.save_sync_time = sd::timestamp_for_display(now);
    settings::save();
    view.error.clear();
    view.note = "Uploaded this device's save.";
    view.downloaded = false;
    s.sync_conflict = false;
    s.sync_refresh = true;
    std::cout << "[saves] sync uploaded " << utf8(view.local) << "\n";
    return true;
}

bool download_sync() {
    State &s = state();
    SyncView &view = s.sync_view;
    settings::Settings &settings = settings::current();
    if (view.remote.empty() || view.remote_hash.empty()) {
        view.error = "The sync folder has no save to download yet.";
        return false;
    }
    const auto now = std::chrono::system_clock::now();
    const sd::SaveCheck check = sd::check_save_folder(view.remote, sd::game_key());
    if (!check.ok()) {
        view.error = "The folder's save cannot be read" + (check.problem.empty() ? "." : ": " + check.problem + ".");
        return false;
    }
    const sd::ImportResult result =
        sd::import_save(check, sd::memory_stick(), sd::backup_directory(savedata_root(), now));
    if (!result.ok) {
        view.error = "The save was not downloaded: " + result.error + ".";
        return false;
    }
    settings.save_sync_hash = sd::sync::hash_folder(sd::sync::game_save_folder(sd::memory_stick()));
    settings.save_sync_time = sd::timestamp_for_display(now);
    settings::save();
    view.error.clear();
    view.note = result.backup.empty() ? "Downloaded the folder's save."
                                      : "Downloaded the folder's save; the earlier one is backed up.";
    view.downloaded = true;
    s.sync_conflict = false;
    s.sync_refresh = true;
    std::cout << "[saves] sync downloaded " << utf8(view.remote) << "\n";
    return true;
}

void run_sync() {
    State &s = state();
    switch (s.sync_view.decision) {
    case sd::sync::Decision::Upload:
    case sd::sync::Decision::NoRemote:
        (void)upload_sync();
        break;
    case sd::sync::Decision::Download:
        (void)download_sync();
        break;
    case sd::sync::Decision::NoLocal:
        if (!s.sync_view.remote_hash.empty())
            (void)download_sync();
        else
            s.sync_view.note = "There is no save on this device yet.";
        break;
    case sd::sync::Decision::InSync:
        s.sync_view.note = "Already in sync.";
        break;
    case sd::sync::Decision::FirstSync:
    case sd::sync::Decision::Conflict:
        s.sync_conflict = true;
        break;
    }
}

// A backup of every save of this game into a new folder named by the time,
// which never replaces anything: in the backups folder, or on Android in a
// folder picked in the system's picker. The result is in s.backed_up. False
// when the player closed the picker without choosing.
bool back_up_now() {
    State &s = state();
    const auto now = std::chrono::system_clock::now();
    std::error_code ec;
#if defined(MHFU_ANDROID_APP)
    const fs::path local = transfer_folder("backup");
    fs::remove_all(local, ec);
    fs::create_directories(local, ec);
    s.backup_target = sd::backup_folder(local, now);
    s.backed_up = sd::back_up_saves(sd::memory_stick(), s.backup_target, false);
    if (s.backed_up.ok) {
        const std::optional<android::PickedExport> copied = android::pick_folder_and_copy(s.backed_up.folder);
        fs::remove_all(local, ec);
        if (!copied) return false;
        std::cout << "[saves] copied to " << copied->where << ": " << (copied->error.empty() ? "done" : copied->error)
                  << std::endl;
        s.backed_up.folder = fs::path(copied->where) / s.backed_up.folder.filename();
        s.backup_target = s.backed_up.folder;
        if (!copied->error.empty()) {
            s.backed_up.ok = false;
            s.backed_up.error = copied->error;
        }
        return true;
    }
    fs::remove_all(local, ec);
#else
    fs::create_directories(backups_directory(), ec);
    s.backup_target = sd::backup_folder(backups_directory(), now);
    s.backed_up = sd::back_up_saves(sd::memory_stick(), s.backup_target, false);
#endif
    std::cout << "[saves] back up to " << utf8(s.backup_target) << ": " << (s.backed_up.ok ? "done" : s.backed_up.error)
              << std::endl;
    return true;
}

#if defined(MHFU_ANDROID_APP)
void review(const fs::path &picked);

// Android: folders come from the system's document picker, as content://
// trees, so imports are checked in a local copy and exports and backups are
// made locally, then copied into the picked folder. The copies live beside
// the memory stick and are removed afterwards.
void open_picker(Stage stage) {
    State &s = state();
    std::error_code ec;
    if (stage == Stage::ChooseImport) {
        const std::optional<android::PickedImport> picked = android::pick_saves_to_import(transfer_folder("import"));
        if (!picked) {
            close();
            return;
        }
        if (!picked->error.empty()) std::cout << "[saves] " << picked->error << "\n";
        review(picked->staged);
        return;
    }
    if (stage == Stage::ChooseBackup) {
        if (back_up_now())
            go(Stage::BackedUp);
        else
            go(Stage::Backup);
        return;
    }
    if (stage == Stage::ChooseSyncFolder) {
        const std::optional<std::string> tree = android::pick_folder();
        if (tree) {
            settings::Settings &settings = settings::current();
            settings.save_sync_folder = *tree;
            settings.save_sync_hash.clear();
            settings.save_sync_time.clear();
            settings::save();
            state().sync_refresh = true;
        }
        go(Stage::Sync);
        return;
    }
    const fs::path local = transfer_folder("export");
    fs::remove_all(local, ec);
    fs::create_directories(local, ec);
    s.exported = sd::export_saves(sd::memory_stick(), local, std::chrono::system_clock::now());
    const fs::path made = s.exported.folder;
    if (!s.exported.ok) {
        go(Stage::Exported);
        return;
    }
    const std::optional<android::PickedExport> copied = android::pick_folder_and_copy(made);
    fs::remove_all(local, ec);
    if (!copied) {
        close();
        return;
    }
    std::cout << "[saves] copied to " << copied->where << ": " << (copied->error.empty() ? "done" : copied->error)
              << std::endl;
    s.exported.folder = fs::path(copied->where) / made.filename();
    if (!copied->error.empty()) {
        s.exported.ok = false;
        s.exported.error = copied->error;
    }
    go(Stage::Exported);
}
#endif

void open_browser(Stage stage) {
#if defined(MHFU_ANDROID_APP)
    open_picker(stage);
#else
    State &s = state();
    FileBrowser::Options options;
    options.extensions = {};
    options.filter_name = "folders";
    options.listed_name = "folders";
    options.empty_note = "No folders here.";
    if (stage == Stage::ChooseImport) {
        options.choose_folder = "Import from this folder";
        // A save folder is chosen as soon as it is opened.
        options.choose_on_open = [](const fs::path &folder) {
            std::error_code ec;
            return fs::is_regular_file(folder / "PARAM.SFO", ec);
        };
    } else if (stage == Stage::ChooseExport) {
        options.choose_folder = "Export to this folder";
    } else if (stage == Stage::ChooseSyncFolder) {
        options.choose_folder = "Use this folder for save sync";
    } else {
        options.choose_folder = "Back up to this folder";
    }
    s.browser =
        std::make_unique<FileBrowser>(s.last_folder.empty() ? FileBrowser::home() : s.last_folder, std::move(options));
    go(stage);
#endif
}

void focus_first() {
    State &s = state();
    if (s.focus) {
        focus_next_row();
        s.focus = false;
    }
}

void review(const fs::path &picked) {
    State &s = state();
    s.picked = picked;
    s.found.clear();
    s.other_games = 0;
    s.review_backed_up = false;
    const auto key = sd::game_key();
    for (sd::SaveCheck &check : sd::find_saves(picked, key)) {
        // Other games' saves in a SAVEDATA folder are only counted; one picked
        // on its own is shown, so the player sees why it is refused.
        std::error_code ec;
        if (check.other_game && !fs::equivalent(check.folder, picked, ec)) {
            ++s.other_games;
            continue;
        }
        Found found;
        found.incoming = sd::summarize_folder(check.folder);
        if (check.ok()) found.current = sd::summarize_folder(savedata_root() / check.name);
        found.check = std::move(check);
        s.found.push_back(std::move(found));
    }
    std::cout << "[saves] " << s.found.size() << " save folder(s) found in " << utf8(picked) << "\n";
    for (const Found &f : s.found)
        std::cout << "[saves]   " << f.check.name << ": " << (f.check.ok() ? "ok" : f.check.problem) << "\n";
    go(Stage::Review);
}

void run_import() {
    State &s = state();
    const auto now = std::chrono::system_clock::now();
    s.backup_dir = sd::backup_directory(savedata_root(), now);
    s.imported.clear();
    for (const Found &found : s.found) {
        if (!found.check.ok()) continue;
        Imported item{found.check.name, sd::import_save(found.check, sd::memory_stick(), s.backup_dir)};
        std::cout << "[saves] import " << item.name << " from " << utf8(found.check.folder) << ": "
                  << (item.result.ok ? "done" : item.result.error)
                  << (item.result.backup.empty() ? "" : "; the replaced save is in " + utf8(item.result.backup))
                  << std::endl;
        s.imported.push_back(std::move(item));
    }
    go(Stage::Imported);
}

void finish_backup(bool replace) {
    State &s = state();
    s.backed_up = sd::back_up_saves(sd::memory_stick(), s.backup_target, replace);
    std::cout << "[saves] back up to " << utf8(s.backup_target) << ": " << (s.backed_up.ok ? "done" : s.backed_up.error)
              << std::endl;
    go(Stage::BackedUp);
}

// A backup into `target`: a new folder named by the time, or, without the
// timestamp, the save folders straight into `target` after asking before
// replacing an earlier backup there.
void start_backup(const fs::path &target) {
    State &s = state();
    const bool timestamp = settings::current().backup_timestamp;
    s.backup_target =
        sd::backup_folder(target, timestamp ? std::optional(std::chrono::system_clock::now()) : std::nullopt);
    s.backup_conflicts = timestamp ? std::vector<std::string>{} : sd::backup_conflicts(sd::memory_stick(), target);
    if (!s.backup_conflicts.empty()) {
        go(Stage::ConfirmBackup);
        return;
    }
    finish_backup(false);
}

bool browse(bool back) {
    State &s = state();
    ImGui::Indent(px(16.0f));
    paragraph(s.stage == Stage::ChooseImport
            ? "Import: open a save folder (ULUS10391, ULUS10391QST), or choose a folder that holds them, such as a "
              "memory stick's PSP/SAVEDATA. You can also drop the folder on the window."
            : s.stage == Stage::ChooseExport ? "Export: choose the folder to copy your saves to."
            : s.stage == Stage::ChooseSyncFolder
            ? "Sync: choose the folder both devices can see. Use a folder only for this game's save; on a computer "
              "it is the folder rclone, Insync, Syncthing or Nextcloud syncs."
            : "Back up: choose the folder the backup goes to.",
        colors::kTextDim);
    ImGui::Unindent(px(16.0f));
    // A folder dropped on the window is chosen at once, as in the browser.
    std::optional<fs::path> dropped = Layer::get().take_dropped_file();
    if (dropped) {
        std::error_code ec;
        if (!fs::is_directory(*dropped, ec)) dropped = dropped->parent_path();
    }
    const FileBrowser::Result result = dropped ? FileBrowser::Result::Chosen : s.browser->frame(back);
    if (result == FileBrowser::Result::Browsing) return true;
    s.last_folder = s.browser->folder();
    const fs::path chosen = dropped ? *dropped : s.browser->chosen();
    const Stage stage = s.stage;
    s.browser.reset();
    if (result == FileBrowser::Result::Cancelled) {
        // Back from a folder browser returns to the choices it came from.
        if (stage == Stage::ChooseBackup) {
            go(Stage::Backup);
            return true;
        }
        if (stage == Stage::ChooseSyncFolder) {
            go(Stage::Sync);
            return true;
        }
        close();
        return false;
    }
    if (stage == Stage::ChooseImport) {
        review(chosen);
    } else if (stage == Stage::ChooseBackup) {
        start_backup(chosen);
    } else if (stage == Stage::ChooseSyncFolder) {
        settings::Settings &settings = settings::current();
        settings.save_sync_folder = utf8(chosen);
        settings.save_sync_hash.clear();
        settings.save_sync_time.clear();
        settings::save();
        state().sync_refresh = true;
        go(Stage::Sync);
    } else {
        s.exported = sd::export_saves(sd::memory_stick(), chosen, std::chrono::system_clock::now());
        std::cout << "[saves] export to " << utf8(chosen) << ": "
                  << (s.exported.ok ? "done, " + utf8(s.exported.folder) : s.exported.error) << std::endl;
        go(Stage::Exported);
    }
    return true;
}

void review_screen(bool back) {
    State &s = state();
    if (back) {
        open_browser(Stage::ChooseImport);
        return;
    }
    std::size_t importable = 0, replacing = 0;
    for (const Found &f : s.found) {
        if (!f.check.ok()) continue;
        ++importable;
        if (f.current.exists) ++replacing;
    }
    section("Import saves");
    ImGui::Indent(px(16.0f));
#if defined(MHFU_ANDROID_APP)
    // The picked folder, not the local copy it was checked in.
    paragraph("From " + utf8(s.picked.filename()), colors::kTextDim);
#else
    paragraph("From " + utf8(s.picked), colors::kTextDim);
#endif
    if (s.found.empty())
        paragraph("No saves of Monster Hunter Freedom Unite were found in this folder. Choose a save folder such as "
                  "ULUS10391, or the PSP/SAVEDATA folder that holds it.",
            colors::kDanger);
    if (s.other_games > 0)
        paragraph(std::to_string(s.other_games) + (s.other_games == 1 ? " save belongs" : " saves belong") +
                " to other games and " + (s.other_games == 1 ? "is" : "are") + " left out.",
            colors::kTextDim);
    ImGui::Unindent(px(16.0f));

    for (std::size_t i = 0; i < s.found.size(); ++i) {
        const Found &f = s.found[i];
        ImGui::PushID(static_cast<int>(i));
        section((sd::save_label(f.check.name) + " (" + f.check.name + ")").c_str());
        if (i == 0) focus_first();
        info_row("Save to import", describe(f.incoming));
        if (f.check.ok()) {
            info_row("Current save", describe(f.current));
        } else {
            ImGui::Indent(px(16.0f));
            paragraph("Cannot be imported: " + f.check.problem, colors::kDanger);
            ImGui::Unindent(px(16.0f));
        }
        ImGui::PopID();
    }

    ImGui::Dummy({0.0f, px(12.0f)});
    if (s.found.empty()) focus_first();
    // Before anything is written: the reminder to back up, and the button.
    const std::vector<std::string> current = sd::saves_to_back_up(sd::memory_stick());
    if (importable > 0 && !current.empty()) {
        ImGui::Indent(px(16.0f));
        if (!s.review_backed_up)
            paragraph("Back up your saves before you import. A save the import replaces is kept, but a backup of "
                      "your own is the sure way back if anything goes wrong.",
                colors::kAccent);
        else if (s.backed_up.ok)
            paragraph(
                "Backed up " + names_text(s.backed_up.saved) + " to " + utf8(s.backed_up.folder) + ".", colors::kGood);
        else
            paragraph("Not backed up: " + s.backed_up.error, colors::kDanger);
        ImGui::Unindent(px(16.0f));
        // One label throughout, so the row keeps its focus after a backup.
        if (button_row("Back up now",
                {false, {},
#if defined(MHFU_ANDROID_APP)
                    "Copies " + names_text(current) +
                        " to a new folder, named by the date and time, in a "
                        "folder you pick. Nothing is moved or deleted."
#else
                    "Copies " + names_text(current) + " to a new folder in " + utf8(backups_directory()) +
                        ", named by the date and time. Nothing is moved or deleted."
#endif
                }))
            s.review_backed_up = back_up_now() || s.review_backed_up;
#if !defined(MHFU_ANDROID_APP)
        if (s.review_backed_up && s.backed_up.ok &&
            button_row("Open the backup", {false, {}, "Show " + utf8(s.backed_up.folder) + "."}))
            open_folder(s.backed_up.folder);
#endif
    }
    if (importable > 0) {
        const std::string label = replacing > 0 ? "Replace and import"
            : importable == 1                   ? "Import this save"
                                                : "Import these saves";
        std::string description = "Copies the save" + std::string(importable == 1 ? "" : "s") + " into Pokee.";
        if (replacing > 0)
            description += " The save" + std::string(replacing == 1 ? " it replaces is" : "s they replace are") +
                " not deleted: " + (replacing == 1 ? "it moves" : "they move") +
                " to the saves folder's .backup folder.";
        if (button_row(label.c_str(), {false, {}, description}, replacing > 0 ? colors::kAccentBright : colors::kText))
            run_import();
    }
    if (button_row("Choose another folder", {false, {}, "Back to the folders."})) open_browser(Stage::ChooseImport);
    if (button_row("Cancel", {false, {}, "Import nothing."})) close();
}

void imported_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    bool any = false, backups = false;
    section("Import saves");
    for (std::size_t i = 0; i < s.imported.size(); ++i) {
        const Imported &item = s.imported[i];
        any = any || item.result.ok;
        backups = backups || !item.result.backup.empty();
        ImGui::PushID(static_cast<int>(i));
        if (i == 0) focus_first();
        info_row(sd::save_label(item.name).c_str(),
            item.result.ok ? (item.result.backup.empty() ? "Imported" : "Imported; the old save was kept")
                           : "Not imported: " + item.result.error);
        ImGui::PopID();
    }
    ImGui::Indent(px(16.0f));
    if (backups) paragraph("The replaced saves are in " + utf8(s.backup_dir) + ".", colors::kTextDim);
    if (any)
        paragraph("The game reads its saves at the title screen. Restart now to load the imported save; progress "
                  "since your last save is lost. If you keep playing instead, do not save before you restart: "
                  "saving would replace the imported save with the game you are playing.");
    ImGui::Unindent(px(16.0f));
    ImGui::Dummy({0.0f, px(12.0f)});
    if (any &&
        button_row("Restart now", {false, {}, "Closes the game and starts it again at the title screen."},
            colors::kAccentBright)) {
        s.restart = true;
        close();
    }
    if (button_row(any ? "Later" : "Done", {false, {}, "Back to the menu."})) close();
}

void exported_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    section("Export saves");
    focus_first();
    if (s.exported.ok) {
        std::string names;
        for (const std::string &name : s.exported.exported) names += (names.empty() ? "" : ", ") + sd::save_label(name);
        info_row("Exported", names);
        info_row("To", utf8(s.exported.folder));
        ImGui::Indent(px(16.0f));
        paragraph("The folder is laid out like a memory stick: copy its PSP folder to the root of a PSP's memory "
                  "stick, or import it from this menu on another machine.",
            colors::kTextDim);
        ImGui::Unindent(px(16.0f));
    } else {
        info_row("Not exported", s.exported.error);
        sandbox_note();
    }
    ImGui::Dummy({0.0f, px(12.0f)});
    if (button_row("Done", {false, {}, "Back to the menu."})) close();
}

void backup_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    settings::Settings &settings = settings::current();
    const std::vector<std::string> names = sd::saves_to_back_up(sd::memory_stick());
    section("Back up saves");
    ImGui::Indent(px(16.0f));
    paragraph(names.empty() ? "There is no save to back up yet."
                            : "Copies " + names_text(names) + " to a folder. To restore a backup, import it.",
        names.empty() ? colors::kDanger : colors::kTextDim);
    ImGui::Unindent(px(16.0f));
    focus_first();
    if (toggle_row("Add a timestamp to the backup name", settings.backup_timestamp,
            {false, {},
                "On: every backup is a new folder named by its date and time, such as 2026-09-19_19-05-12, "
                "holding the save folders. Off: the save folders go straight into the folder you choose, and "
                "an earlier backup there is replaced after asking."})) {
        settings.backup_timestamp = !settings.backup_timestamp;
        settings::save();
    }
#if !defined(MHFU_ANDROID_APP)
    // An Android app's own folders are out of the player's reach: there a
    // backup always goes to a folder picked in the system's picker.
    if (button_row("Back up to the backups folder", {names.empty(), {}, "Into " + utf8(backups_directory()) + "."})) {
        std::error_code ec;
        fs::create_directories(backups_directory(), ec);
        start_backup(backups_directory());
    }
#endif
    if (button_row("Back up to another folder…", {names.empty(), {}, "Choose where the backup goes."}))
        open_browser(Stage::ChooseBackup);
#if !defined(MHFU_ANDROID_APP)
    if (button_row("Open the backups folder", {false, {}, "Show " + utf8(backups_directory()) + "."}))
        open_folder(backups_directory());
#endif
    if (button_row("Cancel", {false, {}, "Back to the menu."})) close();
}

void confirm_backup_screen(bool back) {
    State &s = state();
    if (back) {
        go(Stage::Backup);
        return;
    }
    section("Replace the earlier backup?");
    ImGui::Indent(px(16.0f));
    paragraph(utf8(s.backup_target) + " already holds a backup of " + names_text(s.backup_conflicts) +
        ". Backing up again replaces it with your saves as they are now.");
    ImGui::Unindent(px(16.0f));
    focus_first();
    if (button_row("Cancel", {false, {}, "Keep the earlier backup."})) go(Stage::Backup);
    if (button_row("Replace the backup", {false, {}, "The earlier backup there is replaced."}, colors::kDanger))
        finish_backup(true);
}

void backed_up_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    section("Back up saves");
    focus_first();
    if (s.backed_up.ok) {
        info_row("Backed up", names_text(s.backed_up.saved));
        info_row("To", utf8(s.backed_up.folder));
        ImGui::Indent(px(16.0f));
        paragraph("Your saves stay where they are; this is a copy. To restore it, import it from System, Saves.",
            colors::kTextDim);
        ImGui::Unindent(px(16.0f));
    } else {
        info_row("Not backed up", s.backed_up.error);
        sandbox_note();
    }
    ImGui::Dummy({0.0f, px(12.0f)});
#if !defined(MHFU_ANDROID_APP)
    if (button_row("Open the folder", {false, {}, "Show the backup in the file manager."}))
        open_folder(s.backed_up.ok ? s.backed_up.folder : s.backup_target);
#endif
    if (s.reminding ? button_row("Continue", {false, {}, "Start the game."}, colors::kAccentBright)
                    : button_row("Done", {false, {}, "Back to the menu."}))
        close();
}

// Before the game starts: once for each new release, or when asked for
// before saves are written or converted (request_backup_reminder).
void reminder_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    const std::vector<std::string> names = sd::saves_to_back_up(sd::memory_stick());
    section("Back up your saves");
    ImGui::Indent(px(16.0f));
    paragraph(s.reminder_reason.empty() ? "This is the first start of Pokee " + sd::release_of(kPokeeVersion) +
                ". Before you play, make a copy of your saves: a new version, an import or a crash while "
                "saving could damage them, and a backup is the only way to get them back."
                                        : s.reminder_reason);
    ImGui::Unindent(px(16.0f));
    info_row("Saves", names.empty() ? std::string("None yet") : names_text(names));
    info_row("Saves folder", utf8(savedata_root()));
    ImGui::Dummy({0.0f, px(12.0f)});
    focus_first();
    if (button_row("Back up now",
            {names.empty(), {},
#if defined(MHFU_ANDROID_APP)
                "Copies your saves to a new folder, named by the date and time, in a folder you pick. Nothing "
                "is moved or deleted."
#else
                "Copies your saves to a new folder in " + utf8(backups_directory()) +
                    ", named by the date and time. Nothing is moved or deleted."
#endif
            },
            colors::kAccentBright)) {
        if (back_up_now()) go(Stage::BackedUp);
    }
    if (button_row("Continue without a backup",
            {false, {}, "Start the game. You can back up at any time in the menu: System, Back up saves."}))
        close();
    ImGui::Indent(px(16.0f));
    paragraph("This reminder shows once after each update. Turn it off in the menu: System, Saves.", colors::kTextDim);
    ImGui::Unindent(px(16.0f));
}

} // namespace

bool open_folder(const fs::path &folder) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    const std::string path = utf8(folder);
    if (SDL_OpenURL(folder_url(folder).c_str())) return true;
    std::cout << "[menu] cannot open " << path << ": " << SDL_GetError() << "\n";
    return false;
}

void sync_screen(bool back) {
    State &s = state();
    if (back) {
        close();
        return;
    }
    settings::Settings &settings = settings::current();
    section("Sync saves");
    ImGui::Indent(px(16.0f));
    paragraph("Keep one save on this device and another through a folder both can see: a Google Drive "
              "folder, Syncthing or Nextcloud. The folder holds only this game's save, and either copy can "
              "be restored.",
        colors::kTextDim);
    ImGui::Unindent(px(16.0f));

    if (settings.save_sync_folder.empty()) {
        ImGui::Indent(px(16.0f));
        paragraph("1. Choose a folder both devices can reach. On a phone, pick the Google Drive folder "
                  "through the system folder picker. On a computer, sync the same Drive folder with rclone "
                  "or Insync, or use Syncthing or Nextcloud.",
            colors::kTextDim);
        ImGui::Unindent(px(16.0f));
        if (button_row("Choose the sync folder…",
                {false, {},
                    "A folder used only for this game's save. Google Drive, Syncthing and Nextcloud all work; "
                    "the folder is picked, not typed."}))
            open_browser(Stage::ChooseSyncFolder);
        ImGui::Indent(px(16.0f));
        paragraph("2. On the other device, choose the same folder here.", colors::kTextDim);
        paragraph("3. Upload this device's save, then use Sync on the other device.", colors::kTextDim);
        ImGui::Unindent(px(16.0f));
        if (button_row("Back", {false, {}, "Back to the saves."})) close();
        return;
    }

    if (s.sync_refresh) {
        refresh_sync();
        s.sync_refresh = false;
    }
    const SyncView &view = s.sync_view;
    info_row("Sync folder", sync_folder_name().c_str());
    if (button_row(
            "Change the sync folder…", {false, {}, "Choose another folder. The next sync starts a new comparison."}))
        open_browser(Stage::ChooseSyncFolder);
    if (!settings.save_sync_time.empty()) info_row("Last sync", settings.save_sync_time.c_str());
    if (view.local_summary.exists) info_row("This device", describe(view.local_summary).c_str());
    if (view.remote_summary.exists) {
#if defined(MHFU_ANDROID_APP)
        // The staging copy was made just now, so its file time says nothing
        // about when the folder's save was written; its size does.
        info_row("The folder", human_size(view.remote_summary.bytes).c_str());
#else
        info_row("The folder", describe(view.remote_summary).c_str());
#endif
    }
    ImGui::Dummy({0.0f, px(4.0f)});
    ImGui::Indent(px(16.0f));
    const bool conflict = view.decision == sd::sync::Decision::Conflict ||
        view.decision == sd::sync::Decision::FirstSync || s.sync_conflict;
    paragraph(sd::sync::decision_text(view.decision), conflict ? colors::kDanger : colors::kTextDim);
    if (!view.note.empty()) paragraph(view.note, colors::kTextDim);
    if (!view.error.empty()) paragraph(view.error, colors::kDanger);
    if (view.downloaded)
        paragraph("The game reads its save at the title screen. Restart now to load the downloaded save; progress "
                  "since your last save is lost. If you keep playing instead, do not save before you restart.");
    ImGui::Unindent(px(16.0f));
    if (view.downloaded &&
        button_row("Restart now", {false, {}, "Closes the game and starts it again at the title screen."},
            colors::kAccentBright)) {
        s.restart = true;
        close();
    }
    if (s.sync_conflict) {
        ImGui::Indent(px(16.0f));
        paragraph("Both copies changed since the last sync, so pick the one to keep. The other is kept as a "
                  "backup.",
            colors::kDanger);
        ImGui::Unindent(px(16.0f));
        if (button_row("Keep this device (upload)", {false, {}, "Replace the folder's save with this device's."}))
            (void)upload_sync();
        if (button_row("Use the folder's save (download)",
                {false, {}, "Replace this device's save with the folder's; the earlier save is kept."}))
            (void)download_sync();
        if (button_row("Cancel", {false, {}, "Leave both copies as they are."})) s.sync_conflict = false;
    } else {
        if (button_row("Sync now", {false, {}, "Upload or download only what changed; ask before replacing both."}))
            run_sync();
        if (button_row("Upload this device's save", {false, {}, "Make the folder match this device."}))
            (void)upload_sync();
        if (button_row("Use the folder's save", {false, {}, "Make this device match the folder."}))
            (void)download_sync();
        if (button_row("Back", {false, {}, "Back to the saves."})) close();
    }
}

void save_rows() {
    State &s = state();
    const bool available = !sd::memory_stick().empty();
    if (s.focus_row) {
        focus_next_row();
        s.focus_row = false;
    }
#if defined(MHFU_ANDROID_APP)
    // Android's picker offers no storage's root and no Download folder itself.
    if (button_row("Import save…",
            {!available, {},
                "Copy a save from PPSSPP, a memory stick or an export: pick its PSP folder, or "
                "a folder holding it or the save folders. A save it replaces is kept."}))
        open_browser(Stage::ChooseImport);
    if (button_row("Export save…",
            {!available, {},
                "Copy your game data and downloaded quests to a folder you pick (Android does "
                "not offer Download itself: make or pick a folder in it)."}))
        open_browser(Stage::ChooseExport);
#else
    if (button_row("Import save…",
            {!available, {},
                "Copy a save from a PSP memory stick, PPSSPP or another installation: choose "
                "its folder (ULUS10391, ULUS10391QST) or the PSP/SAVEDATA folder that holds "
                "it. A save it replaces is kept, not deleted."}))
        open_browser(Stage::ChooseImport);
    if (button_row("Export save…",
            {!available, {},
                "Copy your game data and downloaded quests to a folder you choose, to take them "
                "to a PSP or another machine."}))
        open_browser(Stage::ChooseExport);
#endif
    if (button_row("Back up saves…",
            {!available, {},
                "Copy all of this game's saves, the install data included, to the backups "
                "folder or a folder you choose."}))
        go(Stage::Backup);
    settings::Settings &settings = settings::current();
    if (toggle_row("Remind me to back up after updates", settings.backup_reminder,
            {false, {},
                "On: the first time a new version of Pokee starts, it asks you to back up your saves, with a "
                "button that does it for you."})) {
        settings.backup_reminder = !settings.backup_reminder;
        settings::save();
    }
    section("Sync saves");
    ImGui::Indent(px(16.0f));
    paragraph(settings.save_sync_folder.empty()
            ? "Keep the same save on this device and another through a folder both can reach: Google Drive, "
              "Syncthing or Nextcloud."
            : "This device and another use the save in the sync folder. Sync compares them and asks before "
              "replacing a changed save.",
        colors::kTextDim);
    ImGui::Unindent(px(16.0f));
    if (button_row(settings.save_sync_folder.empty() ? "Set up save sync…" : "Sync saves…",
            {!available, {},
                "Choose a folder both devices can see, upload this device's save, and sync it with the other. "
                "The folder is only for this game's save."})) {
        state().sync_refresh = true;
        state().sync_conflict = false;
        state().sync_view = SyncView{};
        go(Stage::Sync);
    }
    if (!settings.save_sync_folder.empty()) info_row("Sync folder", sync_folder_name().c_str());
#if !defined(MHFU_ANDROID_APP)
    if (button_row("Open the saves folder",
            {!available, {}, "Show the folder the game saves to (PSP/SAVEDATA) in the file manager."}))
        open_folder(sd::memory_stick() / "PSP" / "SAVEDATA");
    if (button_row("Open the backups folder", {false, {}, "Show " + utf8(backups_directory()) + "."}))
        open_folder(backups_directory());
#endif
}

bool save_screen_open() {
    return state().stage != Stage::Closed;
}

bool save_screen(bool back) {
    State &s = state();
    switch (s.stage) {
    case Stage::Closed:
        return false;
    case Stage::ChooseImport:
    case Stage::ChooseExport:
    case Stage::ChooseBackup:
    case Stage::ChooseSyncFolder:
        return browse(back);
    case Stage::Backup:
        backup_screen(back);
        break;
    case Stage::ConfirmBackup:
        confirm_backup_screen(back);
        break;
    case Stage::BackedUp:
        backed_up_screen(back);
        break;
    case Stage::Review:
        review_screen(back);
        break;
    case Stage::Imported:
        imported_screen(back);
        break;
    case Stage::Exported:
        exported_screen(back);
        break;
    case Stage::Reminder:
        reminder_screen(back);
        break;
    case Stage::Sync:
        sync_screen(back);
        break;
    }
    return true;
}

bool take_restart_request() {
    return std::exchange(state().restart, false);
}

namespace {

enum class Reminder { Unchecked, Due, Requested, Done };

// Frames presented before the reminder shows: the first second of the game.
constexpr std::uint64_t kReminderAfterFrames = 30u;

Reminder &reminder() {
    static Reminder value = Reminder::Unchecked;
    return value;
}

// MHFU_BACKUP_REMINDER=0 never shows the reminder, =1 shows it at every
// start. Unset, it shows once for each new release, and never in a scripted
// run, whose input it would take.
Reminder check_reminder() {
    const char *variable = std::getenv("MHFU_BACKUP_REMINDER");
    if (variable != nullptr && *variable != '\0') return std::string(variable) == "0" ? Reminder::Done : Reminder::Due;
    for (const char *scripted : {"MHFU_INPUT_SCRIPT", "MHFU_INPUT_LIVE", "MHFU_AUTO_CONFIRM"})
        if (const char *value = std::getenv(scripted); value != nullptr && *value != '\0') return Reminder::Done;
    settings::Settings &settings = settings::current();
    const std::string release = sd::release_of(kPokeeVersion);
    if (!settings.backup_reminder || settings.backup_reminded == release) return Reminder::Done;
    if (sd::saves_to_back_up(sd::memory_stick()).empty()) {
        // Nothing to back up yet: the next release reminds.
        settings.backup_reminded = release;
        settings::save();
        return Reminder::Done;
    }
    return Reminder::Due;
}

} // namespace

void request_backup_reminder(const std::string &reason) {
    state().reminder_reason = reason;
    reminder() = Reminder::Requested;
}

bool backup_reminder_due() {
    Reminder &r = reminder();
    if (r == Reminder::Unchecked && !sd::memory_stick().empty()) r = check_reminder();
    if (r != Reminder::Due && r != Reminder::Requested) return false;
    // Over a picture of the game rather than the empty window of the first
    // frames.
    Layer &layer = Layer::get();
    return layer.attached() && layer.renderer().frames_presented() >= kReminderAfterFrames;
}

bool run_backup_reminder() {
    Layer &layer = Layer::get();
    State &s = state();
    const bool requested = reminder() == Reminder::Requested;
    reminder() = Reminder::Done;
    if (!requested) s.reminder_reason.clear();
    std::cout << "[saves] reminding to back up the saves"
              << (requested ? "" : " at the first start of " + sd::release_of(kPokeeVersion)) << std::endl;
    s.reminding = true;
    // Not go(): no ImGui window is open to scroll yet.
    s.stage = Stage::Reminder;
    s.focus = true;
    layer.renderer().set_game_input(false);
    layer.set_interactive(true);
    const bool window_open = layer.run(
        [&] {
            const bool back = layer.take_back() ||
                ImGui::IsKeyPressed(
                    layer.confirm_south() ? ImGuiKey_GamepadFaceRight : ImGuiKey_GamepadFaceDown, false);
            begin_panel("##backup", "Pokee", "Paused", true);
            begin_content();
            const bool open = save_screen(back);
            begin_footer();
            hints({{Control::Confirm, "Select"}, {Control::Back, "Continue"}});
            end_panel();
            return open;
        },
        true);
    layer.set_interactive(false);
    layer.renderer().set_game_input(true);
    s.reminding = false;
    s.focus_row = false;
    if (window_open && !requested) {
        settings::Settings &settings = settings::current();
        settings.backup_reminded = sd::release_of(kPokeeVersion);
        settings::save();
    }
    std::cout << "[saves] reminder closed" << (s.backed_up.ok ? "" : " without a backup") << std::endl;
    return window_open;
}

} // namespace mhfu::ui
