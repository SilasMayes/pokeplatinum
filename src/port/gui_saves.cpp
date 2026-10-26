// The on-screen save notice ("Autosaved"). The saves page of the settings is in gui_settings.cpp.
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
