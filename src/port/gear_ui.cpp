// Gear: a Kanto Gear-style companion on the lower display (see libntr's
// simulator/sim_companion.h). Battle matchups, party, the current area's wild
// encounters, trainer progress and settings, drawn at the panel's native resolution
// with Dear ImGui.
#include <nitro.h>
#include <simulator/sim.h>
#include <simulator/imgui/imgui.hpp>
#include <simulator/sim_carts.h>
#include <simulator/sim_settings.hpp>
#include <simulator/sim_companion.h>

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

#include "constants/battle/condition.h"
#include "constants/species.h"
#include "generated/genders.h"
#include "generated/time_of_day.h"

#include "port/cheats.h"
#include "port/gear_data.h"
#include "port/gear_dex.h"
#include "port/gear_sprites.h"
#include "port/rom_patches.h"
#include "port/save_slots.h"
#include "port/nuzlocke.h"
#include "port/shiny_hunt.h"
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

enum App { APP_BATTLE, APP_PARTY, APP_AREA, APP_POKETCH, APP_TRAINER, APP_SETTINGS, APP_COUNT };
const char *const kAppNames[APP_COUNT] = {"Battle", "Party", "Area", "Pok\xC3\xA9tch", "Trainer", "Settings"};

App sApp = APP_PARTY;
int sSelectedMon = -1;

GearMon sParty[GEAR_PARTY_MAX];
int sPartyCount = 0;
GearTrainer sTrainer;
int sLevelCap = 0;   // Gear_LevelCap
int sNextGym = -1;   // Gear_NextGymBoss
GearArea sArea;
int sRefreshCountdown = 0;
int sAreaMapID = -1;
int sAreaTimeOfDay = -1;

// Battles: the Battle app comes up by itself and the Gear goes back to what it showed
// before when the battle ends. With battle controls on (the default) the Gear takes the
// whole lower screen and has the battle menu itself; the DS screen comes back for the
// bag, the party and the like. Otherwise the DS screen shows the game's battle menu.
GearBattle sBattle;
int sBattleRefreshCountdown = 0;
bool sInBattle = false;
App sAppBeforeBattle = APP_PARTY;
bool sFullScreenBeforeBattle = false;
int sLastChoosing = -1;
GearBattleMenu sMenu;            // read every frame in battle
int sLastMenuKind = GEAR_MENU_NONE;
bool sBattleLayoutManual = false; // the player chose Full/Split during this battle

bool BattleControls() {
  return SIM_Config_prj_GetConfig()->gearBattleControls != FALSE;
}

// A press that turned into a scroll drag must not also click what it ended on
bool sDragging = false;

int Discovery() {
  int discovery = SIM_Config_prj_GetConfig()->gearDiscovery;
  return discovery < GEAR_DISCOVERY_VANILLA || discovery > GEAR_DISCOVERY_EVERYTHING ? GEAR_DISCOVERY_GUIDE
                                                                                      : discovery;
}

// The level cap shows with the guide on, or when it's enforced
bool ShowLevelCap() {
  const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
  return Discovery() != GEAR_DISCOVERY_VANILLA || (config != nullptr && config->hardLevelCap);
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
    sBattleLayoutManual = false;
    sFullScreenBeforeBattle = SIM_Companion_IsFullScreen() != FALSE;
    SIM_Companion_SetFullScreen(BattleControls() ? TRUE : FALSE);
  } else if (!sBattle.active && sInBattle) {
    sInBattle = false;
    if (sApp == APP_BATTLE) {
      sApp = sAppBeforeBattle == APP_BATTLE ? APP_PARTY : sAppBeforeBattle;
    }
    SIM_Companion_SetFullScreen(sFullScreenBeforeBattle ? TRUE : FALSE);
  }
  if (sBattle.choosing >= 0) {
    sLastChoosing = sBattle.choosing;
  }
}

// Every frame in battle: the battle menu, and room for the DS screen when it's needed
void RefreshBattleMenu() {
  if (!sInBattle || !BattleControls()) {
    sMenu = {};
    sLastMenuKind = GEAR_MENU_NONE;
    return;
  }
  Gear_GetBattleMenu(&sMenu);
  if (sMenu.kind != GEAR_MENU_NONE && sLastMenuKind == GEAR_MENU_NONE) {
    sApp = APP_BATTLE; // something to choose: back to the battle app
  }
  sLastMenuKind = sMenu.kind;

  if (!sBattleLayoutManual) {
    const BOOL wantFull = Gear_BattleNeedsDsScreen() ? FALSE : TRUE;
    if (SIM_Companion_IsFullScreen() != wantFull) {
      SIM_Companion_SetFullScreen(wantFull);
    }
  }
}

void Refresh() {
  RefreshBattle();
  RefreshBattleMenu();
  if (--sRefreshCountdown > 0) {
    return;
  }
  sRefreshCountdown = 10; // a few times a second is plenty

  sPartyCount = Gear_GetParty(sParty);
  if (sSelectedMon >= sPartyCount) {
    sSelectedMon = -1;
  }

  // In battle the party only catches up afterwards: show the fighting Pokemon's HP live
  if (sBattle.active) {
    for (int b = 0; b < GEAR_BATTLERS_MAX; b++) {
      const GearBattler &battler = sBattle.battlers[b];
      if (!battler.present || battler.enemy || !battler.playerControlled) {
        continue;
      }
      for (int i = 0; i < sPartyCount; i++) {
        if (sParty[i].personality == battler.personality && sParty[i].species == battler.species) {
          sParty[i].hp = battler.hp;
          sParty[i].status = battler.status;
        }
      }
    }
  }
  Gear_GetTrainer(&sTrainer);
  sLevelCap = Gear_LevelCap();
  sNextGym = Gear_NextGymBoss();

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

// --- Pokemon pictures (port/gear_sprites.h) ---------------------------------------------------

// Decoded once, kept as textures. Decoding reads the game's files, so only a few new ones
// per frame: a long list fills in over a few frames instead of stalling one.
struct Picture {
  unsigned int texture = 0; // 0: not there (no such sprite)
};
std::unordered_map<u64, Picture> sPictures;
int sPictureLoadsThisFrame = 0;
constexpr int kPictureLoadsPerFrame = 12;

enum PictureKind { PICTURE_ICON = 1, PICTURE_FRONT = 2 };

// The texture, or 0 when there is none (yet)
unsigned int PictureTexture(PictureKind kind, u16 species, u8 form, bool egg, bool shiny, u8 gender, u32 personality) {
  if (species == 0) {
    return 0;
  }
  // Personality only changes Spinda's front sprite
  const u32 spots = kind == PICTURE_FRONT && species == 327 ? personality : 0;
  const u64 key = ((u64)kind << 60) | ((u64)(egg ? 1 : 0) << 59) | ((u64)(shiny ? 1 : 0) << 58) | ((u64)(gender & 3) << 56)
                  | ((u64)form << 48) | ((u64)species << 32) | spots;
  auto found = sPictures.find(key);
  if (found != sPictures.end()) {
    return found->second.texture;
  }
  if (sPictureLoadsThisFrame >= kPictureLoadsPerFrame) {
    return 0;
  }
  sPictureLoadsThisFrame++;

  Picture picture;
  if (kind == PICTURE_ICON) {
    static u32 pixels[GEAR_ICON_WIDTH * GEAR_ICON_HEIGHT];
    if (GearSprites_Icon(species, form, egg ? TRUE : FALSE, pixels)) {
      picture.texture = SIM_UI_CreateImage(GEAR_ICON_WIDTH, GEAR_ICON_HEIGHT, pixels);
    }
  } else {
    static u32 pixels[GEAR_FRONT_SIZE * GEAR_FRONT_SIZE];
    if (GearSprites_Front(species, gender, form, shiny ? TRUE : FALSE, egg ? TRUE : FALSE, personality, pixels)) {
      picture.texture = SIM_UI_CreateImage(GEAR_FRONT_SIZE, GEAR_FRONT_SIZE, pixels);
    }
  }
  sPictures[key] = picture;
  return picture.texture;
}

// A size for pixel art: a whole multiple of its pixels when that's close to the wanted size
float PixelSize(float wanted, float pixels) {
  const float whole = std::floor(wanted / pixels + 0.001f) * pixels;
  return whole >= wanted * 0.75f ? whole : std::floor(wanted);
}

// Party icon (32x32 frames), size x size at pos. frameSeconds > 0 animates it like the
// game's party menu does (two frames); tint darkens it (silhouettes, fainted)
void DrawIcon(ImDrawList *draw, ImVec2 pos, float size, u16 species, u8 form, bool egg, float frameSeconds = 0.0f,
              ImU32 tint = IM_COL32_WHITE) {
  const unsigned int texture = PictureTexture(PICTURE_ICON, species, form, egg, false, 0, 0);
  if (texture == 0) {
    return;
  }
  const int frame = frameSeconds > 0.0f ? (int)(ImGui::GetTime() / frameSeconds) & 1 : 0;
  pos = ImVec2(std::floor(pos.x), std::floor(pos.y));
  draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), pos, ImVec2(pos.x + size, pos.y + size),
                 ImVec2(0.0f, frame * 0.5f), ImVec2(1.0f, frame * 0.5f + 0.5f), tint);
}

// How fast a party Pokemon's icon hops: lively when healthy, slow when hurt, still when fainted
float IconFrameSeconds(int hp, int maxHp) {
  if (hp <= 0 || maxHp <= 0) {
    return 0.0f;
  }
  const float fraction = (float)hp / (float)maxHp;
  return fraction > 0.5f ? 0.18f : (fraction > 0.2f ? 0.32f : 0.6f);
}

// Battle front sprite (80x80), size x size at pos
void DrawFront(ImDrawList *draw, ImVec2 pos, float size, u16 species, u8 form, bool egg, bool shiny, u8 gender,
               u32 personality) {
  const unsigned int texture = PictureTexture(PICTURE_FRONT, species, form, egg, shiny, gender, personality);
  if (texture == 0) {
    return;
  }
  pos = ImVec2(std::floor(pos.x), std::floor(pos.y));
  draw->AddImage(ImTextureRef((ImTextureID)(intptr_t)texture), pos, ImVec2(pos.x + size, pos.y + size));
}

// --- header ------------------------------------------------------------------------------

// A cog: teeth around a ring
void GearIcon(ImDrawList *draw, ImVec2 center, float radius, ImU32 color, ImU32 background) {
  for (int i = 0; i < 8; i++) {
    const float angle = i * 3.14159265f / 4.0f;
    const ImVec2 dir(std::cos(angle), std::sin(angle));
    draw->AddLine(ImVec2(center.x + dir.x * radius * 0.6f, center.y + dir.y * radius * 0.6f),
                  ImVec2(center.x + dir.x * radius * 1.3f, center.y + dir.y * radius * 1.3f), color, radius * 0.45f);
  }
  draw->AddCircleFilled(center, radius, color);
  draw->AddCircleFilled(center, radius * 0.42f, background);
}

// Tabs that apply now: Battle in battles, the Pokétch once you have it (not in battles)
bool AppAvailable(App app) {
  switch (app) {
  case APP_BATTLE:
    return sInBattle;
  case APP_POKETCH:
    return !sInBattle && Gear_PoketchState() >= GEAR_POKETCH_SHOWN;
  default:
    return true;
  }
}

// The Pokétch takes the whole lower screen while it's open
bool sPoketchMadeFull = false;

void SelectApp(App app) {
  if (app == APP_POKETCH && sApp != APP_POKETCH && !SIM_Companion_IsFullScreen()) {
    SIM_Companion_SetFullScreen(TRUE);
    sPoketchMadeFull = true;
  } else if (app != APP_POKETCH && sApp == APP_POKETCH && sPoketchMadeFull) {
    SIM_Companion_SetFullScreen(FALSE);
    sPoketchMadeFull = false;
  }
  sApp = app;
  if (app == APP_PARTY) {
    sSelectedMon = -1;
  }
}

void DrawHeader(const SIM_CompanionFrame *frame) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float height = S(40.0f);
  ImVec2 origin = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x;

  // App tabs (Settings is a gear icon, to leave room)
  float x = origin.x;
  for (int i = 0; i < APP_COUNT; i++) {
    if (!AppAvailable((App)i)) {
      continue;
    }
    const char *name = kAppNames[i];
    const bool icon = i == APP_SETTINGS;
    ImVec2 textSize = TextSize(sBold, S(17.0f), name);
    float tabWidth = icon ? S(40.0f) : textSize.x + S(20.0f);
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y));
    ImGui::PushID(i);
    bool pressed = Tap("tab", ImVec2(tabWidth, height));
    ImGui::PopID();
    bool selected = sApp == i;
    draw->AddRectFilled(ImVec2(x, origin.y + S(4.0f)), ImVec2(x + tabWidth, origin.y + height - S(4.0f)),
                        selected ? kAccent : (ImGui::IsItemActive() ? kPanelPressed : kPanel), S(16.0f));
    const ImU32 ink = selected ? IM_COL32(16, 24, 34, 255) : kText;
    if (icon) {
      GearIcon(draw, ImVec2(x + tabWidth * 0.5f, origin.y + height * 0.5f), S(8.0f), ink,
               selected ? kAccent : (ImGui::IsItemActive() ? kPanelPressed : kPanel));
    } else {
      TextAt(draw, sBold, S(17.0f), ImVec2(x + (tabWidth - textSize.x) * 0.5f, origin.y + (height - textSize.y) * 0.5f),
             ink, name);
    }
    if (pressed) {
      SelectApp((App)i);
    }
    x += tabWidth + S(5.0f);
  }

  // Expand/collapse, then the clock
  const char *toggleLabel = frame->fullScreen ? (frame->canShareScreen ? "Split" : "DS") : "Full";
  ImVec2 toggleText = TextSize(sBold, S(16.0f), toggleLabel);
  float toggleWidth = toggleText.x + S(22.0f);
  float toggleX = origin.x + width - toggleWidth;
  ImGui::SetCursorScreenPos(ImVec2(toggleX, origin.y));
  if (Tap("toggle", ImVec2(toggleWidth, height))) {
    SIM_Companion_ToggleFullScreen();
    if (sInBattle) {
      sBattleLayoutManual = true; // until the battle ends
    }
  }
  draw->AddRect(ImVec2(toggleX, origin.y + S(4.0f)), ImVec2(toggleX + toggleWidth, origin.y + height - S(4.0f)), kAccent,
                S(16.0f), 0, S(2.0f));
  TextAt(draw, sBold, S(16.0f), ImVec2(toggleX + (toggleWidth - toggleText.x) * 0.5f, origin.y + (height - toggleText.y) * 0.5f),
         kAccent, toggleLabel);

  // Fast-forward on/off
  const bool fastForward = SIM_IsFastForwarding() != FALSE;
  const char *ffLabel = ">>";
  ImVec2 ffText = TextSize(sBold, S(16.0f), ffLabel);
  float ffWidth = ffText.x + S(22.0f);
  float ffX = toggleX - ffWidth - S(6.0f);
  ImGui::SetCursorScreenPos(ImVec2(ffX, origin.y));
  if (Tap("fastforward", ImVec2(ffWidth, height))) {
    SIM_SetFastForward(fastForward ? FALSE : TRUE);
  }
  if (fastForward) {
    draw->AddRectFilled(ImVec2(ffX, origin.y + S(4.0f)), ImVec2(ffX + ffWidth, origin.y + height - S(4.0f)), kAccent,
                        S(16.0f));
  } else {
    draw->AddRect(ImVec2(ffX, origin.y + S(4.0f)), ImVec2(ffX + ffWidth, origin.y + height - S(4.0f)), kAccent,
                  S(16.0f), 0, S(2.0f));
  }
  TextAt(draw, sBold, S(16.0f), ImVec2(ffX + (ffWidth - ffText.x) * 0.5f, origin.y + (height - ffText.y) * 0.5f),
         fastForward ? IM_COL32(16, 24, 34, 255) : kAccent, ffLabel);

  char clock[16];
  time_t now = time(nullptr);
  struct tm *local = localtime(&now);
  snprintf(clock, sizeof(clock), "%d:%02d", local != nullptr ? local->tm_hour : 0, local != nullptr ? local->tm_min : 0);
  ImVec2 clockSize = TextSize(sBold, S(20.0f), clock);
  float clockX = ffX - clockSize.x - S(12.0f);
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

  // Its icon, hopping like in the party menu, and the name beside it
  const float iconSize = PixelSize(S(48.0f), 32.0f);
  const bool dead = Nuzlocke_Enabled() && !mon.isEgg && Nuzlocke_IsDead(mon.personality);
  DrawIcon(draw, ImVec2(pos.x + S(2.0f), pos.y + S(4.0f) + (S(48.0f) - iconSize) * 0.5f), iconSize, mon.species, mon.form,
           mon.isEgg, mon.isEgg ? 0.6f : IconFrameSeconds(mon.hp, mon.maxHp),
           mon.hp == 0 && !mon.isEgg ? IM_COL32(150, 150, 150, 255) : IM_COL32_WHITE);
  const float textX = pos.x + S(52.0f);

  TextAt(draw, sBold, S(18.0f), ImVec2(textX, pos.y + pad), kText, MonName(mon));
  if (mon.isEgg) {
    TextAt(draw, sRegular, S(15.0f), ImVec2(textX, pos.y + pad + S(24.0f)), kTextDim, "Not hatched yet");
    return;
  }
  if (mon.shiny) {
    ImVec2 nameSize = TextSize(sBold, S(18.0f), MonName(mon));
    TextAt(draw, sBold, S(18.0f), ImVec2(textX + nameSize.x + S(6.0f), pos.y + pad), kWarn, "*");
  }

  // Level and status under the name
  char level[16];
  snprintf(level, sizeof(level), "Lv %d", mon.level);
  const bool overCap = ShowLevelCap() && sLevelCap > 0 && mon.level > sLevelCap;
  const float line2 = pos.y + pad + S(24.0f);
  TextAt(draw, overCap ? sBold : sRegular, S(15.0f), ImVec2(textX, line2), overCap ? kWarn : kTextDim, level);

  ImU32 statusColor;
  const char *status = mon.hp == 0 ? "FNT" : StatusLabel(mon.status, &statusColor);
  if (mon.hp == 0) {
    statusColor = kBad;
  }
  if (status != nullptr) {
    ImVec2 statusSize = TextSize(sBold, S(13.0f), status);
    ImVec2 badge(textX + TextSize(sRegular, S(15.0f), level).x + S(8.0f), line2 + S(1.0f));
    draw->AddRectFilled(badge, ImVec2(badge.x + statusSize.x + S(10.0f), badge.y + statusSize.y + S(3.0f)), statusColor,
                        S(6.0f));
    TextAt(draw, sBold, S(13.0f), ImVec2(badge.x + S(5.0f), badge.y + S(1.5f)), IM_COL32(255, 255, 255, 255), status);
  }

  // HP bar across the card, the numbers after it
  char hp[24];
  snprintf(hp, sizeof(hp), "%d/%d", mon.hp, mon.maxHp);
  const ImVec2 hpSize = TextSize(sRegular, S(14.0f), hp);
  const float barY = pos.y + S(60.0f);
  TextAt(draw, sRegular, S(14.0f), ImVec2(pos.x + size.x - pad - hpSize.x, barY + S(4.5f) - hpSize.y * 0.5f), kText, hp);
  Bar(draw, ImVec2(pos.x + pad, barY), ImVec2(size.x - pad * 2 - hpSize.x - S(8.0f), S(9.0f)),
      mon.maxHp ? (float)mon.hp / mon.maxHp : 0.0f, HpColor(mon.hp, mon.maxHp));

  if (dead) {
    // Fainted for good: dimmed, with a cross
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(24, 29, 38, 150), S(12.0f));
    const ImVec2 c(pos.x + size.x - pad - S(8.0f), pos.y + pad + S(9.0f));
    draw->AddLine(ImVec2(c.x, c.y - S(9.0f)), ImVec2(c.x, c.y + S(9.0f)), kBad, S(3.0f));
    draw->AddLine(ImVec2(c.x - S(6.0f), c.y - S(3.0f)), ImVec2(c.x + S(6.0f), c.y - S(3.0f)), kBad, S(3.0f));
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

  // Its portrait at the top right; the rows beside it stop short of it
  const float portrait = PixelSize(S(160.0f), 80.0f);
  const float portraitBottom = pos.y + portrait;
  DrawFront(draw, ImVec2(pos.x + width - portrait, pos.y), portrait, mon.species, mon.form, mon.isEgg, mon.shiny,
            mon.gender, mon.personality);
  auto rowWidth = [&](float y) { return y < portraitBottom ? width - portrait - S(8.0f) : width; };

  ImGui::Dummy(ImVec2(0, S(4.0f)));
  ImGui::PushFont(sBold, S(24.0f));
  ImGui::TextUnformatted(MonName(mon));
  ImGui::PopFont();
  if (mon.isEgg) {
    ImGui::TextDisabled("This egg hasn't hatched yet.");
    ImGui::Dummy(ImVec2(0, portraitBottom - ImGui::GetCursorScreenPos().y));
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
  Bar(draw, ImVec2(pos.x + S(140.0f), pos.y + S(5.0f)), ImVec2(rowWidth(pos.y) - S(140.0f), S(10.0f)),
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
  Bar(draw, ImVec2(pos.x + S(140.0f), pos.y + S(5.0f)), ImVec2(rowWidth(pos.y) - S(140.0f), S(8.0f)),
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
    const float moveWidth = rowWidth(pos.y);
    draw->AddRectFilled(pos, ImVec2(pos.x + moveWidth, pos.y + rowHeight), kPanel, S(10.0f));
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
    TextAt(draw, sRegular, S(15.0f), ImVec2(pos.x + moveWidth - infoSize.x - S(10.0f), pos.y + S(13.0f)),
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

bool DexSwitch();
void DrawDex();
void OpenSpecies(u16 species);
void PokeBall(ImDrawList *draw, ImVec2 center, float radius);

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
  case GEAR_ENC_GBA:
    return "GBA slot";
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

// "12,345"
const char *Thousands(u32 value, char *out, size_t size) {
  char digits[16];
  snprintf(digits, sizeof(digits), "%u", (unsigned)value);
  const size_t length = strlen(digits);
  size_t used = 0;
  for (size_t i = 0; i < length && used + 2 < size; i++) {
    if (i > 0 && (length - i) % 3 == 0) {
      out[used++] = ',';
    }
    out[used++] = digits[i];
  }
  out[used] = '\0';
  return out;
}

// --- Nuzlocke (port/nuzlocke.h) -------------------------------------------------------------

const char *NuzlockeResultWord(int result) {
  return result == NUZLOCKE_CAUGHT ? "caught" : (result == NUZLOCKE_MISSED ? "missed" : "in battle");
}

// The area's encounter: still open, or what it was
void DrawNuzlockeArea() {
  if (!Nuzlocke_Enabled()) {
    return;
  }
  const int label = Gear_CurrentLocationLabel();
  if (label < 0) {
    return;
  }
  const NuzlockeArea *area = Nuzlocke_Area((u16)label);
  if (area == nullptr) {
    ImGui::TextColored(ImColor(kGood), "Nuzlocke: this area's first encounter is still to come");
  } else {
    ImGui::TextColored(ImColor(area->result == NUZLOCKE_CAUGHT ? kText : kTextDim), "Nuzlocke: this area's encounter was %s, %s",
                       Gear_SpeciesName(area->species), NuzlockeResultWord(area->result));
  }
}

// The run: areas used, and who died
void DrawNuzlockeRun() {
  if (!Nuzlocke_Enabled()) {
    return;
  }
  const NuzlockeRun *run = Nuzlocke_Get();
  int caught = 0;
  for (int i = 0; i < run->areaCount; i++) {
    caught += run->areas[i].result == NUZLOCKE_CAUGHT ? 1 : 0;
  }
  ImGui::Dummy(ImVec2(0, S(6.0f)));
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextColored(ImColor(kAccent), "Nuzlocke");
  ImGui::PopFont();
  ImGui::Text("%d area%s: %d caught, %d missed. %d fainted for good.", run->areaCount, run->areaCount == 1 ? "" : "s",
              caught, run->areaCount - caught, run->deathCount);
  const float iconSize = PixelSize(S(32.0f), 32.0f);
  for (int i = 0; i < run->deathCount; i++) {
    const NuzlockeDeath &death = run->deaths[i];
    const ImVec2 rowPos = ImGui::GetCursorScreenPos();
    DrawIcon(ImGui::GetWindowDrawList(), ImVec2(rowPos.x, rowPos.y - S(8.0f)), iconSize, death.species, 0, false, 0.0f,
             IM_COL32(150, 150, 150, 255));
    ImGui::SetCursorScreenPos(ImVec2(rowPos.x + iconSize + S(4.0f), rowPos.y));
    ImGui::TextColored(ImColor(kTextDim), "%s (%s, Lv %d), %s", death.name, Gear_SpeciesName(death.species), death.level,
                       Gear_LocationLabelName(death.location));
    ImGui::SetCursorScreenPos(ImVec2(rowPos.x, ImGui::GetCursorScreenPos().y + S(8.0f)));
  }
}

// Shiny-hunt counters (port/shiny_hunt.h): met, eggs and resets since the last shiny,
// the Poke Radar chain, the last find
void DrawHunt() {
  const ShinyHuntStats *hunt = ShinyHunt_Get();
  static double sConfirmUntil = 0.0; // the reset button waits for a second tap until then
  const double now = ImGui::GetTime();

  ImGui::Dummy(ImVec2(0, S(2.0f)));
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextColored(ImColor(kAccent), "Shiny hunt");
  ImGui::PopFont();
  const bool confirming = now < sConfirmUntil;
  const char *resetLabel = confirming ? "Tap again to reset" : "Reset";
  const float buttonWidth = TextSize(sRegular, S(15.0f), resetLabel).x + S(20.0f);
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttonWidth);
  if (ImGui::Button(resetLabel, ImVec2(buttonWidth, 0)) && !sDragging) {
    if (confirming) {
      ShinyHunt_ResetCounters();
      sConfirmUntil = 0.0;
    } else {
      sConfirmUntil = now + 3.0;
    }
  }

  if (Cheats_ShinyOddsBoosted()) {
    if (Cheats_ShinyOdds() <= 1) {
      ImGui::TextColored(ImColor(kWarn), "Every wild Pokemon is shiny (Settings > Cheats)");
    } else {
      ImGui::TextColored(ImColor(kWarn), "Shiny odds 1 in %u (Settings > Cheats)", (unsigned)Cheats_ShinyOdds());
    }
  }
  char met[24];
  ImGui::Text("%s met since the last shiny", Thousands(hunt->encounters, met, sizeof(met)));
  if (hunt->encounters > 0) {
    ImGui::SameLine();
    const double chance = ShinyHunt_Chance(hunt->encounters) * 100.0;
    ImGui::TextColored(ImColor(kTextDim), chance < 10.0 ? "(%.1f%% odds by now)" : "(%.0f%% odds by now)", chance);
  }
  if (hunt->eggs > 0 || hunt->resets > 0) {
    char eggs[24];
    char resets[24];
    ImGui::TextColored(ImColor(kTextDim), "Eggs hatched: %s   Soft resets: %s", Thousands(hunt->eggs, eggs, sizeof(eggs)),
                       Thousands(hunt->resets, resets, sizeof(resets)));
  }
  if (sArea.radarChain > 0) {
    // As CheckPatchShiny in pokeradar.c: each new patch is shiny 1 in (8200 - 200 x chain), 200 at best
    int rate = 8200 - sArea.radarChain * 200;
    rate = rate < 200 ? 200 : rate;
    ImGui::TextColored(ImColor(sArea.radarChain >= 40 ? kGood : kWarn), "Poke Radar chain %d: a patch is shiny 1 in %d",
                       sArea.radarChain, rate);
  }
  if (hunt->findCount > 0) {
    const ShinyHuntFind &last = hunt->finds[0];
    char count[24];
    const char *what = last.method == SHINY_HUNT_EGG ? (last.count == 1 ? "egg" : "eggs") : "met";
    ImGui::TextColored(ImColor(kTextDim), "Last shiny: %s after %s %s%s%s", Gear_SpeciesName(last.species),
                       Thousands(last.count, count, sizeof(count)), what, last.date[0] ? ", " : "", last.date);
  }
}

void DrawArea() {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!sArea.valid) {
    ImGui::TextDisabled("Start or continue a game to see this area.");
    return;
  }
  if (Discovery() != GEAR_DISCOVERY_VANILLA && DexSwitch()) {
    DrawDex();
    return;
  }

  ImGui::PushFont(sBold, S(22.0f));
  ImGui::TextUnformatted(sArea.name);
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ImColor(kTextDim), "  %s", TimeOfDayName(sArea.timeOfDay));

  DrawNuzlockeArea();
  DrawHunt();

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
    // Tap a Pokemon for its Pokedex page
    ImGui::PushID(i);
    const bool tapped = Tap("encounter", ImVec2(width, rowHeight));
    const bool held = ImGui::IsItemActive() && !sDragging;
    ImGui::PopID();
    ImGui::SetCursorScreenPos(pos);
    if (tapped && showName) {
      OpenSpecies(enc.species);
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowHeight), held && showName ? kPanelPressed : kPanel, S(8.0f));

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

    // Its icon; a silhouette while it's never been seen
    const float iconSize = PixelSize(S(32.0f), 32.0f);
    DrawIcon(draw, ImVec2(pos.x + S(30.0f), pos.y + (rowHeight - iconSize) * 0.5f - S(2.0f)), iconSize, enc.species, 0, false,
             0.0f, showName ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));
    const float nameX = pos.x + S(66.0f);

    const char *speciesName = showName ? Gear_SpeciesName(enc.species) : "???";
    TextAt(draw, sBold, S(17.0f), ImVec2(nameX, pos.y + S(7.0f)), showName ? kText : kTextDim, speciesName);
    // How many met since the last shiny one
    const u32 metCount = showName ? ShinyHunt_SpeciesCount(enc.species) : 0;
    if (metCount > 0) {
      char metText[24];
      char number[16];
      snprintf(metText, sizeof(metText), "x%s", Thousands(metCount, number, sizeof(number)));
      TextAt(draw, sRegular, S(14.0f), ImVec2(nameX + TextSize(sBold, S(17.0f), speciesName).x + S(8.0f), pos.y + S(10.0f)),
             kTextDim, metText);
    }

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

// --- Pokédex (port/gear_dex.h) ------------------------------------------------------------------

bool sDexOpen = false;       // the Area tab shows the Pokédex instead of this area
u16 sDexSpecies = 0;         // a species page (0: the list)
u16 sDexBack[8];             // pages to go back to
int sDexBackCount = 0;
int sDexLetter = 0;          // 0 all, else 'A'..'Z'
GearSpecies sDexData;
u16 sDexLoaded = 0;
u16 sDexOrder[MAX_SPECIES];
int sDexOrderCount = 0;

bool DexCanShow(u16 species) {
  return Discovery() == GEAR_DISCOVERY_EVERYTHING || Gear_SpeciesSeen(species);
}

void OpenSpecies(u16 species) {
  if (sDexOpen && sDexSpecies != 0 && sDexSpecies != species && sDexBackCount < 8) {
    sDexBack[sDexBackCount++] = sDexSpecies;
  } else if (!sDexOpen) {
    sDexBackCount = 0;
  }
  sDexOpen = true;
  sDexSpecies = species;
  ImGui::SetScrollY(0.0f);
}

// This area | Pokédex; true when the Pokédex is open
bool DexSwitch() {
  const char *labels[2] = {"This area", "Pok\xC3\xA9" "dex"};
  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = S(6.0f);
  const float w = (width - gap) * 0.5f;
  const float h = S(34.0f);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  for (int i = 0; i < 2; i++) {
    if (i > 0) {
      ImGui::SameLine(0.0f, gap);
    }
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(i);
    if (Tap("areaview", ImVec2(w, h))) {
      if (i == 1 && sDexOpen && sDexSpecies != 0) {
        sDexSpecies = 0; // Pokédex again: back to the list
        sDexBackCount = 0;
      }
      sDexOpen = i == 1;
    }
    ImGui::PopID();
    const bool selected = (i == 1) == sDexOpen;
    draw->AddRectFilled(pos, ImVec2(pos.x + w, pos.y + h), selected ? kAccent : kPanel, S(12.0f));
    ImVec2 size = TextSize(sBold, S(16.0f), labels[i]);
    TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + (w - size.x) * 0.5f, pos.y + (h - size.y) * 0.5f),
           selected ? IM_COL32(16, 24, 34, 255) : kText, labels[i]);
  }
  ImGui::Dummy(ImVec2(0, S(2.0f)));
  return sDexOpen;
}

// Caught: a ball; seen: a ring; else a dot
void SeenMark(ImDrawList *draw, ImVec2 center, u16 species) {
  if (Gear_SpeciesCaught(species)) {
    PokeBall(draw, center, S(7.0f));
  } else if (Gear_SpeciesSeen(species)) {
    draw->AddCircle(center, S(6.0f), kTextDim, 0, S(2.0f));
  } else {
    draw->AddCircleFilled(center, S(2.5f), kTextDim);
  }
}

void DrawDexList() {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (sDexOrderCount == 0) {
    sDexOrderCount = Gear_SpeciesByName(sDexOrder, MAX_SPECIES);
  }
  const float width = ImGui::GetContentRegionAvail().x;

  // First letters: All, A-Z, in two rows
  const float gap = S(4.0f);
  const int perRow = 14;
  const float bw = (width - gap * (perRow - 1)) / perRow;
  const float bh = S(32.0f);
  for (int i = 0; i < 27; i++) {
    if (i % perRow != 0) {
      ImGui::SameLine(0.0f, gap);
    }
    const int letter = i == 0 ? 0 : 'A' + i - 1;
    char label[4];
    snprintf(label, sizeof(label), "%s", i == 0 ? "All" : "");
    if (i > 0) {
      label[0] = (char)letter;
      label[1] = '\0';
    }
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushID(i);
    if (Tap("letter", ImVec2(bw, bh))) {
      sDexLetter = letter;
      ImGui::SetScrollY(0.0f);
    }
    ImGui::PopID();
    const bool selected = sDexLetter == letter;
    draw->AddRectFilled(pos, ImVec2(pos.x + bw, pos.y + bh), selected ? kAccent : kPanel, S(8.0f));
    const float fontSize = i == 0 ? S(13.0f) : S(15.0f);
    ImVec2 size = TextSize(sBold, fontSize, label);
    TextAt(draw, sBold, fontSize, ImVec2(pos.x + (bw - size.x) * 0.5f, pos.y + (bh - size.y) * 0.5f),
           selected ? IM_COL32(16, 24, 34, 255) : kText, label);
  }

  // The species to list
  static u16 shown[MAX_SPECIES];
  int count = 0;
  for (int i = 0; i < sDexOrderCount; i++) {
    const u16 species = sDexOrder[i];
    const char *name = Gear_SpeciesName(species);
    if (sDexLetter != 0 && name[0] != sDexLetter) {
      continue;
    }
    if (Discovery() != GEAR_DISCOVERY_EVERYTHING && !Gear_SpeciesSeen(species)) {
      continue;
    }
    shown[count++] = species;
  }
  ImGui::Dummy(ImVec2(0, S(2.0f)));
  if (count == 0) {
    ImGui::TextDisabled(Discovery() == GEAR_DISCOVERY_EVERYTHING ? "No Pokemon by that letter."
                                                                 : "No Pokemon you've seen by that letter yet.");
    return;
  }
  ImGui::TextColored(ImColor(kTextDim), "%d Pokemon%s", count,
                     Discovery() == GEAR_DISCOVERY_EVERYTHING ? "" : " you've seen (the Guide hides the rest)");

  const float rowHeight = S(36.0f);
  const float iconSize = PixelSize(S(32.0f), 32.0f);
  ImGuiListClipper clipper;
  clipper.Begin(count, rowHeight + ImGui::GetStyle().ItemSpacing.y);
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
      const u16 species = shown[i];
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImGui::PushID(species);
      const bool tapped = Tap("species", ImVec2(width, rowHeight));
      const bool held = ImGui::IsItemActive() && !sDragging;
      ImGui::PopID();
      draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowHeight), held ? kPanelPressed : kPanel, S(8.0f));
      SeenMark(draw, ImVec2(pos.x + S(16.0f), pos.y + rowHeight * 0.5f), species);
      DrawIcon(draw, ImVec2(pos.x + S(28.0f), pos.y + (rowHeight - iconSize) * 0.5f - S(2.0f)), iconSize, species, 0, false);
      TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + S(66.0f), pos.y + S(8.0f)), kText, Gear_SpeciesName(species));
      char number[8];
      snprintf(number, sizeof(number), "#%03u", (unsigned)species);
      TextAt(draw, sRegular, S(13.0f), ImVec2(pos.x + S(66.0f) + TextSize(sBold, S(16.0f), Gear_SpeciesName(species)).x + S(8.0f), pos.y + S(10.0f)),
             kTextDim, number);
      u8 t1, t2;
      Gear_SpeciesTypes(species, &t1, &t2);
      float chipsWidth = TypeChip(draw, ImVec2(0, -1000.0f), t1, S(20.0f)) + (t2 != t1 ? S(6.0f) + TypeChip(draw, ImVec2(0, -1000.0f), t2, S(20.0f)) : 0.0f);
      TypeChips(draw, ImVec2(pos.x + width - S(8.0f) - chipsWidth, pos.y + (rowHeight - S(20.0f)) * 0.5f), t1, t2, S(20.0f));
      if (tapped) {
        OpenSpecies(species);
      }
    }
  }
  clipper.End();
}

// Section heading inside a species page
void DexHeading(const char *text) {
  ImGui::Dummy(ImVec2(0, S(6.0f)));
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextColored(ImColor(kAccent), "%s", text);
  ImGui::PopFont();
}

// Moves as wrapped names in their type's colour
void WrappedMoves(const u16 *moves, int count, const char *const *prefixes) {
  bool first = true;
  for (int i = 0; i < count; i++) {
    GearMove info;
    Gear_MoveInfo(moves[i], &info);
    char text[48];
    snprintf(text, sizeof(text), "%s%s", prefixes != nullptr ? prefixes[i] : "", Gear_MoveName(moves[i]));
    if (!first) {
      ImGui::SameLine(0.0f, S(4.0f));
      ImGui::TextColored(ImColor(kTextDim), "/");
      ImGui::SameLine(0.0f, S(4.0f));
      if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(text).x) {
        ImGui::NewLine();
      }
    }
    ImGui::TextColored(ImColor(TypeColor(info.type)), "%s", text);
    first = false;
  }
}

void DrawSpeciesPage() {
  if (sDexLoaded != sDexSpecies) {
    Gear_GetSpecies(sDexSpecies, &sDexData);
    sDexLoaded = sDexSpecies;
  }
  const GearSpecies &d = sDexData;
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;

  // Back: the page before, else the list
  ImVec2 pos = ImGui::GetCursorScreenPos();
  char backLabel[40];
  snprintf(backLabel, sizeof(backLabel), "< %s", sDexBackCount > 0 ? Gear_SpeciesName(sDexBack[sDexBackCount - 1]) : "All Pokemon");
  const float backWidth = TextSize(sBold, S(15.0f), backLabel).x + S(28.0f);
  if (Tap("dexback", ImVec2(backWidth, S(34.0f)))) {
    sDexSpecies = sDexBackCount > 0 ? sDexBack[--sDexBackCount] : 0;
    ImGui::SetScrollY(0.0f);
  }
  draw->AddRectFilled(pos, ImVec2(pos.x + backWidth, pos.y + S(34.0f)), kPanel, S(14.0f));
  TextAt(draw, sBold, S(15.0f), ImVec2(pos.x + S(14.0f), pos.y + S(8.0f)), kText, backLabel);

  // Portrait at the top right
  const float portrait = PixelSize(S(120.0f), 80.0f);
  const float portraitBottom = pos.y + portrait;
  DrawFront(draw, ImVec2(pos.x + width - portrait, pos.y), portrait, d.species, 0, false, false, 0, 0);

  ImGui::Dummy(ImVec2(0, S(2.0f)));
  ImGui::PushFont(sBold, S(24.0f));
  ImGui::TextUnformatted(Gear_SpeciesName(d.species));
  ImGui::PopFont();
  ImGui::SameLine();
  if (d.sinnohNumber != 0) {
    ImGui::TextColored(ImColor(kTextDim), "#%03u  Sinnoh #%03u", (unsigned)d.species, (unsigned)d.sinnohNumber);
  } else {
    ImGui::TextColored(ImColor(kTextDim), "#%03u", (unsigned)d.species);
  }
  pos = ImGui::GetCursorScreenPos();
  float chips = TypeChips(draw, pos, d.types[0], d.types[1], S(24.0f));
  ImGui::Dummy(ImVec2(chips, S(28.0f)));
  ImGui::TextColored(ImColor(Gear_SpeciesCaught(d.species) ? kGood : kTextDim), "%s",
                     Gear_SpeciesCaught(d.species) ? "Caught" : (Gear_SpeciesSeen(d.species) ? "Seen, not caught" : "Not seen yet"));
  if (ImGui::GetCursorScreenPos().y < portraitBottom) {
    ImGui::Dummy(ImVec2(0, portraitBottom - ImGui::GetCursorScreenPos().y));
  }

  // Base stats
  DexHeading("Base stats");
  const char *statNames[6] = {"HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed"};
  int total = 0;
  for (int i = 0; i < 6; i++) {
    total += d.baseStats[i];
    pos = ImGui::GetCursorScreenPos();
    TextAt(draw, sRegular, S(15.0f), pos, kTextDim, statNames[i]);
    char value[8];
    snprintf(value, sizeof(value), "%d", d.baseStats[i]);
    TextAt(draw, sBold, S(15.0f), ImVec2(pos.x + S(80.0f), pos.y), kText, value);
    const float barX = pos.x + S(118.0f);
    const int v = d.baseStats[i];
    const ImU32 color = v >= 120 ? kGood : (v >= 80 ? IM_COL32(150, 210, 90, 255) : (v >= 50 ? kWarn : kBad));
    Bar(draw, ImVec2(barX, pos.y + S(5.0f)), ImVec2(width - (barX - pos.x), S(9.0f)), v / 200.0f, color);
    ImGui::Dummy(ImVec2(width, S(20.0f)));
  }
  ImGui::TextColored(ImColor(kTextDim), "Total %d", total);

  // Abilities, breeding and the rest
  DexHeading("About");
  if (d.abilities[1] != 0 && d.abilities[1] != d.abilities[0]) {
    ImGui::Text("Abilities: %s / %s", Gear_AbilityName(d.abilities[0]), Gear_AbilityName(d.abilities[1]));
  } else {
    ImGui::Text("Ability: %s", Gear_AbilityName(d.abilities[0]));
  }
  char gender[32];
  if (d.genderRatio == 255) {
    snprintf(gender, sizeof(gender), "Genderless");
  } else if (d.genderRatio == 254) {
    snprintf(gender, sizeof(gender), "Always female");
  } else if (d.genderRatio == 0) {
    snprintf(gender, sizeof(gender), "Always male");
  } else {
    snprintf(gender, sizeof(gender), "%d%% male", (int)((254 - d.genderRatio) * 100 + 127) / 254);
  }
  ImGui::Text("%s  Catch rate %d  %s", gender, d.catchRate, Gear_ExpRateName(d.expRate));
  if (d.eggGroups[1] != d.eggGroups[0]) {
    ImGui::Text("Egg groups: %s / %s, hatches in %d steps", Gear_EggGroupName(d.eggGroups[0]),
                Gear_EggGroupName(d.eggGroups[1]), (d.hatchCycles + 1) * 255);
  } else {
    ImGui::Text("Egg group: %s, hatches in %d steps", Gear_EggGroupName(d.eggGroups[0]), (d.hatchCycles + 1) * 255);
  }
  char evs[64] = "";
  size_t used = 0;
  const char *evNames[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};
  for (int i = 0; i < 6; i++) {
    if (d.evYield[i] > 0) {
      used += snprintf(evs + used, sizeof(evs) - used, "%s%d %s", used ? ", " : "", d.evYield[i], evNames[i]);
    }
  }
  ImGui::Text("EVs: %s", evs[0] ? evs : "-");
  if (d.heldItems[0] != 0 || d.heldItems[1] != 0) {
    if (d.heldItems[0] != 0 && d.heldItems[0] == d.heldItems[1]) {
      ImGui::Text("Wild ones hold: %s (always)", Gear_ItemName(d.heldItems[0]));
    } else if (d.heldItems[0] != 0 && d.heldItems[1] != 0) {
      ImGui::Text("Wild ones hold: %s (50%%), %s (5%%)", Gear_ItemName(d.heldItems[0]), Gear_ItemName(d.heldItems[1]));
    } else {
      ImGui::Text("Wild ones hold: %s (%s)", Gear_ItemName(d.heldItems[0] ? d.heldItems[0] : d.heldItems[1]),
                  d.heldItems[0] ? "50%" : "5%");
    }
  }

  // Evolution: from, into (tap to open)
  if (d.preEvoCount > 0 || d.evoCount > 0) {
    DexHeading("Evolution");
    const float iconSize = PixelSize(S(32.0f), 32.0f);
    auto row = [&](const GearDexEvolution &evo, const char *direction) {
      char how[96];
      Gear_EvolutionText(&evo, how, sizeof(how));
      ImVec2 p = ImGui::GetCursorScreenPos();
      ImGui::PushID(evo.species);
      ImGui::PushID(direction);
      const bool tapped = Tap("evo", ImVec2(width, S(38.0f)));
      const bool held = ImGui::IsItemActive() && !sDragging;
      ImGui::PopID();
      ImGui::PopID();
      draw->AddRectFilled(p, ImVec2(p.x + width, p.y + S(38.0f)), held ? kPanelPressed : kPanel, S(8.0f));
      const bool known = DexCanShow(evo.species);
      DrawIcon(draw, ImVec2(p.x + S(4.0f), p.y + (S(38.0f) - iconSize) * 0.5f - S(2.0f)), iconSize, evo.species, 0, false, 0.0f,
               known ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 255));
      char text[128];
      snprintf(text, sizeof(text), "%s %s", direction, known ? Gear_SpeciesName(evo.species) : "???");
      TextAt(draw, sBold, S(15.0f), ImVec2(p.x + S(42.0f), p.y + S(3.0f)), kText, text);
      TextAt(draw, sRegular, S(13.0f), ImVec2(p.x + S(42.0f), p.y + S(20.0f)), kTextDim, how);
      if (tapped && known) {
        OpenSpecies(evo.species);
      }
    };
    for (int i = 0; i < d.preEvoCount; i++) {
      row(d.preEvos[i], "From");
    }
    for (int i = 0; i < d.evoCount; i++) {
      row(d.evos[i], "Into");
    }
  }

  // Where to find it
  DexHeading("Where to find it");
  if (d.placeCount == 0) {
    ImGui::TextDisabled("Not in the wild: an egg, an evolution, a gift or a one-off.");
  }
  // One row per place, its ways of finding it under the name
  bool listed[GEAR_DEX_MAX_PLACES] = {};
  for (int i = 0; i < d.placeCount; i++) {
    if (listed[i]) {
      continue;
    }
    char detail[256] = "";
    size_t used = 0;
    for (int j = i; j < d.placeCount; j++) {
      const GearDexPlace &place = d.places[j];
      if (place.label != d.places[i].label || listed[j]) {
        continue;
      }
      listed[j] = true;
      char levels[16];
      if (place.minLevel == place.maxLevel) {
        snprintf(levels, sizeof(levels), "Lv %d", place.minLevel);
      } else {
        snprintf(levels, sizeof(levels), "Lv %d-%d", place.minLevel, place.maxLevel);
      }
      char rate[48] = "";
      const u8 *r = place.rates;
      if (place.method == GEAR_ENC_GBA) {
        static const char *const kGames[5] = {"Ruby", "Sapphire", "Emerald", "FireRed", "LeafGreen"};
        size_t n = 0;
        for (int g = 0; g < 5; g++) {
          if (place.gbaGames & (1 << g)) {
            n += snprintf(rate + n, sizeof(rate) - n, "%s%s", n ? "/" : "", kGames[g]);
          }
        }
      } else if (r[0] == r[1] && r[1] == r[2]) {
        if (r[0] > 0) {
          snprintf(rate, sizeof(rate), "%d%%", r[0]);
        }
      } else {
        size_t n = 0;
        const char *times[3] = {"morning", "day", "night"};
        for (int t = 0; t < 3; t++) {
          if (r[t] > 0) {
            n += snprintf(rate + n, sizeof(rate) - n, "%s%d%% %s", n ? ", " : "", r[t], times[t]);
          }
        }
      }
      if (used < sizeof(detail)) {
        used += snprintf(detail + used, sizeof(detail) - used, "%s%s %s%s%s", used ? "\n" : "", MethodName(place.method),
                         levels, rate[0] ? "  " : "", rate);
      }
    }
    int lines = 1;
    for (const char *c = detail; *c; c++) {
      lines += *c == '\n';
    }
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = S(22.0f) + lines * S(17.0f);
    draw->AddRectFilled(p, ImVec2(p.x + width, p.y + h), kPanel, S(8.0f));
    TextAt(draw, sBold, S(15.0f), ImVec2(p.x + S(10.0f), p.y + S(3.0f)), kText, Gear_LocationLabelName(d.places[i].label));
    TextAt(draw, sRegular, S(13.0f), ImVec2(p.x + S(10.0f), p.y + S(21.0f)), kTextDim, detail);
    ImGui::Dummy(ImVec2(width, h));
  }

  // Moves
  DexHeading("Level-up moves");
  for (int i = 0; i < d.levelMoveCount; i++) {
    GearMove info;
    Gear_MoveInfo(d.levelMoves[i].move, &info);
    ImVec2 p = ImGui::GetCursorScreenPos();
    char level[8];
    snprintf(level, sizeof(level), "%d", d.levelMoves[i].level);
    TextAt(draw, sRegular, S(14.0f), ImVec2(p.x, p.y + S(3.0f)), kTextDim, level);
    float chip = TypeChip(draw, ImVec2(p.x + S(30.0f), p.y + S(1.0f)), info.type, S(20.0f));
    TextAt(draw, sBold, S(15.0f), ImVec2(p.x + S(36.0f) + chip, p.y + S(2.0f)), kText, Gear_MoveName(info.id));
    char stats[24];
    const char *category = info.category == 0 ? "Phys" : (info.category == 1 ? "Spec" : "Status");
    if (info.power > 1) {
      snprintf(stats, sizeof(stats), "%s %d", category, info.power);
    } else {
      snprintf(stats, sizeof(stats), "%s", category);
    }
    ImVec2 statsSize = TextSize(sRegular, S(13.0f), stats);
    TextAt(draw, sRegular, S(13.0f), ImVec2(p.x + width - statsSize.x, p.y + S(4.0f)), kTextDim, stats);
    ImGui::Dummy(ImVec2(width, S(20.0f)));
  }
  if (d.tmCount > 0) {
    DexHeading("TMs and HMs");
    static u16 moves[100];
    static char prefixBuf[100][8];
    static const char *prefixes[100];
    for (int i = 0; i < d.tmCount; i++) {
      moves[i] = Gear_TMMove(d.tms[i]);
      if (d.tms[i] < 92) {
        snprintf(prefixBuf[i], sizeof(prefixBuf[i]), "TM%02d ", d.tms[i] + 1);
      } else {
        snprintf(prefixBuf[i], sizeof(prefixBuf[i]), "HM%02d ", d.tms[i] - 91);
      }
      prefixes[i] = prefixBuf[i];
    }
    WrappedMoves(moves, d.tmCount, prefixes);
  }
  if (d.eggMoveCount > 0) {
    DexHeading("Egg moves");
    WrappedMoves(d.eggMoves, d.eggMoveCount, nullptr);
  }
  if (d.tutorMoveCount > 0) {
    DexHeading("Move tutors");
    WrappedMoves(d.tutorMoves, d.tutorMoveCount, nullptr);
  }
}

void DrawDex() {
  if (sDexSpecies != 0) {
    DrawSpeciesPage();
  } else {
    DrawDexList();
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

// What one of a battler's moves does to a target, from the battle's own damage formula
struct DamageEstimate {
  bool known = false; // false: status move, or power the battle only knows when it's used
  int low = 0;        // the whole move (every hit), no critical hit
  int high = 0;
  int hits = 1;       // 0: 2 to 5 hits (low/high are per hit then)
  bool ko = false;    // knocks the target out whatever the roll
  bool maybeKo = false;
};

DamageEstimate EstimateFor(int attacker, int slot, int moveId, int target) {
  DamageEstimate e;
  if (attacker < 0 || attacker >= GEAR_BATTLERS_MAX || target < 0 || target >= GEAR_BATTLERS_MAX || slot < 0 || slot > 3) {
    return e;
  }
  const GearBattler &a = sBattle.battlers[attacker];
  const GearBattler &t = sBattle.battlers[target];
  if (!a.present || !t.present || a.moves[slot].id != moveId || a.damageMin[slot][target] < 0) {
    return e;
  }
  e.known = true;
  e.hits = a.moveHits[slot];
  const int perHits = e.hits > 1 ? e.hits : 1;
  e.low = a.damageMin[slot][target] * perHits;
  e.high = a.damageMax[slot][target] * perHits;
  if (t.hp > 0 && e.high > 0) {
    const int bestLow = e.hits == 0 ? e.low * 2 : e.low;
    const int bestHigh = e.hits == 0 ? e.high * 5 : e.high;
    e.ko = bestLow >= t.hp;
    e.maybeKo = !e.ko && bestHigh >= t.hp;
  }
  return e;
}

// "34-41%" of the target's full HP (rounded outwards); "x2-5" after a per-hit range
void PercentText(const DamageEstimate &e, int maxHp, char *out, size_t size) {
  if (e.high == 0) {
    snprintf(out, size, "0%%");
    return;
  }
  const int hp = maxHp > 0 ? maxHp : 1;
  const int low = e.low * 100 / hp;
  int high = (e.high * 100 + hp - 1) / hp;
  const char *suffix = e.hits == 0 ? " x2-5" : "";
  if (low == high) {
    snprintf(out, size, "%d%%%s", low, suffix);
  } else {
    snprintf(out, size, "%d-%d%%%s", low, high, suffix);
  }
}

// "34-41 HP (52-63%)"
void ExactText(const DamageEstimate &e, int maxHp, char *out, size_t size) {
  char percent[24];
  PercentText(e, maxHp, percent, sizeof(percent));
  if (e.low == e.high) {
    snprintf(out, size, "%d HP%s (%s)", e.low, e.hits == 0 ? " a hit" : "", percent);
  } else {
    snprintf(out, size, "%d-%d HP%s (%s)", e.low, e.high, e.hits == 0 ? " a hit" : "", percent);
  }
}

// Who moves first between two battlers using moves of the same priority, as
// BattleSystem_CompareBattlerSpeed: 1 a, -1 b, 0 a speed tie (decided at random). Quick
// Claw and Custap Berry aren't counted.
int FasterOf(const GearBattler &a, const GearBattler &b) {
  int order = a.speed > b.speed ? 1 : (a.speed < b.speed ? -1 : 0);
  // Lagging Tail (Full Incense), then Stall: the holder goes last, or if both have it the
  // slower one goes first. Otherwise Trick Room puts the slower one first.
  for (int kind = 2; kind >= 1; kind--) {
    const bool aLast = a.movesLast == kind;
    const bool bLast = b.movesLast == kind;
    if (aLast && bLast) {
      return -order;
    }
    if (aLast != bLast) {
      return aLast ? -1 : 1;
    }
  }
  return sBattle.trickRoom ? -order : order;
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
  // Its icon at the left
  const float iconSize = PixelSize(S(40.0f), 32.0f);
  DrawIcon(draw, ImVec2(pos.x + S(2.0f), pos.y + (size.y - iconSize) * 0.5f - S(2.0f)), iconSize, b.species, b.form, false,
           0.0f, fainted ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
  const float left = pos.x + S(4.0f) + iconSize;
  float x = left;
  const float line1 = pos.y + S(5.0f);
  if (side != nullptr) {
    x += Badge(draw, ImVec2(x, line1 + S(1.0f)), side, kAccent, S(13.0f)) + S(6.0f);
  }
  if (b.enemy && !sBattle.trainer && Gear_SpeciesCaught(b.species)) {
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
  } else if (!b.enemy || discovery == GEAR_DISCOVERY_EVERYTHING) {
    snprintf(hp, sizeof(hp), "%d/%d", b.hp, b.maxHp);
  } else {
    snprintf(hp, sizeof(hp), "%d%%", percent);
  }
  ImVec2 hpSize = TextSize(sBold, S(14.0f), hp);
  TextAt(draw, sBold, S(14.0f), ImVec2(pos.x + size.x - pad - hpSize.x, line1 + S(1.5f)), fainted ? kBad : kText, hp);

  const float line2 = pos.y + S(25.0f);
  float x2 = left;
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

// One of the active Pokemon's moves and how it fares against each opponent: the type
// matchup and, with the guide on, the damage it does (and whether it knocks them out)
void DrawBattleMove(int attacker, int slot, const GearMove &move, const int *opponents, int opponentCount, ImVec2 size) {
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
  const char *name = Gear_MoveName(move.id);
  TextAt(draw, sBold, S(15.0f), ImVec2(x, pos.y + S(5.0f)), kText, name);

  // Priority (moves first or last whatever the speed)
  int priority = 0;
  if (attacker >= 0 && attacker < GEAR_BATTLERS_MAX && slot >= 0 && slot < 4 &&
      sBattle.battlers[attacker].moves[slot].id == move.id) {
    priority = sBattle.battlers[attacker].movePriority[slot];
  }
  if (priority != 0 && discovery != GEAR_DISCOVERY_VANILLA) {
    char text[8];
    snprintf(text, sizeof(text), "%+d", priority);
    const float nameWidth = TextSize(sBold, S(15.0f), name).x;
    TextAt(draw, sBold, S(13.0f), ImVec2(x + nameWidth + S(6.0f), pos.y + S(6.5f)), priority > 0 ? kGood : kResisted, text);
  }

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
    const DamageEstimate e = EstimateFor(attacker, slot, move.id, opponents[0]);
    Matchup matchup = MatchupFor(Gear_TypeEffectiveness(move.type, target.type1, target.type2));
    if (e.known && e.high == 0) {
      matchup = MatchupFor(0); // an ability or Levitate takes it
    }
    char text[48];
    if (e.known && e.high > 0) {
      char percent[24];
      PercentText(e, target.maxHp, percent, sizeof(percent));
      snprintf(text, sizeof(text), "%s  %s", matchup.shortLabel, percent);
    } else {
      snprintf(text, sizeof(text), "%s  %s", matchup.shortLabel, matchup.words);
    }
    TextAt(draw, sBold, S(13.0f), ImVec2(x, line2), matchup.color, text);
    if (e.ko || e.maybeKo) {
      const char *label = e.ko ? "KO" : "KO?";
      const float badgeWidth = TextSize(sBold, S(12.0f), label).x + S(12.0f) * 0.7f;
      Badge(draw, ImVec2(pos.x + size.x - pad - badgeWidth, line2), label, e.ko ? kGood : IM_COL32(176, 140, 40, 255),
            S(12.0f));
    }
  } else {
    float tx = x;
    const char *sides[2] = {"L", "R"};
    for (int i = 0; i < opponentCount; i++) {
      const GearBattler &target = sBattle.battlers[opponents[i]];
      char text[32];
      ImU32 color = kTextDim;
      if (target.hp == 0) {
        snprintf(text, sizeof(text), "%s -", sides[i]);
      } else {
        const DamageEstimate e = EstimateFor(attacker, slot, move.id, opponents[i]);
        Matchup matchup = MatchupFor(Gear_TypeEffectiveness(move.type, target.type1, target.type2));
        if (e.known && e.high == 0) {
          matchup = MatchupFor(0);
        }
        color = matchup.color;
        if (e.ko) {
          snprintf(text, sizeof(text), "%s %s KO", sides[i], matchup.shortLabel);
          color = kGood;
        } else if (e.known && e.high > 0) {
          char percent[24];
          PercentText(e, target.maxHp, percent, sizeof(percent));
          snprintf(text, sizeof(text), "%s %s %s", sides[i], matchup.shortLabel, percent);
        } else {
          snprintf(text, sizeof(text), "%s %s", sides[i], matchup.shortLabel);
        }
      }
      TextAt(draw, sBold, S(13.0f), ImVec2(tx, line2), color, text);
      tx += TextSize(sBold, S(13.0f), text).x + S(12.0f);
    }
  }

  ImGui::Dummy(size);
}

// Who moves first against each opponent (same priority): a line above the moves
void DrawTurnOrder(int mineIndex, const int *opponents, int opponentCount) {
  if (Discovery() == GEAR_DISCOVERY_VANILLA || mineIndex < 0 || mineIndex >= GEAR_BATTLERS_MAX) {
    return;
  }
  const GearBattler &mine = sBattle.battlers[mineIndex];
  if (!mine.present || mine.hp == 0) {
    return;
  }
  const bool exact = Discovery() == GEAR_DISCOVERY_EVERYTHING;
  char text[160];
  size_t used = 0;
  text[0] = '\0';
  ImU32 color = kTextDim;
  int shown = 0;
  const char *sides[2] = {"L", "R"};
  for (int i = 0; i < opponentCount; i++) {
    const GearBattler &foe = sBattle.battlers[opponents[i]];
    if (foe.hp == 0) {
      continue;
    }
    const int order = FasterOf(mine, foe);
    const char *what = order > 0 ? "you first" : (order < 0 ? "they go first" : "speed tie");
    if (opponentCount == 1) {
      what = order > 0 ? "You move first" : (order < 0 ? "They move first" : "Speed tie (random)");
      color = order > 0 ? kGood : (order < 0 ? kResisted : kWarn);
    }
    if (used < sizeof(text)) {
      used += snprintf(text + used, sizeof(text) - used, "%s%s%s%s", shown ? "   " : "",
                       opponentCount > 1 ? sides[i] : "", opponentCount > 1 ? ": " : "", what);
    }
    if (exact && used < sizeof(text)) {
      used += snprintf(text + used, sizeof(text) - used, " (%u vs %u)", (unsigned)mine.speed, (unsigned)foe.speed);
    }
    shown++;
  }
  if (shown == 0) {
    return;
  }
  if (sBattle.trickRoom && used < sizeof(text)) {
    snprintf(text + used, sizeof(text) - used, "   Trick Room");
  }
  ImGui::PushFont(sBold, S(13.0f));
  ImGui::TextColored(ImColor(color), "%s", text);
  ImGui::PopFont();
}

void SectionLabel(const char *text) {
  ImGui::PushFont(sBold, S(14.0f));
  ImGui::TextColored(ImColor(kAccent), "%s", text);
  ImGui::PopFont();
}

// A battler's level, HP, stat changes, ability and item; with showMoves its moves, and
// what each does to the targets
void DrawBattlerDetails(int index, bool showMoves, const int *targets, int targetCount) {
  const GearBattler &b = sBattle.battlers[index];
  const int discovery = Discovery();
  ImGui::PushID(index);
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
  if (discovery == GEAR_DISCOVERY_EVERYTHING && b.hp > 0) {
    ImGui::TextColored(ImColor(kTextDim), "Speed in battle: %u%s", (unsigned)b.speed,
                       b.movesLast == 2 ? " (moves last: item)" : (b.movesLast == 1 ? " (moves last: Stall)" : ""));
  }
  if (showMoves) {
    const char *sides[2] = {"L", "R"};
    for (int m = 0; m < 4; m++) {
      const GearMove &move = b.moves[m];
      if (move.id == 0) {
        continue;
      }
      ImGui::TextColored(ImColor(TypeColor(move.type)), "%s", Gear_MoveName(move.id));
      ImGui::SameLine();
      ImGui::TextColored(ImColor(kTextDim), "%d/%d", move.pp, move.maxPp);
      if (b.movePriority[m] != 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImColor(b.movePriority[m] > 0 ? kGood : kResisted), "%+d", b.movePriority[m]);
      }
      if (b.hp == 0 || discovery == GEAR_DISCOVERY_VANILLA) {
        continue;
      }
      // What it does to each target
      for (int t = 0; t < targetCount; t++) {
        const GearBattler &target = sBattle.battlers[targets[t]];
        if (!target.present || target.hp == 0) {
          continue;
        }
        const DamageEstimate e = EstimateFor(index, m, move.id, targets[t]);
        if (!e.known) {
          continue;
        }
        char text[64];
        const bool exact = discovery == GEAR_DISCOVERY_EVERYTHING || !target.enemy;
        if (e.high == 0) {
          snprintf(text, sizeof(text), "no effect");
        } else if (exact) {
          ExactText(e, target.maxHp, text, sizeof(text));
        } else {
          PercentText(e, target.maxHp, text, sizeof(text));
        }
        ImGui::SameLine(0.0f, S(10.0f));
        if (targetCount > 1) {
          ImGui::TextColored(ImColor(kTextDim), "%s", sides[t]);
          ImGui::SameLine(0.0f, S(4.0f));
        }
        ImGui::TextColored(ImColor(e.ko ? kGood : (e.maybeKo ? kWarn : kText)), "%s%s", text,
                           e.ko ? " KO" : (e.maybeKo ? " may KO" : ""));
      }
    }
  }
  ImGui::PopID();
}

// --- battle menu (the DS bottom screen's, on the Gear) ----------------------------------

constexpr ImU32 kFightColor = IM_COL32(206, 68, 62, 255);
constexpr ImU32 kBagColor = IM_COL32(214, 150, 46, 255);
constexpr ImU32 kPokemonColor = IM_COL32(62, 158, 86, 255);
constexpr ImU32 kRunColor = IM_COL32(64, 112, 200, 255);
constexpr ImU32 kCursorColor = IM_COL32(255, 214, 102, 255);

ImU32 Darker(ImU32 color) {
  ImVec4 c = ImColor(color).Value;
  return ImColor(c.x * 0.75f, c.y * 0.75f, c.z * 0.75f, c.w);
}

// Outline where the game's own cursor (D-pad) is
void CursorOutline(ImDrawList *draw, ImVec2 pos, ImVec2 size, int button) {
  if (sMenu.cursor == button) {
    draw->AddRect(ImVec2(pos.x - S(2.0f), pos.y - S(2.0f)), ImVec2(pos.x + size.x + S(2.0f), pos.y + size.y + S(2.0f)),
                  kCursorColor, S(12.0f), 0, S(3.0f));
  }
}

// A filled button of the battle menu
void MenuButton(int button, const char *label, const char *sub, ImU32 color, ImVec2 size, bool enabled = true) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(button);
  const bool pressed = Tap("menu", size);
  ImGui::PopID();
  const bool held = ImGui::IsItemActive() && !sDragging;
  const ImU32 fill = !enabled ? kPanel : (held ? Darker(color) : color);
  draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), fill, S(12.0f));

  const ImU32 textColor = enabled ? IM_COL32(255, 255, 255, 255) : kTextDim;
  ImVec2 labelSize = TextSize(sBold, S(19.0f), label);
  float textHeight = labelSize.y;
  ImVec2 subSize(0, 0);
  if (sub != nullptr && sub[0] != '\0') {
    subSize = TextSize(sRegular, S(13.0f), sub);
    textHeight += S(2.0f) + subSize.y;
  }
  float y = pos.y + (size.y - textHeight) * 0.5f;
  TextAt(draw, sBold, S(19.0f), ImVec2(pos.x + (size.x - labelSize.x) * 0.5f, y), textColor, label);
  if (subSize.y > 0) {
    TextAt(draw, sRegular, S(13.0f), ImVec2(pos.x + (size.x - subSize.x) * 0.5f, y + labelSize.y + S(2.0f)),
           enabled ? IM_COL32(255, 255, 255, 200) : kTextDim, sub);
  }
  CursorOutline(draw, pos, size, button);
  if (pressed && enabled) {
    Gear_PressBattleButton(button);
  }
}

// Draws over the last item: a pressed look and the game's cursor
void PressOverlay(ImVec2 pos, ImVec2 size, int button, bool enabled) {
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (!enabled) {
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(24, 29, 38, 150), S(10.0f));
  }
  CursorOutline(draw, pos, size, button);
}

// A tappable move of the move menu, with its matchups
void MoveMenuButton(int button, int attacker, const GearMove &move, const int *opponents, int opponentCount, ImVec2 size) {
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(button);
  const bool pressed = Tap("move", size);
  ImGui::PopID();
  const bool held = ImGui::IsItemActive() && !sDragging;
  ImGui::SetCursorScreenPos(pos);
  DrawBattleMove(attacker, button - 1, move, opponents, opponentCount, size);
  if (held) {
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 30),
                                              S(10.0f));
  }
  PressOverlay(pos, size, button, move.id != 0);
  if (pressed && move.id != 0) {
    Gear_PressBattleButton(button);
  }
}

// A tappable target of the target menu
void TargetButton(int button, ImVec2 size) {
  const int battler = sMenu.targetBattler[button];
  const bool present = battler >= 0 && battler < GEAR_BATTLERS_MAX && sBattle.battlers[battler].present;
  const bool enabled = present && sMenu.targetOk[button];
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(button);
  const bool pressed = Tap("target", size);
  ImGui::PopID();
  ImGui::SetCursorScreenPos(pos);
  if (present) {
    DrawOpponentCard(battler, sBattle.battlers[battler].enemy ? "Foe" : "Ally", size);
  } else {
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), kPanel, S(10.0f));
    ImGui::Dummy(size);
  }
  if (ImGui::IsItemActive() && enabled) {
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), IM_COL32(255, 255, 255, 30),
                                              S(10.0f));
  }
  PressOverlay(pos, size, button, enabled);
  if (pressed && enabled) {
    Gear_PressBattleButton(button);
  }
}

// The battle menu the game is showing, as Gear buttons. Returns FALSE when there's none.
bool DrawBattleControls(const int *opponents, int opponentCount) {
  const GearBattleMenu &m = sMenu;
  if (m.kind == GEAR_MENU_NONE) {
    return false;
  }
  const float width = ImGui::GetContentRegionAvail().x;
  const float gap = S(6.0f);
  ImGui::Dummy(ImVec2(0, S(2.0f)));

  switch (m.kind) {
  case GEAR_MENU_ACTION: {
    const char *labels[4] = {"Fight", "Bag", "Pokemon", m.cancel ? "Cancel" : "Run"};
    if (m.safari) {
      labels[0] = "Ball";
      labels[1] = "Bait";
      labels[2] = "Mud";
    }
    MenuButton(0, labels[0], nullptr, kFightColor, ImVec2(width, S(78.0f)));
    const float third = (width - gap * 2) / 3.0f;
    MenuButton(1, labels[1], nullptr, kBagColor, ImVec2(third, S(58.0f)));
    ImGui::SameLine(0.0f, gap);
    MenuButton(3, labels[3], nullptr, m.cancel ? IM_COL32(110, 118, 134, 255) : kRunColor, ImVec2(third, S(58.0f)));
    ImGui::SameLine(0.0f, gap);
    MenuButton(2, labels[2], nullptr, kPokemonColor, ImVec2(third, S(58.0f)));
    break;
  }
  case GEAR_MENU_FIRST:
    MenuButton(0, "Fight", nullptr, kFightColor, ImVec2(width, S(78.0f)));
    break;
  case GEAR_MENU_PAL_PARK:
    MenuButton(0, "Park Ball", nullptr, kFightColor, ImVec2(width, S(78.0f)));
    MenuButton(1, "Run", nullptr, kRunColor, ImVec2(width, S(52.0f)));
    break;
  case GEAR_MENU_MOVES: {
    const float half = (width - gap) * 0.5f;
    DrawTurnOrder(m.battler, opponents, opponentCount);
    for (int i = 0; i < 4; i++) {
      if (i % 2 != 0) {
        ImGui::SameLine(0.0f, gap);
      }
      MoveMenuButton(i + 1, m.battler, m.moves[i], opponents, opponentCount, ImVec2(half, S(60.0f)));
    }
    MenuButton(0, "Cancel", nullptr, IM_COL32(110, 118, 134, 255), ImVec2(width, S(44.0f)));
    break;
  }
  case GEAR_MENU_TARGETS: {
    const float half = (width - gap) * 0.5f;
    // As on the DS screen: the opponents above, your side below
    TargetButton(3, ImVec2(half, S(52.0f)));
    ImGui::SameLine(0.0f, gap);
    TargetButton(1, ImVec2(half, S(52.0f)));
    TargetButton(0, ImVec2(half, S(52.0f)));
    ImGui::SameLine(0.0f, gap);
    TargetButton(2, ImVec2(half, S(52.0f)));
    MenuButton(4, "Cancel", nullptr, IM_COL32(110, 118, 134, 255), ImVec2(width, S(44.0f)));
    break;
  }
  case GEAR_MENU_YES_NO:
    MenuButton(0, m.yes, nullptr, kPokemonColor, ImVec2(width, S(62.0f)));
    MenuButton(1, m.no, nullptr, IM_COL32(110, 118, 134, 255), ImVec2(width, S(62.0f)));
    break;
  case GEAR_MENU_STOP:
    MenuButton(0, "Stop watching", nullptr, kRunColor, ImVec2(width, S(56.0f)));
    break;
  default:
    return false;
  }
  return true;
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

  // With the battle menu on the Gear: your side too, then the menu
  const bool controls = BattleControls();
  const int active = sMenu.kind != GEAR_MENU_NONE && sMenu.battler >= 0 && sMenu.battler < GEAR_BATTLERS_MAX
                         ? sMenu.battler
                         : ActiveBattler();
  if (controls) {
    int mine[2];
    int mineCount = 0;
    for (int b = 0; b < GEAR_BATTLERS_MAX && mineCount < 2; b += 2) {
      if (sBattle.battlers[b].present && !sBattle.battlers[b].enemy) {
        mine[mineCount++] = b;
      }
    }
    const float myWidth = mineCount > 0 ? (width - gap * (mineCount - 1)) / mineCount : width;
    for (int i = 0; i < mineCount; i++) {
      if (i > 0) {
        ImGui::SameLine(0.0f, gap);
      }
      ImVec2 cardPos = ImGui::GetCursorScreenPos();
      DrawOpponentCard(mine[i], nullptr, ImVec2(myWidth, S(46.0f)));
      if (mineCount > 1 && mine[i] == active && sMenu.kind != GEAR_MENU_NONE) {
        // Whose command it is
        ImGui::GetWindowDrawList()->AddRect(cardPos, ImVec2(cardPos.x + myWidth, cardPos.y + S(46.0f)), kAccent, S(10.0f),
                                            0, S(2.5f));
      }
    }
  }

  if (!(controls && DrawBattleControls(opponents, opponentCount)) && active >= 0) {
    // The active Pokemon's moves, two by two
    const GearBattler &mine = sBattle.battlers[active];
    const float moveWidth = (width - gap) * 0.5f;
    DrawTurnOrder(active, opponents, opponentCount);
    for (int m = 0; m < 4; m++) {
      if (m % 2 != 0) {
        ImGui::SameLine(0.0f, gap);
      }
      ImGui::PushID(m);
      DrawBattleMove(active, m, mine.moves[m], opponents, opponentCount, ImVec2(moveWidth, S(44.0f)));
      ImGui::PopID();
    }
  }

  if (active >= 0) {
    // Below the fold in the split view: the active Pokemon itself, and with everything
    // revealed its moves' exact damage
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    SectionLabel("Your Pokemon");
    DrawBattlerDetails(active, discovery == GEAR_DISCOVERY_EVERYTHING, opponents, opponentCount);
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
    // Their moves against your side
    int targets[2];
    int targetCount = 0;
    for (int t = 0; t < GEAR_BATTLERS_MAX && targetCount < 2; t += 2) {
      if (sBattle.battlers[t].present && !sBattle.battlers[t].enemy) {
        targets[targetCount++] = t;
      }
    }
    DrawBattlerDetails(opponents[i], discovery == GEAR_DISCOVERY_EVERYTHING, targets, targetCount);
  }

  // The opposing team, with everything revealed
  if (sBattle.trainer && discovery == GEAR_DISCOVERY_EVERYTHING && sBattle.enemyPartyCount > 0) {
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    SectionLabel("Opposing team");
    const float iconSize = PixelSize(S(32.0f), 32.0f);
    for (int i = 0; i < sBattle.enemyPartyCount; i++) {
      const GearBattleMon &mon = sBattle.enemyParty[i];
      const ImVec2 rowPos = ImGui::GetCursorScreenPos();
      DrawIcon(ImGui::GetWindowDrawList(), ImVec2(rowPos.x, rowPos.y - S(8.0f)), iconSize, mon.species, 0, mon.isEgg, 0.0f,
               mon.fainted ? IM_COL32(110, 110, 110, 255) : IM_COL32_WHITE);
      ImGui::SetCursorScreenPos(ImVec2(rowPos.x + iconSize + S(4.0f), rowPos.y));
      ImGui::TextColored(ImColor(mon.fainted ? kTextDim : kText), "%s  Lv %d%s", mon.isEgg ? "Egg" : Gear_SpeciesName(mon.species),
                         mon.level, mon.fainted ? "  (fainted)" : "");
      ImGui::SetCursorScreenPos(ImVec2(rowPos.x, ImGui::GetCursorScreenPos().y + S(8.0f)));
    }
  } else if (!sBattle.trainer && opponentCount > 0 && discovery != GEAR_DISCOVERY_VANILLA) {
    const GearBattler &wild = sBattle.battlers[opponents[0]];
    ImGui::Dummy(ImVec2(0, S(4.0f)));
    ImGui::TextColored(ImColor(Gear_SpeciesCaught(wild.species) ? kGood : kWarn), "%s",
                       Gear_SpeciesCaught(wild.species) ? "Already caught" : "Not caught yet");
  }
  switch (Nuzlocke_CurrentEncounter()) {
  case NUZLOCKE_ENC_FIRST:
    ImGui::TextColored(ImColor(kGood), "Nuzlocke: this area's first encounter. This one counts!");
    break;
  case NUZLOCKE_ENC_USED: {
    const int label = Gear_CurrentLocationLabel();
    const NuzlockeArea *area = label >= 0 ? Nuzlocke_Area((u16)label) : nullptr;
    if (area != nullptr) {
      ImGui::TextColored(ImColor(kWarn), "Nuzlocke: this area's encounter was %s (%s)", Gear_SpeciesName(area->species),
                         NuzlockeResultWord(area->result));
    }
    break;
  }
  case NUZLOCKE_ENC_DUPE:
    ImGui::TextColored(ImColor(kTextDim), "Nuzlocke: already caught, so it doesn't count (dupes clause)");
    break;
  case NUZLOCKE_ENC_SHINY:
    ImGui::TextColored(ImColor(kGood), "Nuzlocke: shiny! It doesn't count, catch it (shiny clause)");
    break;
  default:
    break;
  }
  if (!sBattle.trainer && opponentCount > 0) {
    // The shiny hunt: this one counted
    const GearBattler &wild = sBattle.battlers[opponents[0]];
    char met[24];
    char total[24];
    ImGui::TextColored(ImColor(kTextDim), "%s %s met since the last shiny one (%s of any)",
                       Thousands(ShinyHunt_SpeciesCount(wild.species), met, sizeof(met)), Gear_SpeciesName(wild.species),
                       Thousands(ShinyHunt_Get()->encounters, total, sizeof(total)));
  }
}

// --- Pokétch ---------------------------------------------------------------------------------

// The game's own Pokétch, its screen and side buttons as large as they fit (the DS draws
// them there and takes the touches)
void DrawPoketch(const SIM_CompanionFrame *frame) {
  const int state = Gear_PoketchState();
  if (state == GEAR_POKETCH_HIDDEN) {
    ImGui::TextDisabled("The Pok\xC3\xA9tch comes back when this screen closes.");
    return;
  }
  if (state != GEAR_POKETCH_SHOWN) {
    ImGui::TextDisabled("You don't have the Pok\xC3\xA9tch yet.");
    return;
  }
  if (!frame->fullScreen) {
    // Split: it's on the DS screen right above
    ImGui::TextColored(ImColor(kTextDim), "The Pok\xC3\xA9tch is on the DS screen above.");
    if (ImGui::Button("Show it bigger here", ImVec2(0, S(36.0f))) && !sDragging) {
      SIM_Companion_SetFullScreen(TRUE);
    }
    return;
  }

  // DS pixels 16..255 x 16..175: the screen and the app buttons beside it
  constexpr int kX = 16, kY = 16, kW = 240, kH = 160;
  const float width = ImGui::GetContentRegionAvail().x;
  const float room = ImGui::GetContentRegionAvail().y - S(56.0f);
  float scale = width / kW;
  if (kH * scale > room && room > 0.0f) {
    scale = room / kH;
  }
  // Whole DS pixels when that's close
  const float whole = std::floor(scale);
  if (whole >= 1.0f && whole >= scale * 0.8f) {
    scale = whole;
  }
  const ImVec2 pos = ImGui::GetCursorScreenPos();
  const float w = std::floor(kW * scale);
  const float h = std::floor(kH * scale);
  const float x = std::floor(pos.x + (width - w) * 0.5f);
  SIM_Companion_ShowDsScreen(x, pos.y, w, h, kX, kY, kW, kH);
  ImGui::Dummy(ImVec2(width, h + S(6.0f)));

  ImGui::PushFont(sBold, S(18.0f));
  ImGui::TextUnformatted(Gear_PoketchAppName(Gear_PoketchApp()));
  ImGui::PopFont();
  ImGui::TextColored(ImColor(kTextDim), "The buttons at the right switch apps. Tap a tab to go back.");
}

// --- bosses ------------------------------------------------------------------------------

const char *const kStatShort[6] = {"HP", "Atk", "Def", "SpA", "SpD", "Spe"};

// One Pokemon of a boss's team: name, level and item; its moves; with everything revealed
// its ability, nature and stats
void DrawBossMon(const GearMon &mon) {
  const int discovery = Discovery();
  // Its icon at the left of its lines
  const float iconSize = PixelSize(S(32.0f), 32.0f);
  const ImVec2 rowPos = ImGui::GetCursorScreenPos();
  DrawIcon(ImGui::GetWindowDrawList(), ImVec2(rowPos.x, rowPos.y - S(9.0f)), iconSize, mon.species, mon.form, false);
  ImGui::Indent(iconSize + S(4.0f));
  ImGui::PushFont(sBold, S(15.0f));
  ImGui::TextUnformatted(Gear_SpeciesName(mon.species));
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextColored(ImColor(kTextDim), "Lv %d", mon.level);
  if (mon.heldItem != 0) {
    ImGui::SameLine();
    ImGui::TextColored(ImColor(kWarn), "@ %s", Gear_ItemName(mon.heldItem));
  }
  // Moves, in their type's colour
  ImGui::Indent(S(10.0f));
  bool first = true;
  for (int m = 0; m < 4; m++) {
    if (mon.moves[m].id == 0) {
      continue;
    }
    const char *name = Gear_MoveName(mon.moves[m].id);
    if (!first) {
      ImGui::SameLine(0.0f, S(4.0f));
      ImGui::TextColored(ImColor(kTextDim), "/");
      ImGui::SameLine(0.0f, S(4.0f));
      // Wrap before a move that wouldn't fit
      if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name).x) {
        ImGui::NewLine();
      }
    }
    ImGui::TextColored(ImColor(TypeColor(mon.moves[m].type)), "%s", name);
    first = false;
  }
  if (discovery == GEAR_DISCOVERY_EVERYTHING) {
    ImGui::TextColored(ImColor(kTextDim), "%s, %s nature", Gear_AbilityName(mon.ability), Gear_NatureName(mon.nature));
    char stats[96];
    size_t used = 0;
    for (int i = 0; i < 6 && used < sizeof(stats); i++) {
      used += snprintf(stats + used, sizeof(stats) - used, "%s%s %d", i ? "  " : "", kStatShort[i], mon.stats[i]);
    }
    ImGui::TextColored(ImColor(kTextDim), "%s", stats);
  }
  ImGui::Unindent(S(10.0f));
  ImGui::Unindent(iconSize + S(4.0f));
  // Room for the icon when the lines were fewer than it is tall
  const float bottom = rowPos.y + iconSize - S(6.0f);
  if (ImGui::GetCursorScreenPos().y < bottom) {
    ImGui::Dummy(ImVec2(0, bottom - ImGui::GetCursorScreenPos().y));
  }
}

// A boss: a tappable header (title, name, where, team size and top level) and, open, the
// team
void DrawBossCard(int index, bool *open) {
  const GearBoss *boss = Gear_Boss(index);
  if (boss == nullptr) {
    return;
  }
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const float width = ImGui::GetContentRegionAvail().x;
  const float height = S(40.0f);
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImGui::PushID(index);
  if (Tap("boss", ImVec2(width, height))) {
    *open = !*open;
  }
  const bool held = ImGui::IsItemActive() && !sDragging;
  ImGui::PopID();

  draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), held ? kPanelPressed : kPanel, S(10.0f));
  const float pad = S(10.0f);
  char title[64];
  snprintf(title, sizeof(title), "%s %s", boss->title, boss->name);
  TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + pad, pos.y + S(4.0f)), boss->beaten ? kTextDim : kText, title);
  char where[48];
  snprintf(where, sizeof(where), "%s%s", boss->place, boss->doubles && !strstr(boss->place, "double") ? ", double" : "");
  TextAt(draw, sRegular, S(13.0f), ImVec2(pos.x + pad, pos.y + S(22.0f)), kTextDim, where);

  char right[32];
  if (boss->beaten) {
    snprintf(right, sizeof(right), "Beaten");
  } else {
    snprintf(right, sizeof(right), "%d Pokemon, Lv %d", boss->partyCount, boss->aceLevel);
  }
  ImVec2 rightSize = TextSize(sRegular, S(14.0f), right);
  TextAt(draw, sRegular, S(14.0f), ImVec2(pos.x + width - pad - rightSize.x - S(16.0f), pos.y + S(12.0f)),
         boss->beaten ? kGood : kTextDim, right);
  // Open/closed arrow
  const ImVec2 arrow(pos.x + width - pad - S(4.0f), pos.y + height * 0.5f);
  if (*open) {
    draw->AddTriangleFilled(ImVec2(arrow.x - S(5.0f), arrow.y - S(3.0f)), ImVec2(arrow.x + S(5.0f), arrow.y - S(3.0f)),
                            ImVec2(arrow.x, arrow.y + S(4.0f)), kTextDim);
  } else {
    draw->AddTriangleFilled(ImVec2(arrow.x - S(3.0f), arrow.y - S(5.0f)), ImVec2(arrow.x - S(3.0f), arrow.y + S(5.0f)),
                            ImVec2(arrow.x + S(4.0f), arrow.y), kTextDim);
  }

  if (*open) {
    ImGui::Dummy(ImVec2(0, S(2.0f)));
    for (int i = 0; i < boss->partyCount; i++) {
      ImGui::PushID(i);
      DrawBossMon(boss->party[i]);
      ImGui::PopID();
    }
  }
  ImGui::Dummy(ImVec2(0, S(4.0f)));
}

// The level cap, the bosses before the next badge, and all of them by stage
void DrawBosses() {
  constexpr int kMaxBosses = 64;
  const int count = Gear_BossCount() < kMaxBosses ? Gear_BossCount() : kMaxBosses;
  static bool sOpen[kMaxBosses];
  static bool sStageOpen[9];
  static int sOpenedFor = -2; // the next gym the "coming up" cards were opened for

  if (ShowLevelCap() && sNextGym >= 0 && sLevelCap > 0) {
    const GearBoss *next = Gear_Boss(sNextGym);
    ImGui::Dummy(ImVec2(0, S(6.0f)));
    ImGui::PushFont(sBold, S(18.0f));
    ImGui::Text("Level cap %d", sLevelCap);
    ImGui::PopFont();
    if (next != nullptr) {
      ImGui::SameLine();
      ImGui::TextColored(ImColor(kTextDim), " %s's %s", next->name, Gear_SpeciesName(next->aceSpecies));
    }
    const SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
    if (config != nullptr && config->hardLevelCap) {
      ImGui::TextColored(ImColor(kTextDim), "Enforced: no EXP past it (Settings > Gameplay)");
    }
    for (int i = 0; i < sPartyCount; i++) {
      if (!sParty[i].isEgg && sParty[i].level > sLevelCap) {
        ImGui::TextColored(ImColor(kWarn), "%s is over the cap (Lv %d)", MonName(sParty[i]), sParty[i].level);
      }
    }
  }

  if (Discovery() == GEAR_DISCOVERY_VANILLA) {
    return;
  }

  // The bosses before the next badge (the gym leader last), opened when that changes
  const int stage = sTrainer.badgeCount;
  if (sOpenedFor != sNextGym) {
    sOpenedFor = sNextGym;
    for (int i = 0; i < count; i++) {
      const GearBoss *boss = Gear_Boss(i);
      sOpen[i] = boss != nullptr && boss->stage == stage && !boss->beaten;
    }
  }
  ImGui::Dummy(ImVec2(0, S(4.0f)));
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextColored(ImColor(kAccent), "%s", stage >= 8 ? "Coming up: the Pokemon League" : "Coming up before the next badge");
  ImGui::PopFont();
  for (int i = 0; i < count; i++) {
    const GearBoss *boss = Gear_Boss(i);
    if (boss != nullptr && boss->stage == stage && !boss->beaten) {
      DrawBossCard(i, &sOpen[i]);
    }
  }

  // Everyone, by stage
  ImGui::Dummy(ImVec2(0, S(4.0f)));
  ImGui::PushFont(sBold, S(16.0f));
  ImGui::TextColored(ImColor(kAccent), "All bosses");
  ImGui::PopFont();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  for (int st = 0; st <= 8; st++) {
    char label[48];
    if (st == 8) {
      snprintf(label, sizeof(label), "The Pokemon League");
    } else {
      snprintf(label, sizeof(label), "Before badge %d", st + 1);
    }
    ImGui::PushID(st);
    const float width = ImGui::GetContentRegionAvail().x;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    if (Tap("stage", ImVec2(width, S(30.0f)))) {
      sStageOpen[st] = !sStageOpen[st];
    }
    ImGui::PopID();
    TextAt(draw, sBold, S(15.0f), ImVec2(pos.x + S(4.0f), pos.y + S(6.0f)), st < stage ? kTextDim : kText, label);
    TextAt(draw, sBold, S(16.0f), ImVec2(pos.x + width - S(16.0f), pos.y + S(5.0f)), kTextDim, sStageOpen[st] ? "-" : "+");
    if (sStageOpen[st]) {
      for (int i = 0; i < count; i++) {
        const GearBoss *boss = Gear_Boss(i);
        if (boss != nullptr && boss->stage == st) {
          DrawBossCard(i, &sOpen[i]);
        }
      }
    }
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

  DrawNuzlockeRun();
  DrawBosses();
}

// --- settings ----------------------------------------------------------------------------

// The game being played; switching and adding games is on the settings screen (top)
void DrawGames() {
  const SIM_Cart *current = SIM_Carts_Current();
  ImGui::PushFont(sBold, S(18.0f));
  ImGui::TextUnformatted("Game");
  ImGui::PopFont();
  ImGui::Text("Playing: %s", current->name);
  if (current->info[0] != '\0') {
    ImGui::TextColored(ImColor(kTextDim), "%s", current->info);
  }
  const float width = ImGui::GetContentRegionAvail().x;
  if (ImGui::Button("Settings (switch or add games)...", ImVec2(width, S(40.0f))) && !sDragging) {
    SIM::Settings::Open("Games");
  }
  ImGui::TextWrapped("The settings open on the top screen (also with the back button or R3).");
  ImGui::Dummy(ImVec2(0, S(8.0f)));
}

void DrawSettings() {
  SIM_Config_prj_type *config = SIM_Config_prj_GetConfig();
  bool changed = false;

  DrawGames();

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
  ImGui::TextDisabled("Turn it back on in Settings > Gear (back button or R3).");

  bool controls = config->gearBattleControls != FALSE;
  if (ImGui::Checkbox("Battle menu on the Gear", &controls) && !sDragging) {
    config->gearBattleControls = controls ? TRUE : FALSE;
    changed = true;
  }
  ImGui::TextWrapped("In battles the Gear takes the whole lower screen with Fight, Bag, Pokemon, Run and your moves "
                     "(with their matchups). The DS screen comes back for the bag and the party.");

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
  sPictureLoadsThisFrame = 0;

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
  if (sApp == APP_POKETCH && !sInBattle && Gear_PoketchState() == GEAR_POKETCH_NOT_OBTAINED) {
    SelectApp(APP_PARTY);
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
  case APP_POKETCH:
    DrawPoketch(frame);
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
