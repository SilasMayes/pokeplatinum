// The game's pages of libntr's settings screen (simulator/sim_settings.hpp): games, saves,
// gameplay, the Gear, cheats and debugging. Touch- and pad-friendly (simulator/sim_ui.hpp).
#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/sim_carts.h>
#include <simulator/sim_companion.h>
#include <simulator/sim_files.h>
#include <simulator/sim_gui.hpp>
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "port/autosave.h"
#include "port/save_slots.h"
#include "port/sim_config_prj.h"
#include "port/soft_reset.h"

extern "C" {
extern const unsigned char AtkinsonHyperlegible_Regular_ttf[];
extern const unsigned int AtkinsonHyperlegible_Regular_ttf_size;
extern const unsigned char AtkinsonHyperlegible_Bold_ttf[];
extern const unsigned int AtkinsonHyperlegible_Bold_ttf_size;
}

using namespace SIM::UI;

namespace {

void SaveConfig()
{
    SIM_Config_prj_SaveConfigFile(SIM_Config_prj_GetConfig());
}

bool ConfigToggle(const char *label, const char *help, BOOL *value, bool enabled = true)
{
    bool on = *value != FALSE;
    if (Toggle(label, help, &on, enabled)) {
        *value = on ? TRUE : FALSE;
        SaveConfig();
        return true;
    }
    return false;
}

// --- Games -----------------------------------------------------------------------------------

std::string sGamesMessage;
int sConfirmSwitch = -1;
Uint64 sConfirmUntil = 0;

void GameAdded(BOOL ok, const char *id)
{
    (void)id;
    sGamesMessage = ok ? "Added. Choose it below to switch to it." : "";
}

Uint64 sResetConfirmUntil = 0;

// A soft reset, as with L+R+Start+Select (tap twice)
void DrawReset(Uint64 now)
{
    const bool allowed = SoftReset_Allowed() != FALSE;
    const bool confirming = allowed && now < sResetConfirmUntil;
    const char *help = !allowed  ? "Not right now: the game is saving or linked up."
        : confirming             ? "Anything since your last save is lost."
                                 : "A soft reset, like L+R+Start+Select: back to the title screen and your last save, "
                                   "e.g. to reset for a nature. Nothing is saved.";
    if (Button(confirming ? "Tap again to reset###reset" : "Reset to the title screen###reset", help, confirming, allowed)) {
        if (confirming) {
            sResetConfirmUntil = 0;
            SoftReset_Request();
            SIM::Settings::Close();
        } else {
            sResetConfirmUntil = now + 4000;
        }
    }
}

void DrawGames()
{
    SIM_Cart carts[SIM_CARTS_MAX];
    int count = SIM_Carts_List(carts);
    const SIM_Cart *current = SIM_Carts_Current();
    const Uint64 now = SDL_GetTicks64();
    if (now > sConfirmUntil) {
        sConfirmSwitch = -1;
    }

    DrawReset(now);
    Section("Your games");

    for (int i = 0; i < count; i++) {
        ImGui::PushID(i);
        const bool playing = strcmp(carts[i].id, current->id) == 0;
        std::string subtitle;
        if (playing) {
            subtitle = "Playing now";
        } else if (sConfirmSwitch == i) {
            subtitle = "Tap again to save and switch (the app restarts)";
        } else {
            subtitle = "Tap to switch to it";
        }
        if (carts[i].info[0] != '\0') {
            subtitle += std::string(". ") + carts[i].info;
        }
        if (Card(carts[i].name, subtitle.c_str(), playing) && !playing) {
            if (sConfirmSwitch == i) {
                SIM_Carts_SwitchTo(carts[i].id);
            } else {
                sConfirmSwitch = i;
                sConfirmUntil = now + 4000;
            }
        }
        ImGui::PopID();
    }
    Status(sGamesMessage.c_str());

    if (Button("Add a game", "A ROM hack like Renegade Platinum: choose a folder with your ROM and its patches, and "
                             "the app applies them.")) {
        sGamesMessage.clear();
        SIM_Carts_Add(GameAdded);
    }
    bool ask = SIM_Carts_AskAtLaunch() != FALSE;
    if (Toggle("Choose the game when the app starts", "Only asked when more than one game is installed.", &ask)) {
        SIM_Carts_SetAskAtLaunch(ask ? TRUE : FALSE);
    }
    Note("Each game has its own files and saves; the settings are shared. Switching saves your game first when it "
         "can be.");
}

// --- Saves -----------------------------------------------------------------------------------

std::string sSavesStatus;
bool sSavesError = false;
int sSelectedSlot = -1;
int sImportSlot = -1;

void SetSavesStatus(const std::string &text, bool error)
{
    sSavesStatus = text;
    sSavesError = error;
}

void OnImportPicked(const char *path, void *)
{
    if (path == nullptr || sImportSlot < 0) {
        SetSavesStatus("", false);
        return;
    }
    char err[128] = "";
    if (SaveSlots_Import(sImportSlot, path, err, sizeof(err))) {
        char text[96];
        snprintf(text, sizeof(text), "Imported into slot %d.", sImportSlot + 1);
        SetSavesStatus(text, false);
    } else {
        SetSavesStatus(std::string("Import failed: ") + err, true);
    }
#ifdef SDK_BUILD_ANDROID
    // The picker made a private copy for us
    remove(path);
#endif
    sImportSlot = -1;
}

void OnExportDone(BOOL saved, void *)
{
    if (saved) {
        SetSavesStatus("Save exported.", false);
    } else {
        SetSavesStatus("", false);
    }
}

void ExportSlot(int slot)
{
    // Padded to a full 512 KB image (what emulators expect) in a temporary file, then the
    // player chooses where it goes
    const char *tempPath = "export.sav";
    char err[128] = "";
    if (!SaveSlots_Export(slot, tempPath, err, sizeof(err))) {
        SetSavesStatus(std::string("Export failed: ") + err, true);
        return;
    }
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    char name[64];
    if (info.hasSummary && info.name[0] != '\0') {
        snprintf(name, sizeof(name), "Pokemon Platinum - %s (slot %d).sav", info.name, slot + 1);
    } else {
        snprintf(name, sizeof(name), "Pokemon Platinum (slot %d).sav", slot + 1);
    }
    SIM_Files_SaveCopy("Export save", tempPath, name, OnExportDone, nullptr);
}

void DrawSaves()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    const int current = SaveSlots_Current();
    if (sSelectedSlot < 0) {
        sSelectedSlot = current;
    }

    Note("Each slot is a separate game. On the main menu, L and R switch slots too.");
    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        SaveSlotInfo info;
        SaveSlots_GetInfo(slot, &info);
        char title[64];
        char subtitle[128];
        if (!info.hasData) {
            snprintf(title, sizeof(title), "Slot %d: empty", slot + 1);
            snprintf(subtitle, sizeof(subtitle), "%s", slot == current ? "In use" : "");
        } else if (info.hasSummary) {
            snprintf(title, sizeof(title), "Slot %d: %s", slot + 1, info.name);
            snprintf(subtitle, sizeof(subtitle), "%s%d:%02d played, %d badge%s%s%s", slot == current ? "In use. " : "",
                info.hours, info.minutes, info.badges, info.badges == 1 ? "" : "s",
                info.savedAt[0] != '\0' ? ", saved " : "", info.savedAt);
        } else {
            snprintf(title, sizeof(title), "Slot %d: saved game", slot + 1);
            snprintf(subtitle, sizeof(subtitle), "%sDetails show once it has been loaded", slot == current ? "In use. " : "");
        }
        ImGui::PushID(slot);
        if (Card(title, subtitle, slot == sSelectedSlot)) {
            sSelectedSlot = slot;
            sSavesStatus.clear();
        }
        ImGui::PopID();
    }

    // What to do with the selected slot
    const int slot = sSelectedSlot;
    const bool busy = SIM_Files_IsBusy() != FALSE;
    const bool atMenu = SaveSlots_IsMainMenuActive() != FALSE;
    char label[64];
    snprintf(label, sizeof(label), "Slot %d", slot + 1);
    Section(label);
    if (slot != current) {
        if (Button("Use this slot", atMenu ? "Loads it on the main menu." : "Go back to the main menu first (or press L/R "
                   "there).", false, atMenu)) {
            SaveSlots_RequestSwitch(slot);
        }
    }
    const bool inUse = SaveSlots_IsInUse(slot) != FALSE;
    if (Button("Import a save", inUse ? "This slot's game is running: go back to the main menu to replace it."
                                      : "A .sav or .dsv from an emulator or another device. The old one is kept as .bak.",
            false, !busy && !inUse)) {
        sImportSlot = slot;
        sSavesStatus.clear();
        SIM_Files_PickToOpen("Import save (.sav / .dsv)", ".sav,.dsv,.bin,.*", OnImportPicked, nullptr);
    }
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    if (Button("Export this save", "A 512 KB .sav that emulators can load.", false, !busy && info.hasData)) {
        sSavesStatus.clear();
        ExportSlot(slot);
    }
    Status(sSavesStatus.c_str(), sSavesError);

    Section("Autosave");
    ConfigToggle("Save automatically", "While exploring, every few minutes.", &config->autosave);
    int minutes = config->autosaveMinutes;
    if (Stepper("Every", nullptr, &minutes, 1, 30, 1, "%d min", config->autosave != FALSE)) {
        config->autosaveMinutes = minutes;
        SaveConfig();
    }
    ConfigToggle("Save when leaving the app", "When it's closed or sent to the background.", &config->autosaveOnExit);
    Note("Autosave only saves where the menu's SAVE would, while you're standing still, and after you've saved this "
         "game yourself once.");

    FieldSystem *fieldSystem = Autosave_GetFieldSystem();
    const char *reason = nullptr;
    const bool canSave = Autosave_CanSaveNow(fieldSystem, &reason) != FALSE;
    std::string help = canSave ? std::string("Saves the game now, like the menu's SAVE.")
                               : std::string("Not right now: ") + (reason != nullptr ? reason : "the game can't save here");
    if (Button("Save now", help.c_str(), false, canSave)) {
        if (Autosave_SaveNow(fieldSystem, "Saved")) {
            SetSavesStatus("Saved.", false);
        } else {
            SetSavesStatus("The game could not be saved.", true);
        }
    }
}

// --- Gameplay --------------------------------------------------------------------------------

void DrawGameplay()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    ConfigToggle("60 FPS", "Smooth walking and scrolling: the overworld runs at 60 frames a second. Music keeps its "
                           "speed.", &config->enable60fps);
    const bool fps60 = config->enable60fps != FALSE;
    ConfigToggle("Keep the real speed", "Walking, biking, animations and map props move at the DS speed at 60 FPS.",
        &config->enable60fpsSpeedFix, fps60);
    ConfigToggle("60 FPS in battles and menus too", "Everything off the overworld then runs twice as fast.",
        &config->enable60fpsEverywhere, fps60);
}

// --- Gear ------------------------------------------------------------------------------------

void DrawGear()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (ConfigToggle("Show the Gear", "The companion on the lower screen: battle info, party, encounters, trainer.",
            &config->gearEnabled)) {
        if (!config->gearEnabled) {
            SIM_Companion_SetFullScreen(FALSE);
        }
    }
    static const char *const kLevels[] = { "Vanilla", "Guide", "Everything" };
    static const char *const kHelp[] = {
        "Only what the game itself shows: no encounter guide or hidden values.",
        "Adds the encounter guide and type matchups; species you haven't seen stay hidden.",
        "Also hidden values: IVs/EVs, the opponents' abilities, items and whole team.",
    };
    int level = config->gearDiscovery;
    if (level < 0 || level > 2) {
        level = GEAR_DISCOVERY_GUIDE;
    }
    if (Choice("What it reveals", kHelp[level], &level, kLevels, 3, config->gearEnabled != FALSE)) {
        config->gearDiscovery = level;
        SaveConfig();
    }
    Note("Tap Full in the Gear (or press L3) to give it the whole lower screen; battles bring the DS screen back.");
}

// --- Cheats ----------------------------------------------------------------------------------

void DrawCheats()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    ConfigToggle("Walk through walls", nullptr, &config->walkThroughWalls);
    ConfigToggle("No wild encounters", "Wild Pokemon don't appear in grass, caves or water.",
        &config->disableRandomEncounters);
    ConfigToggle("Run from trainer battles", nullptr, &config->runFromTrainerBattles);
}

// --- Debugging (on the Advanced page) --------------------------------------------------------

void DrawDebug()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    Section("Game checks");
    ConfigToggle("Check the game's assertions", "Report failed GF_ASSERTs as a communication error.",
        &config->enableAsserts);
    ConfigToggle("Stop in the debugger on a failed check", nullptr, &config->breakDebuggerOnGfAssert);
    char text[96];
    snprintf(text, sizeof(text), "pokeplatinum %s", SIM_GetProjectGitHash());
    Note(text);
}

} // namespace

void SIM_Prj_SettingsPages()
{
    SIM::Settings::AddPage("Games", "Reset to the title, switch games, add Renegade Platinum", DrawGames, 10);
    SIM::Settings::AddPage("Saves", "Slots, autosave, import and export", DrawSaves, 20);
    SIM::Settings::AddPage("Gameplay", "60 FPS", DrawGameplay, 30);
    SIM::Settings::AddPage("Gear", "The lower-screen companion", DrawGear, 70);
    SIM::Settings::AddPage("Cheats", "Walk through walls, no encounters", DrawCheats, 80);
    SIM::Settings::AddPage("Advanced", nullptr, DrawDebug, 90);
}

extern "C" void SIM_Prj_UIFont(const void **regular, int *regularSize, const void **bold, int *boldSize)
{
    *regular = AtkinsonHyperlegible_Regular_ttf;
    *regularSize = (int)AtkinsonHyperlegible_Regular_ttf_size;
    *bold = AtkinsonHyperlegible_Bold_ttf;
    *boldSize = (int)AtkinsonHyperlegible_Bold_ttf_size;
}
