// JustSkip — Game speed control for Crimson Desert
// Copyright (c) 2026 wealdly. All rights reserved.
// Hooks QueryPerformanceCounter to scale time. No Cheat Engine needed.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Xinput.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <intrin.h>
#include <Psapi.h>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "psapi.lib")

#include "MinHook.h"

// ---------------------------------------------------------------------------
//  XInput — types and state (implementation after Log/Config below)
// ---------------------------------------------------------------------------
typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD dwUserIndex, XINPUT_STATE* pState);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);
static PFN_XInputSetState g_origXInputSetState  = nullptr; // trampoline for game's SetState
static PFN_XInputGetState g_origXInputGetState  = nullptr; // MinHook trampoline for game's GetState
static PFN_XInputGetState g_origXInputGetStateEx = nullptr; // MinHook trampoline for game's GetStateEx (ordinal 100)
static bool               g_xinputLoaded         = false;  // set after hooks installed
static WORD               g_suppressMask         = 0;       // modifier | all slot gamepad buttons (precomputed)
static bool               g_suppressButtons      = true;    // INI: SuppressButtons=1
static bool               g_suppressHookFailed   = false;   // set on hook failure; prevents hot-reload re-enabling
static std::atomic<WORD>  g_suppressLastLog{0};             // last stripped mask logged (dedup, written from hook thread)

// Shadow state — raw button data saved by hook BEFORE stripping, read by hotkey thread.
// Avoids independent XInput calls from the hotkey thread, which fail when Steam routes
// button state only through the module the game originally loaded (not system32).
//
// Debounce: the game polls XInput many times per frame from multiple code paths.
// A single poll returning wButtons=0 must NOT immediately clear the shadow — it may
// just be a sub-frame gap. Only clear buttons after they've been consistently absent
// for SHADOW_DEBOUNCE_MS, matching the release debounce in SuppressButtons.
static const DWORD SHADOW_DEBOUNCE_MS = 32; // ~2 frames @ 60fps

struct PadShadow {
    volatile WORD  buttons     = 0;
    volatile BYTE  ltrigger    = 0;
    volatile bool  valid       = false;
    volatile DWORD btnZeroTick = 0;   // GetTickCount when buttons first became 0
};
static PadShadow g_padShadow[XUSER_MAX_COUNT];



// ---------------------------------------------------------------------------
//  Configuration (loaded from INI)
// ---------------------------------------------------------------------------
struct SpeedSlot {
    int    vkKey;         // Virtual-key code (keyboard)
    WORD   gamepadButton; // XInput button flag (0 = none)
    bool   isHold;        // true = active only while held, false = toggle
    float  speed;         // multiplier
    bool   active;        // runtime state for toggles
};

static SpeedSlot  g_slots[6];
static int        g_slotCount  = 0;
static bool       g_enabled    = true;
static int        g_reloadKey  = 0;
static bool       g_gamepadEnabled    = true;  // master toggle for all gamepad functionality
static WORD       g_gamepadModifier   = 0;  // XInput buttons that must be held
static WORD       g_gamepadReloadBtn  = 0;  // XInput button for reload (with modifier)
static int        g_gamepadIndex      = 0;  // Controller index 0-3
static bool       g_debugLog   = false;
static char       g_iniPath[MAX_PATH];
static char       g_logPath[MAX_PATH];

// Combat detection (multi-signal fusion)
static bool       g_combatDetect    = false;  // feature enabled?
static float      g_combatSpeed     = 1.0f;   // speed during combat (e.g. 0.95)
static DWORD      g_combatTimeout   = 3000;   // ms after last signal to exit combat
static WORD       g_combatVibThresh = 5000;   // vibration intensity threshold
static WORD       g_combatAttackMask= 0;      // XInput buttons considered "attack"
static int        g_combatMashCount = 3;      // attack presses within window = combat
static DWORD      g_combatMashWindow= 2000;   // ms window for mash detection
static std::atomic<DWORD> g_lastCombatSignal{0};  // unified: any combat signal timestamp

// Signature-based combat detection (advanced, optional)
static BYTE*      g_combatFlagAddr  = nullptr; // resolved ptr to game's combat flag
static char       g_combatSigPattern[512] = {}; // AOB pattern from INI
static int        g_combatSigOffset = 0;        // offset from pattern match to flag

// Hold-key safety
static DWORD      g_maxHoldMs       = 120000;   // max continuous hold before force-release
static bool       g_holdFocusCheck  = true;      // release holds when game not foreground

// On-screen display (OSD)
static bool       g_osdEnabled      = true;
static DWORD      g_osdDuration     = 2000;      // ms to show notification
static int        g_osdFontSize     = 22;
static int        g_osdPosX         = 20;        // offset from top-left of game window
static int        g_osdPosY         = 20;
static HWND       g_osdWnd          = nullptr;
static DWORD      g_osdThreadId     = 0;

// Startup grace period — ignore speed changes while game initializes
static DWORD      g_startupGraceMs  = 5000;    // ms after hook install before allowing speed
static DWORD      g_hookInstalledAt = 0;        // tick when QPC hook went live

// ---------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------
static void Log(const char* fmt, ...) {
    if (!g_debugLog) return;
    FILE* f = nullptr;
    fopen_s(&f, g_logPath, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// ---------------------------------------------------------------------------
//  INI helpers
// ---------------------------------------------------------------------------
static int ReadIniInt(const char* section, const char* key, int def) {
    return GetPrivateProfileIntA(section, key, def, g_iniPath);
}

static float ReadIniFloat(const char* section, const char* key, float def) {
    char buf[64];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath);
    if (buf[0] == '\0') return def;
    return (float)atof(buf);
}

static int ParseHexOrDec(const char* s) {
    if (!s || !*s) return 0;
    return (int)strtoul(s, nullptr, 16);
}

static int ReadIniHex(const char* section, const char* key, int def) {
    char buf[32];
    GetPrivateProfileStringA(section, key, "", buf, sizeof(buf), g_iniPath);
    if (buf[0] == '\0') return def;
    return ParseHexOrDec(buf);
}

// ---------------------------------------------------------------------------
//  Load config
// ---------------------------------------------------------------------------
static void LoadConfig() {
    bool wasDebug = g_debugLog;  // preserve caller's debug state
    g_enabled  = ReadIniInt("Settings", "Enabled", 1) != 0;
    g_debugLog = ReadIniInt("Settings", "DebugLog", 0) != 0;
    if (wasDebug) g_debugLog = true;  // don't let INI silence an active session
    g_reloadKey = ReadIniHex("Settings", "ReloadKey", 0x00);

    // Gamepad settings
    g_gamepadEnabled   = ReadIniInt("Settings", "GamepadEnabled", 1) != 0;
    g_gamepadModifier  = (WORD)ReadIniHex("Settings", "GamepadModifier", 0x0000);
    g_gamepadReloadBtn = (WORD)ReadIniHex("Settings", "GamepadReloadButton", 0x0000);
    g_gamepadIndex     = ReadIniInt("Settings", "GamepadIndex", 0);
    if (g_gamepadIndex < 0 || g_gamepadIndex > 3) g_gamepadIndex = 0;
    g_suppressButtons  = !g_suppressHookFailed && ReadIniInt("Settings", "SuppressButtons", 1) != 0;

    // Combat detection (multi-signal fusion)
    g_combatDetect    = ReadIniInt("Combat", "Enabled", 0) != 0;
    g_combatSpeed     = ReadIniFloat("Combat", "CombatSpeed", 1.0f);
    g_combatTimeout   = (DWORD)ReadIniInt("Combat", "Timeout", 3000);
    g_combatVibThresh = (WORD)ReadIniInt("Combat", "VibrationThreshold", 5000);
    g_combatMashCount = ReadIniInt("Combat", "MashCount", 3);
    g_combatMashWindow= (DWORD)ReadIniInt("Combat", "MashWindow", 2000);

    // Attack buttons mask — default: RT(bit from triggers not in wButtons), X, Y, RB
    // XInput wButtons doesn't include triggers, so we use face buttons + bumpers
    g_combatAttackMask = (WORD)ReadIniHex("Combat", "AttackButtons", 0xC200); // X+Y+RB

    // Signature pattern (advanced)
    GetPrivateProfileStringA("Combat", "SignaturePattern", "",
                             g_combatSigPattern, sizeof(g_combatSigPattern), g_iniPath);
    g_combatSigOffset = ReadIniInt("Combat", "SignatureOffset", 0);

    Log("Combat detection: %s, speed=%.2f, timeout=%lu ms, vibThresh=%u",
        g_combatDetect ? "ON" : "OFF", g_combatSpeed, g_combatTimeout, g_combatVibThresh);
    Log("  AttackButtons=0x%04X, MashCount=%d, MashWindow=%lu",
        g_combatAttackMask, g_combatMashCount, g_combatMashWindow);
    if (g_combatSigPattern[0])
        Log("  SignaturePattern: %s (offset=%d)", g_combatSigPattern, g_combatSigOffset);

    Log("Gamepad: %s, modifier=0x%04X reload=0x%04X index=%d",
        g_gamepadEnabled ? "ON" : "OFF", g_gamepadModifier, g_gamepadReloadBtn, g_gamepadIndex);

    // Hold-key safety
    g_maxHoldMs    = (DWORD)ReadIniInt("Settings", "MaxHoldSeconds", 120) * 1000;
    g_holdFocusCheck = ReadIniInt("Settings", "HoldFocusCheck", 1) != 0;
    g_startupGraceMs = (DWORD)ReadIniInt("Settings", "StartupGraceSeconds", 5) * 1000;
    Log("Hold safety: maxHold=%lus focusCheck=%d startupGrace=%lus",
        g_maxHoldMs / 1000, g_holdFocusCheck, g_startupGraceMs / 1000);

    // OSD (on-screen display)
    g_osdEnabled  = ReadIniInt("OSD", "Enabled", 1) != 0;
    g_osdDuration = (DWORD)ReadIniInt("OSD", "Duration", 2000);
    g_osdFontSize = ReadIniInt("OSD", "FontSize", 22);
    g_osdPosX     = ReadIniInt("OSD", "PosX", 20);
    g_osdPosY     = ReadIniInt("OSD", "PosY", 20);
    Log("OSD: %s, duration=%lums, font=%d, pos=(%d,%d)",
        g_osdEnabled ? "ON" : "OFF", g_osdDuration, g_osdFontSize, g_osdPosX, g_osdPosY);

    // Read speed slots — up to 6 sections named Speed1..Speed6
    g_slotCount = 0;
    for (int i = 1; i <= 6 && g_slotCount < 6; i++) {
        char section[32];
        sprintf_s(section, "Speed%d", i);

        char keyBuf[32];
        GetPrivateProfileStringA(section, "Hotkey", "", keyBuf, sizeof(keyBuf), g_iniPath);

        char padBuf[32];
        GetPrivateProfileStringA(section, "GamepadButton", "", padBuf, sizeof(padBuf), g_iniPath);

        // Skip slot only if BOTH keyboard and gamepad are unconfigured
        if (keyBuf[0] == '\0' && padBuf[0] == '\0') continue;

        SpeedSlot& s = g_slots[g_slotCount];
        s.vkKey        = ParseHexOrDec(keyBuf);
        s.gamepadButton = (WORD)ParseHexOrDec(padBuf);
        s.isHold       = ReadIniInt(section, "Hold", 0) != 0;
        s.speed        = ReadIniFloat(section, "Speed", 1.0f);
        s.active       = false;
        g_slotCount++;

        Log("  Slot %d: key=0x%02X pad=0x%04X hold=%d speed=%.2f",
            i, s.vkKey, s.gamepadButton, s.isHold, s.speed);
    }

    Log("Config loaded: %d slots, enabled=%d", g_slotCount, g_enabled);

    // Precompute suppress mask used by HookedXInputGetState
    g_suppressMask = g_gamepadModifier;
    for (int i = 0; i < g_slotCount; i++)
        g_suppressMask |= g_slots[i].gamepadButton;
    Log("Button suppression: %s, mask=0x%04X",
        g_suppressButtons ? "ON" : "OFF", g_suppressMask);
}

// ---------------------------------------------------------------------------
//  AOB Signature Scanner (for combat flag detection)
// ---------------------------------------------------------------------------
static bool ParseAOB(const char* pattern, BYTE* out, bool* mask, int maxLen, int& outLen) {
    outLen = 0;
    const char* p = pattern;
    while (*p && outLen < maxLen) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p == '?' && (*(p+1) == '?' || *(p+1) == ' ' || *(p+1) == '\0')) {
            out[outLen] = 0;
            mask[outLen] = false; // wildcard
            outLen++;
            p += (*p == '?' && *(p+1) == '?') ? 2 : 1;
        } else {
            char hex[3] = { p[0], p[1], 0 };
            out[outLen] = (BYTE)strtoul(hex, nullptr, 16);
            mask[outLen] = true;
            outLen++;
            p += 2;
        }
    }
    return outLen > 0;
}

static BYTE* ScanSignature(const char* pattern, int offset) {
    BYTE sig[256];
    bool msk[256];
    int  sigLen = 0;

    if (!ParseAOB(pattern, sig, msk, 256, sigLen) || sigLen == 0) {
        Log("Signature: failed to parse pattern");
        return nullptr;
    }

    // Scan the main executable module's .text section
    HMODULE hExe = GetModuleHandleA(nullptr);
    if (!hExe) return nullptr;

    BYTE* base = (BYTE*)hExe;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt  = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);

    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            BYTE* start = base + sec[i].VirtualAddress;
            DWORD size  = sec[i].Misc.VirtualSize;

            for (DWORD j = 0; j <= size - sigLen; j++) {
                bool found = true;
                for (int k = 0; k < sigLen; k++) {
                    if (msk[k] && start[j + k] != sig[k]) {
                        found = false;
                        break;
                    }
                }
                if (found) {
                    BYTE* match = start + j;
                    Log("Signature found at %p (base+0x%llX)",
                        match, (ULONGLONG)(match - base));

                    // If offset is relative (dereference a 32-bit RIP-relative offset)
                    // Common pattern: instruction at match+offset contains a rel32
                    // The combat flag = match+offset+4 + *(int32_t*)(match+offset)
                    if (offset >= 0) {
                        BYTE* flagAddr = match + offset;
                        // Check if this looks like a RIP-relative address
                        // (offset points to a 4-byte displacement in the instruction)
                        int32_t relDisp = *(int32_t*)flagAddr;
                        BYTE* resolved = flagAddr + 4 + relDisp;
                        Log("Signature: offset=%d, rel32=%d, resolved=%p", offset, relDisp, resolved);
                        return resolved;
                    }
                    return match;
                }
            }
        }
    }
    Log("Signature: pattern not found in executable");
    return nullptr;
}

static void ResolveCombatSignature() {
    if (g_combatSigPattern[0] == '\0') return;
    g_combatFlagAddr = ScanSignature(g_combatSigPattern, g_combatSigOffset);
    if (g_combatFlagAddr) {
        Log("Combat flag resolved at %p (value=%u)", g_combatFlagAddr, *g_combatFlagAddr);
    } else {
        Log("WARNING: Combat signature scan failed — falling back to signal fusion");
    }
}

typedef DWORD(WINAPI* PFN_XInputGetStateEx)(DWORD dwUserIndex, XINPUT_STATE* pState);

// XInputSetState hook — detect vibration (combat indicator)
static DWORD WINAPI HookedXInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
    if (g_combatDetect && pVibration) {
        WORD maxMotor = pVibration->wLeftMotorSpeed > pVibration->wRightMotorSpeed
                      ? pVibration->wLeftMotorSpeed : pVibration->wRightMotorSpeed;
        // Only count vibration above the threshold (filters horse ride, cutscene rumble)
        if (maxMotor >= g_combatVibThresh) {
            DWORD now = GetTickCount();
            DWORD prev = g_lastCombatSignal.load(std::memory_order_relaxed);
            g_lastCombatSignal.store(now, std::memory_order_relaxed);
            if (prev == 0 || (now - prev) > g_combatTimeout) {
                Log("Combat signal: vibration (L=%u R=%u, max=%u >= thresh=%u)",
                    pVibration->wLeftMotorSpeed, pVibration->wRightMotorSpeed,
                    maxMotor, g_combatVibThresh);
            }
        }
    }
    return g_origXInputSetState ? g_origXInputSetState(dwUserIndex, pVibration) : ERROR_SUCCESS;
}

// Shared suppression logic — called by both GetState and GetStateEx hooks.
// Strategy: eat-and-replay. Back is ALWAYS stripped while held. On release:
//   - If held < 250ms and no speed button was pressed → inject Back for ~150ms
//     so the game registers the quick tap (zoom toggle).
//   - If held >= 250ms OR a speed button was pressed → game never sees Back.
//
// Release debounce: the game polls XInput several times per frame from different
// codepaths. A single poll returning Back=0 should not count as "released" — only
// consistent absence for >= RELEASE_DEBOUNCE_MS counts. Without this, a brief
// gap between polls fires the quick-tap replay immediately, which then holds the
// modifier in the injected/replaying state so the hotkey thread never sees it as
// physically held and combos never fire.
static __forceinline void SuppressButtons(XINPUT_STATE* pState) {
    static DWORD s_modDownTick    = 0;
    static DWORD s_modUpTick      = 0;
    static DWORD s_replayUntil    = 0;
    static bool  s_comboSeen      = false;

    static const DWORD RELEASE_DEBOUNCE_MS = SHADOW_DEBOUNCE_MS;

    const WORD speedMask = g_suppressMask & ~g_gamepadModifier;

    // Derive held state from the OR of all shadow slots — NOT from pState->Gamepad.wButtons.
    // The game polls all 4 XInput indices; the virtual slot (often index 0) returns wButtons=0
    // even while the physical controller (index 1+) holds Back. Reading only the current slot
    // causes false releases that trigger the debounce countdown and let Back bleed through.
    bool modHeld      = false;
    bool speedHeld    = false;
    for (int _si = 0; _si < XUSER_MAX_COUNT; _si++) {
        const PadShadow& _sh = g_padShadow[_si];
        if (_sh.valid) {
            if ((_sh.buttons & g_gamepadModifier) == g_gamepadModifier) modHeld   = true;
            if (_sh.buttons & speedMask)                                 speedHeld = true;
        }
    }

    // Replay phase: check without GetTickCount first — s_replayUntil == 0 is the common path
    if (s_replayUntil != 0) {
        DWORD now = GetTickCount();
        if (now <= s_replayUntil && !modHeld) {
            pState->Gamepad.wButtons |= g_gamepadModifier;
            return;
        }
        s_replayUntil = 0;
    }

    if (!modHeld) {
        if (s_modDownTick != 0) {
            DWORD now = GetTickCount();
            if (s_modUpTick == 0) s_modUpTick = now;
            if ((now - s_modUpTick) >= RELEASE_DEBOUNCE_MS) {
                if (!s_comboSeen && (now - s_modDownTick) < 250) {
                    s_replayUntil = now + 150;
                    pState->Gamepad.wButtons |= g_gamepadModifier;
                    Log("GetState hook: quick-tap replay (held %lums)",
                        s_modUpTick - s_modDownTick);
                }
                s_modDownTick = 0;
                s_modUpTick   = 0;
                s_comboSeen   = false;
                if (g_debugLog) g_suppressLastLog.store(0, std::memory_order_relaxed);
            }
        }
        return;
    }

    // Modifier is physically held
    s_modUpTick = 0;
    if (s_modDownTick == 0)
        s_modDownTick = GetTickCount();

    if (speedHeld)
        s_comboSeen = true;

    WORD before = pState->Gamepad.wButtons;
    pState->Gamepad.wButtons &= ~g_suppressMask;
    if (g_debugLog) {
        WORD stripped = before & g_suppressMask;
        if (stripped != 0) {
            WORD prev = g_suppressLastLog.exchange(stripped, std::memory_order_relaxed);
            if (stripped != prev)
                Log("GetState hook: stripped 0x%04X from game wButtons", stripped);
        } else {
            g_suppressLastLog.store(0, std::memory_order_relaxed);
        }
    }
}

// Shared GetState hook body — called by both standard and Ex variants.
// Populates the shadow state buffer (raw buttons BEFORE stripping) so the
// hotkey thread can read real physical state without making its own XInput calls.
static __forceinline DWORD XInputGetStateImpl(DWORD dwUserIndex, XINPUT_STATE* pState,
                                              PFN_XInputGetState orig) {
    DWORD result = orig(dwUserIndex, pState);
    if (dwUserIndex < XUSER_MAX_COUNT && pState) {
        PadShadow& sh = g_padShadow[dwUserIndex];
        if (result == ERROR_SUCCESS) {
            WORD rawBtns = pState->Gamepad.wButtons;
            if (rawBtns != 0) {
                sh.buttons     = rawBtns;
                sh.btnZeroTick = 0;
            } else if (sh.buttons != 0) {
                // Only pay for GetTickCount when there's a non-zero value to potentially clear
                DWORD tick = GetTickCount();
                if (sh.btnZeroTick == 0) sh.btnZeroTick = tick;
                if ((tick - sh.btnZeroTick) >= SHADOW_DEBOUNCE_MS)
                    sh.buttons = 0;
            }
            sh.ltrigger = pState->Gamepad.bLeftTrigger;
            sh.valid    = true;
        } else if (result == ERROR_DEVICE_NOT_CONNECTED) {
            sh.valid       = false;
            sh.buttons     = 0;
            sh.btnZeroTick = 0;
        }
    }
    if (result == ERROR_SUCCESS && pState &&
        g_suppressButtons && g_suppressMask != 0 && g_gamepadModifier != 0)
        SuppressButtons(pState);
    return result;
}

// XInputGetState hook (standard export + ordinal 3)
static DWORD WINAPI HookedXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    return XInputGetStateImpl(dwUserIndex, pState, g_origXInputGetState);
}

// XInputGetStateEx hook (undocumented ordinal 100 — used by many games)
static DWORD WINAPI HookedXInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState) {
    return XInputGetStateImpl(dwUserIndex, pState, g_origXInputGetStateEx);
}

// ---------------------------------------------------------------------------
//  QPC caller bypass — let frame-generation DLLs see real (unscaled) time
// ---------------------------------------------------------------------------
struct ModuleRange {
    uintptr_t base;
    uintptr_t end;
};
static ModuleRange g_bypassModules[16] = {};
static int         g_bypassCount       = 0;

static void CacheBypassModule(const char* name) {
    if (g_bypassCount >= 16) return;
    HMODULE hMod = GetModuleHandleA(name);
    if (!hMod) return;
    MODULEINFO mi = {};
    if (GetModuleInformation(GetCurrentProcess(), hMod, &mi, sizeof(mi))) {
        g_bypassModules[g_bypassCount].base = (uintptr_t)mi.lpBaseOfDll;
        g_bypassModules[g_bypassCount].end  = (uintptr_t)mi.lpBaseOfDll + mi.SizeOfImage;
        g_bypassCount++;
        Log("QPC bypass: %s [0x%llX - 0x%llX]", name,
            (unsigned long long)g_bypassModules[g_bypassCount-1].base,
            (unsigned long long)g_bypassModules[g_bypassCount-1].end);
    }
}

static void CacheBypassModules() {
    g_bypassCount = 0;
    // OptiScaler / FSR frame generation modules
    CacheBypassModule("version.dll");                             // OptiScaler proxy
    CacheBypassModule("OptiScaler.dll");                          // alternate name
    CacheBypassModule("amd_fidelityfx_framegeneration_dx12.dll"); // FSR FG
    CacheBypassModule("amd_fidelityfx_dx12.dll");                 // FSR core
    // Streamline / DLSS-G frame generation
    CacheBypassModule("sl.dlss_g.dll");
    CacheBypassModule("sl.common.dll");
    CacheBypassModule("sl.interposer.dll");
    // d3d12core.dll is OptiScaler's proxy in the d3d12/ subfolder
    CacheBypassModule("d3d12core.dll");
    Log("QPC bypass: %d modules cached", g_bypassCount);
}

static __forceinline bool IsCallerBypassed() {
    void* retAddr = _ReturnAddress();
    uintptr_t addr = (uintptr_t)retAddr;
    for (int i = 0; i < g_bypassCount; i++) {
        if (addr >= g_bypassModules[i].base && addr < g_bypassModules[i].end)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
//  QPC time scaling
// ---------------------------------------------------------------------------
typedef BOOL(WINAPI* PFN_QPC)(LARGE_INTEGER* lpCounter);
static PFN_QPC g_origQPC = nullptr;

static SRWLOCK            g_timeLock    = SRWLOCK_INIT;
static LARGE_INTEGER      g_realBase    = {};
static LARGE_INTEGER      g_fakeBase    = {};
static double             g_curSpeed    = 1.0;
static bool               g_timeInited  = false;
static std::atomic<bool>  g_everScaled{false};     // set on first non-1.0 speed; enables scaling path

static void SetSpeed(double speed) {
    if (speed != 1.0)
        g_everScaled.store(true, std::memory_order_relaxed);

    AcquireSRWLockExclusive(&g_timeLock);

    LARGE_INTEGER realNow;
    g_origQPC(&realNow);

    if (g_timeInited) {
        LONGLONG realDelta = realNow.QuadPart - g_realBase.QuadPart;
        if (realDelta > 0)
            g_fakeBase.QuadPart += (LONGLONG)((double)realDelta * g_curSpeed);
    } else {
        g_fakeBase = realNow;
        g_timeInited = true;
    }

    g_realBase = realNow;
    g_curSpeed = speed;

    ReleaseSRWLockExclusive(&g_timeLock);
}

static BOOL WINAPI HookedQPC(LARGE_INTEGER* lpCounter) {
    if (!lpCounter) return FALSE;

    // Check bypass BEFORE calling real QPC — bypassed callers (OptiScaler, DLSS-G, etc.)
    // call QPC very frequently; the old order caused every bypassed call to invoke real QPC
    // twice (once here, once via g_origQPC below), doubling their overhead.
    if (g_bypassCount > 0 && IsCallerBypassed()) {
        return g_origQPC(lpCounter);
    }

    // Fast pass-through when no speed change has ever been applied.
    // Eliminates all lock/math overhead while the mod is idle at 1x speed.
    if (!g_everScaled.load(std::memory_order_relaxed)) {
        return g_origQPC(lpCounter);
    }

    LARGE_INTEGER realNow;
    g_origQPC(&realNow);

    AcquireSRWLockShared(&g_timeLock);

    if (!g_timeInited) {
        ReleaseSRWLockShared(&g_timeLock);
        AcquireSRWLockExclusive(&g_timeLock);
        if (!g_timeInited) {
            g_realBase = realNow;
            g_fakeBase = realNow;
            g_timeInited = true;
        }
        *lpCounter = realNow;
        ReleaseSRWLockExclusive(&g_timeLock);
        return TRUE;
    }

    LONGLONG realDelta = realNow.QuadPart - g_realBase.QuadPart;
    double speed = g_curSpeed;

    if (realDelta > 0) {
        lpCounter->QuadPart = g_fakeBase.QuadPart + (LONGLONG)((double)realDelta * speed);
    } else {
        lpCounter->QuadPart = g_fakeBase.QuadPart;
    }

    ReleaseSRWLockShared(&g_timeLock);
    return TRUE;
}

// ---------------------------------------------------------------------------
//  On-screen display (OSD) — lightweight overlay for speed notifications
// ---------------------------------------------------------------------------
#define WM_OSD_SHOW  (WM_USER + 100)
#define OSD_TIMER_ID 1

static char g_osdText[128] = {};

static void OSD_Paint(HWND hwnd) {
    char text[128];
    strncpy_s(text, g_osdText, _TRUNCATE);
    if (text[0] == '\0') { ShowWindow(hwnd, SW_HIDE); return; }

    // Find game window by PID (GetForegroundWindow is unreliable during loading screens)
    RECT gameRect = {};
    DWORD myPid = GetCurrentProcessId();
    HWND gameWnd = nullptr;
    HWND candidate = nullptr;
    while ((candidate = FindWindowExA(nullptr, candidate, nullptr, nullptr)) != nullptr) {
        DWORD wndPid = 0;
        GetWindowThreadProcessId(candidate, &wndPid);
        if (wndPid == myPid && IsWindowVisible(candidate) && candidate != hwnd) {
            RECT r;
            if (GetWindowRect(candidate, &r) && (r.right - r.left) > 400) {
                gameWnd = candidate;
                gameRect = r;
                break;
            }
        }
    }

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);

    HFONT font = CreateFontA(-g_osdFontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    SelectObject(memDC, font);

    SIZE sz;
    GetTextExtentPoint32A(memDC, text, (int)strlen(text), &sz);
    int w = sz.cx + 24, h = sz.cy + 12;

    // Create 32-bit DIB for per-pixel alpha
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    DWORD* bits = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, (void**)&bits, nullptr, 0);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, dib);
    SelectObject(memDC, font);

    // Fill background: semi-transparent dark
    for (int i = 0; i < w * h; i++)
        bits[i] = 0xC0181818;  // ARGB: ~75% opaque near-black

    // Draw text — GDI ignores alpha, so we detect changed pixels and fix alpha after
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, RGB(255, 255, 255));
    RECT rc = { 12, 6, w - 12, h - 6 };
    DrawTextA(memDC, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // Fix alpha: GDI sets alpha=0 on text pixels, so detect non-black and force alpha
    for (int i = 0; i < w * h; i++) {
        BYTE r = (bits[i] >> 16) & 0xFF;
        BYTE g = (bits[i] >> 8) & 0xFF;
        BYTE b = bits[i] & 0xFF;
        BYTE a = (bits[i] >> 24) & 0xFF;
        if (a == 0 && (r > 0x20 || g > 0x20 || b > 0x20)) {
            // Text pixel — make fully opaque and pre-multiply
            bits[i] = (0xFF << 24) | (r << 16) | (g << 8) | b;
        } else {
            // Background — pre-multiply alpha
            bits[i] = ((DWORD)a << 24) |
                      (((r * a) / 255) << 16) |
                      (((g * a) / 255) << 8) |
                      ((b * a) / 255);
        }
    }

    POINT pos = { gameRect.left + g_osdPosX, gameRect.top + g_osdPosY };
    SIZE size = { w, h };
    POINT src = { 0, 0 };
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(hwnd, screenDC, &pos, &size, memDC, &src, 0, &blend, ULW_ALPHA);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteObject(font);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

static LRESULT CALLBACK OsdWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_OSD_SHOW:
        OSD_Paint(hwnd);
        SetTimer(hwnd, OSD_TIMER_ID, g_osdDuration, nullptr);
        return 0;
    case WM_TIMER:
        if (wParam == OSD_TIMER_ID) {
            KillTimer(hwnd, OSD_TIMER_ID);
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI OsdThread(LPVOID) {
    WNDCLASSEXA wc = { sizeof(wc) };
    wc.lpfnWndProc   = OsdWndProc;
    wc.hInstance      = GetModuleHandleA(nullptr);
    wc.lpszClassName  = "JustSkipOSD";
    RegisterClassExA(&wc);

    g_osdWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        "JustSkipOSD", "", WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_osdWnd) {
        Log("WARNING: OSD window creation failed (err=%lu)", GetLastError());
        return 1;
    }
    Log("OSD window created");

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}

static void ShowOSD(const char* fmt, ...) {
    if (!g_osdEnabled || !g_osdWnd) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_osdText, sizeof(g_osdText), fmt, ap);
    va_end(ap);
    PostMessageA(g_osdWnd, WM_OSD_SHOW, 0, 0);
}

// ---------------------------------------------------------------------------
//  Hotkey polling thread
// ---------------------------------------------------------------------------
static DWORD WINAPI HotkeyThread(LPVOID) {
    Log("Hotkey thread started");

    if (g_gamepadEnabled && g_gamepadModifier != 0) {
        if (g_xinputLoaded)
            Log("Gamepad support active");
    } else {
        Log("Gamepad disabled — keyboard only");
    }

    bool prevDown[6] = {};
    DWORD holdStart[6] = {};  // tick when hold key first went down (0 = not held)

    while (true) {
        Sleep(10);

        if (!g_enabled) {
            Sleep(100);
            continue;
        }

        // --- Gamepad state — OR all shadow slots together ---
        // The hook populates g_padShadow[slot] for every XInput call the game makes.
        // We combine all valid slots so we work regardless of which index Steam assigns
        // the physical controller to. No index detection or scanning needed here.
        WORD padButtons = 0;
        BYTE padLeftTrigger = 0;
        bool padConnected = false;

        DWORD now = GetTickCount();

        if (g_gamepadEnabled && g_xinputLoaded && g_gamepadModifier != 0) {
            for (int idx = 0; idx < XUSER_MAX_COUNT; idx++) {
                const PadShadow& sh = g_padShadow[idx];
                if (sh.valid) {
                    padConnected    = true;
                    padButtons     |= sh.buttons;
                    if (sh.ltrigger > padLeftTrigger)
                        padLeftTrigger = sh.ltrigger;
                }
            }
        }

        bool modHeld = padConnected && g_gamepadModifier != 0 &&
                       (padButtons & g_gamepadModifier) == g_gamepadModifier;

        // --- Reload key (edge-detected, keyboard OR gamepad) ---
        static bool reloadDown = false;
        bool reloadPressed = false;
        if (g_reloadKey && (GetAsyncKeyState(g_reloadKey) & 0x8000))
            reloadPressed = true;
        if (modHeld && g_gamepadReloadBtn && (padButtons & g_gamepadReloadBtn))
            reloadPressed = true;

        if (reloadPressed) {
            if (!reloadDown) {
                reloadDown = true;
                for (int i = 0; i < g_slotCount; i++)
                    g_slots[i].active = false;
                LoadConfig();
                SetSpeed(1.0);
                Log("Config reloaded");
                ShowOSD("JustSkip: Config Reloaded");
            }
        } else {
            reloadDown = false;
        }

        float targetSpeed = 1.0f;
        bool anyHoldActive = false;

        // --- Focus check: if game isn't foreground, don't trust hold keys ---
        bool gameFocused = true;
        if (g_holdFocusCheck) {
            HWND fg = GetForegroundWindow();
            if (fg) {
                DWORD fgPid = 0;
                GetWindowThreadProcessId(fg, &fgPid);
                gameFocused = (fgPid == GetCurrentProcessId());
            }
        }

        // --- Startup grace period: ignore speed changes while game initializes ---
        bool inGrace = g_hookInstalledAt && g_startupGraceMs &&
                       (now - g_hookInstalledAt) < g_startupGraceMs;

        // --- Hold keys take priority (keyboard + gamepad) ---
        for (int i = 0; i < g_slotCount; i++) {
            SpeedSlot& s = g_slots[i];
            if (!s.isHold) continue;

            bool down = false;
            if (s.vkKey && (GetAsyncKeyState(s.vkKey) & 0x8000))
                down = true;
            if (modHeld && s.gamepadButton && (padButtons & s.gamepadButton))
                down = true;

            // Safety: force-release if game lost focus
            if (down && !gameFocused) {
                down = false;
                if (holdStart[i]) {
                    Log("Hold slot %d: force-released (game not focused)", i);
                    holdStart[i] = 0;
                }
            }

            // Safety: force-release if held longer than max duration
            if (down) {
                if (!holdStart[i]) holdStart[i] = now;
                if (g_maxHoldMs > 0 && (now - holdStart[i]) > g_maxHoldMs) {
                    down = false;
                    Log("Hold slot %d: force-released (max duration %lums exceeded)", i, g_maxHoldMs);
                    holdStart[i] = 0;
                }
            } else {
                holdStart[i] = 0;
            }

            if (down) {
                if (s.speed > targetSpeed)
                    targetSpeed = s.speed;
                anyHoldActive = true;
            }
        }

        // --- Toggle keys (mutually exclusive, keyboard + gamepad) ---
        if (!anyHoldActive) {
            for (int i = 0; i < g_slotCount; i++) {
                SpeedSlot& s = g_slots[i];
                if (s.isHold) continue;

                bool down = false;
                if (s.vkKey && (GetAsyncKeyState(s.vkKey) & 0x8000))
                    down = true;
                if (modHeld && s.gamepadButton && (padButtons & s.gamepadButton))
                    down = true;

                if (down && !prevDown[i]) {
                    s.active = !s.active;
                    Log("Toggle slot %d: %s (speed=%.2f)", i, s.active ? "ON" : "OFF", s.speed);

                    if (s.active) {
                        for (int j = 0; j < g_slotCount; j++) {
                            if (j != i && !g_slots[j].isHold)
                                g_slots[j].active = false;
                        }
                    }
                }
                prevDown[i] = down;
            }

            for (int i = 0; i < g_slotCount; i++) {
                if (!g_slots[i].isHold && g_slots[i].active) {
                    targetSpeed = g_slots[i].speed;
                    break;
                }
            }
        }

        // --- Combat detection (multi-signal fusion, optional) ---
        static bool  wasCombat = false;
        static DWORD attackTimestamps[16] = {};
        static int   attackHead = 0;
        static WORD  prevAttackButtons = 0;

        if (g_combatDetect) {
            bool inCombat = false;

            // Priority 1: Signature-based (perfect, if available)
            if (g_combatFlagAddr) {
                __try {
                    inCombat = (*g_combatFlagAddr != 0);
                } __except(EXCEPTION_EXECUTE_HANDLER) {
                    // Pointer went bad (game update?) — disable
                    Log("WARNING: Combat signature pointer invalid — disabling");
                    g_combatFlagAddr = nullptr;
                }
            }

            // Priority 2: Multi-signal fusion (heuristic fallback)
            if (!g_combatFlagAddr) {
                DWORD lastSig = g_lastCombatSignal.load(std::memory_order_relaxed);

                // Signal A: Strong vibration (already handled by XInputSetState hook,
                //           updates g_lastCombatSignal directly)

                // Signal B: LT held + attack button (lock-on + attack = combat)
                if (padConnected && padLeftTrigger > 128 && g_combatAttackMask) {
                    if (padButtons & g_combatAttackMask) {
                        DWORD prevSig = lastSig;
                        g_lastCombatSignal.store(now, std::memory_order_relaxed);
                        if (prevSig == 0 || (now - prevSig) > g_combatTimeout) {
                            Log("Combat signal: LT + attack (LT=%u, btns=0x%04X)",
                                padLeftTrigger, padButtons & g_combatAttackMask);
                        }
                        lastSig = now;
                    }
                }

                // Signal C: Rapid attack button mashing (3+ presses in 2s)
                if (padConnected && g_combatAttackMask) {
                    WORD attackNow = padButtons & g_combatAttackMask;
                    WORD attackEdge = attackNow & ~prevAttackButtons; // newly pressed
                    prevAttackButtons = attackNow;

                    if (attackEdge) {
                        attackTimestamps[attackHead % 16] = now;
                        attackHead++;

                        // Count presses within the mash window
                        int recentPresses = 0;
                        for (int k = 0; k < 16; k++) {
                            if (attackTimestamps[k] && (now - attackTimestamps[k]) <= g_combatMashWindow)
                                recentPresses++;
                        }
                        if (recentPresses >= g_combatMashCount) {
                            g_lastCombatSignal.store(now, std::memory_order_relaxed);
                            if (lastSig == 0 || (now - lastSig) > g_combatTimeout) {
                                Log("Combat signal: attack mash (%d presses in %lums)",
                                    recentPresses, g_combatMashWindow);
                            }
                        }
                    }
                }

                lastSig = g_lastCombatSignal.load(std::memory_order_relaxed);
                inCombat = (lastSig != 0) && (now - lastSig) < g_combatTimeout;
            }

            if (inCombat) {
                targetSpeed = g_combatSpeed;
                if (!wasCombat) {
                    Log("Combat mode: speed -> %.2fx", g_combatSpeed);
                    wasCombat = true;
                }
            } else if (wasCombat) {
                Log("Combat ended: resuming normal speed");
                wasCombat = false;
            }
        }

        // --- Apply speed (skip during startup grace period) ---
        if (inGrace) targetSpeed = 1.0f;

        static float lastSpeed = 1.0f;
        if (targetSpeed != lastSpeed) {
            SetSpeed(targetSpeed);
            lastSpeed = targetSpeed;
            Log("Speed changed to %.2fx", targetSpeed);
            if (targetSpeed == 1.0f)
                ShowOSD("Speed: 1x (Normal)");
            else
                ShowOSD("Speed: %.2fx", targetSpeed);
        }
    }

    return 0;
}

// ---------------------------------------------------------------------------
//  Entry point
// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;

    DisableThreadLibraryCalls(hModule);

    // Build paths relative to this DLL
    GetModuleFileNameA(hModule, g_iniPath, MAX_PATH);
    char* dot = strrchr(g_iniPath, '.');
    if (dot) strcpy_s(dot, 5, ".ini");

    strcpy_s(g_logPath, g_iniPath);
    dot = strrchr(g_logPath, '.');
    if (dot) strcpy_s(dot, 5, ".log");

    DeleteFileA(g_logPath);

    // Always log startup details regardless of DebugLog setting
    g_debugLog = true;
    Log("=== JustSkip v2.7 starting ===");
    Log("INI path: %s", g_iniPath);
    Log("Log path: %s", g_logPath);

    LoadConfig();

    // LoadConfig() overwrites g_debugLog from INI — save that, then force logging
    // through the rest of startup so all init messages are always visible.
    bool userDebug = g_debugLog;
    g_debugLog = true;

    // Resolve combat signature if configured
    if (g_combatDetect) {
        ResolveCombatSignature();
    }

    if (!g_enabled) {
        Log("Mod disabled via INI — exiting");
        return TRUE;
    }

    if (MH_Initialize() != MH_OK) {
        Log("ERROR: MH_Initialize failed");
        return TRUE;
    }

    // Helper: create + enable one hook; returns false and logs on any failure.
    auto InstallHook = [](LPVOID target, LPVOID detour, LPVOID* orig, const char* name) -> bool {
        if (MH_CreateHook(target, detour, orig) != MH_OK) {
            Log("ERROR: MH_CreateHook(%s) failed", name);
            return false;
        }
        if (MH_EnableHook(target) != MH_OK) {
            Log("ERROR: MH_EnableHook(%s) failed", name);
            return false;
        }
        return true;
    };

    auto pfnQPC = reinterpret_cast<LPVOID>(&QueryPerformanceCounter);
    if (!InstallHook(pfnQPC, &HookedQPC, reinterpret_cast<LPVOID*>(&g_origQPC), "QPC")) {
        MH_Uninitialize();
        return TRUE;
    }

    Log("QPC hook installed");
    g_hookInstalledAt = GetTickCount();

    // Cache frame-generation module ranges for QPC bypass
    CacheBypassModules();

    // Hook game's XInput for combat detection (vibration monitoring)
    if (g_gamepadEnabled) {
        HMODULE hGameXInput = GetModuleHandleA("xinput1_4.dll");
        if (!hGameXInput) hGameXInput = GetModuleHandleA("xinput1_3.dll");
        if (!hGameXInput) hGameXInput = GetModuleHandleA("xinput9_1_0.dll");
        // Game may not have loaded XInput yet at DLL_PROCESS_ATTACH — try loading it
        if (!hGameXInput) hGameXInput = LoadLibraryA("xinput1_4.dll");
        if (!hGameXInput) hGameXInput = LoadLibraryA("xinput1_3.dll");
        if (hGameXInput) {
            // Hook SetState for combat detection (vibration monitoring)
            if (g_combatDetect) {
                auto pfnSetState = (PFN_XInputSetState)GetProcAddress(hGameXInput, "XInputSetState");
                if (pfnSetState && InstallHook(pfnSetState, &HookedXInputSetState,
                                               reinterpret_cast<LPVOID*>(&g_origXInputSetState),
                                               "XInputSetState")) {
                    Log("XInputSetState hook installed (combat detection active)");
                } else {
                    Log("WARNING: XInputSetState hook failed/not found — combat detection disabled");
                    g_combatDetect = false;
                }
            }

            // Hook GetState + GetStateEx to suppress modifier + speed buttons
            if (g_suppressButtons && g_suppressMask != 0 && g_gamepadModifier != 0) {
                bool anyHooked = false;

                // Standard XInputGetState (export by name, fallback ordinal 3)
                auto pfnGetState = (PFN_XInputGetState)GetProcAddress(hGameXInput, "XInputGetState");
                if (!pfnGetState)
                    pfnGetState = (PFN_XInputGetState)GetProcAddress(hGameXInput, MAKEINTRESOURCEA(3));
                if (pfnGetState && InstallHook(pfnGetState, &HookedXInputGetState,
                                               reinterpret_cast<LPVOID*>(&g_origXInputGetState),
                                               "XInputGetState")) {
                    Log("XInputGetState hook installed (suppress mask=0x%04X)", g_suppressMask);
                    anyHooked = true;
                }

                // Undocumented XInputGetStateEx (ordinal 100) — many games use this
                auto pfnGetStateEx = (PFN_XInputGetState)GetProcAddress(hGameXInput, MAKEINTRESOURCEA(100));
                if (pfnGetStateEx && pfnGetStateEx != pfnGetState &&
                    InstallHook(pfnGetStateEx, &HookedXInputGetStateEx,
                                reinterpret_cast<LPVOID*>(&g_origXInputGetStateEx),
                                "XInputGetStateEx")) {
                    Log("XInputGetStateEx hook installed (ordinal 100)");
                    anyHooked = true;
                }

                if (!anyHooked) {
                    Log("WARNING: No GetState hooks installed — button suppression disabled");
                    g_suppressButtons = false;
                    g_suppressHookFailed = true;
                } else {
                    g_xinputLoaded = true;  // shadow buffer will be populated by the hooks
                }
            }
        } else {
            Log("WARNING: No XInput DLL loaded by game — combat hooks skipped");
            if (g_combatDetect) g_combatDetect = false;
        }
    }

    Log("JustSkip loaded");

    // Restore user's DebugLog preference now that startup logging is complete
    g_debugLog = userDebug;

    // Start OSD thread first so it's ready before hotkey thread sends messages
    if (g_osdEnabled) {
        HANDLE hOsd = CreateThread(nullptr, 0, OsdThread, nullptr, 0, &g_osdThreadId);
        if (hOsd) {
            // Wait briefly for the OSD window to be created
            for (int i = 0; i < 50 && !g_osdWnd; i++) Sleep(10);
            CloseHandle(hOsd);
            Log("OSD thread started (tid=%lu)", g_osdThreadId);
        }
    }

    HANDLE hHotkey = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    if (hHotkey) CloseHandle(hHotkey);

    return TRUE;
}
