// Gear: a Kanto Gear-style companion on the lower display (see libntr's
// simulator/sim_companion.h). Battle matchups, party, the current area's wild
// encounters, trainer progress and settings, drawn at the panel's native resolution
// with Dear ImGui.
#include <nitro.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim_companion.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "constants/battle/condition.h"
#include "generated/genders.h"
#include "generated/time_of_day.h"

#include "port/gear_data.h"
#include "port/rom_patches.h"
#include "port/save_slots.h"
#include "port/sim_config_prj.h"

extern "C" {
extern const unsigned char AtkinsonHyperlegible_Regular_ttf[];
extern const unsigned int AtkinsonHyperlegible_Regular_ttf_size;
extern const unsigned char AtkinsonHyperlegible_Bold_ttf[];
extern const unsigned int AtkinsonHyperlegible_Bold_ttf_size;
}

namespace {

// --- look --------------------------------------------------------------------------------

constexpr ImU32 kBackground = IM_COL32(24, 29, 38, 255);
constexpr ImU32 kPanel = IM_COL32(38, 46, 60, 255);
constexpr ImU32 kPanelPressed = IM_COL32(52, 63, 82, 255);
constexpr ImU32 kAccent = IM_COL32(96, 172, 232, 255);
constexpr ImU32 kText = IM_COL32(234, 239, 246, 255);
constexpr ImU32 kTextDim = IM_COL32(140, 152, 170, 255);
constexpr ImU32 kGood = IM_COL32(88, 200, 120, 255);
constexpr ImU32 kWarn = IM_COL32(236, 196, 70, 255);
constexpr ImU32 kBad = IM_COL32(232, 88, 80, 255);
constexpr ImU32 kBarBack = IM_COL32(18, 22, 30, 255);

// Standard type colors, by the game's type numbers (TYPE_MYSTERY is "???")
constexpr ImU32 kTypeColors[18] = {
    IM_COL32(168, 167, 122, 255), // Normal
    IM_COL32(194, 46, 40, 255),   // Fighting
    IM_COL32(169, 143, 243, 255), // Flying
    IM_COL32(163, 62, 161, 255),  // Poison
    IM_COL32(226, 191, 101, 255), // Ground
    IM_COL32(182, 161, 54, 255),  // Rock
    IM_COL32(166, 185, 26, 255),  // Bug
    IM_COL32(115, 87, 151, 255),  // Ghost
    IM_COL32(183, 183, 206, 255), // Steel
    IM_COL32(104, 160, 144, 255), // ???
    IM_COL32(238, 129, 48, 255),  // Fire
    IM_COL32(99, 144, 240, 255),  // Water
    IM_COL32(122, 199, 76, 255),  // Grass
    IM_COL32(247, 208, 44, 255),  // Electric
    IM_COL32(249, 85, 135, 255),  // Psychic
    IM_COL32(150, 217, 214, 255), // Ice
    IM_COL32(111, 53, 252, 255),  // Dragon
    IM_COL32(112, 87, 70, 255),   // Dark
};

// Type 9 is "???" in vanilla; ROM hacks such as Renegade Platinum make it Fairy
constexpr ImU32 kFairyColor = IM_COL32(214, 133, 173, 255);

ImU32 TypeColor(u8 type) {
  if (type == 9 && RomPatches_Info()->extraType) {
    return kFairyColor;
  }
  return type < 18 ? kTypeColors[type] : kTextDim;
}

ImFont *sRegular = nullptr;
ImFont *sBold = nullptr;
float sScale = 1.0f;
float sBaseFontSize = 20.0f;

float S(float value) {
  return value * sScale;
}

// --- state -------------------------------------------------------------------------------

enum App { APP_BATTLE, APP_PARTY, APP_AREA, APP_TRAINER, APP_SETTINGS, APP_COUNT };
const char *const kAppNames[APP_COUNT] = {"Battle", "Party", "Area", "Trainer", "Settings"};

App sApp = APP_PARTY;
int sSelectedMon = -1;

GearMon sParty[GEAR_PARTY_MAX];
int sPartyCount = 0;
GearTrainer sTrainer;
GearArea sArea;
int sRefreshCountdown = 0;
int sAreaMapID = -1;
int sAreaTimeOfDay = -1;

// Battles: the Battle app comes up by itself (with the DS screen, for the battle menu)
// and the Gear goes back to what it showed before when the battle ends
GearBattle sBattle;
int sBattleRefreshCountdown = 0;
bool sInBattle = false;
App sAppBeforeBattle = APP_PARTY;
bool sFullScreenBeforeBattle = false;
int sLastChoosing = -1;

// A press that turned into a scroll drag must not also click what it ended on
bool sDragging = false;

int Discovery() {
  int discovery = SIM_Config_prj_GetConfig()->gearDiscovery;
  return discovery < GEAR_DISCOVERY_VANILLA || discovery > GEAR_DISCOVERY_EVERYTHING ? GEAR_DISCOVERY_GUIDE
                                                                                      : discovery;
}

void RefreshBattle() {
  if (--sBattleRefreshCountdown > 0) {
    return;
  }
  sBattleRefreshCountdown = 4;

  Gear_GetBattle(&sBattle);
  if (sBattle.active && !sInBattle) {
    sInBattle = true;
    sAppBeforeBattle = sApp;
    sApp = APP_BATTLE;
    sLastChoosing = -1;
    sFullScreenBeforeBattle = SIM_Companion_IsFullScreen() != FALSE;
    SIM_Companion_SetFullScreen(FALSE);
  } else if (!sBattle.active && sInBattle) {
    sInBattle = false;
    if (sApp == APP_BATTLE) {
      sApp = sAppBeforeBattle == APP_BATTLE ? APP_PARTY : sAppBeforeBattle;
    }
    if (sFullScreenBeforeBattle) {
      SIM_Companion_SetFullScreen(TRUE);
    }
  }
  if (sBattle.choosing >= 0) {
    sLastChoosing = sBattle.choosing;
  }
}

void Refresh() {
  RefreshBattle();
  if (--sRefreshCountdown > 0) {
    return;
  }
  sRefreshCountdown = 10; // a few times a second is plenty

  sPartyCount = Gear_GetParty(sParty);
  if (sSelectedMon >= sPartyCount) {
    sSelectedMon = -1;
  }
  Gear_GetTrainer(&sTrainer);

  // The encounter table only changes with the map or the time of day
  static int sAreaRefresh = 0;
  if (++sAreaRefresh >= 6 || sAreaMapID < 0) {
    sAreaRefresh = 0;
    Gear_GetArea(&sArea);
    sAreaMapID = sArea.valid ? sArea.mapHeaderID : -1;
    sAreaTimeOfDay = sArea.timeOfDay;
  }
}

// --- widgets -----------------------------------------------------------------------------

void UpdateDragState() {
  ImGuiIO &io = ImGui::GetIO();
  if (io.MouseClicked[0]) {
    sDragging = false;
  }
  if (ImGui::IsMouseDragging(0, S(10.0f))) {
    sDragging = true;
  }
}

// Invisible button that ignores presses that became scroll drags
bool Tap(const char *id, ImVec2 size) {
  bool released = ImGui::InvisibleButton(id, size);
  return released && !sDragging;
}

// Lets a finger scroll the current window
void TouchScroll() {
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) &&
      ImGui::IsMouseDragging(0, S(10.0f))) {
    ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
  }
}

void TextAt(ImDrawList *draw, ImFont *font, float size, ImVec2 pos, ImU32 color, const char *text) {
  draw->AddText(font, size, pos, color, text);
}

ImVec2 TextSize(ImFont *font, float size, const char *text) {
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

// Rounded type label; returns its width
float TypeChip(ImDrawList *draw, ImVec2 pos, u8 type, float height) {
  const char *name = Gear_TypeName(type);
  float fontSize = height * 0.62f;
  ImVec2 textSize = TextSize(sBold, fontSize, name);
  float width = textSize.x + height * 0.8f;
  draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), TypeColor(type), height * 0.5f);
  TextAt(draw, sBold, fontSize, ImVec2(pos.x + (width - textSize.x) * 0.5f, pos.y + (height - textSize.y) * 0.5f),
         IM_COL32(255, 255, 255, 255), name);
  return width;
}

float TypeChips(ImDrawList *draw, ImVec2 pos, u8 type1, u8 type2, float height) {
  float width = TypeChip(draw, pos, type1, height);
  if (type2 != type1) {
    width += S(6.0f);
    width += TypeChip(draw, ImVec2(pos.x + width, pos.y), type2, height);
  }
  return width;
}

void Bar(ImDrawList *draw, ImVec2 pos, ImVec2 size, float fraction, ImU32 color) {
  fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
  draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kBarBack, size.y * 0.5f);
  if (fraction > 0.0f) {
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x * fraction, pos.y + size.y), color, size.y * 0.5f);
  }
}

ImU32 HpColor(int hp, int maxHp) {
  if (maxHp <= 0) {
    return kBad;
  }
  float fraction = (float)hp / (float)maxHp;
  return fraction > 0.5f ? kGood : (fraction > 0.2f ? kWarn : kBad);
}

// Short status label and its color, or nullptr when healthy
const char *StatusLabel(u32 status, ImU32 *color) {
  if (status & (MON_CONDITION_SLEEP_0 | MON_CONDITION_SLEEP_1 | MON_CONDITION_SLEEP_2)) {
    *color = IM_COL32(140, 140, 160, 255);
    return "SLP";
  }
  if (status & MON_CONDITION_TOXIC) {
    *color = IM_COL32(150, 60, 160, 255);
    return "TOX";
  }
  if (status & MON_CONDITION_POISON) {
    *color = IM_COL32(163, 62, 161, 255);
    return "PSN";
  }
  if (status & MON_CONDITION_BURN) {
    *color = IM_COL32(238, 120, 48, 255);
    return "BRN";
  }
  if (status & MON_CONDITION_FREEZE) {
    *color = IM_COL32(120, 200, 230, 255);
    return "FRZ";
  }
  if (status & MON_CONDITION_PARALYSIS) {
    *color = IM_COL32(220, 190, 40, 255);
    return "PAR";
  }
  return nullptr;
}

const char *MonName(const GearMon &mon) {
  return mon.isEgg ? "Egg" : mon.nickname;
}

const char *GenderSymbol(u8 gender) {
  return gender == GENDER_MALE ? "M" : (gender == GENDER_FEMALE ? "F" : "");
}

// --- header ------------------------------------------------------------------------------

void DrawHeader(const SIM_CompanionFrame *frame) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float height = S(40.0f);
  ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;

  // App tabs
  float x = origin.x;
  for (int i = 0; i < APP_COUNT; i++) {
    if (i == APP_BATTLE && !sInBattle) {
      continue;
    }
    const char *name = kAppNames[i];
    ImVec2 textSize = TextSize(sBold, S(17.0f), name);
    float tabWidth = textSize.x + S(22.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
    ImGui::PushID(i);
    bool pressed = Tap("tab", ImVec2(tabWidth, height));
    ImGui::PopID();
    bool selected = sApp == i;
    draw->AddRectFilled(ImVec2(x, origin.y + S(4.0f)), ImVec2(x + tabWidth, origin.y + height - S(4.0f)),
                        selected ? kAccent : (ImGui::IsItemActive() ? kPanelPressed : kPanel), S(16.0f));
    TextAt(draw, sBold, S(17.0f), ImVec2(x + (tabWidth - textSize.x) * 0.5f, origin.y + (height - textSize.y) * 0.5f),
           selected ? IM_COL32(16, 24, 34, 255) : kText, name);
    if (pressed) {
      sApp = (App)i;
      if (i == APP_PARTY) {
        sSelectedMon = -1;
      }
    }
    x += tabWidth + S(6.0f);
  }

  // Expand/collapse, then the clock
  const char *toggleLabel = frame->fullScreen ? (frame->canShareScreen ? "Split" : "DS") : "Full";
  ImVec2 toggleText = TextSize(sBold, S(16.0f), toggleLabel);
  float toggleWidth = toggleText.x + S(22.0f);
  float toggleX = origin.x + width - toggleWidth;
  ImGui::SetCursorScreenPos(ImVec2(toggleX, origin.y));
  if (Tap("toggle", ImVec2(toggleWidth, height))) {
    SIM_Companion_ToggleFullScreen();
  }
  draw->AddRect(ImVec2(toggleX, origin.y + S(4.0f)), ImVec2(toggleX + toggleWidth, origin.y + height - S(4.0f)), kAccent,
                S(16.0f), 0, S(2.0f));
  TextAt(draw, sBold, S(16.0f), ImVec2(toggleX + (toggleWidth - toggleText.x) * 0.5f, origin.y + (height - toggleText.y) * 0.5f),
         kAccent, toggleLabel);

  char clock[16];
  time_t now = time(nullptr);
  struct tm *local = localtime(&now);
  snprintf(clock, sizeof(clock), "%d:%02d", local != nullptr ? local->tm_hour : 0, local != nullptr ? local->tm_min : 0);
  ImVec2 clockSize = TextSize(sBold, S(20.0f), clock);
  float clockX = toggleX - clockSize.x - S(12.0f);
  if (clockX > x) {
    TextAt(draw, sBold, S(20.0f), ImVec2(clockX, origin.y + (height - clockSize.y) * 0.5f), kText, clock);
  }

  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + S(6.0f)));
}

// --- party -------------------------------------------------------------------------------

void DrawMonCard(int index, ImVec2 size) {
  const GearMon &mon = sParty[index];
  ImDrawList *draw = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(index);
  bool pressed = Tap("card", size);
  bool held = ImGui::IsItemActive() && !sDragging;
  ImGui::PopID();
  if (pressed) {
    sSelectedMon = index;
  }

  draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), held ? kPanelPressed : kPanel, S(12.0f));
  const float pad = S(10.0f);

  TextAt(draw, sBold, S(18.0f), ImVec2(pos.x + pad, pos.y + pad), kText, MonName(mon));
  if (mon.isEgg) {
    TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + pad, pos.y + pad + S(24.0f)), kTextDim, "Not hatched yet");
    return;
  }
  if (mon.shiny) {
    ImVec2 nameSize = TextSize(sBold, S(18.0f), MonName(mon));
    TextAt(draw, sBold, S(18.0f), ImVec2(pos.x + pad + nameSize.x + S(6.0f), pos.y + pad), kWarn, "*");
  }

  char level[16];
  snprintf(level, sizeof(level), "Lv %d", mon.level);
  ImVec2 levelSize = TextSize(sRegular, S(16.0f), level);
  TextAt(draw, sRegular, S(16.0f), ImVec2(pos.x + size.x - pad - levelSize.x, pos.y + pad + S(1.0f)), kTextDim, level);

  // HP bar and numbers
  float barY = pos.y + pad + S(28.0f);
  Bar(draw, ImVec2(pos.x + pad, barY), ImVec2(size.x - pad * 2, S(9.0f)), mon.maxHp ? (float)mon.hp / mon.maxHp : 0.0f,
      HpColor(mon.hp, mon.maxHp));

  char hp[24];
  snprintf(hp, sizeof(hp), "%d/%d", mon.hp, mon.maxHp);
  TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + pad, barY + S(13.0f)), kText, hp);

  ImU32 statusColor;
  const char *status = mon.hp == 0 ? "FNT" : StatusLabel(mon.status, &statusColor);
  if (mon.hp == 0) {
    statusColor = kBad;
  }
  if (status != nullptr) {
    ImVec2 statusSize = TextSize(sBold, S(14.0f), status);
    ImVec2 badge(pos.x + size.x - pad - statusSize.x - S(12.0f), barY + S(12.0f));
    draw->AddRectFilled(badge, ImVec2(badge.x + statusSize.x + S(12.0f), badge.y + statusSize.y + S(4.0f)), statusColor,
                        S(6.0f));
    TextAt(draw, sBold, S(14.0f), ImVec2(badge.x + S(6.0f), badge.y + S(2.0f)), IM_COL32(255, 255, 255, 255), status);
  }
}

void DrawMonDetail(const GearMon &mon) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;

  // Back
  ImVec2 pos = ImGui::GetCursorScreenPos();
  if (Tap("back", ImVec2(S(90.0f), S(34.0f)))) {
    sSelectedMon = -1;
  }
  draw->AddRectFilled(pos, ImVec2(pos.x + S(90.0f), pos.y + S(34.0f)), kPanel, S(14.0f));
  TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + S(18.0f), pos.y + S(7.0f)), kText, "< Party");

  ImGui::Dummy(ImVec2(0, S(4.0f)));
  ImGui::PushFont(sBold, S(24.0f));
  ImGui::TextUnformatted(MonName(mon));
  ImGui::PopFont();
  if (mon.isEgg) {
    ImGui::TextDisabled("This egg hasn't hatched yet.");
    return;
  }

  // Species (when nicknamed), level, gender, shiny
  const char *species = Gear_SpeciesName(mon.species);
  ImGui::SameLine();
  ImGui::TextColored(ImColor(kTextDim), "%s  Lv %d %s%s", strcmp(mon.nickname, species) != 0 ? species : "", mon.level,
                     GenderSymbol(mon.gender), mon.shiny ? "  Shiny" : "");

  pos = ImGui::GetCursorScreenPos();
  float chipsWidth = TypeChips(draw, pos, mon.type1, mon.type2, S(26.0f));
  ImGui::Dummy(ImVec2(chipsWidth, S(30.0f)));

  // HP and EXP
  pos = ImGui::GetCursorScreenPos();
  char line[64];
  snprintf(line, sizeof(line), "HP %d/%d", mon.hp, mon.maxHp);
  TextAt(draw, sBold, S(16.0f), pos, kText, line);
  Bar(draw, ImVec2(pos.x + S(140.0f), pos.y + S(5.0f)), ImVec2(width - S(140.0f), S(10.0f)),
      mon.maxHp ? (float)mon.hp / mon.maxHp : 0.0f, HpColor(mon.hp, mon.maxHp));
  ImGui::Dummy(ImVec2(width, S(24.0f)));

  pos = ImGui::GetCursorScreenPos();
  if (mon.level < 100) {
    snprintf(line, sizeof(line), "%u to Lv %d", mon.expNextLevel - mon.exp, mon.level + 1);
  } else {
    snprintf(line, sizeof(line), "Max level");
  }
  TextAt(draw, sRegular, S(15.0f), pos, kTextDim, line);
  float expRange = (float)(mon.expNextLevel - mon.expLevelStart);
  Bar(draw, ImVec2(pos.x + S(140.0f), pos.y + S(5.0f)), ImVec2(width - S(140.0f), S(8.0f)),
      expRange > 0 ? (float)(mon.exp - mon.expLevelStart) / expRange : 1.0f, kAccent);
  ImGui::Dummy(ImVec2(width, S(24.0f)));

  ImGui::Text("Item: %s", mon.heldItem ? Gear_ItemName(mon.heldItem) : "None");
  ImGui::Text("Ability: %s", Gear_AbilityName(mon.ability));
  ImGui::Text("Nature: %s", Gear_NatureName(mon.nature));
  ImGui::Dummy(ImVec2(0, S(4.0f)));

  // Moves
  for (int m = 0; m < 4; m++) {
    const GearMove &move = mon.moves[m];
    if (move.id == 0) {
      continue;
    }
    pos = ImGui::GetCursorScreenPos();
    const float rowHeight = S(44.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowHeight), kPanel, S(10.0f));
    float chip = TypeChip(draw, ImVec2(pos.x + S(8.0f), pos.y + S(10.0f)), move.type, S(24.0f));
    TextAt(draw, sBold, S(17.0f), ImVec2(pos.x + S(16.0f) + chip, pos.y + S(11.0f)), kText, Gear_MoveName(move.id));

    char info[48];
    const char *category = move.category == 0 ? "Phys" : (move.category == 1 ? "Spec" : "Status");
    if (move.power > 1) {
      snprintf(info, sizeof(info), "%s %d  PP %d/%d", category, move.power, move.pp, move.maxPp);
    } else {
      snprintf(info, sizeof(info), "%s  PP %d/%d", category, move.pp, move.maxPp);
    }
    ImVec2 infoSize = TextSize(sRegular, S(15.0f), info);
    TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + width - infoSize.x - S(10.0f), pos.y + S(13.0f)),
           move.pp == 0 ? kBad : kTextDim, info);
    ImGui::Dummy(ImVec2(width, rowHeight + S(6.0f)));
  }

  // Stats (and hidden values when the player asked for everything)
  const char *statNames[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
  const bool hidden = Discovery() == GEAR_DISCOVERY_EVERYTHING;
  if (ImGui::BeginTable("stats", hidden ? 4 : 2, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Stat");
    ImGui::TableNextColumn();
    ImGui::TextDisabled("Value");
    if (hidden) {
      ImGui::TableNextColumn();
      ImGui::TextDisabled("IV");
      ImGui::TableNextColumn();
      ImGui::TextDisabled("EV");
    }
    for (int s = 0; s < 6; s++) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(statNames[s]);
      ImGui::TableNextColumn();
      ImGui::Text("%d", mon.stats[s]);
      if (hidden) {
        ImGui::TableNextColumn();
        ImGui::Text("%d", mon.ivs[s]);
        ImGui::TableNextColumn();
        ImGui::Text("%d", mon.evs[s]);
      }
    }
    ImGui::EndTable();
  }
  ImGui::Text("Friendship: %d", mon.friendship);
}

void DrawParty(const SIM_CompanionFrame *frame) {
  if (sPartyCount == 0) {
    ImGui::TextDisabled("No Pokemon yet.");
    return;
  }
  if (sSelectedMon >= 0) {
    DrawMonDetail(sParty[sSelectedMon]);
    return;
  }

  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = S(8.0f);
  // Three across fits the shared strip; two across when there's a whole screen
  const int columns = frame->fullScreen ? 2 : 3;
  const float cardWidth = (width - gap * (columns - 1)) / columns;
  const float cardHeight = S(82.0f);
  for (int i = 0; i < sPartyCount; i++) {
    if (i % columns != 0) {
      ImGui::SameLine(0.0f, gap);
    }
    DrawMonCard(i, ImVec2(cardWidth, cardHeight));
  }
}

// --- area --------------------------------------------------------------------------------

const char *MethodName(int method) {
  switch (method) {
  case GEAR_ENC_GRASS:
    return "Walking";
  case GEAR_ENC_SURF:
    return "Surfing";
  case GEAR_ENC_OLD_ROD:
    return "Old Rod";
  case GEAR_ENC_GOOD_ROD:
    return "Good Rod";
  case GEAR_ENC_SUPER_ROD:
    return "Super Rod";
  case GEAR_ENC_RADAR:
    return "Poke Radar";
  case GEAR_ENC_SWARM:
    return "Swarms";
  default:
    return "";
  }
}

const char *TimeOfDayName(int timeOfDay) {
  switch (timeOfDay) {
  case TIMEOFDAY_MORNING:
    return "Morning";
  case TIMEOFDAY_DAY:
    return "Day";
  case TIMEOFDAY_TWILIGHT:
    return "Evening";
  default:
    return "Night";
  }
}

void DrawArea() {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!sArea.valid) {
    ImGui::TextDisabled("Start or continue a game to see this area.");
    return;
  }

  ImGui::PushFont(sBold, S(22.0f));
  ImGui::TextUnformatted(sArea.name);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ImColor(kTextDim), "  %s", TimeOfDayName(sArea.timeOfDay));

  if (Discovery() == GEAR_DISCOVERY_VANILLA) {
    ImGui::TextDisabled("The encounter guide is off (Settings > Discovery).");
    return;
  }
  if (!sArea.hasEncounters) {
    ImGui::TextDisabled("No wild Pokemon here.");
    return;
  }

  // Progress for the normal (walking/water) encounters; radar and swarm Pokemon are extras
  int total = 0;
  int caught = 0;
  for (int i = 0; i < sArea.count; i++) {
    const GearEncounter &enc = sArea.entries[i];
    if (enc.method == GEAR_ENC_RADAR || enc.method == GEAR_ENC_SWARM) {
      continue;
    }
    bool repeat = false;
    for (int j = 0; j < i; j++) {
      repeat = repeat || sArea.entries[j].species == enc.species;
    }
    if (!repeat) {
      total++;
      caught += Gear_SpeciesCaught(enc.species) ? 1 : 0;
    }
  }
  ImGui::TextColored(ImColor(caught == total ? kGood : kTextDim), "Caught %d of %d species here", caught, total);

  const float width = ImGui::GetContentRegionAvail().x;
  int method = -1;
  for (int i = 0; i < sArea.count; i++) {
    const GearEncounter &enc = sArea.entries[i];
    if (enc.method != method) {
      method = enc.method;
      ImGui::Dummy(ImVec2(0, S(4.0f)));
      ImGui::PushFont(sBold, S(16.0f));
      ImGui::TextColored(ImColor(kAccent), "%s", MethodName(method));
      ImGui::PopFont();
    }

    const bool seen = Gear_SpeciesSeen(enc.species);
    const bool isCaught = Gear_SpeciesCaught(enc.species);
    const bool showName = seen || Discovery() == GEAR_DISCOVERY_EVERYTHING;

    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float rowHeight = S(34.0f);
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowHeight), kPanel, S(8.0f));

    // Caught: filled ball. Seen: ring. Never seen: dim dot
    ImVec2 center(pos.x + S(18.0f), pos.y + rowHeight * 0.5f);
    if (isCaught) {
      draw->AddCircleFilled(center, S(8.0f), kBad);
      draw->AddRectFilled(ImVec2(center.x - S(8.0f), center.y), ImVec2(center.x + S(8.0f), center.y + S(8.0f)),
                          IM_COL32(240, 240, 240, 255), 0);
      draw->AddCircle(center, S(8.0f), IM_COL32(30, 30, 30, 255), 0, S(1.5f));
      draw->AddLine(ImVec2(center.x - S(8.0f), center.y), ImVec2(center.x + S(8.0f), center.y), IM_COL32(30, 30, 30, 255),
                    S(1.5f));
    } else if (seen) {
      draw->AddCircle(center, S(7.0f), kTextDim, 0, S(2.0f));
    } else {
      draw->AddCircleFilled(center, S(3.0f), kTextDim);
    }

    TextAt(draw, sBold, S(17.0f), ImVec2(pos.x + S(36.0f), pos.y + S(7.0f)), showName ? kText : kTextDim,
           showName ? Gear_SpeciesName(enc.species) : "???");

    char info[32];
    if (enc.minLevel == enc.maxLevel) {
      snprintf(info, sizeof(info), "Lv %d", enc.minLevel);
    } else {
      snprintf(info, sizeof(info), "Lv %d-%d", enc.minLevel, enc.maxLevel);
    }
    char rate[16] = "";
    if (enc.rate > 0) {
      snprintf(rate, sizeof(rate), "%d%%", enc.rate);
    }
    ImVec2 rateSize = TextSize(sBold, S(16.0f), rate);
    ImVec2 infoSize = TextSize(sRegular, S(15.0f), info);
    TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + width - S(10.0f) - rateSize.x, pos.y + S(8.0f)), kText, rate);
    TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + width - S(70.0f) - infoSize.x, pos.y + S(9.0f)), kTextDim, info);

    ImGui::Dummy(ImVec2(width, rowHeight + S(4.0f)));
  }
}

// --- battle ------------------------------------------------------------------------------

constexpr ImU32 kResisted = IM_COL32(236, 140, 70, 255);

struct Matchup {
  const char *shortLabel; // "x2"
  const char *words;      // "Super effective"
  ImU32 color;
};

Matchup MatchupFor(int tenths) {
  if (tenths == 0) {
    return {"x0", "No effect", kBad};
  }
  if (tenths < 5) {
    return {"x\xC2\xBC", "Not very effective", kResisted};
  }
  if (tenths < 10) {
    return {"x\xC2\xBD", "Not very effective", kResisted};
  }
  if (tenths == 10) {
    return {"x1", "Normal damage", kTextDim};
  }
  if (tenths < 40) {
    return {"x2", "Super effective", kGood};
  }
  return {"x4", "Super effective", kGood};
}

bool IsDamaging(const GearMove &move) {
  return move.category != 2 && move.power > 0;
}

// The player's Pokemon whose moves to show: the one picking a command (or the last one
// that did), otherwise the first one still standing
int ActiveBattler() {
  if (sLastChoosing >= 0 && sLastChoosing < GEAR_BATTLERS_MAX) {
    const GearBattler &b = sBattle.battlers[sLastChoosing];
    if (b.present && !b.enemy) {
      return sLastChoosing;
    }
  }
  int fallback = -1;
  for (int i = 0; i < GEAR_BATTLERS_MAX; i++) {
    const GearBattler &b = sBattle.battlers[i];
    if (!b.present || b.enemy) {
      continue;
    }
    if (b.playerControlled && b.hp > 0) {
      return i;
    }
    if (fallback < 0) {
      fallback = i;
    }
  }
  return fallback;
}

// Opponents in the order they stand on the top screen (left to right)
int Opponents(int out[2]) {
  int count = 0;
  const int order[2] = {3, 1}; // the second enemy slot stands to the left of the first
  for (int i = 0; i < 2; i++) {
    const int battler = order[i];
    if (battler < GEAR_BATTLERS_MAX && sBattle.battlers[battler].present && sBattle.battlers[battler].enemy) {
      out[count++] = battler;
    }
  }
  return count;
}

const char *const kStageNames[8] = {"", "Atk", "Def", "Spe", "SpA", "SpD", "Acc", "Eva"};

// "Atk +1  Spe -2", or an empty string when nothing is raised or lowered
void StageText(const GearBattler &b, char *out, size_t size) {
  out[0] = '\0';
  size_t used = 0;
  for (int s = 1; s < 8; s++) {
    if (b.statStages[s] != 0 && used < size) {
      used += snprintf(out + used, size - used, "%s%s %+d", used ? "  " : "", kStageNames[s], b.statStages[s]);
    }
  }
}

// Small filled label; returns its width
float Badge(ImDrawList *draw, ImVec2 pos, const char *text, ImU32 color, float fontSize) {
  ImVec2 size = TextSize(sBold, fontSize, text);
  float width = size.x + fontSize * 0.7f;
  draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + size.y + fontSize * 0.25f), color, fontSize * 0.4f);
  TextAt(draw, sBold, fontSize, ImVec2(pos.x + fontSize * 0.35f, pos.y + fontSize * 0.12f), IM_COL32(255, 255, 255, 255), text);
  return width;
}

void PokeBall(ImDrawList *draw, ImVec2 center, float radius) {
  draw->AddCircleFilled(center, radius, kBad);
  draw->PathArcTo(center, radius, 0.0f, 3.14159265f);
  draw->PathFillConvex(IM_COL32(240, 240, 240, 255));
  draw->AddCircle(center, radius, IM_COL32(30, 30, 30, 255), 0, S(1.5f));
  draw->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), IM_COL32(30, 30, 30, 255), S(1.5f));
}

// One opposing Pokemon: name, level and HP, then its types and status
void DrawOpponentCard(int battler, const char *side, ImVec2 size) {
  const GearBattler &b = sBattle.battlers[battler];
  ImDrawList *draw = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  const int discovery = Discovery();
  const bool fainted = b.hp == 0;
  const float pad = S(8.0f);

  draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kPanel, S(10.0f));
  float x = pos.x + pad;
  const float line1 = pos.y + S(5.0f);
  if (side != nullptr) {
    x += Badge(draw, ImVec2(x, line1 + S(1.0f)), side, kAccent, S(13.0f)) + S(6.0f);
  }
  if (!sBattle.trainer && Gear_SpeciesCaught(b.species)) {
    PokeBall(draw, ImVec2(x + S(7.0f), line1 + S(9.0f)), S(7.0f));
    x += S(20.0f);
  }
  TextAt(draw, sBold, S(16.0f), ImVec2(x, line1), fainted ? kTextDim : kText, b.nickname);
  x += TextSize(sBold, S(16.0f), b.nickname).x + S(6.0f);
  char level[16];
  snprintf(level, sizeof(level), "Lv %d%s", b.level, b.shiny ? " *" : "");
  TextAt(draw, sRegular, S(14.0f), ImVec2(x, line1 + S(1.5f)), kTextDim, level);

  // HP: what the game's bar shows, exact numbers only with everything revealed
  char hp[24];
  const int percent = b.maxHp ? (b.hp * 100 + b.maxHp - 1) / b.maxHp : 0;
  if (fainted) {
    snprintf(hp, sizeof(hp), "Fainted");
  } else if (discovery == GEAR_DISCOVERY_EVERYTHING) {
    snprintf(hp, sizeof(hp), "%d/%d", b.hp, b.maxHp);
  } else {
    snprintf(hp, sizeof(hp), "%d%%", percent);
  }
  ImVec2 hpSize = TextSize(sBold, S(14.0f), hp);
  TextAt(draw, sBold, S(14.0f), ImVec2(pos.x + size.x - pad - hpSize.x, line1 + S(1.5f)), fainted ? kBad : kText, hp);

  const float line2 = pos.y + S(25.0f);
  float x2 = pos.x + pad;
  if (discovery != GEAR_DISCOVERY_VANILLA) {
    x2 += TypeChips(draw, ImVec2(x2, line2), b.type1, b.type2, S(17.0f)) + S(6.0f);
  }
  ImU32 statusColor = kBad;
  const char *status = fainted ? nullptr : StatusLabel(b.status, &statusColor);
  if (status != nullptr) {
    x2 += Badge(draw, ImVec2(x2, line2), status, statusColor, S(12.0f)) + S(6.0f);
  }
  if (!fainted && b.confused && discovery != GEAR_DISCOVERY_VANILLA) {
    x2 += Badge(draw, ImVec2(x2, line2), "CNF", IM_COL32(180, 90, 200, 255), S(12.0f)) + S(6.0f);
  }
  const float barRight = pos.x + size.x - pad;
  if (barRight - x2 > S(30.0f)) {
    Bar(draw, ImVec2(x2, line2 + S(5.0f)), ImVec2(barRight - x2, S(7.0f)), b.maxHp ? (float)b.hp / b.maxHp : 0.0f,
        HpColor(b.hp, b.maxHp));
  }

  ImGui::Dummy(size);
}

// One of the active Pokemon's moves and how it fares against each opponent
void DrawBattleMove(const GearMove &move, const int *opponents, int opponentCount, ImVec2 size) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  const float pad = S(8.0f);
  const int discovery = Discovery();

  draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kPanel, S(10.0f));
  if (move.id == 0) {
    TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + pad + S(8.0f), pos.y + S(13.0f)), kTextDim, "-");
    ImGui::Dummy(size);
    return;
  }

  // Type stripe on the left edge
  draw->AddRectFilled(pos, ImVec2(pos.x + S(7.0f), pos.y + size.y), TypeColor(move.type), S(10.0f),
                      ImDrawFlags_RoundCornersLeft);
  const float x = pos.x + S(7.0f) + pad;
  TextAt(draw, sBold, S(15.0f), ImVec2(x, pos.y + S(5.0f)), kText, Gear_MoveName(move.id));

  char pp[16];
  snprintf(pp, sizeof(pp), "%d/%d", move.pp, move.maxPp);
  ImVec2 ppSize = TextSize(sRegular, S(13.0f), pp);
  TextAt(draw, sRegular, S(13.0f), ImVec2(pos.x + size.x - pad - ppSize.x, pos.y + S(6.0f)), move.pp == 0 ? kBad : kTextDim, pp);

  const float line2 = pos.y + S(24.0f);
  if (discovery == GEAR_DISCOVERY_VANILLA || !IsDamaging(move)) {
    char info[32];
    const char *category = move.category == 0 ? "Physical" : (move.category == 1 ? "Special" : "Status");
    if (move.power > 1) {
      snprintf(info, sizeof(info), "%s  %s %d", Gear_TypeName(move.type), category, move.power);
    } else {
      snprintf(info, sizeof(info), "%s  %s", Gear_TypeName(move.type), category);
    }
    TextAt(draw, sRegular, S(13.0f), ImVec2(x, line2), kTextDim, info);
  } else if (opponentCount == 1) {
    const GearBattler &target = sBattle.battlers[opponents[0]];
    Matchup matchup = MatchupFor(Gear_TypeEffectiveness(move.type, target.type1, target.type2));
    char text[48];
    snprintf(text, sizeof(text), "%s  %s", matchup.shortLabel, matchup.words);
    TextAt(draw, sBold, S(13.0f), ImVec2(x, line2), matchup.color, text);
  } else {
    float tx = x;
    const char *sides[2] = {"L", "R"};
    for (int i = 0; i < opponentCount; i++) {
      const GearBattler &target = sBattle.battlers[opponents[i]];
      char text[16];
      if (target.hp == 0) {
        snprintf(text, sizeof(text), "%s -", sides[i]);
        TextAt(draw, sBold, S(13.0f), ImVec2(tx, line2), kTextDim, text);
      } else {
        Matchup matchup = MatchupFor(Gear_TypeEffectiveness(move.type, target.type1, target.type2));
        snprintf(text, sizeof(text), "%s %s", sides[i], matchup.shortLabel);
        TextAt(draw, sBold, S(13.0f), ImVec2(tx, line2), matchup.color, text);
      }
      tx += TextSize(sBold, S(13.0f), text).x + S(14.0f);
    }
  }

  ImGui::Dummy(size);
}

void SectionLabel(const char *text) {
  ImGui::PushFont(sBold, S(14.0f));
  ImGui::TextColored(ImColor(kAccent), "%s", text);
  ImGui::PopFont();
}

void DrawBattlerDetails(const GearBattler &b, bool showMoves) {
  const int discovery = Discovery();
  ImGui::PushID(&b);
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextUnformatted(b.nickname);
  ImGui::PopFont();
  ImGui::SameLine();
  if (!b.enemy || discovery == GEAR_DISCOVERY_EVERYTHING) {
    ImGui::TextColored(ImColor(kTextDim), "Lv %d  HP %d/%d", b.level, b.hp, b.maxHp);
  } else {
    ImGui::TextColored(ImColor(kTextDim), "Lv %d", b.level);
  }

  if (discovery != GEAR_DISCOVERY_VANILLA) {
    char stages[96];
    StageText(b, stages, sizeof(stages));
    if (stages[0] != '\0') {
      ImGui::TextColored(ImColor(kWarn), "%s", stages);
    }
  }
  if (!b.enemy || discovery == GEAR_DISCOVERY_EVERYTHING) {
    ImGui::TextColored(ImColor(kTextDim), "Ability: %s   Item: %s", Gear_AbilityName(b.ability),
                       b.heldItem ? Gear_ItemName(b.heldItem) : "None");
  }
  if (showMoves && ImGui::BeginTable("moves", 2, ImGuiTableFlags_SizingStretchSame)) {
    for (int m = 0; m < 4; m++) {
      const GearMove &move = b.moves[m];
      if (move.id == 0) {
        continue;
      }
      ImGui::TableNextColumn();
      ImGui::TextColored(ImColor(TypeColor(move.type)), "%s", Gear_MoveName(move.id));
      ImGui::SameLine();
      ImGui::TextColored(ImColor(kTextDim), "%d/%d", move.pp, move.maxPp);
    }
    ImGui::EndTable();
  }
  ImGui::PopID();
}

void DrawBattle(const SIM_CompanionFrame *frame) {
  (void)frame;
  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = S(6.0f);
  const int discovery = Discovery();

  // Opponents, side by side in double battles
  int opponents[2];
  const int opponentCount = Opponents(opponents);
  if (opponentCount == 0) {
    ImGui::TextDisabled("Waiting for the battle to start...");
    return;
  }
  const float cardWidth = (width - gap * (opponentCount - 1)) / opponentCount;
  const char *sides[2] = {"L", "R"};
  for (int i = 0; i < opponentCount; i++) {
    if (i > 0) {
      ImGui::SameLine(0.0f, gap);
    }
    DrawOpponentCard(opponents[i], opponentCount > 1 ? sides[i] : nullptr, ImVec2(cardWidth, S(46.0f)));
  }

  // The active Pokemon's moves, two by two
  const int active = ActiveBattler();
  if (active >= 0) {
    const GearBattler &mine = sBattle.battlers[active];
    const float moveWidth = (width - gap) * 0.5f;
    for (int m = 0; m < 4; m++) {
      if (m % 2 != 0) {
        ImGui::SameLine(0.0f, gap);
      }
      ImGui::PushID(m);
      DrawBattleMove(mine.moves[m], opponents, opponentCount, ImVec2(moveWidth, S(44.0f)));
      ImGui::PopID();
    }

    // Below the fold in the split view: the active Pokemon itself
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    SectionLabel("Your Pokemon");
    DrawBattlerDetails(mine, false);
  }

  // What the opponents are up to
  bool labelled = false;
  for (int i = 0; i < opponentCount; i++) {
    const GearBattler &b = sBattle.battlers[opponents[i]];
    if (b.hp == 0) {
      continue;
    }
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    if (!labelled) {
      SectionLabel(opponentCount > 1 ? "Opponents" : "Opponent");
      labelled = true;
    }
    DrawBattlerDetails(b, discovery == GEAR_DISCOVERY_EVERYTHING);
  }

  // The opposing team, with everything revealed
  if (sBattle.trainer && discovery == GEAR_DISCOVERY_EVERYTHING && sBattle.enemyPartyCount > 0) {
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    SectionLabel("Opposing team");
    for (int i = 0; i < sBattle.enemyPartyCount; i++) {
      const GearBattleMon &mon = sBattle.enemyParty[i];
      ImGui::TextColored(ImColor(mon.fainted ? kTextDim : kText), "%s  Lv %d%s", mon.isEgg ? "Egg" : Gear_SpeciesName(mon.species),
                         mon.level, mon.fainted ? "  (fainted)" : "");
    }
  } else if (!sBattle.trainer && opponentCount > 0 && discovery != GEAR_DISCOVERY_VANILLA) {
    const GearBattler &wild = sBattle.battlers[opponents[0]];
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    ImGui::TextColored(ImColor(Gear_SpeciesCaught(wild.species) ? kGood : kWarn), "%s",
                       Gear_SpeciesCaught(wild.species) ? "Already caught" : "Not caught yet");
  }
}

// --- trainer -----------------------------------------------------------------------------

void DrawTrainer() {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!Gear_IsGameRunning()) {
    ImGui::TextDisabled("Start or continue a game to see your progress.");
    return;
  }

  ImGui::PushFont(sBold, S(22.0f));
  ImGui::TextUnformatted(sTrainer.name);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ImColor(kTextDim), "  ID %05u", sTrainer.id);

  ImGui::Text("Money: $%u", sTrainer.money);
  ImGui::Text("Play time: %d:%02d", sTrainer.hours, sTrainer.minutes);

  // Badges
  ImGui::Dummy(ImVec2(0, S(4.0f)));
  ImGui::Text("Badges: %d", sTrainer.badgeCount);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  const float radius = S(14.0f);
  for (int i = 0; i < 8; i++) {
    ImVec2 center(pos.x + radius + i * (radius * 2.0f + S(8.0f)), pos.y + radius);
    bool has = sTrainer.badgeMask & (1 << i);
    if (has) {
      draw->AddCircleFilled(center, radius, kWarn);
    } else {
      draw->AddCircle(center, radius, kTextDim, 0, S(2.0f));
    }
    char number[4];
    snprintf(number, sizeof(number), "%d", i + 1);
    ImVec2 size = TextSize(sBold, S(14.0f), number);
    TextAt(draw, sBold, S(14.0f), ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f),
           has ? IM_COL32(40, 30, 10, 255) : kTextDim, number);
  }
  ImGui::Dummy(ImVec2(8 * (radius * 2.0f + S(8.0f)), radius * 2.0f + S(6.0f)));

  if (sTrainer.pokedexObtained) {
    ImGui::Text("Sinnoh Dex: %d seen, %d caught", sTrainer.seenLocal, sTrainer.caughtLocal);
    if (sTrainer.nationalDex) {
      ImGui::Text("National Dex: %d seen, %d caught", sTrainer.seenNational, sTrainer.caughtNational);
    }
  } else {
    ImGui::TextDisabled("No Pokedex yet.");
  }

  ImGui::Dummy(ImVec2(0, S(4.0f)));
  SaveSlotInfo info;
  SaveSlots_GetInfo(SaveSlots_Current(), &info);
  ImGui::TextColored(ImColor(kTextDim), "Save slot %d%s%s", SaveSlots_Current() + 1,
                     info.savedAt[0] ? ", last saved " : "", info.savedAt);
}

// --- settings ----------------------------------------------------------------------------

void DrawSettings() {
  SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
  bool changed = false;

  ImGui::PushFont(sBold, S(18.0f));
  ImGui::TextUnformatted("Discovery");
  ImGui::PopFont();
  ImGui::TextWrapped("How much the Gear reveals. It never changes the game itself.");

  static const char *const names[3] = {"Vanilla", "Guide", "Everything"};
  static const char *const help[3] = {
      "Only what the game tells you: no encounter guide or hidden values.",
      "Wild encounters here; species you haven't seen stay hidden.",
      "Everything, including unseen species and IVs/EVs.",
  };
  int discovery = Discovery();
  for (int i = 0; i < 3; i++) {
    if (ImGui::RadioButton(names[i], discovery == i) && !sDragging) {
      config->gearDiscovery = i;
      changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", help[i]);
  }

  ImGui::Dummy(ImVec2(0, S(8.0f)));
  ImGui::PushFont(sBold, S(18.0f));
  ImGui::TextUnformatted("Screen");
  ImGui::PopFont();
  ImGui::TextWrapped("Tap Full (or press L3) to give the Gear the whole lower screen, and Split to bring the DS "
                     "touch screen back. Battles bring the DS screen back by themselves.");
  bool enabled = config->gearEnabled != FALSE;
  if (ImGui::Checkbox("Show the Gear on the lower screen", &enabled) && !sDragging) {
    config->gearEnabled = enabled ? TRUE : FALSE;
    changed = true;
    if (!enabled) {
      SIM_Companion_SetFullScreen(FALSE);
    }
  }
  ImGui::TextDisabled("Turn it back on in the settings GUI (R3): Application > Config.");

  if (changed) {
    SIM_Config_prj_SaveConfigFile(config);
  }
}

} // namespace

// --- libntr hooks --------------------------------------------------------------------------

extern "C" BOOL SIM_Prj_CompanionEnabled(void) {
  return SIM_Config_prj_GetConfig()->gearEnabled;
}

extern "C" void SIM_Prj_CompanionSetup(float outputWidth) {
  // Laid out for a 540 px wide panel and scaled from there (the Thor's is 1080)
  sScale = outputWidth / 540.0f;
  if (sScale < 1.0f) {
    sScale = 1.0f;
  }
  sBaseFontSize = 17.0f * sScale;

  ImGuiIO &io = ImGui::GetIO();
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false; // the data lives in the binary
  sRegular = io.Fonts->AddFontFromMemoryTTF((void *)AtkinsonHyperlegible_Regular_ttf,
                                            (int)AtkinsonHyperlegible_Regular_ttf_size, sBaseFontSize, &config);
  sBold = io.Fonts->AddFontFromMemoryTTF((void *)AtkinsonHyperlegible_Bold_ttf, (int)AtkinsonHyperlegible_Bold_ttf_size,
                                         sBaseFontSize, &config);
  io.FontDefault = sRegular;

  ImGuiStyle &style = ImGui::GetStyle();
  ImGui::StyleColorsDark(&style);
  style.FontSizeBase = sBaseFontSize;
  style.WindowPadding = ImVec2(12, 10);
  style.ItemSpacing = ImVec2(8, 6);
  style.FramePadding = ImVec2(8, 6);
  style.FrameRounding = 8;
  style.GrabRounding = 8;
  style.ScrollbarSize = 6;
  style.ScaleAllSizes(sScale);
  style.Colors[ImGuiCol_WindowBg] = ImColor(kBackground);
  style.Colors[ImGuiCol_ChildBg] = ImColor(kBackground);
  style.Colors[ImGuiCol_Text] = ImColor(kText);
  style.Colors[ImGuiCol_TextDisabled] = ImColor(kTextDim);
  style.Colors[ImGuiCol_CheckMark] = ImColor(kAccent);
  style.Colors[ImGuiCol_FrameBg] = ImColor(kPanel);
  style.Colors[ImGuiCol_FrameBgHovered] = ImColor(kPanelPressed);
  style.Colors[ImGuiCol_FrameBgActive] = ImColor(kPanelPressed);
  style.Colors[ImGuiCol_TableRowBg] = ImColor(kPanel);
}

extern "C" void SIM_Prj_CompanionDraw(const SIM_CompanionFrame *frame) {
  Refresh();
  UpdateDragState();

  ImGui::SetNextWindowPos(ImVec2(frame->x, frame->y));
  ImGui::SetNextWindowSize(ImVec2(frame->width, frame->height));
  ImGui::Begin("Gear", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse |
                   ImGuiWindowFlags_NoNav);

  DrawHeader(frame);

  ImGui::BeginChild("content", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollWithMouse);
  TouchScroll();
  if (sApp == APP_BATTLE && !sInBattle) {
    sApp = APP_PARTY;
  }
  switch (sApp) {
  case APP_BATTLE:
    DrawBattle(frame);
    break;
  case APP_PARTY:
    DrawParty(frame);
    break;
  case APP_AREA:
    DrawArea();
    break;
  case APP_TRAINER:
    DrawTrainer();
    break;
  case APP_SETTINGS:
    DrawSettings();
    break;
  default:
    break;
  }
  ImGui::Dummy(ImVec2(0, S(8.0f)));
  ImGui::EndChild();

  ImGui::End();
}
