// JustSkip — Game speed control for Crimson Desert
// Copyright (c) 2026 wealdly. All rights reserved.
// Hooks QueryPerformanceCounter to scale time. No Cheat Engine needed.

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <mutex>

#include "MinHook.h"

// ---------------------------------------------------------------------------
//  Configuration (loaded from INI)
// ---------------------------------------------------------------------------
struct SpeedSlot {
    int  vkKey;       // Virtual-key code
    bool isHold;      // true = active only while held, false = toggle
    float speed;      // multiplier
    bool active;      // runtime state for toggles
};

static SpeedSlot  g_slots[6];
static int        g_slotCount  = 0;
static float      g_baseSpeed  = 1.0f;   // the "normal" speed (always 1.0)
static bool       g_enabled    = true;
static int        g_reloadKey  = 0;
static bool       g_debugLog   = false;
static char       g_iniPath[MAX_PATH];
static char       g_logPath[MAX_PATH];

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
    g_enabled  = ReadIniInt("Settings", "Enabled", 1) != 0;
    g_debugLog = ReadIniInt("Settings", "DebugLog", 0) != 0;
    g_reloadKey = ReadIniHex("Settings", "ReloadKey", 0x00);

    // Read speed slots — up to 6 sections named Speed1..Speed6
    g_slotCount = 0;
    for (int i = 1; i <= 6 && g_slotCount < 6; i++) {
        char section[32];
        sprintf_s(section, "Speed%d", i);

        char keyBuf[32];
        GetPrivateProfileStringA(section, "Hotkey", "", keyBuf, sizeof(keyBuf), g_iniPath);
        if (keyBuf[0] == '\0') continue;

        SpeedSlot& s = g_slots[g_slotCount];
        s.vkKey  = ParseHexOrDec(keyBuf);
        s.isHold = ReadIniInt(section, "Hold", 0) != 0;
        s.speed  = ReadIniFloat(section, "Speed", 1.0f);
        s.active = false;
        g_slotCount++;

        Log("  Slot %d: key=0x%02X hold=%d speed=%.2f", i, s.vkKey, s.isHold, s.speed);
    }

    Log("Config loaded: %d slots, enabled=%d", g_slotCount, g_enabled);
}

// ---------------------------------------------------------------------------
//  QPC time scaling
// ---------------------------------------------------------------------------
typedef BOOL(WINAPI* PFN_QPC)(LARGE_INTEGER* lpCounter);
static PFN_QPC g_origQPC = nullptr;

static std::mutex    g_timeMtx;
static LARGE_INTEGER g_realBase   = {};
static LARGE_INTEGER g_fakeBase   = {};
static double        g_curSpeed   = 1.0;
static bool          g_timeInited = false;

static void SetSpeed(double speed) {
    std::lock_guard<std::mutex> lk(g_timeMtx);

    LARGE_INTEGER realNow;
    g_origQPC(&realNow);

    if (g_timeInited) {
        double realDelta = (double)(realNow.QuadPart - g_realBase.QuadPart);
        g_fakeBase.QuadPart = g_fakeBase.QuadPart + (LONGLONG)(realDelta * g_curSpeed);
    } else {
        g_fakeBase = realNow;
        g_timeInited = true;
    }

    g_realBase = realNow;
    g_curSpeed = speed;
}

static BOOL WINAPI HookedQPC(LARGE_INTEGER* lpCounter) {
    if (!lpCounter) return FALSE;

    std::lock_guard<std::mutex> lk(g_timeMtx);

    LARGE_INTEGER realNow;
    g_origQPC(&realNow);

    if (!g_timeInited) {
        *lpCounter = realNow;
        g_realBase = realNow;
        g_fakeBase = realNow;
        g_timeInited = true;
        return TRUE;
    }

    double realDelta = (double)(realNow.QuadPart - g_realBase.QuadPart);
    lpCounter->QuadPart = g_fakeBase.QuadPart + (LONGLONG)(realDelta * g_curSpeed);
    return TRUE;
}

// ---------------------------------------------------------------------------
//  Hotkey polling thread
// ---------------------------------------------------------------------------
static DWORD WINAPI HotkeyThread(LPVOID) {
    Log("Hotkey thread started");

    bool prevDown[6] = {};

    while (true) {
        Sleep(10);

        if (!g_enabled) {
            Sleep(100);
            continue;
        }

        // Reload key (edge-detected)
        static bool reloadDown = false;
        if (g_reloadKey && (GetAsyncKeyState(g_reloadKey) & 0x8000)) {
            if (!reloadDown) {
                reloadDown = true;
                for (int i = 0; i < g_slotCount; i++)
                    g_slots[i].active = false;
                LoadConfig();
                SetSpeed(1.0);
                Log("Config reloaded via hotkey");
            }
        } else {
            reloadDown = false;
        }

        float targetSpeed = g_baseSpeed;
        bool anyHoldActive = false;

        // Hold keys take priority
        for (int i = 0; i < g_slotCount; i++) {
            SpeedSlot& s = g_slots[i];
            if (!s.isHold) continue;

            bool down = (GetAsyncKeyState(s.vkKey) & 0x8000) != 0;
            if (down) {
                if (s.speed > targetSpeed)
                    targetSpeed = s.speed;
                anyHoldActive = true;
            }
        }

        // Toggle keys (mutually exclusive)
        if (!anyHoldActive) {
            for (int i = 0; i < g_slotCount; i++) {
                SpeedSlot& s = g_slots[i];
                if (s.isHold) continue;

                bool down = (GetAsyncKeyState(s.vkKey) & 0x8000) != 0;
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

        static float lastSpeed = 1.0f;
        if (targetSpeed != lastSpeed) {
            SetSpeed(targetSpeed);
            lastSpeed = targetSpeed;
            Log("Speed changed to %.2fx", targetSpeed);
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

    LoadConfig();
    if (!g_enabled) return TRUE;

    if (MH_Initialize() != MH_OK) {
        Log("ERROR: MH_Initialize failed");
        return TRUE;
    }

    auto pfnQPC = reinterpret_cast<LPVOID>(&QueryPerformanceCounter);

    if (MH_CreateHook(pfnQPC, &HookedQPC,
                       reinterpret_cast<LPVOID*>(&g_origQPC)) != MH_OK) {
        Log("ERROR: MH_CreateHook(QPC) failed");
        MH_Uninitialize();
        return TRUE;
    }

    if (MH_EnableHook(pfnQPC) != MH_OK) {
        Log("ERROR: MH_EnableHook(QPC) failed");
        MH_Uninitialize();
        return TRUE;
    }

    Log("JustSkip loaded — QPC hook installed");

    CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);

    return TRUE;
}
