// Settings > Save editor (port/save_editor.h): the party, the PC boxes, giving Pokemon, the
// bag, the trainer and the legendary Pokemon's Mystery Gift events. The page shows one view
// at a time; B (or the back button) goes back one.
#include <nitro.h>
#include <simulator/sim_settings.hpp>
#include <simulator/sim_ui.hpp>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "port/gear_data.h"
#include "port/save_editor.h"

void SaveEditor_Draw();

using namespace SIM::UI;

namespace {

enum class View { Home, Party, Boxes, Mon, Give, Bag, Trainer, Events, Species, Move, Item, Nature };

// What a picker's choice is for
enum class Pick { MonMove, MonItem, MonNature, GiveSpecies, GiveItem, GiveNature, BagAdd };

constexpr int kNoFocus = -2;
constexpr int kFirst = -1;

struct Frame {
    View view;
    int focus; // the item the pad's cursor goes back to
};

std::vector<Frame> sStack = { { View::Home, kFirst } };
int sPendingFocus = kNoFocus; // next frame: the item the pad's cursor goes to
int sWantFocus = kNoFocus;    // this frame
bool sScrollTop = false;
int sGen = 0; // changes whenever what's shown may have: cached data is read again
int sLastFrame = -10;

std::string sStatus;
bool sStatusError = false;

SaveEdRef sMon = { SAVE_ED_PARTY, 0 };
int sBox = 0;
int sPocket = 0;
Pick sPick = Pick::MonMove;
int sPickSlot = 0;
int sItemPocket = 0;
char sLetter = 0; // the letter a long list shows (0: its first)
bool sLearnableOnly = true;
int sNumber = 1; // the species picker's Pokedex number
SaveEdNewMon sGive = { 0, 50, -1, FALSE, FALSE, 0 };

const char *const kStatNames[SAVE_ED_STATS] = { "HP", "Attack", "Defense", "Sp. Atk", "Sp. Def", "Speed" };
const char *const kStatShort[SAVE_ED_STATS] = { "HP", "Atk", "Def", "SpA", "SpD", "Spe" };

// --- navigation ------------------------------------------------------------------------

void SetStatus(const std::string &text, bool error = false)
{
    sStatus = text;
    sStatusError = error;
}

void Changed()
{
    sGen++;
}

void Go(View view, int focusKey)
{
    sStack.back().focus = focusKey;
    sStack.push_back({ view, kFirst });
    sPendingFocus = kFirst;
    sScrollTop = true;
    sStatus.clear();
    sLetter = 0;
    Changed();
}

void Back()
{
    if (sStack.size() > 1) {
        sStack.pop_back();
    }
    sPendingFocus = sStack.back().focus;
    sLetter = 0;
    Changed();
}

// Before an item the pad's cursor can go to
void Want(int key)
{
    if (sWantFocus != kNoFocus && (sWantFocus == kFirst || sWantFocus == key)) {
        FocusNextItem();
        sWantFocus = kNoFocus;
    }
}

void ShowStatus()
{
    if (!sStatus.empty()) {
        Status(sStatus.c_str(), sStatusError);
    }
}

std::string Format(const char *format, ...) __attribute__((format(printf, 1, 2)));
std::string Format(const char *format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

// --- names and lists -------------------------------------------------------------------

std::string NatureText(int nature)
{
    int raised, lowered;
    SaveEd_NatureStats(nature, &raised, &lowered);
    if (raised == lowered) {
        return Format("%s (no effect)", Gear_NatureName(nature));
    }
    return Format("%s (+%s, -%s)", Gear_NatureName(nature), kStatShort[raised], kStatShort[lowered]);
}

std::string TypesText(int type1, int type2)
{
    return type1 == type2 ? Gear_TypeName(type1) : Format("%s/%s", Gear_TypeName(type1), Gear_TypeName(type2));
}

std::string MoveText(u16 move)
{
    int power = SaveEd_MovePower(move);
    std::string text = Gear_TypeName(SaveEd_MoveType(move));
    text += power > 1 ? Format(" - %d power", power) : (power == 1 ? " - varies" : " - status");
    text += Format(" - %d PP", SaveEd_MovePP(move));
    return text;
}

char FirstLetter(const char *name)
{
    for (const char *c = name; *c != '\0'; c++) {
        if (isalpha((unsigned char)*c)) {
            return (char)toupper((unsigned char)*c);
        }
    }
    return '#';
}

// A long list is shown a letter at a time: tiles for its letters. Returns the letter shown.
char LetterTiles(const std::vector<u16> &ids, const char *(*name)(u16), int firstKey)
{
    std::vector<char> letters;
    for (u16 id : ids) {
        char letter = FirstLetter(name(id));
        if (std::find(letters.begin(), letters.end(), letter) == letters.end()) {
            letters.push_back(letter);
        }
    }
    std::sort(letters.begin(), letters.end());
    if (letters.empty()) {
        return 0;
    }
    if (std::find(letters.begin(), letters.end(), sLetter) == letters.end()) {
        sLetter = letters[0];
    }
    BeginTiles("##letters", 9, S(44.0f));
    for (size_t i = 0; i < letters.size(); i++) {
        char text[2] = { letters[i], '\0' };
        Want(firstKey + (int)i);
        if (Tile(text, nullptr, letters[i] == sLetter)) {
            sLetter = letters[i];
        }
    }
    EndTiles();
    return sLetter;
}

const char *SpeciesName(u16 species)
{
    return Gear_SpeciesName(species);
}

const char *MoveName(u16 move)
{
    return Gear_MoveName(move);
}

const char *ItemName(u16 item)
{
    return Gear_ItemName(item);
}

const std::vector<u16> &AllSpecies()
{
    static std::vector<u16> species;
    if (species.empty()) {
        for (u16 s = 1; s <= SAVE_ED_MAX_SPECIES; s++) {
            species.push_back(s);
        }
        std::stable_sort(species.begin(), species.end(),
            [](u16 a, u16 b) { return strcmp(Gear_SpeciesName(a), Gear_SpeciesName(b)) < 0; });
    }
    return species;
}

const std::vector<u16> &AllMoves()
{
    static std::vector<u16> moves;
    if (moves.empty()) {
        for (u16 m = 1; m <= SAVE_ED_MAX_MOVE; m++) {
            moves.push_back(m);
        }
        std::stable_sort(moves.begin(), moves.end(),
            [](u16 a, u16 b) { return strcmp(Gear_MoveName(a), Gear_MoveName(b)) < 0; });
    }
    return moves;
}

const std::vector<u16> &PocketItems(int pocket)
{
    static std::vector<u16> items[SAVE_ED_POCKETS];
    static bool loaded[SAVE_ED_POCKETS];
    if (pocket < 0 || pocket >= SAVE_ED_POCKETS) {
        pocket = 0;
    }
    if (!loaded[pocket]) {
        u16 ids[512];
        int count = SaveEd_PocketItems(pocket, ids, 512);
        items[pocket].assign(ids, ids + count);
        loaded[pocket] = true;
    }
    return items[pocket];
}

// --- cached reads (what's shown only changes with edits: the game waits) ------------------

const SaveEdMon *MonData(SaveEdRef ref)
{
    static SaveEdMon mon;
    static SaveEdRef cachedRef = { -100, 0 };
    static int gen = -1;
    static bool ok = false;
    if (gen != sGen || cachedRef.box != ref.box || cachedRef.slot != ref.slot) {
        ok = SaveEd_GetMon(ref, &mon);
        gen = sGen;
        cachedRef = ref;
    }
    return ok ? &mon : nullptr;
}

struct PartyData {
    int gen = -1;
    int count = 0;
    SaveEdMon mons[6];
};

const PartyData &Party()
{
    static PartyData party;
    if (party.gen != sGen) {
        party.gen = sGen;
        party.count = std::min(SaveEd_PartyCount(), 6);
        for (int i = 0; i < party.count; i++) {
            SaveEd_GetMon({ SAVE_ED_PARTY, i }, &party.mons[i]);
        }
    }
    return party;
}

struct BoxesData {
    int gen = -1;
    char names[SAVE_ED_BOXES][SAVE_ED_NAME_LEN];
    int counts[SAVE_ED_BOXES];
    int box = -1;
    SaveEdSlot slots[SAVE_ED_BOX_SLOTS];
};

const BoxesData &Boxes(int box)
{
    static BoxesData data;
    if (data.gen != sGen) {
        data.gen = sGen;
        for (int b = 0; b < SAVE_ED_BOXES; b++) {
            SaveEd_BoxName(b, data.names[b], SAVE_ED_NAME_LEN);
            data.counts[b] = SaveEd_BoxCount(b);
        }
        data.box = -1;
    }
    if (data.box != box) {
        data.box = box;
        for (int s = 0; s < SAVE_ED_BOX_SLOTS; s++) {
            SaveEd_GetBoxSlot(box, s, &data.slots[s]);
        }
    }
    return data;
}

// --- Home --------------------------------------------------------------------------------

void DrawHome()
{
    Note("Changes go straight into the game you're playing. Save in the game to keep them; reset without "
         "saving to undo them.");
    ShowStatus();
    const PartyData &party = Party();
    std::string partyText = Format("%d Pokemon. Level, nature, IVs, EVs, moves, item, shiny...", party.count);
    Want(0);
    if (Button("Party", partyText.c_str())) {
        Go(View::Party, 0);
    }
    Want(1);
    if (Button("PC boxes", "Edit the Pokemon in your boxes the same way")) {
        Go(View::Boxes, 1);
    }
    Want(2);
    if (Button("Give a Pokemon", "Any species at any level, with the nature you want, shiny or not")) {
        sGive.item = 0;
        Go(View::Give, 2);
    }
    Want(3);
    if (Button("Bag", "Add items, change how many you have")) {
        Go(View::Bag, 3);
    }
    Want(4);
    if (Button("Trainer", "Money, Battle Points, badges, play time, National Pokedex")) {
        Go(View::Trainer, 4);
    }
    Want(5);
    if (Button("Legendary events", "The Mystery Gift events: Darkrai, Shaymin, Arceus and Rotom's forms")) {
        Go(View::Events, 5);
    }
}

// --- Party and boxes ---------------------------------------------------------------------

std::string MonSummary(const SaveEdMon &mon)
{
    if (mon.isEgg) {
        return "Egg";
    }
    std::string text = Format("%s - %s - %s", Gear_SpeciesName(mon.species), Gear_NatureName(mon.nature),
        Gear_AbilityName(mon.ability));
    if (mon.item != 0) {
        text += Format(" - %s", Gear_ItemName(mon.item));
    }
    if (mon.shiny) {
        text += " - Shiny";
    }
    return text;
}

void OpenMon(SaveEdRef ref, int focusKey)
{
    sMon = ref;
    Go(View::Mon, focusKey);
}

void DrawParty()
{
    const PartyData &party = Party();
    for (int i = 0; i < party.count; i++) {
        const SaveEdMon &mon = party.mons[i];
        std::string title = mon.isEgg ? Format("Egg###p%d", i) : Format("%s   Lv. %d###p%d", mon.name, mon.level, i);
        std::string summary = MonSummary(mon);
        Want(i);
        if (Card(title.c_str(), summary.c_str())) {
            OpenMon({ SAVE_ED_PARTY, i }, i);
        }
    }
    if (party.count < 6) {
        Want(10);
        if (Button("Give a Pokemon")) {
            Go(View::Give, 10);
        }
    }
}

void DrawBoxes()
{
    const BoxesData &boxes = Boxes(sBox);
    if (sWantFocus == kFirst) {
        sWantFocus = sBox;
    }
    BeginTiles("##boxes", 6);
    for (int b = 0; b < SAVE_ED_BOXES; b++) {
        std::string count = Format("%d/30", boxes.counts[b]);
        Want(b);
        if (Tile(boxes.names[b], count.c_str(), b == sBox)) {
            sBox = b;
        }
    }
    EndTiles();

    Section(boxes.names[sBox]);
    const BoxesData &box = Boxes(sBox);
    BeginTiles("##slots", 6);
    for (int s = 0; s < SAVE_ED_BOX_SLOTS; s++) {
        const SaveEdSlot &slot = box.slots[s];
        if (slot.species == 0) {
            Tile("-", nullptr, false, false);
            continue;
        }
        std::string level = slot.isEgg ? std::string() : Format("Lv. %d%s", slot.level, slot.shiny ? " *" : "");
        Want(100 + s);
        if (Tile(slot.isEgg ? "Egg" : Gear_SpeciesName(slot.species), level.c_str())) {
            OpenMon({ sBox, s }, 100 + s);
        }
    }
    EndTiles();
    Note("* Shiny");
}

// --- one Pokemon ---------------------------------------------------------------------------

int AbilitySlot(const SaveEdMon &mon)
{
    return mon.abilities[1] != 0 && mon.ability == mon.abilities[1] ? 1 : 0;
}

void Repersonalize(const SaveEdMon &mon, int nature, int gender, bool shiny, int abilitySlot)
{
    if (SaveEd_SetPersonality(sMon, nature, mon.genderFixed ? -1 : gender, shiny, abilitySlot)) {
        Changed();
    } else {
        SetStatus("No personality value has all of that. Try again.", true);
    }
}

void DrawMon()
{
    const SaveEdMon *data = MonData(sMon);
    if (data == nullptr) {
        Back();
        return;
    }
    const SaveEdMon mon = *data; // edits below change the cache

    std::string title = mon.isEgg ? std::string("Egg") : Format("%s   Lv. %d", mon.name, mon.level);
    std::string info;
    if (!mon.isEgg) {
        info = Format("%s - %s - %s\n", Gear_SpeciesName(mon.species), TypesText(mon.type1, mon.type2).c_str(),
            mon.genderFixed ? (mon.gender == 2 ? "No gender" : (mon.gender == 0 ? "Male" : "Female"))
                            : (mon.gender == 0 ? "Male" : "Female"));
        info += Format("HP %d/%d - Atk %d - Def %d - SpA %d - SpD %d - Spe %d", mon.hp, mon.maxHp,
            mon.stats[SAVE_ED_ATK], mon.stats[SAVE_ED_DEF], mon.stats[SAVE_ED_SPATK], mon.stats[SAVE_ED_SPDEF],
            mon.stats[SAVE_ED_SPEED]);
    }
    Panel(title.c_str(), info.c_str());
    if (mon.isEgg) {
        Note("Eggs can't be edited until they hatch.");
        return;
    }
    ShowStatus();

    Section("Basics");
    int level = mon.level;
    Want(0);
    if (NumberField("Level", nullptr, &level, 1, SAVE_ED_MAX_LEVEL) && SaveEd_SetLevel(sMon, level)) {
        Changed();
    }
    std::string nature = NatureText(mon.nature);
    Want(1);
    if (Button("Nature###nature", nature.c_str())) {
        sPick = Pick::MonNature;
        Go(View::Nature, 1);
    }
    bool shiny = mon.shiny != FALSE;
    Want(2);
    if (Toggle("Shiny", nullptr, &shiny)) {
        Repersonalize(mon, mon.nature, mon.gender, shiny, AbilitySlot(mon));
    }
    if (!mon.genderFixed) {
        static const char *const kGenders[] = { "Male", "Female" };
        int gender = mon.gender == 1 ? 1 : 0;
        Want(3);
        if (Choice("Gender", nullptr, &gender, kGenders, 2)) {
            Repersonalize(mon, mon.nature, gender, shiny, AbilitySlot(mon));
        }
    }
    if (mon.abilities[1] != 0) {
        const char *abilities[2] = { Gear_AbilityName(mon.abilities[0]), Gear_AbilityName(mon.abilities[1]) };
        int slot = AbilitySlot(mon);
        Want(4);
        if (Choice("Ability", nullptr, &slot, abilities, 2)) {
            Repersonalize(mon, mon.nature, mon.gender, shiny, slot);
        }
    } else {
        std::string ability = Format("Ability: %s (its only one)", Gear_AbilityName(mon.ability));
        Note(ability.c_str());
    }
    Note("Nature, shininess, gender and ability all come from the Pokemon's personality value: changing one picks "
         "a new value that keeps the others.");
    Want(5);
    if (Button("Held item###item", mon.item != 0 ? Gear_ItemName(mon.item) : "None")) {
        sPick = Pick::MonItem;
        sItemPocket = 0;
        Go(View::Item, 5);
    }
    int friendship = mon.friendship;
    Want(6);
    if (NumberField("Friendship", "255 is the most. Some Pokemon evolve when it's 220 or more.", &friendship, 0, 255)
        && SaveEd_SetFriendship(sMon, friendship)) {
        Changed();
    }

    Section("IVs (0 to 31)");
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        int iv = mon.ivs[s];
        std::string label = Format("%s##iv", kStatNames[s]);
        Want(10 + s);
        if (NumberField(label.c_str(), nullptr, &iv, 0, SAVE_ED_MAX_IV) && SaveEd_SetIV(sMon, (SaveEdStat)s, iv)) {
            Changed();
        }
    }
    Want(16);
    if (Button("All IVs 31")) {
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            SaveEd_SetIV(sMon, (SaveEdStat)s, SAVE_ED_MAX_IV);
        }
        Changed();
    }

    int total = 0;
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        total += mon.evs[s];
    }
    std::string evTitle = Format("EVs (%d of 510 used, 252 at most each)", total);
    Section(evTitle.c_str());
    for (int s = 0; s < SAVE_ED_STATS; s++) {
        int ev = mon.evs[s];
        std::string label = Format("%s##ev", kStatNames[s]);
        Want(20 + s);
        if (NumberField(label.c_str(), nullptr, &ev, 0, SAVE_ED_MAX_EV) && SaveEd_SetEV(sMon, (SaveEdStat)s, ev)) {
            Changed();
        }
    }
    Want(26);
    if (Button("Clear the EVs")) {
        for (int s = 0; s < SAVE_ED_STATS; s++) {
            SaveEd_SetEV(sMon, (SaveEdStat)s, 0);
        }
        Changed();
    }

    Section("Moves");
    int moveCount = 0;
    for (int m = 0; m < 4; m++) {
        if (mon.moves[m] == 0) {
            continue;
        }
        moveCount++;
        std::string label = Format("%s###move%d", Gear_MoveName(mon.moves[m]), m);
        std::string help = MoveText(mon.moves[m]) + Format(" (%d left)", mon.pp[m]);
        Want(30 + m);
        if (Button(label.c_str(), help.c_str())) {
            sPick = Pick::MonMove;
            sPickSlot = m;
            Go(View::Move, 30 + m);
        }
    }
    if (moveCount < 4) {
        Want(34);
        if (Button("Add a move")) {
            sPick = Pick::MonMove;
            sPickSlot = moveCount;
            Go(View::Move, 34);
        }
    }

    Section("Care");
    Want(40);
    if (Button("Heal", sMon.box == SAVE_ED_PARTY ? "HP, status and PP" : "PP (Pokemon in the PC are always healed)")) {
        if (SaveEd_Heal(sMon)) {
            Changed();
            SetStatus("Healed.");
        }
    }
}

// --- Give ------------------------------------------------------------------------------

void DrawGive()
{
    std::string species = sGive.species != 0 ? Format("#%03d %s", sGive.species, Gear_SpeciesName(sGive.species))
                                             : std::string("Choose one");
    Want(0);
    if (Button("Species###species", species.c_str(), sGive.species == 0)) {
        sPick = Pick::GiveSpecies;
        sNumber = sGive.species != 0 ? sGive.species : sNumber;
        Go(View::Species, 0);
    }
    Want(1);
    NumberField("Level", nullptr, &sGive.level, 1, SAVE_ED_MAX_LEVEL);
    std::string nature = sGive.nature < 0 ? std::string("Random") : NatureText(sGive.nature);
    Want(2);
    if (Button("Nature###nature", nature.c_str())) {
        sPick = Pick::GiveNature;
        Go(View::Nature, 2);
    }
    bool shiny = sGive.shiny != FALSE;
    Want(3);
    if (Toggle("Shiny", nullptr, &shiny)) {
        sGive.shiny = shiny;
    }
    bool maxIVs = sGive.maxIVs != FALSE;
    Want(4);
    if (Toggle("Perfect IVs", "All 31 (otherwise random, like a wild Pokemon)", &maxIVs)) {
        sGive.maxIVs = maxIVs;
    }
    Want(5);
    if (Button("Held item###item", sGive.item != 0 ? Gear_ItemName(sGive.item) : "None")) {
        sPick = Pick::GiveItem;
        sItemPocket = 0;
        Go(View::Item, 5);
    }

    const bool partyRoom = Party().count < 6;
    Want(6);
    int choice = ButtonPair("Add to the party", "Send to the PC", true, sGive.species != 0 && partyRoom,
        sGive.species != 0);
    if (choice != 0) {
        int box = 0, slot = 0;
        int result = SaveEd_GiveMon(&sGive, choice == 1, &box, &slot);
        Changed();
        if (result == 1) {
            SetStatus(Format("%s joined your party.", Gear_SpeciesName(sGive.species)));
        } else if (result == 2) {
            char name[SAVE_ED_NAME_LEN];
            SaveEd_BoxName(box, name, sizeof(name));
            SetStatus(Format("%s was sent to %s.", Gear_SpeciesName(sGive.species), name));
        } else {
            SetStatus("There's no room for it.", true);
        }
    }
    ShowStatus();
    Note("It's caught here in a Poke Ball, with you as its trainer, and the Pokedex counts it as caught.");
}

// --- Bag -------------------------------------------------------------------------------

void DrawBag()
{
    BeginTiles("##pockets", 4, S(48.0f));
    for (int p = 0; p < SAVE_ED_POCKETS; p++) {
        Want(p);
        if (Tile(SaveEd_PocketName(p), nullptr, p == sPocket)) {
            sPocket = p;
            sStatus.clear();
        }
    }
    EndTiles();
    ShowStatus();

    Want(10);
    std::string add = Format("Add to %s", SaveEd_PocketName(sPocket));
    if (Button(add.c_str())) {
        sPick = Pick::BagAdd;
        sItemPocket = sPocket;
        Go(View::Item, 10);
    }

    static SaveEdBagItem items[200];
    static int count = 0, gen = -1, pocket = -1;
    if (gen != sGen || pocket != sPocket) {
        gen = sGen;
        pocket = sPocket;
        count = SaveEd_BagPocket(sPocket, items, 200);
    }
    Section(SaveEd_PocketName(sPocket));
    if (count == 0) {
        Note("Nothing in this pocket.");
    }
    for (int i = 0; i < count; i++) {
        const u16 item = items[i].item;
        const int max = SaveEd_ItemMaxQuantity(item);
        if (max <= 1) {
            std::string label = Format("%s###i%d", Gear_ItemName(item), item);
            Want(100 + item);
            Card(label.c_str(), sPocket == 7 ? "Key item" : nullptr);
            continue;
        }
        int quantity = items[i].quantity;
        std::string label = Format("%s###i%d", Gear_ItemName(item), item);
        Want(100 + item);
        if (NumberField(label.c_str(), nullptr, &quantity, 0, max)) {
            SaveEd_SetItemQuantity(item, quantity);
            Changed();
        }
    }
    if (count > 0 && SaveEd_ItemMaxQuantity(items[0].item) > 1) {
        Note("Set an item to 0 to take it out of the bag.");
    }
}

// --- Trainer -----------------------------------------------------------------------------

void DrawTrainer()
{
    SaveEdTrainer trainer;
    SaveEd_GetTrainer(&trainer);
    const int badges = __builtin_popcount(trainer.badges);
    std::string info = Format("ID %05u - %d badge%s", trainer.id, badges, badges == 1 ? "" : "s");
    Panel(trainer.name, info.c_str());
    ShowStatus();

    int money = (int)trainer.money;
    Want(0);
    if (NumberField("Money", nullptr, &money, 0, SAVE_ED_MAX_MONEY)) {
        SaveEd_SetMoney(money);
        Changed();
    }
    int points = trainer.battlePoints;
    Want(1);
    if (NumberField("Battle Points", "Spent at the Battle Frontier for items and TMs", &points, 0, SAVE_ED_MAX_BP)) {
        SaveEd_SetBattlePoints(points);
        Changed();
    }

    Section("Badges");
    for (int b = 0; b < 8; b++) {
        bool has = (trainer.badges & (1 << b)) != 0;
        std::string label = Format("%s Badge", SaveEd_BadgeName(b));
        Want(10 + b);
        if (Toggle(label.c_str(), nullptr, &has)) {
            SaveEd_SetBadge(b, has);
            Changed();
        }
    }
    Note("Badges let you use the HMs outside battle and make traded Pokemon obey you.");

    Section("Play time");
    int hours = trainer.hours, minutes = trainer.minutes;
    Want(20);
    if (NumberField("Hours", nullptr, &hours, 0, SAVE_ED_MAX_HOURS)) {
        SaveEd_SetPlayTime(hours, minutes);
        Changed();
    }
    Want(21);
    if (NumberField("Minutes", nullptr, &minutes, 0, 59)) {
        SaveEd_SetPlayTime(hours, minutes);
        Changed();
    }

    Section("Pokedex");
    if (trainer.nationalDex) {
        Status("You have the National Pokedex.");
    } else {
        Want(30);
        if (Button("Get the National Pokedex", "Professor Rowan gives it after you enter the Hall of Fame. With it, "
                                               "Pokemon from outside Sinnoh start to appear.")) {
            SaveEd_GiveNationalDex();
            Changed();
            SetStatus("You have the National Pokedex now.");
        }
    }
}

// --- Events ------------------------------------------------------------------------------

void Requirement(bool done, const char *text)
{
    std::string line = Format("%s %s", done ? "Done:" : "Needed:", text);
    Status(line.c_str(), !done);
}

void DrawEvents()
{
    struct EventText {
        const char *title;
        const char *item;
        const char *storyStep; // nullptr: none
        const char *howTo;
    };
    static const EventText kEvents[SAVE_ED_EVENTS] = {
        { "Darkrai", "Member Card", "Give the Lunar Wing to the sailor's son in Canalave City (Cresselia)",
            "Go into the Harbor Inn in Canalave City and sleep there: you wake up on Newmoon Island, where Darkrai "
            "is (level 50)." },
        { "Shaymin", "Oak's Letter", nullptr,
            "Go to the stone tablet on Route 224: Professor Oak is waiting. Then Seabreak Path opens and leads to "
            "Flower Paradise, where Shaymin is (level 30)." },
        { "Arceus", "Azure Flute", nullptr,
            "Go to the top of Spear Pillar on Mt. Coronet and play the flute there: stairs appear to the Hall of "
            "Origin, where Arceus is (level 80)." },
        { "Rotom's forms", "Secret Key", "Team Galactic leaves the Eterna City building (story)",
            "Open the locked room in the Team Galactic building in Eterna City (1F) with the Secret Key: in Rotom's "
            "Room your Rotom can change into its appliance forms." },
    };

    Note("Nintendo gave these out by Mystery Gift. Starting one gives you its key item, just as the delivery man "
         "in a Poke Mart would. The game checks when you arrive at the place: if you're already there, leave and "
         "come back.");
    ShowStatus();

    static SaveEdEventState states[SAVE_ED_EVENTS];
    static int gen = -1;
    if (gen != sGen) {
        gen = sGen;
        for (int e = 0; e < SAVE_ED_EVENTS; e++) {
            SaveEd_GetEvent((SaveEdEvent)e, &states[e]);
        }
    }

    for (int e = 0; e < SAVE_ED_EVENTS; e++) {
        const EventText &text = kEvents[e];
        const SaveEdEventState &state = states[e];
        std::string title = Format("%s (%s)", text.title, text.item);
        Section(title.c_str());
        if (state.caught) {
            std::string caught = Format("Done: %s was caught.", text.title);
            Status(caught.c_str());
            continue;
        }
        if (state.needsPostgame) {
            Requirement(state.gameCompleted, "Enter the Hall of Fame");
            Requirement(state.nationalDex, "The National Pokedex");
        }
        if (text.storyStep != nullptr) {
            Requirement(state.storyStep, text.storyStep);
        }
        const bool on = state.started && state.hasItem;
        Requirement(on, on ? "The event is on" : "Start the event");
        Note(text.howTo);
        if (!on) {
            std::string label = Format("Start the %s event", text.title);
            Want(e);
            if (Button(label.c_str(), nullptr, true)) {
                sPendingFocus = kFirst;
                if (SaveEd_StartEvent((SaveEdEvent)e)) {
                    SetStatus(Format("The %s event is on: the %s is in your bag.", text.title, text.item));
                } else {
                    SetStatus("Your bag has no room for the key item.", true);
                }
                Changed();
            }
        }
    }
}

// --- pickers -------------------------------------------------------------------------------

void DrawSpecies()
{
    Want(0);
    NumberField("Pokedex number", nullptr, &sNumber, 1, SAVE_ED_MAX_SPECIES);
    std::string choose = Format("Choose #%03d %s", sNumber, Gear_SpeciesName(sNumber));
    Want(1);
    if (Button(choose.c_str(), nullptr, true)) {
        sGive.species = sNumber;
        Back();
        return;
    }

    Section("By name");
    const std::vector<u16> &species = AllSpecies();
    char letter = LetterTiles(species, SpeciesName, 10);
    for (u16 s : species) {
        if (FirstLetter(Gear_SpeciesName(s)) != letter) {
            continue;
        }
        std::string label = Format("%s###s%d", Gear_SpeciesName(s), s);
        std::string help = Format("#%03d%s", s, Gear_SpeciesCaught(s) ? " - caught" : "");
        Want(1000 + s);
        if (Button(label.c_str(), help.c_str())) {
            sGive.species = s;
            sNumber = s;
            Back();
            return;
        }
    }
}

void DrawMove()
{
    const SaveEdMon *data = MonData(sMon);
    if (data == nullptr) {
        Back();
        return;
    }
    const SaveEdMon mon = *data;
    const u16 current = sPickSlot < 4 ? mon.moves[sPickSlot] : 0;
    int moveCount = 0;
    for (u16 move : mon.moves) {
        moveCount += move != 0;
    }
    ShowStatus();
    if (current != 0) {
        std::string text = Format("Replacing %s", Gear_MoveName(current));
        Note(text.c_str());
        if (moveCount > 1) {
            if (Button("Forget this move")) {
                SaveEd_SetMove(sMon, sPickSlot, 0);
                Changed();
                Back();
                return;
            }
        }
    }
    Want(1);
    if (Toggle("Only moves it can learn", "By leveling up and by TM or HM", &sLearnableOnly)) {
        sLetter = 0;
    }

    static std::vector<u16> learnable;
    static int learnableSpecies = -1, learnableForm = -1;
    if (learnableSpecies != mon.species || learnableForm != mon.form) {
        u16 moves[256];
        int count = SaveEd_LearnableMoves(mon.species, mon.form, moves, 256);
        learnable.assign(moves, moves + count);
        learnableSpecies = mon.species;
        learnableForm = mon.form;
    }
    const std::vector<u16> &moves = sLearnableOnly ? learnable : AllMoves();
    char letter = 0;
    if (moves.size() > 30) {
        letter = LetterTiles(moves, MoveName, 10);
    }
    for (u16 move : moves) {
        if (letter != 0 && FirstLetter(Gear_MoveName(move)) != letter) {
            continue;
        }
        bool known = std::find(std::begin(mon.moves), std::end(mon.moves), move) != std::end(mon.moves);
        std::string label = Format("%s###m%d", Gear_MoveName(move), move);
        std::string help = MoveText(move) + (known ? " - knows it" : "");
        Want(1000 + move);
        if (Button(label.c_str(), help.c_str(), false, !known)) {
            if (SaveEd_SetMove(sMon, sPickSlot, move)) {
                Changed();
                Back();
                return;
            }
        }
    }
}

void DrawItem()
{
    const bool forBag = sPick == Pick::BagAdd;
    if (!forBag) {
        if (Button("No item")) {
            if (sPick == Pick::MonItem) {
                SaveEd_SetItem(sMon, 0);
            } else {
                sGive.item = 0;
            }
            Back();
            return;
        }
        BeginTiles("##pockets", 4, S(48.0f));
        for (int p = 0; p < SAVE_ED_POCKETS; p++) {
            Want(1 + p);
            if (Tile(SaveEd_PocketName(p), nullptr, p == sItemPocket)) {
                sItemPocket = p;
                sLetter = 0;
            }
        }
        EndTiles();
    }
    ShowStatus();

    const std::vector<u16> &items = PocketItems(sItemPocket);
    char letter = 0;
    if (items.size() > 30) {
        letter = LetterTiles(items, ItemName, 10);
    }
    for (u16 item : items) {
        if (letter != 0 && FirstLetter(Gear_ItemName(item)) != letter) {
            continue;
        }
        std::string label = Format("%s###it%d", Gear_ItemName(item), item);
        std::string help;
        int have = forBag ? SaveEd_ItemQuantity(item) : 0;
        if (forBag && have > 0) {
            help = Format("You have %d", have);
        }
        Want(1000 + item);
        if (Button(label.c_str(), help.empty() ? nullptr : help.c_str(), false,
                !forBag || have < SaveEd_ItemMaxQuantity(item))) {
            if (sPick == Pick::MonItem) {
                SaveEd_SetItem(sMon, item);
                Back();
                return;
            }
            if (sPick == Pick::GiveItem) {
                sGive.item = item;
                Back();
                return;
            }
            // The bag: one more (and stay, to add more)
            if (SaveEd_SetItemQuantity(item, have + 1)) {
                SetStatus(Format("Added a %s (you have %d).", Gear_ItemName(item), have + 1));
            } else {
                SetStatus("The pocket is full.", true);
            }
            Changed();
        }
    }
}

void DrawNature()
{
    const bool forGive = sPick == Pick::GiveNature;
    int current = forGive ? sGive.nature : -1;
    const SaveEdMon *mon = nullptr;
    if (!forGive) {
        mon = MonData(sMon);
        if (mon == nullptr) {
            Back();
            return;
        }
        current = mon->nature;
    }
    ShowStatus();
    if (forGive) {
        Want(100);
        if (Button("Random", nullptr, false)) {
            sGive.nature = -1;
            Back();
            return;
        }
    }
    Note("Rows: the stat it raises. Columns: the stat it lowers. Raising and lowering the same stat does nothing.");
    if (sWantFocus == kFirst && current >= 0) {
        sWantFocus = current; // the pad's cursor starts on the nature it has
    }
    int chosen = -1;
    BeginTiles("##natures", 5, S(60.0f));
    for (int n = 0; n < 25; n++) {
        int raised, lowered;
        SaveEd_NatureStats(n, &raised, &lowered);
        std::string sub = raised == lowered ? std::string("-") : Format("+%s -%s", kStatShort[raised], kStatShort[lowered]);
        Want(n);
        if (Tile(Gear_NatureName(n), sub.c_str(), n == current)) {
            chosen = n;
        }
    }
    EndTiles();

    if (chosen < 0) {
        return;
    }
    if (forGive) {
        sGive.nature = chosen;
        Back();
        return;
    }
    const SaveEdMon copy = *mon;
    if (SaveEd_SetPersonality(sMon, chosen, copy.genderFixed ? -1 : copy.gender, copy.shiny, AbilitySlot(copy))) {
        Back();
    } else {
        SetStatus("No personality value has all of that. Try again.", true);
    }
}

const char *BackLabel()
{
    if (sStack.size() < 2) {
        return "";
    }
    static std::string label;
    switch (sStack[sStack.size() - 2].view) {
    case View::Home:
        return "Save editor";
    case View::Party:
        return "Party";
    case View::Boxes:
        return "PC boxes";
    case View::Give:
        return "Give a Pokemon";
    case View::Bag:
        return "Bag";
    case View::Mon: {
        const SaveEdMon *mon = MonData(sMon);
        label = mon != nullptr ? mon->name : "Pokemon";
        return label.c_str();
    }
    default:
        return "Back";
    }
}

const char *ViewTitle(View view)
{
    switch (view) {
    case View::Party:
        return "Party";
    case View::Boxes:
        return "PC boxes";
    case View::Give:
        return "Give a Pokemon";
    case View::Bag:
        return "Bag";
    case View::Trainer:
        return "Trainer";
    case View::Events:
        return "Legendary events";
    case View::Species:
        return "Choose a species";
    case View::Move:
        return "Choose a move";
    case View::Item:
        return sPick == Pick::BagAdd ? "Add an item" : "Choose an item";
    case View::Nature:
        return "Choose a nature";
    default:
        return nullptr;
    }
}

} // namespace

void SaveEditor_Draw()
{
    // Back on the page after it wasn't shown: it starts over (the game may have changed)
    const int frame = ImGui::GetFrameCount();
    if (frame > sLastFrame + 1) {
        sStack.resize(1);
        sStack[0].focus = kFirst;
        sPendingFocus = kNoFocus;
        sStatus.clear();
        Changed();
    }
    sLastFrame = frame;

    if (const char *why = SaveEd_Unavailable()) {
        if (sStack.size() > 1) {
            sStack.resize(1);
            sStatus.clear();
        }
        Panel("Not right now", why);
        Note("The save editor changes your party and PC Pokemon, gives Pokemon and items, and edits money, badges "
             "and the legendary Pokemon events.");
        return;
    }

    sWantFocus = sPendingFocus;
    sPendingFocus = kNoFocus;

    const View view = sStack.back().view;
    if (view != View::Home) {
        if (BackButton(BackLabel())) {
            Back();
            return;
        }
        if (const char *title = ViewTitle(view)) {
            Section(title);
        }
    }

    switch (view) {
    case View::Home:
        DrawHome();
        break;
    case View::Party:
        DrawParty();
        break;
    case View::Boxes:
        DrawBoxes();
        break;
    case View::Mon:
        DrawMon();
        break;
    case View::Give:
        DrawGive();
        break;
    case View::Bag:
        DrawBag();
        break;
    case View::Trainer:
        DrawTrainer();
        break;
    case View::Events:
        DrawEvents();
        break;
    case View::Species:
        DrawSpecies();
        break;
    case View::Move:
        DrawMove();
        break;
    case View::Item:
        DrawItem();
        break;
    case View::Nature:
        DrawNature();
        break;
    }

    // A new view starts at its top (after its items: the pad's cursor going to the first one
    // would scroll the old position just enough to show it)
    if (sScrollTop) {
        ImGui::SetScrollY(0.0f);
        sScrollTop = false;
    }
    // The item that was to take the pad's cursor isn't there any more: the first one
    if (sWantFocus != kNoFocus && sPendingFocus == kNoFocus && sWantFocus != kFirst) {
        sPendingFocus = kFirst;
    }
    if (sStack.size() > 1 && sStack.back().view == view && SIM::Settings::PageBack()) {
        Back();
    }
}
