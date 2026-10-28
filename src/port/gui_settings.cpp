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
#include <ctime>
#include <string>

#include "port/autosave.h"
#include "port/nuzlocke.h"
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

void SaveEditor_Draw(); // gui_save_editor.cpp

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
                                      : "A .sav (melonDS, most emulators) or .dsv (DeSmuME) from an emulator or "
                                        "another device. The old one is kept as .bak.",
            false, !busy && !inUse)) {
        sImportSlot = slot;
        sSavesStatus.clear();
        SIM_Files_PickToOpen("Import save (.sav / .dsv)", ".sav,.dsv,.bin,.*", OnImportPicked, nullptr);
    }
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    if (Button("Export this save", "A 512 KB .sav that melonDS and other emulators load as it is.", false,
            !busy && info.hasData)) {
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

// The game's clock: hours and days ahead of the device's (night, a weekday's event, a
// time-of-day evolution or encounter without changing the device's clock)
void DrawClock()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    Section("Clock");
    int total = config->clockOffsetMinutes;
    int days = total / (24 * 60);
    int hours = (total - days * 24 * 60) / 60;
    bool changed = Stepper("Hours ahead", nullptr, &hours, -23, 23, 1, "%+d h");
    changed |= Stepper("Days ahead", nullptr, &days, 0, 6, 1, days == 1 ? "%d day" : "%d days");
    if (changed) {
        config->clockOffsetMinutes = days * 24 * 60 + hours * 60;
        SIM_SetClockOffset((long long)config->clockOffsetMinutes * 60);
        SaveConfig();
    }

    // What the game sees now
    const time_t now = time(nullptr) + (time_t)SIM_GetClockOffset();
    const struct tm *local = localtime(&now);
    if (local != nullptr) {
        static const char *const kDays[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
        const int hour = local->tm_hour;
        // As TimeOfDayForHour (rtc.c)
        const char *part = hour < 4 ? "late night" : hour < 10 ? "morning" : hour < 17 ? "day" : hour < 20 ? "evening" : "night";
        char text[96];
        snprintf(text, sizeof(text), "Game clock: %s %02d:%02d (%s)%s", kDays[local->tm_wday], hour, local->tm_min, part,
            config->clockOffsetMinutes == 0 ? ", the device's time" : "");
        Note(text);
    }
}

void DrawGameplay()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    Section("Speed");
    ConfigToggle("Instant text", "Messages appear at once, up to where they wait for a button.",
        &config->instantText);
    ConfigToggle("Instant HP and EXP bars", "Battle bars jump to their new value instead of sliding there.",
        &config->instantBars);
    Section("Field moves");
    ConfigToggle("Field moves without HMs", "With the HM in your bag (and its badge), Cut, Fly, Surf, Strength, "
        "Rock Smash, Waterfall, Rock Climb and Defog work without teaching the move: a Pokemon that could learn "
        "it does it. Fly and the others show in that Pokemon's menu.", &config->hmFree);
    Section("Level cap");
    ConfigToggle("Hard level cap", "Pokemon get no EXP from battles past the level cap: the level of the next gym "
        "leader's strongest Pokemon (the Champion's after 8 badges), read from the game's own trainer data. The Gear's "
        "Trainer tab shows the cap and the bosses' teams.", &config->hardLevelCap);
    DrawClock();
    Section("Nuzlocke");
    ConfigToggle("Nuzlocke helper", "Keeps the run's books on the Gear: each area's first wild encounter (the one "
        "you may catch), whether you caught it, and who fainted for good. It shows; it doesn't stop anything.",
        &config->nuzlocke);
    const bool nuzlocke = config->nuzlocke != FALSE;
    ConfigToggle("Dupes clause", "Species you've already caught don't count as an area's first encounter.",
        &config->nuzlockeDupes, nuzlocke);
    ConfigToggle("Shiny clause", "Shiny Pokemon never count, and can always be caught.", &config->nuzlockeShiny, nuzlocke);
    ConfigToggle("Send fainted Pokemon to the PC", "After each battle, as long as someone is left to carry on.",
        &config->nuzlockeToPC, nuzlocke);
    static double sResetUntil = 0.0;
    const double now = ImGui::GetTime();
    if (Button(now < sResetUntil ? "Tap again to start over" : "Start a new run",
            "Clears this save slot's Nuzlocke books (areas and deaths).", false, nuzlocke)) {
        if (now < sResetUntil) {
            Nuzlocke_Reset();
            sResetUntil = 0.0;
        } else {
            sResetUntil = now + 3.0;
        }
    }
    Section("Frame rate");
    ConfigToggle("60 FPS", "Smooth walking and scrolling: the overworld runs at 60 frames a second. Music keeps its "
                           "speed.", &config->enable60fps);
    const bool fps60 = config->enable60fps != FALSE;
    ConfigToggle("Keep the real speed", "Walking, biking, animations and map props move at the DS speed at 60 FPS.",
        &config->enable60fpsSpeedFix, fps60);
    ConfigToggle("60 FPS in battles and menus too", "Everything off the overworld then runs twice as fast.",
        &config->enable60fpsEverywhere, fps60);
    ConfigToggle("Draw every DS frame", "Battles and menus make a new picture every other DS frame; normally only "
        "those are drawn, which halves the work there. Turn this on if something flickers.",
        &config->drawEveryFrame);
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
        "Adds the encounter guide, type matchups, damage estimates (% of HP) and who moves first; "
        "species you haven't seen stay hidden.",
        "Also hidden values: IVs/EVs, exact damage and speed, the opponents' abilities, items, moves and "
        "whole team.",
    };
    int level = config->gearDiscovery;
    if (level < 0 || level > 2) {
        level = GEAR_DISCOVERY_GUIDE;
    }
    if (Choice("What it reveals", kHelp[level], &level, kLevels, 3, config->gearEnabled != FALSE)) {
        config->gearDiscovery = level;
        SaveConfig();
    }
    ConfigToggle("Battle menu on the Gear", "In battles the Gear takes the whole lower screen with Fight, Bag, Pokemon, "
        "Run and your moves with their matchups. The DS screen comes back for the bag and the party.",
        &config->gearBattleControls, config->gearEnabled != FALSE);
    Note("Tap Full in the Gear (or press L3) to give it the whole lower screen, Split for the DS screen.");
}

// --- Cheats ----------------------------------------------------------------------------------

// An int setting picked from a few values
bool ConfigChoice(const char *label, const char *help, int *setting, const int *values, const char *const *labels,
    int count)
{
    int index = 0;
    for (int i = 0; i < count; i++) {
        if (values[i] == *setting) {
            index = i;
        }
    }
    if (Choice(label, help, &index, labels, count)) {
        *setting = values[index];
        SaveConfig();
        return true;
    }
    return false;
}

void DrawCheats()
{
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();

    Section("Exploring");
    ConfigToggle("Walk through walls", nullptr, &config->walkThroughWalls);
    ConfigToggle("No wild encounters", "Wild Pokemon don't appear in grass, caves or water.",
        &config->disableRandomEncounters);
    ConfigToggle("Eggs hatch in a few steps", "Eggs in your party hatch after a few steps.", &config->fastEggs);

    Section("Battles");
    static const int kExpValues[] = { 100, 150, 200, 300, 500 };
    static const char *const kExpLabels[] = { "1x", "1.5x", "2x", "3x", "5x" };
    ConfigChoice("EXP from battles", "How much EXP your Pokemon get, on top of the game's own bonuses.",
        &config->expPercent, kExpValues, kExpLabels, 5);
    ConfigToggle("Every Poke Ball catches", "Wild Pokemon are caught with the first ball, whatever the ball.",
        &config->alwaysCatch);
    ConfigToggle("Moves don't use PP", "Your Pokemon's moves never run out in battle.", &config->infinitePP);
    ConfigToggle("Always get away", "Running from wild Pokemon always works, even when they trap you.",
        &config->alwaysEscape);
    ConfigToggle("Run from trainer battles", nullptr, &config->runFromTrainerBattles);

    Section("Pokemon");
    static const int kShinyValues[] = { 0, 1024, 256, 64, 1 };
    static const char *const kShinyLabels[] = { "Normal", "1/1024", "1/256", "1/64", "Always" };
    ConfigChoice("Shiny odds", "Wild Pokemon (legendaries too) and hatched eggs. Normal is the game's own 1 in 8192.",
        &config->shinyOdds, kShinyValues, kShinyLabels, 5);
    ConfigToggle("Any Pokemon learns any TM or HM", "Every Pokemon can be taught every TM and HM.", &config->anyTM);
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
    SIM::Settings::AddPage("Save editor", "Pokemon, items, money, badges, legendary events", SaveEditor_Draw, 25);
    SIM::Settings::AddPage("Gameplay", "Instant text, HM-free moves, level cap, clock, Nuzlocke, 60 FPS", DrawGameplay, 30);
    SIM::Settings::AddPage("Gear", "The lower-screen companion", DrawGear, 70);
    SIM::Settings::AddPage("Cheats", "Shiny odds, EXP, catching, PP, walls, encounters", DrawCheats, 80);
    SIM::Settings::AddPage("Advanced", nullptr, DrawDebug, 90);
}

extern "C" void SIM_Prj_UIFont(const void **regular, int *regularSize, const void **bold, int *boldSize)
{
    *regular = AtkinsonHyperlegible_Regular_ttf;
    *regularSize = (int)AtkinsonHyperlegible_Regular_ttf_size;
    *bold = AtkinsonHyperlegible_Bold_ttf;
    *boldSize = (int)AtkinsonHyperlegible_Bold_ttf_size;
}
