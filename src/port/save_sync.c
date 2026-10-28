// Save sync. See include/port/save_sync.h
#include "port/save_sync.h"

#include <nitro.h>
#include <SDL2/SDL.h>
#include <errno.h>
#include <simulator/sim_backup.h>
#include <simulator/sim_carts.h>
#include <simulator/sim_files.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#define MKDIR(path) mkdir(path, 0777)
#endif

#include "port/autosave.h"
#include "port/nuzlocke.h"
#include "port/save_slots.h"
#include "port/shiny_hunt.h"

#define SETTINGS_FILE "save_sync.ini"
#define CHECK_MS 4000      // how often the folder is looked at during a game
#define CHECK_MS_MENU 1500 // on the main menu, where the other device's saves come in
#define SETTLE_MS 2000     // a save file left alone this long has been written whole
#define NOTICE_MS 4500

// Files that go along with each slot's save: (here, there)
static const char *const kSidecars[][2] = {
    { "save_slot%d.nuzlocke", "slot%d.nuzlocke" },
    { "save_slot%d.hunt", "slot%d.hunt" },
};

typedef struct ThereMeta {
    BOOL valid;
    u64 hash;
    char device[64];
    char savedAt[32];
    char name[16];
    int hours;
    int minutes;
    int badges;
    int seen;
    BOOL pokedex;
} ThereMeta;

typedef struct SlotState {
    BOOL hereKnown;       // the fields below match the file
    long long hereMtime;  // (nanoseconds where the system has them)
    long long hereSize;
    u64 hereHash;         // 0: no save
    u64 hereChangedAtMs;  // when the file was last seen changing
    ThereMeta there;
    BOOL lastLoaded;
    BOOL lastKnown;       // synced with this folder before
    u64 lastHash;
    int state;
    int noticedState;     // the state last told about, so each is told once
    BOOL pullPending;     // the slot in use: brought in as the main menu reloads it
} SlotState;

static BOOL sEnabled = FALSE;
static char sFolder[1024];
static char sLastEvent[160];
static char sPendingNotice[128]; // shown on the next frame (after the game's own "Saved")
static u64 sNextCheckMs = 0;
static BOOL sStartup = FALSE;
static BOOL sFolderProblem = FALSE;
static BOOL sBroughtInCurrent = FALSE;
static SlotState sSlots[SAVE_SLOT_COUNT];
static u8 *sImage = NULL; // SAVE_SLOT_IMAGE_SIZE

// --- settings ----------------------------------------------------------------------------------

static void LoadSettings(void)
{
    sEnabled = FALSE;
    sFolder[0] = '\0';
    FILE *file = fopen(SIM_SettingsPath(SETTINGS_FILE), "r");
    if (file == NULL) {
        return;
    }
    char line[1100];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "Enabled=", 8) == 0) {
            sEnabled = atoi(line + 8) != 0;
        } else if (strncmp(line, "Folder=", 7) == 0) {
            snprintf(sFolder, sizeof(sFolder), "%s", line + 7);
        }
    }
    fclose(file);
}

static void SaveSettings(void)
{
    FILE *file = fopen(SIM_SettingsPath(SETTINGS_FILE), "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "[Sync]\nEnabled=%d\nFolder=%s\n", sEnabled ? 1 : 0, sFolder);
    fclose(file);
}

static void ForgetEverything(void)
{
    memset(sSlots, 0, sizeof(sSlots));
    sNextCheckMs = 0;
}

void SaveSync_Init(void)
{
    LoadSettings();
    ForgetEverything();
}

BOOL SaveSync_Enabled(void)
{
    return sEnabled && sFolder[0] != '\0';
}

void SaveSync_SetEnabled(BOOL enabled)
{
    sEnabled = enabled;
    SaveSettings();
    ForgetEverything();
}

const char *SaveSync_Folder(void)
{
    return sFolder;
}

void SaveSync_SetFolder(const char *path)
{
    snprintf(sFolder, sizeof(sFolder), "%s", path != NULL ? path : "");
    // No trailing separator
    size_t length = strlen(sFolder);
    while (length > 1 && (sFolder[length - 1] == '/' || sFolder[length - 1] == '\\')) {
        sFolder[--length] = '\0';
    }
    if (sFolder[0] != '\0') {
        sEnabled = TRUE;
    }
    SaveSettings();
    ForgetEverything();
}

const char *SaveSync_LastEvent(void)
{
    return sLastEvent;
}

static void SetEvent(const char *text, BOOL notice)
{
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    char when[8] = "";
    if (local != NULL) {
        strftime(when, sizeof(when), "%H:%M", local);
    }
    snprintf(sLastEvent, sizeof(sLastEvent), "%s %s", when, text);
    if (notice) {
        snprintf(sPendingNotice, sizeof(sPendingNotice), "%s", text);
    }
}

// --- paths and files ---------------------------------------------------------------------------

void SaveSync_GameFolder(char *out, int outSize)
{
    // The game's name as the folder's: the same on every device for the same ROM and patches
    const SIM_Cart *cart = SIM_Carts_Current();
    char name[SIM_CART_NAME_LEN];
    snprintf(name, sizeof(name), "%s", cart != NULL && cart->name[0] != '\0' ? cart->name : "Pokemon Platinum");
    for (char *c = name; *c != '\0'; c++) {
        const unsigned char u = (unsigned char)*c;
        const BOOL ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == ' '
            || u == '-' || u == '(' || u == ')' || u == '.' || u == '_';
        if (!ok) {
            *c = '_';
        }
    }
    snprintf(out, outSize, "%s/%s", sFolder, name);
}

static void TherePath(int slot, const char *pattern, char *out, int outSize)
{
    char folder[1100];
    SaveSync_GameFolder(folder, sizeof(folder));
    char file[64];
    snprintf(file, sizeof(file), pattern, slot + 1);
    snprintf(out, outSize, "%s/%s", folder, file);
}

static u64 Hash(const u8 *data, size_t size)
{
    // FNV-1a, 64 bits
    u64 hash = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x100000001B3ULL;
    }
    return hash != 0 ? hash : 1;
}

static BOOL EnsureImage(void)
{
    if (sImage == NULL) {
        sImage = malloc(SAVE_SLOT_IMAGE_SIZE);
    }
    return sImage != NULL;
}

// A save file as the 512 KB image a .sav holds (unwritten flash reads 0xFF): its hash, 0 if
// it has no save
static u64 ReadImage(const char *path, u8 *image)
{
    memset(image, 0xFF, SAVE_SLOT_IMAGE_SIZE);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t got = fread(image, 1, SAVE_SLOT_IMAGE_SIZE, file);
    fclose(file);
    BOOL hasData = FALSE;
    for (size_t i = 0; i < got; i++) {
        if (image[i] != 0x00 && image[i] != 0xFF) {
            hasData = TRUE;
            break;
        }
    }
    return hasData ? Hash(image, SAVE_SLOT_IMAGE_SIZE) : 0;
}

static BOOL StatFile(const char *path, long long *mtime, long long *size)
{
    struct stat info;
    if (stat(path, &info) != 0) {
        return FALSE;
    }
#if defined(__APPLE__)
    *mtime = (long long)info.st_mtimespec.tv_sec * 1000000000LL + info.st_mtimespec.tv_nsec;
#elif defined(_WIN32)
    *mtime = (long long)info.st_mtime * 1000000000LL;
#elif defined(__GLIBC__) && !defined(__USE_XOPEN2K8)
    // (strict C99: glibc names the nanoseconds so)
    *mtime = (long long)info.st_mtime * 1000000000LL + (long long)info.st_mtimensec;
#else
    *mtime = (long long)info.st_mtim.tv_sec * 1000000000LL + info.st_mtim.tv_nsec;
#endif
    *size = (long long)info.st_size;
    return TRUE;
}

// Written next to the target, then renamed over it: whoever reads it (the sync program, the
// other device) never sees half a file
static BOOL WriteFileAtomic(const char *path, const void *data, size_t size)
{
    char tmpPath[1200];
    const char *slash = strrchr(path, '/');
    if (slash != NULL) {
        snprintf(tmpPath, sizeof(tmpPath), "%.*s/.%s.tmp", (int)(slash - path), path, slash + 1);
    } else {
        snprintf(tmpPath, sizeof(tmpPath), ".%s.tmp", path);
    }
    FILE *file = fopen(tmpPath, "wb");
    if (file == NULL) {
        return FALSE;
    }
    BOOL ok = fwrite(data, 1, size, file) == size;
    ok = (fflush(file) == 0) && ok;
    ok = (fclose(file) == 0) && ok;
    if (!ok) {
        remove(tmpPath);
        return FALSE;
    }
#ifdef _WIN32
    remove(path); // rename() doesn't replace on Windows
#endif
    if (rename(tmpPath, path) != 0) {
        remove(tmpPath);
        return FALSE;
    }
    return TRUE;
}

// A small file copied whole (the Nuzlocke and shiny hunt records); a missing source does
// nothing
static void CopySmallFile(const char *from, const char *to)
{
    FILE *file = fopen(from, "rb");
    if (file == NULL) {
        return;
    }
    static char buffer[64 * 1024];
    size_t got = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    WriteFileAtomic(to, buffer, got);
}

static void ParseHex64(const char *text, u64 *out)
{
    u64 value = 0;
    for (; *text != '\0'; text++) {
        const char c = *text;
        int digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (digit < 0) {
            break;
        }
        value = (value << 4) | (u64)digit;
    }
    *out = value;
}

static void ReadThere(int slot, ThereMeta *out)
{
    memset(out, 0, sizeof(*out));
    char path[1200];
    TherePath(slot, "slot%d.txt", path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char line[160];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *value = eq + 1;
        if (strcmp(key, "hash") == 0) {
            ParseHex64(value, &out->hash);
        } else if (strcmp(key, "device") == 0) {
            snprintf(out->device, sizeof(out->device), "%s", value);
        } else if (strcmp(key, "saved") == 0) {
            snprintf(out->savedAt, sizeof(out->savedAt), "%s", value);
        } else if (strcmp(key, "name") == 0) {
            snprintf(out->name, sizeof(out->name), "%s", value);
        } else if (strcmp(key, "hours") == 0) {
            out->hours = atoi(value);
        } else if (strcmp(key, "minutes") == 0) {
            out->minutes = atoi(value);
        } else if (strcmp(key, "badges") == 0) {
            out->badges = atoi(value);
        } else if (strcmp(key, "seen") == 0) {
            out->seen = atoi(value);
        } else if (strcmp(key, "pokedex") == 0) {
            out->pokedex = atoi(value) != 0;
        }
    }
    fclose(file);
    out->valid = out->hash != 0;
    if (out->device[0] == '\0') {
        snprintf(out->device, sizeof(out->device), "the other device");
    }
}

static void LastPath(int slot, char *out, int outSize)
{
    snprintf(out, outSize, "save_slot%d.sync", slot + 1);
}

static void LoadLast(int slot)
{
    SlotState *s = &sSlots[slot];
    if (s->lastLoaded) {
        return;
    }
    s->lastLoaded = TRUE;
    s->lastKnown = FALSE;
    s->lastHash = 0;

    char path[64];
    LastPath(slot, path, sizeof(path));
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return;
    }
    char folder[1100];
    SaveSync_GameFolder(folder, sizeof(folder));
    BOOL sameFolder = FALSE;
    u64 hash = 0;
    char line[1200];
    while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "folder=", 7) == 0) {
            sameFolder = strcmp(line + 7, folder) == 0;
        } else if (strncmp(line, "hash=", 5) == 0) {
            ParseHex64(line + 5, &hash);
        }
    }
    fclose(file);
    // Synced with another folder before: as if never synced here
    if (sameFolder) {
        s->lastKnown = TRUE;
        s->lastHash = hash;
    }
}

static void SaveLast(int slot, u64 hash)
{
    SlotState *s = &sSlots[slot];
    s->lastLoaded = TRUE;
    s->lastKnown = TRUE;
    s->lastHash = hash;

    char path[64];
    LastPath(slot, path, sizeof(path));
    char folder[1100];
    SaveSync_GameFolder(folder, sizeof(folder));
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        return;
    }
    fprintf(file, "folder=%s\nhash=%016llx\n", folder, (unsigned long long)hash);
    fclose(file);
}

// This device's save of the slot: hashed again only when the file changed
static void LookHere(int slot, u64 now)
{
    SlotState *s = &sSlots[slot];
    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));
    long long mtime = 0, size = 0;
    if (!StatFile(path, &mtime, &size)) {
        mtime = -1;
        size = -1;
    }
    if (s->hereKnown && mtime == s->hereMtime && size == s->hereSize) {
        return;
    }
    if (s->hereKnown) {
        s->hereChangedAtMs = now;
    }
    s->hereKnown = TRUE;
    s->hereMtime = mtime;
    s->hereSize = size;
    s->hereHash = mtime < 0 || !EnsureImage() ? 0 : ReadImage(path, sImage);
}

// --- sending and bringing in -------------------------------------------------------------------

static BOOL EnsureGameFolder(void)
{
    char folder[1100];
    SaveSync_GameFolder(folder, sizeof(folder));
    struct stat info;
    if (stat(sFolder, &info) != 0) {
        return FALSE; // the chosen folder is gone (or not readable)
    }
    if (stat(folder, &info) == 0) {
        return TRUE;
    }
    return MKDIR(folder) == 0 || errno == EEXIST;
}

static BOOL Send(int slot)
{
    SlotState *s = &sSlots[slot];
    if (!EnsureImage() || !EnsureGameFolder()) {
        return FALSE;
    }
    char path[SIM_BACKUP_PATH_MAX];
    SaveSlots_FilePath(slot, path, sizeof(path));
    const u64 hash = ReadImage(path, sImage);
    if (hash == 0) {
        return FALSE;
    }

    char there[1200];
    TherePath(slot, "slot%d.sav", there, sizeof(there));
    if (!WriteFileAtomic(there, sImage, SAVE_SLOT_IMAGE_SIZE)) {
        return FALSE;
    }
    for (size_t i = 0; i < sizeof(kSidecars) / sizeof(kSidecars[0]); i++) {
        char from[64];
        snprintf(from, sizeof(from), kSidecars[i][0], slot + 1);
        TherePath(slot, kSidecars[i][1], there, sizeof(there));
        CopySmallFile(from, there);
    }

    // The description last: the other device takes the save once this says it's there
    SaveSlotInfo info;
    SaveSlots_GetInfo(slot, &info);
    char savedAt[32];
    snprintf(savedAt, sizeof(savedAt), "%s", info.savedAt);
    if (savedAt[0] == '\0') {
        time_t now = time(NULL);
        struct tm *local = localtime(&now);
        if (local != NULL) {
            strftime(savedAt, sizeof(savedAt), "%Y-%m-%d %H:%M", local);
        }
    }
    char meta[512];
    int length = snprintf(meta, sizeof(meta),
        "hash=%016llx\ndevice=%s\nsaved=%s\nname=%s\nhours=%d\nminutes=%d\nbadges=%d\nseen=%d\npokedex=%d\n",
        (unsigned long long)hash, SIM_DeviceName(), savedAt, info.hasSummary ? info.name : "", info.hours, info.minutes,
        info.badges, info.seen, info.pokedexObtained ? 1 : 0);
    TherePath(slot, "slot%d.txt", there, sizeof(there));
    if (length <= 0 || !WriteFileAtomic(there, meta, (size_t)length)) {
        return FALSE;
    }

    SaveLast(slot, hash);
    s->hereHash = hash;
    ReadThere(slot, &s->there);
    char text[96];
    snprintf(text, sizeof(text), "Sent slot %d to the sync folder", slot + 1);
    SetEvent(text, FALSE);
    return TRUE;
}

// SAVE_SYNC_IN_STEP when brought in; SAVE_SYNC_ARRIVING while the folder's save isn't the one
// its description says yet
static int BringIn(int slot)
{
    SlotState *s = &sSlots[slot];
    ReadThere(slot, &s->there);
    if (!s->there.valid || !EnsureImage()) {
        return SAVE_SYNC_ARRIVING;
    }
    char there[1200];
    TherePath(slot, "slot%d.sav", there, sizeof(there));
    if (ReadImage(there, sImage) != s->there.hash) {
        return SAVE_SYNC_ARRIVING;
    }
    char err[128] = "";
    if (!SaveSlots_ReplaceImage(slot, sImage, err, sizeof(err))) {
        return SAVE_SYNC_ERROR;
    }
    for (size_t i = 0; i < sizeof(kSidecars) / sizeof(kSidecars[0]); i++) {
        char to[64];
        snprintf(to, sizeof(to), kSidecars[i][0], slot + 1);
        TherePath(slot, kSidecars[i][1], there, sizeof(there));
        CopySmallFile(there, to);
    }
    Nuzlocke_ForgetLoaded();
    ShinyHunt_ForgetLoaded();

    SaveSlotInfo info;
    memset(&info, 0, sizeof(info));
    info.hasData = TRUE;
    info.hasSummary = TRUE;
    snprintf(info.name, sizeof(info.name), "%s", s->there.name);
    info.hours = s->there.hours;
    info.minutes = s->there.minutes;
    info.badges = s->there.badges;
    info.seen = s->there.seen;
    info.pokedexObtained = s->there.pokedex;
    snprintf(info.savedAt, sizeof(info.savedAt), "%s", s->there.savedAt);
    if (info.name[0] != '\0') {
        SaveSlots_WriteInfo(slot, &info);
    }

    SaveLast(slot, s->there.hash);
    if (slot == SaveSlots_Current()) {
        sBroughtInCurrent = TRUE;
    }
    s->hereKnown = FALSE;
    s->hereHash = s->there.hash;
    char text[128];
    snprintf(text, sizeof(text), "Loaded slot %d from %s", slot + 1, s->there.device);
    SetEvent(text, TRUE);
    return SAVE_SYNC_IN_STEP;
}

// --- deciding ------------------------------------------------------------------------------------

static BOOL CanBringIn(int slot)
{
    // Only where nothing holds on to the save: before it's loaded, or on the main menu (the
    // slot in use is then reloaded there)
    return sStartup || SaveSlots_IsMainMenuActive();
}

static void Tell(int slot, int state)
{
    SlotState *s = &sSlots[slot];
    if (state == s->noticedState) {
        return;
    }
    s->noticedState = state;
    char text[128];
    if (state == SAVE_SYNC_CONFLICT) {
        snprintf(text, sizeof(text), "Slot %d was also saved on %s. Choose which to keep in Settings > Saves.", slot + 1,
            s->there.device);
        SetEvent(text, TRUE);
    } else if (state == SAVE_SYNC_NEWER_THERE && slot == SaveSlots_Current()) {
        snprintf(text, sizeof(text), "%s has a newer save. Reset to the title to load it.", s->there.device);
        SetEvent(text, TRUE);
    } else if (state == SAVE_SYNC_ERROR) {
        snprintf(text, sizeof(text), "Save sync: couldn't copy slot %d.", slot + 1);
        SetEvent(text, TRUE);
    }
}

static void CheckSlot(int slot, u64 now, BOOL justSaved)
{
    SlotState *s = &sSlots[slot];
    LookHere(slot, now);
    ReadThere(slot, &s->there);
    LoadLast(slot);

    const u64 here = s->hereHash;
    const u64 there = s->there.valid ? s->there.hash : 0;
    int state;
    if (here == there) {
        state = here == 0 ? SAVE_SYNC_NOTHING : SAVE_SYNC_IN_STEP;
        if (here != 0 && (!s->lastKnown || s->lastHash != here)) {
            SaveLast(slot, here);
        }
        s->pullPending = FALSE;
    } else if (here != 0 && (there == 0 || (s->lastKnown && there == s->lastHash))) {
        // Saved here since: send it once the file is whole
        if (!justSaved && now - s->hereChangedAtMs < SETTLE_MS) {
            state = SAVE_SYNC_SENDING;
        } else {
            state = Send(slot) ? SAVE_SYNC_IN_STEP : SAVE_SYNC_ERROR;
        }
    } else if (there != 0 && (here == 0 || (s->lastKnown && here == s->lastHash))) {
        // Saved on the other device since
        if (!CanBringIn(slot)) {
            state = SAVE_SYNC_NEWER_THERE;
        } else if (slot == SaveSlots_Current() && !sStartup) {
            // Brought in as the main menu reloads it (SaveSync_BeforeLoad)
            char path[1200];
            TherePath(slot, "slot%d.sav", path, sizeof(path));
            if (EnsureImage() && ReadImage(path, sImage) == there) {
                s->pullPending = TRUE;
                SaveSlots_RequestSwitch(slot);
                state = SAVE_SYNC_NEWER_THERE;
            } else {
                state = SAVE_SYNC_ARRIVING;
            }
        } else {
            state = BringIn(slot);
        }
    } else {
        state = SAVE_SYNC_CONFLICT;
    }
    s->state = state;
    Tell(slot, state);
}

static void CheckAll(BOOL justSavedSlot, int savedSlot)
{
    const u64 now = SDL_GetTicks64();
    if (!SaveSync_Enabled()) {
        return;
    }
    if (!SIM_Files_HasStorageAccess() || !EnsureGameFolder()) {
        if (!sFolderProblem) {
            SetEvent("Save sync: the folder can't be used. See Settings > Saves.", TRUE);
        }
        sFolderProblem = TRUE;
        for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
            sSlots[slot].state = SAVE_SYNC_NO_ACCESS;
        }
        return;
    }
    sFolderProblem = FALSE;
    for (int slot = 0; slot < SAVE_SLOT_COUNT; slot++) {
        CheckSlot(slot, now, justSavedSlot && slot == savedSlot);
    }
}

// --- the game's side ------------------------------------------------------------------------------

BOOL SaveSync_AtStartup(void)
{
    if (!SaveSync_Enabled()) {
        return FALSE;
    }
    sStartup = TRUE;
    sBroughtInCurrent = FALSE;
    CheckAll(FALSE, -1);
    sStartup = FALSE;
    return sBroughtInCurrent;
}

void SaveSync_OnSaved(int slot)
{
    if (!SaveSync_Enabled() || slot < 0 || slot >= SAVE_SLOT_COUNT) {
        return;
    }
    sSlots[slot].hereKnown = FALSE;
    const u64 now = SDL_GetTicks64();
    if (!SIM_Files_HasStorageAccess() || !EnsureGameFolder()) {
        sSlots[slot].state = SAVE_SYNC_NO_ACCESS;
        return;
    }
    CheckSlot(slot, now, TRUE);
}

void SaveSync_BeforeLoad(int slot)
{
    if (!SaveSync_Enabled() || slot < 0 || slot >= SAVE_SLOT_COUNT || !sSlots[slot].pullPending) {
        return;
    }
    SlotState *s = &sSlots[slot];
    s->pullPending = FALSE;
    sStartup = TRUE; // nothing holds on to the save now
    s->state = BringIn(slot);
    sStartup = FALSE;
    Tell(slot, s->state);
}

void SaveSync_Update(void)
{
    if (sPendingNotice[0] != '\0') {
        Autosave_ShowNoticeFor(sPendingNotice, NOTICE_MS);
        sPendingNotice[0] = '\0';
    }
    if (!SaveSync_Enabled()) {
        return;
    }
    const u64 now = SDL_GetTicks64();
    if (now < sNextCheckMs) {
        return;
    }
    sNextCheckMs = now + (SaveSlots_IsMainMenuActive() ? CHECK_MS_MENU : CHECK_MS);
    CheckAll(FALSE, -1);
}

void SaveSync_CheckNow(void)
{
    sNextCheckMs = 0;
}

void SaveSync_GetSlot(int slot, SaveSyncSlot *out)
{
    memset(out, 0, sizeof(*out));
    if (!SaveSync_Enabled()) {
        out->state = SAVE_SYNC_OFF;
        return;
    }
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) {
        return;
    }
    const SlotState *s = &sSlots[slot];
    out->state = s->state;
    out->thereHasSave = s->there.valid;
    snprintf(out->thereDevice, sizeof(out->thereDevice), "%s", s->there.device);
    snprintf(out->thereSavedAt, sizeof(out->thereSavedAt), "%s", s->there.savedAt);
    snprintf(out->thereName, sizeof(out->thereName), "%s", s->there.name);
    out->thereHours = s->there.hours;
    out->thereMinutes = s->there.minutes;
    out->thereBadges = s->there.badges;
}

BOOL SaveSync_Resolve(int slot, BOOL keepThis, char *err, int errSize)
{
    if (!SaveSync_Enabled() || slot < 0 || slot >= SAVE_SLOT_COUNT) {
        snprintf(err, errSize, "Save sync is off.");
        return FALSE;
    }
    SlotState *s = &sSlots[slot];
    if (!SIM_Files_HasStorageAccess() || !EnsureGameFolder()) {
        snprintf(err, errSize, "The sync folder can't be used.");
        return FALSE;
    }
    if (keepThis) {
        if (!Send(slot)) {
            snprintf(err, errSize, "Couldn't copy the save to the folder.");
            s->state = SAVE_SYNC_ERROR;
            return FALSE;
        }
        s->state = SAVE_SYNC_IN_STEP;
        s->noticedState = SAVE_SYNC_IN_STEP;
        return TRUE;
    }
    if (!SaveSlots_IsMainMenuActive()) {
        snprintf(err, errSize, "Go back to the main menu first (Settings > Games > Reset to the title screen).");
        return FALSE;
    }
    if (slot == SaveSlots_Current()) {
        // Taken as the menu reloads the slot
        char path[1200];
        TherePath(slot, "slot%d.sav", path, sizeof(path));
        ReadThere(slot, &s->there);
        if (!s->there.valid || !EnsureImage() || ReadImage(path, sImage) != s->there.hash) {
            snprintf(err, errSize, "The other device's save hasn't fully arrived yet.");
            return FALSE;
        }
        s->pullPending = TRUE;
        SaveSlots_RequestSwitch(slot);
        return TRUE;
    }
    const int state = BringIn(slot);
    s->state = state;
    s->noticedState = state;
    if (state != SAVE_SYNC_IN_STEP) {
        snprintf(err, errSize, state == SAVE_SYNC_ARRIVING ? "The other device's save hasn't fully arrived yet."
                                                            : "That save couldn't be loaded.");
        return FALSE;
    }
    return TRUE;
}
