// Settings GUI: save slots, import/export, autosave. Also the on-screen save notice.
#include <nitro.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim.h>
#include <simulator/sim_files.h>
#include <simulator/sim_gui.hpp>

#include <cstdio>
#include <cstring>
#include <string>

#include "sim_gui_prj.hpp"

#include "port/autosave.h"
#include "port/save_slots.h"
#include "port/sim_config_prj.h"

namespace SIM::GUI {

static std::string sStatus;
static int sImportSlot = -1;

static void OnImportPicked(const char *path, void *)
{
    if (path == nullptr || sImportSlot < 0) {
        sStatus = "Import cancelled.";
        return;
    }

    char err[128] = "";
    if (SaveSlots_Import(sImportSlot, path, err, sizeof(err))) {
        char text[96];
        snprintf(text, sizeof(text), "Imported into slot %d.", sImportSlot + 1);
        sStatus = text;
    } else {
        sStatus = std::string("Import failed: ") + err;
    }

#ifdef SDK_BUILD_ANDROID
    // The picker made a private copy for us
    remove(path);
#endif
    sImportSlot = -1;
}

static void OnExportDone(BOOL saved, void *)
{
    sStatus = saved ? "Save exported." : "Export cancelled or failed.";
}

static void ExportSlot(int slot)
{
    // Pad the slot to a full 512 KB image (what emulators expect) in a temporary file,
    // then let the player choose where it goes
    const char *tempPath = "export.sav";
    char err[128] = "";
    if (!SaveSlots_Export(slot, tempPath, err, sizeof(err))) {
        sStatus = std::string("Export failed: ") + err;
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

static void SlotRow(int slot)
{
    const bool current = slot == SaveSlots_Current();
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);

    ImGui::PushID(slot);
    ImGui::Separator();

    if (!info.hasData) {
        ImGui::Text("%s Slot %d: empty", current ? ">" : " ", slot + 1);
    } else if (info.hasSummary) {
        ImGui::Text("%s Slot %d: %s  %d:%02d  %d badge%s", current ? ">" : " ", slot + 1,
            info.name, info.hours, info.minutes, info.badges, info.badges == 1 ? "" : "s");
        if (info.savedAt[0] != '\0') {
            ImGui::TextDisabled("    saved %s", info.savedAt);
        }
    } else {
        ImGui::Text("%s Slot %d: saved game (details after it's loaded once)", current ? ">" : " ", slot + 1);
    }

    const bool atMenu = SaveSlots_IsMainMenuActive();
    const bool busy = SIM_Files_IsBusy();

    ImGui::BeginDisabled(!atMenu || current);
    if (ImGui::Button("Use this slot")) {
        SaveSlots_RequestSwitch(slot);
    }
    ImGui::EndDisabled();
    if (!atMenu && !current && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Switch slots from the main menu (or press L/R there)");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(busy || SaveSlots_IsInUse(slot));
    if (ImGui::Button("Import...")) {
        sImportSlot = slot;
        sStatus.clear();
        SIM_Files_PickToOpen("Import save (.sav / .dsv)", ".sav,.dsv,.bin,.*", OnImportPicked, nullptr);
    }
    ImGui::EndDisabled();
    if (SaveSlots_IsInUse(slot) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("This slot's game is running. Go back to the main menu to replace it.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(busy || !info.hasData);
    if (ImGui::Button("Export...")) {
        sStatus.clear();
        ExportSlot(slot);
    }
    ImGui::EndDisabled();

    ImGui::PopID();
}

void SavesMain(bool *openState)
{
    ImGui::Begin("Saves", openState);
    SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    bool changed = false;

    ImGui::TextWrapped("Each slot is a separate game. On the main menu, L/R switch slots.");
    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        SlotRow(slot);
    }
    ImGui::Separator();
    if (!sStatus.empty()) {
        ImGui::TextWrapped("%s", sStatus.c_str());
        ImGui::Separator();
    }

    ImGui::Text("Autosave");
    bool autosave = config->autosave != FALSE;
    if (ImGui::Checkbox("Save automatically while exploring", &autosave)) {
        config->autosave = autosave ? TRUE : FALSE;
        changed = true;
    }
    ImGui::BeginDisabled(!autosave);
    int minutes = config->autosaveMinutes;
    if (ImGui::SliderInt("Every (minutes)", &minutes, 1, 30)) {
        config->autosaveMinutes = minutes;
        changed = true;
    }
    ImGui::EndDisabled();

    bool onExit = config->autosaveOnExit != FALSE;
    if (ImGui::Checkbox("Save when leaving or closing the app", &onExit)) {
        config->autosaveOnExit = onExit ? TRUE : FALSE;
        changed = true;
    }
    ImGui::TextDisabled("Autosave only runs where the menu's SAVE would, while you're\n"
                        "standing still, and after you've saved this game once yourself.");

    FieldSystem *fieldSystem = Autosave_GetFieldSystem();
    const char *reason = nullptr;
    const bool canSave = Autosave_CanSaveNow(fieldSystem, &reason);
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save now")) {
        Autosave_SaveNow(fieldSystem, "Saved");
    }
    ImGui::EndDisabled();
    if (!canSave && reason != nullptr) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", reason);
    }

    if (changed) {
        SIM_Config_prj_SaveConfigFile(config);
    }

    ImGui::End();
}

// Drawn every frame, GUI open or not
void PrjOverlay()
{
    float alpha = 0.0f;
    const char *notice = Autosave_GetNotice(&alpha);
    if (notice == nullptr) {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float margin = 12.0f * ImGui::GetIO().FontGlobalScale;
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, viewport->WorkPos.y + margin),
        ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.6f * alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::Begin("##saveNotice", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    ImGui::TextUnformatted(notice);
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace SIM::GUI
