// Settings GUI: the games installed side by side (libntr sim_carts.h)
#include <nitro.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim.h>
#include <simulator/sim_carts.h>
#include <simulator/sim_gui.hpp>

#include <cstring>
#include <string>

#include "sim_gui_prj.hpp"

namespace SIM::GUI {

static std::string sMessage;

static void GameAdded(BOOL ok, const char *id) {
    (void)id;
    sMessage = ok ? "Added. Choose Play to switch to it." : "";
}

void GamesMain(bool *openState) {
    ImGui::Begin("Games", openState, ImGuiWindowFlags_AlwaysAutoResize);

    SIM_Cart carts[SIM_CARTS_MAX];
    int count = SIM_Carts_List(carts);
    const SIM_Cart *current = SIM_Carts_Current();

    ImGui::TextWrapped("Each game has its own ROM files and saves. Switching restarts the app "
                       "(your game is saved first when it can be).");
    ImGui::Separator();

    for (int i = 0; i < count; i++) {
        ImGui::PushID(i);
        bool isCurrent = strcmp(carts[i].id, current->id) == 0;
        ImGui::Text("%s%s", carts[i].name, isCurrent ? "  (playing)" : "");
        if (!isCurrent) {
            ImGui::SameLine();
            if (ImGui::Button("Play")) {
                SIM_Carts_SwitchTo(carts[i].id);
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Add a game (a ROM, or a ROM + patches like Renegade Platinum)")) {
        sMessage.clear();
        SIM_Carts_Add(GameAdded);
    }
    if (!sMessage.empty()) {
        ImGui::TextUnformatted(sMessage.c_str());
    }

    bool ask = SIM_Carts_AskAtLaunch() != FALSE;
    if (ImGui::Checkbox("Choose the game when the app starts", &ask)) {
        SIM_Carts_SetAskAtLaunch(ask ? TRUE : FALSE);
    }
    ImGui::TextDisabled("(Only asked when more than one game is installed.)");

    ImGui::End();
}

}
