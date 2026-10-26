#include "native_game_hooks.hpp"

#ifndef GDCLIENT_ENABLE_GAME_HOOKS
#define GDCLIENT_ENABLE_GAME_HOOKS 0
#endif

#if GDCLIENT_ENABLE_GAME_HOOKS
#include <MinHook.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <utility>
#include <limits>
#include <cstring>

namespace gdclient::native {
namespace {

struct BuildConfig {
    std::uint32_t exeStamp;
    std::uint32_t cocosStamp;
    std::uint32_t extensionsStamp;
    const wchar_t* name;
    std::uintptr_t gameLayerUpdateRva;
    std::uintptr_t playLayerInitRva;
    std::uintptr_t playLayerPostUpdateRva;
    std::uintptr_t playLayerDestroyPlayerRva;
    std::uintptr_t playLayerResetLevelRva;
    std::uintptr_t playLayerOnQuitRva;
    std::uintptr_t playLayerLevelCompleteRva;
    std::uintptr_t playLayerShouldDrawHitboxesRva;
    std::uintptr_t playLayerShouldDrawHitboxTrailRva;
    std::uintptr_t playLayerShouldRecordHitboxTrailRva;
    std::uintptr_t playLayerTogglePracticeModeRva;
    std::uintptr_t gameLayerHandleButtonRva;
    std::uintptr_t gameManagerIsIconUnlockedRva;
    std::uintptr_t gameManagerIsColorUnlockedRva;
    std::uintptr_t playerSetRotationRva;
    std::uintptr_t gameLayerPlayer1Offset;
    std::uintptr_t playerPositionOffset;
};

// These signatures are the three-file PE identity checks used by the
// gdsolver 2.209 build guard. Function RVAs are from the matching Windows
// bindings. A matching game version string by itself is not sufficient.
constexpr BuildConfig kBuilds[] = {
    // Geometry Dash 2.209, Steam build 25909607 (2026-10-13).
    {0x6ace3633u, 0x6acbff38u, 0x6acbefc5u, L"2.209 Steam 25909607",
     0x33dc40, 0x30a570, 0x510310, 0x50ea00, 0x514fb0, 0x517450,
     0x502830, 0, 0, 0, 0, 0x339ac0, 0x274d60, 0, 0x4fe4f0, 0x878, 0xa98},
    // Geometry Dash 2.2091, Steam build 26002420 (2026-10-17 hotfix).
    {0x6ad2d987u, 0x6ad2d6fcu, 0x6ad2d956u, L"2.2091 Steam 26002420",
     0x3406d0, 0x5039e0, 0x513ca0, 0x512390, 0x518c00, 0x51b0b0,
     0x5061b0, 0x505bc0, 0x505bf0, 0x505c20, 0x519ff0, 0x33c5b0, 0x2773d0, 0x2777a0,
     0x501e70, 0x878, 0xa98}
};

HMODULE g_gameModule = nullptr;
const BuildConfig* g_build = nullptr;
std::atomic<bool> g_supported{false};
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_inLevel{false};
std::atomic<bool> g_practiceMode{false};
std::atomic<bool> g_practiceHookAvailable{false};
std::atomic<bool> g_speedHookAvailable{false};
std::atomic<bool> g_noclipHookAvailable{false};
std::atomic<bool> g_hitboxHookAvailable{false};
std::atomic<bool> g_hitboxTrailHookAvailable{false};
std::atomic<bool> g_hitboxTrailEnabled{false};
std::atomic<bool> g_noclipEnabled{false};
std::atomic<bool> g_hitboxesEnabled{false};
std::atomic<bool> g_deathLimitEnabled{false};
std::atomic<std::uint32_t> g_deathLimit{10};
std::atomic<bool> g_accuracyLimitEnabled{false};
std::atomic<double> g_minAccuracy{75.0};
std::atomic<double> g_speed{1.0};
std::atomic<int> g_physicsTps{0};
// Accessed only by the game update thread. Fixed-step accumulator is reset at level lifecycle boundaries.
double g_physicsAccumulator = 0.0;
int g_lastUpdateTps = 0;
std::atomic<void*> g_activePlayLayer{nullptr};
std::atomic<bool> g_collisionThisFrame{false};
std::atomic<std::uint64_t> g_frames{0};
std::atomic<std::uint64_t> g_collisionFrames{0};
std::atomic<std::uint64_t> g_contacts{0};
std::atomic<double> g_accuracy{100.0};
std::atomic<float> g_playerX{0.0f};
std::atomic<float> g_playerY{0.0f};
std::atomic<float> g_playerRotation{0.0f};
std::atomic<bool> g_limitTriggered{false};
std::atomic<bool> g_frameStepEnabled{false};
std::atomic<bool> g_frameStepPaused{false};
std::atomic<std::uint32_t> g_frameStepBudget{0};
std::atomic<int> g_gameThreadPriorityLevel{0};
std::atomic<bool> g_completionGuardAvailable{false};
// True once any gameplay-affecting hook has been active at any point during
// the current attempt (since the level was entered or last reset). Cleared
// in resetRunStats(), which every attempt boundary already calls.
std::atomic<bool> g_assistedThisAttempt{false};
// Set by the UI layer for assists that live outside native_game_hooks.cpp
// (the input-level macro player, the auto-clicker). See setExternalAssistActive().
std::atomic<bool> g_externalAssistActive{false};
// Incremented every time hookLevelComplete suppresses a real completion
// because the attempt was assisted. The UI polls this to show a toast.
std::atomic<std::uint64_t> g_completionBlockedCount{0};
std::atomic<bool> g_nativeMacroHookAvailable{false};
std::atomic<bool> g_nativeMacroRecording{false};
std::atomic<bool> g_nativeMacroPlaying{false};
std::atomic<bool> g_nativeMacroLoop{false};
std::atomic<bool> g_nativeMacroStopRequested{false};
std::atomic<bool> g_nativeMacroPracticeConfirmed{false};
std::atomic<bool> g_unlockIconsEnabled{false};
std::atomic<bool> g_unlockIconsHookAvailable{false};
std::atomic<bool> g_unlockColorsHookAvailable{false};
std::atomic<std::uint64_t> g_gameTimelineMicros{0};
std::atomic<std::uint64_t> g_simulationFrame{0};
std::atomic<std::uint64_t> g_macroRecordStartMicros{0};
std::atomic<std::uint64_t> g_macroPlaybackStartMicros{0};
std::atomic<std::uint64_t> g_macroRecordStartFrame{0};
std::atomic<double> g_nativeMacroPlaybackRate{1.0};
std::atomic<DWORD> g_gameThreadId{0};
std::atomic<int> g_originalGameThreadPriority{THREAD_PRIORITY_NORMAL};
std::atomic<int> g_appliedGameThreadPriority{THREAD_PRIORITY_ERROR_RETURN};
std::atomic<bool> g_originalPriorityCaptured{false};

#if GDCLIENT_ENABLE_GAME_HOOKS
using LayerUpdateFn = void(__fastcall*)(void*, float);
using PlayInitFn = bool(__fastcall*)(void*, void*, bool);
using PlayPostUpdateFn = void(__fastcall*)(void*, float);
using DestroyPlayerFn = void(__fastcall*)(void*, void*, void*);
using ResetLevelFn = void(__fastcall*)(void*);
using OnQuitFn = void(__fastcall*)(void*);
using ShouldDrawHitboxesFn = bool(__fastcall*)(void*);
using ShouldDrawHitboxTrailFn = bool(__fastcall*)(void*);
using ShouldRecordHitboxTrailFn = bool(__fastcall*)(void*);
using TogglePracticeFn = void(__fastcall*)(void*, bool);
using LevelCompleteFn = void(__fastcall*)(void*);
using HandleButtonFn = void(__fastcall*)(void*, bool, int, bool);
using IsIconUnlockedFn = bool(__fastcall*)(void*, int, int);
using IsColorUnlockedFn = bool(__fastcall*)(void*, int, int);
using PlayerSetRotationFn = void(__fastcall*)(void*, float);

struct NativeMacroEvent {
    std::uint64_t timeMicros = 0;
    std::uint64_t frameIndex = 0;
    std::int32_t button = 0;
    std::uint8_t type = 0;     // 0 = handleButton; 1 = level reset
    std::uint8_t down = 0;
    std::uint8_t player1 = 1;
    std::uint8_t reserved = 0;
    float playerX = 0.0f;
    float playerY = 0.0f;
    float playerRotation = 0.0f;
};
constexpr std::uint8_t kMacroButton = 0;
constexpr std::uint8_t kMacroReset = 1;
std::mutex g_nativeMacroMutex;
std::vector<NativeMacroEvent> g_nativeMacroEvents;
std::size_t g_nativeMacroPlaybackIndex = 0;
struct HeldMacroInput { int button; bool player1; };
std::vector<HeldMacroInput> g_nativeMacroHeldInputs;

LayerUpdateFn g_originalLayerUpdate = nullptr;
PlayInitFn g_originalPlayInit = nullptr;
PlayPostUpdateFn g_originalPostUpdate = nullptr;
DestroyPlayerFn g_originalDestroyPlayer = nullptr;
ResetLevelFn g_originalResetLevel = nullptr;
OnQuitFn g_originalOnQuit = nullptr;
ShouldDrawHitboxesFn g_originalShouldDrawHitboxes = nullptr;
ShouldDrawHitboxTrailFn g_originalShouldDrawHitboxTrail = nullptr;
ShouldRecordHitboxTrailFn g_originalShouldRecordHitboxTrail = nullptr;
TogglePracticeFn g_originalTogglePractice = nullptr;
LevelCompleteFn g_originalLevelComplete = nullptr;
HandleButtonFn g_originalHandleButton = nullptr;
IsIconUnlockedFn g_originalIsIconUnlocked = nullptr;
IsColorUnlockedFn g_originalIsColorUnlocked = nullptr;
PlayerSetRotationFn g_originalPlayerSetRotation = nullptr;
std::vector<void*> g_hookTargets;
bool g_minhookInitialized = false;

void debugLine(const wchar_t* message) {
    OutputDebugStringW(message);
}

std::uint32_t peTimestamp(HMODULE module) {
    if (!module) return 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    return nt->FileHeader.TimeDateStamp;
}

std::uintptr_t imageSize(HMODULE module) {
    if (!module) return 0;
    const auto* base = reinterpret_cast<const std::uint8_t*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    return nt->OptionalHeader.SizeOfImage;
}

bool readableRange(const void* address, std::size_t bytes) {
    if (!address || bytes == 0) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT) return false;
    if (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const DWORD protection = info.Protect & 0xff;
    if (protection != PAGE_READONLY && protection != PAGE_READWRITE &&
        protection != PAGE_WRITECOPY && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(address);
    const auto regionStart = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto regionEnd = regionStart + info.RegionSize;
    return start >= regionStart && start <= regionEnd && bytes <= regionEnd - start;
}

const BuildConfig* detectBuild() {
    const auto exe = peTimestamp(GetModuleHandleW(nullptr));
    const auto cocos = peTimestamp(GetModuleHandleW(L"libcocos2d.dll"));
    const auto extensions = peTimestamp(GetModuleHandleW(L"libExtensions.dll"));
    for (const auto& build : kBuilds) {
        if (exe == build.exeStamp && cocos == build.cocosStamp && extensions == build.extensionsStamp) return &build;
    }
    wchar_t line[256]{};
    swprintf_s(line, _countof(line),
        L"GD Client: unsupported executable fingerprint (exe=%08X cocos=%08X extensions=%08X); game hooks disabled.\n",
        exe, cocos, extensions);
    debugLine(line);
    return nullptr;
}

void* resolveGameRva(std::uintptr_t rva) {
    if (!g_gameModule || !g_build || rva == 0) return nullptr;
    const auto size = imageSize(g_gameModule);
    if (rva >= size) return nullptr;
    auto* address = reinterpret_cast<std::uint8_t*>(g_gameModule) + rva;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(address, &info, sizeof(info))) return nullptr;
    if (info.AllocationBase != g_gameModule || info.State != MEM_COMMIT || (info.Protect & PAGE_NOACCESS)) return nullptr;
    const DWORD protection = info.Protect & 0xff;
    if (protection != PAGE_EXECUTE && protection != PAGE_EXECUTE_READ &&
        protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return nullptr;
    return address;
}

bool installOne(std::uintptr_t rva, void* detour, void** original, const wchar_t* name, bool required) {
    void* target = resolveGameRva(rva);
    if (!target) {
        wchar_t msg[192]{};
        swprintf_s(msg, _countof(msg), L"GD Client: cannot resolve %s RVA 0x%llX.\n", name, static_cast<unsigned long long>(rva));
        debugLine(msg);
        return !required;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);
    if (status != MH_OK) {
        wchar_t msg[192]{};
        swprintf_s(msg, _countof(msg), L"GD Client: MH_CreateHook failed for %s (status %d).\n", name, static_cast<int>(status));
        debugLine(msg);
        return !required;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        MH_RemoveHook(target);
        wchar_t msg[192]{};
        swprintf_s(msg, _countof(msg), L"GD Client: MH_EnableHook failed for %s (status %d).\n", name, static_cast<int>(status));
        debugLine(msg);
        return !required;
    }
    g_hookTargets.push_back(target);
    return true;
}

void resetRunStats() {
    g_collisionThisFrame.store(false);
    g_frames.store(0);
    g_collisionFrames.store(0);
    g_contacts.store(0);
    g_accuracy.store(100.0);
    g_limitTriggered.store(false);
    // New attempt: whatever was assisted before this point no longer applies.
    g_assistedThisAttempt.store(false);
}

// Anything that changes real gameplay outcome: timing, collision, input
// automation, or revealing information the stock game doesn't show during
// play. If you add a new gameplay-affecting hook, add its check here.
bool anyNativeAssistActive() {
    if (std::abs(g_speed.load() - 1.0) > 0.001) return true;
    if (g_noclipEnabled.load()) return true;
    if (g_frameStepEnabled.load()) return true;
    if (g_physicsTps.load() != 0) return true;
    if (g_nativeMacroPlaying.load()) return true;
    if (g_hitboxesEnabled.load()) return true;
    if (g_hitboxTrailEnabled.load()) return true;
    if (g_externalAssistActive.load()) return true;
    return false;
}

int priorityValue(int level) {
    switch (std::clamp(level, 0, 2)) {
        case 1: return THREAD_PRIORITY_ABOVE_NORMAL;
        case 2: return THREAD_PRIORITY_HIGHEST;
        default: return THREAD_PRIORITY_NORMAL;
    }
}

std::filesystem::path nativeMacroPath(bool createFolder) {
    wchar_t appData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData, _countof(appData));
    if (length == 0 || length >= _countof(appData)) return {};
    const auto folder = std::filesystem::path(appData) / L"GDClient";
    if (createFolder) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) return {};
    }
    return folder / L"native_practice.gdm3";
}

bool macroAllowedNow() {
    if (!g_nativeMacroPracticeConfirmed.load() || !g_inLevel.load()) return false;
    return !g_practiceHookAvailable.load() || g_practiceMode.load();
}

void appendMacroEvent(const NativeMacroEvent& event) {
    if (!g_nativeMacroRecording.load() || !macroAllowedNow()) return;
    std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
    if (!g_nativeMacroRecording.load()) return;
    // Hard cap prevents an accidentally left-on recording from consuming unbounded memory.
    if (g_nativeMacroEvents.size() >= 5'000'000) {
        g_nativeMacroRecording.store(false);
        OutputDebugStringW(L"GD Client: native macro reached its 5,000,000-event safety cap.\n");
        return;
    }
    g_nativeMacroEvents.push_back(event);
}

void releaseNativeMacroInputs(void* layer) {
    if (!g_originalHandleButton || !layer) {
        g_nativeMacroHeldInputs.clear();
        return;
    }
    for (const auto& held : g_nativeMacroHeldInputs) {
        g_originalHandleButton(layer, false, held.button, held.player1);
    }
    g_nativeMacroHeldInputs.clear();
}

void capturePlayerSnapshot(void* layer, NativeMacroEvent& event) {
    if (!g_build || !layer || g_build->gameLayerPlayer1Offset == 0 || g_build->playerPositionOffset == 0) return;
    auto** playerSlot = reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(layer) + g_build->gameLayerPlayer1Offset);
    if (!readableRange(playerSlot, sizeof(void*))) return;
    void* player = *playerSlot;
    if (!player) return;
    auto* position = reinterpret_cast<const float*>(reinterpret_cast<const std::uint8_t*>(player) + g_build->playerPositionOffset);
    if (!readableRange(position, sizeof(float) * 2)) return;
    const float px = position[0];
    const float py = position[1];
    if (std::isfinite(px) && std::isfinite(py) && std::abs(px) < 1'000'000.0f && std::abs(py) < 1'000'000.0f) {
        event.playerX = px;
        event.playerY = py;
        event.playerRotation = g_playerRotation.load();
        g_playerX.store(px);
        g_playerY.store(py);
    }
}

void dispatchNativeMacroButton(void* layer, const NativeMacroEvent& event) {
    if (!g_originalHandleButton || !layer) return;
    g_originalHandleButton(layer, event.down != 0, event.button, event.player1 != 0);
    const auto it = std::find_if(g_nativeMacroHeldInputs.begin(), g_nativeMacroHeldInputs.end(), [&](const HeldMacroInput& held) {
        return held.button == event.button && held.player1 == (event.player1 != 0);
    });
    if (event.down) {
        if (it == g_nativeMacroHeldInputs.end()) g_nativeMacroHeldInputs.push_back({event.button, event.player1 != 0});
    } else if (it != g_nativeMacroHeldInputs.end()) {
        g_nativeMacroHeldInputs.erase(it);
    }
}

#pragma pack(push, 1)
struct NativeMacroHeader { char magic[4]; std::uint32_t version; std::uint64_t eventCount; };
struct NativeMacroDiskEvent {
    std::uint64_t timeMicros;
    std::uint64_t frameIndex;
    std::int32_t button;
    std::uint8_t type;
    std::uint8_t down;
    std::uint8_t player1;
    std::uint8_t reserved;
    float playerX;
    float playerY;
    float playerRotation;
};
#pragma pack(pop)

void finishNativeMacroPlayback(void* layer) {
    releaseNativeMacroInputs(layer);
    g_nativeMacroPlaying.store(false);
    g_nativeMacroStopRequested.store(false);
    g_nativeMacroPracticeConfirmed.store(false);
}

void processNativeMacroPlayback(void* layer) {
    if (g_nativeMacroStopRequested.exchange(false)) {
        finishNativeMacroPlayback(layer);
        return;
    }
    if (!g_nativeMacroPlaying.load()) return;
    if (!macroAllowedNow()) {
        finishNativeMacroPlayback(layer);
        return;
    }

    const std::uint64_t now = g_gameTimelineMicros.load();
    const std::uint64_t start = g_macroPlaybackStartMicros.load();
    const std::uint64_t rawElapsed = now >= start ? now - start : 0;
    const double rate = std::clamp(g_nativeMacroPlaybackRate.load(), 0.25, 4.0);
    const long double scaledElapsed = static_cast<long double>(rawElapsed) * static_cast<long double>(rate);
    const auto maxElapsed = static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
    const std::uint64_t elapsed = scaledElapsed >= maxElapsed
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(scaledElapsed);
    std::vector<NativeMacroEvent> due;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
        while (g_nativeMacroPlaybackIndex < g_nativeMacroEvents.size() &&
               g_nativeMacroEvents[g_nativeMacroPlaybackIndex].timeMicros <= elapsed) {
            due.push_back(g_nativeMacroEvents[g_nativeMacroPlaybackIndex++]);
        }
        if (g_nativeMacroPlaybackIndex >= g_nativeMacroEvents.size()) {
            if (g_nativeMacroLoop.load() && !g_nativeMacroEvents.empty()) {
                g_nativeMacroPlaybackIndex = 0;
                g_macroPlaybackStartMicros.store(now);
            } else {
                finished = true;
            }
        }
    }

    for (const auto& event : due) {
        if (event.type == kMacroReset) {
            if (g_originalResetLevel) g_originalResetLevel(layer);
            resetRunStats();
        } else {
            dispatchNativeMacroButton(layer, event);
        }
    }
    if (finished) finishNativeMacroPlayback(layer);
}


void applyGameThreadPriorityOnGameThread() {
    const DWORD threadId = GetCurrentThreadId();
    if (g_gameThreadId.load() != threadId) {
        g_gameThreadId.store(threadId);
        const int original = GetThreadPriority(GetCurrentThread());
        if (original != THREAD_PRIORITY_ERROR_RETURN) {
            g_originalGameThreadPriority.store(original);
            g_originalPriorityCaptured.store(true);
        }
        g_appliedGameThreadPriority.store(THREAD_PRIORITY_ERROR_RETURN);
    }
    const int wanted = priorityValue(g_gameThreadPriorityLevel.load());
    if (wanted != g_appliedGameThreadPriority.load() && SetThreadPriority(GetCurrentThread(), wanted)) {
        g_appliedGameThreadPriority.store(wanted);
    }
}

bool consumeFrameStepBudget() {
    auto budget = g_frameStepBudget.load();
    while (budget > 0) {
        if (g_frameStepBudget.compare_exchange_weak(budget, budget - 1)) return true;
    }
    return false;
}

void runOneLayerTick(void* self, float dt) {
    // Keep a live player snapshot for customizable HUD labels. The inferred member
    // offsets are used only after a strict known-build fingerprint and readable-page checks.
    NativeMacroEvent liveSnapshot{};
    capturePlayerSnapshot(self, liveSnapshot);

    // The engine-input macro timeline advances by exactly the dt passed to GD.
    g_simulationFrame.fetch_add(1);
    if (dt > 0.0f && std::isfinite(dt)) {
        const auto increment = static_cast<std::uint64_t>(std::max(0.0, static_cast<double>(dt) * 1'000'000.0));
        g_gameTimelineMicros.fetch_add(increment);
    }
    processNativeMacroPlayback(self);
    g_originalLayerUpdate(self, dt);

    if (self == g_activePlayLayer.load() && g_inLevel.load() && anyNativeAssistActive()) {
        g_assistedThisAttempt.store(true);
    }
}

void __fastcall hookLayerUpdate(void* self, float dt) {
    if (!g_originalLayerUpdate) return;
    const bool active = self == g_activePlayLayer.load() && g_inLevel.load();
    if (!active) {
        g_originalLayerUpdate(self, dt);
        return;
    }

    if (g_nativeMacroRecording.load() && !macroAllowedNow()) {
        g_nativeMacroRecording.store(false);
        g_nativeMacroPracticeConfirmed.store(false);
        OutputDebugStringW(L"GD Client: native macro recording stopped because Practice Mode is no longer confirmed.\n");
    }

    applyGameThreadPriorityOnGameThread();

    const double multiplier = std::clamp(g_speed.load(), 0.05, 10.0);
    const int tps = g_physicsTps.load();
    if (tps != g_lastUpdateTps) { g_physicsAccumulator = 0.0; g_lastUpdateTps = tps; }
    const bool singleStepRequested = g_frameStepEnabled.load() && g_frameStepPaused.load();
    if (singleStepRequested) {
        // A paused frame-step loop should not call GD with dt=0 every render frame;
        // that permits subsystems with dt-independent work to keep changing state.
        if (!consumeFrameStepBudget()) return;
        g_physicsAccumulator = 0.0;
        const float stepDt = tps > 0 ? (1.0f / static_cast<float>(tps))
                                     : static_cast<float>(dt * multiplier);
        runOneLayerTick(self, stepDt);
        return;
    }

    const double scaledDt = static_cast<double>(dt) * multiplier;
    if (tps <= 0) {
        g_physicsAccumulator = 0.0;
        runOneLayerTick(self, static_cast<float>(scaledDt));
        return;
    }

    // Experimental fixed-step substepping. This does not change the display/present
    // rate. It accumulates elapsed simulation time and invokes the verified game-layer
    // update with fixed-size steps. A hard cap prevents a catch-up spiral when the game
    // stalls; overload is intentionally handled as time dilation rather than an unbounded loop.
    if (!std::isfinite(scaledDt) || scaledDt <= 0.0) return;
    constexpr int kMaxSubstepsPerRenderUpdate = 32;
    const double step = 1.0 / static_cast<double>(tps);
    g_physicsAccumulator += std::min(scaledDt, 0.25);
    const double maxAccumulated = step * static_cast<double>(kMaxSubstepsPerRenderUpdate);
    if (g_physicsAccumulator > maxAccumulated) g_physicsAccumulator = maxAccumulated;
    const int steps = std::clamp(static_cast<int>(g_physicsAccumulator / step), 0, kMaxSubstepsPerRenderUpdate);
    if (steps <= 0) return;
    for (int i = 0; i < steps; ++i) {
        runOneLayerTick(self, static_cast<float>(step));
        g_physicsAccumulator -= step;
    }
    if (g_physicsAccumulator < 0.0) g_physicsAccumulator = 0.0;
}

bool __fastcall hookPlayInit(void* self, void* level, bool dontCreateObjects) {
    const bool result = g_originalPlayInit ? g_originalPlayInit(self, level, dontCreateObjects) : false;
    g_activePlayLayer.store(result ? self : nullptr);
    g_inLevel.store(result);
    g_practiceMode.store(false);
    g_noclipEnabled.store(false);
    g_hitboxTrailEnabled.store(false);
    g_frameStepEnabled.store(false);
    g_frameStepPaused.store(false);
    g_frameStepBudget.store(0);
    g_physicsTps.store(0);
    g_physicsAccumulator = 0.0;
    g_lastUpdateTps = 0;
    g_nativeMacroRecording.store(false);
    g_nativeMacroPlaying.store(false);
    g_nativeMacroStopRequested.store(false);
    g_nativeMacroPracticeConfirmed.store(false);
    g_nativeMacroPlaybackIndex = 0;
    g_nativeMacroHeldInputs.clear();
    g_gameTimelineMicros.store(0);
    g_simulationFrame.store(0);
    g_playerX.store(0.0f);
    g_playerY.store(0.0f);
    g_playerRotation.store(0.0f);
    resetRunStats();
    return result;
}

void __fastcall hookPlayPostUpdate(void* self, float dt) {
    if (g_originalPostUpdate) g_originalPostUpdate(self, dt);
    if (self != g_activePlayLayer.load() || !g_inLevel.load()) return;
    const bool collisionThisFrame = g_collisionThisFrame.exchange(false);
    if (!g_noclipEnabled.load()) {
        if (collisionThisFrame) g_collisionFrames.fetch_add(1);
        return;
    }
    const auto frames = g_frames.fetch_add(1) + 1;
    if (collisionThisFrame) g_collisionFrames.fetch_add(1);
    const auto collisionFrames = g_collisionFrames.load();
    const double accuracy = frames ? std::clamp(100.0 * (1.0 - static_cast<double>(collisionFrames) / static_cast<double>(frames)), 0.0, 100.0) : 100.0;
    g_accuracy.store(accuracy);
}

void __fastcall hookDestroyPlayer(void* self, void* player, void* object) {
    const bool activeLayer = self == g_activePlayLayer.load() && g_inLevel.load();
    if (!activeLayer || !g_noclipEnabled.load() || !g_originalDestroyPlayer) {
        if (g_originalDestroyPlayer) g_originalDestroyPlayer(self, player, object);
        return;
    }

    // Count one hazard/contact event per invocation, but accuracy counts distinct
    // collision frames. Limits are evaluated before suppressing the original death.
    g_collisionThisFrame.store(true);
    const auto contacts = g_contacts.fetch_add(1) + 1;
    const auto frames = g_frames.load();
    const auto collisionFrames = g_collisionFrames.load();
    const auto configuredLimit = std::max<std::uint32_t>(1, g_deathLimit.load());
    const bool deathLimitHit = g_deathLimitEnabled.load() && contacts >= configuredLimit;
    bool accuracyLimitHit = false;
    if (g_accuracyLimitEnabled.load() && frames >= 120) {
        const double projected = std::clamp(
            100.0 * (1.0 - static_cast<double>(collisionFrames + 1) / static_cast<double>(frames + 1)), 0.0, 100.0);
        accuracyLimitHit = projected < std::clamp(g_minAccuracy.load(), 0.0, 100.0);
    }

    if (deathLimitHit || accuracyLimitHit) {
        g_noclipEnabled.store(false);
        g_limitTriggered.store(true);
        // Let GD process this collision normally: the configured limit is a real
        // death threshold, not a status-only toggle.
        g_originalDestroyPlayer(self, player, object);
        return;
    }

    // Suppress the game function for this collision: the player does not die.
}

void __fastcall hookResetLevel(void* self) {
    if (self == g_activePlayLayer.load() && g_inLevel.load() && g_nativeMacroRecording.load() && macroAllowedNow()) {
        NativeMacroEvent event{};
        const auto now = g_gameTimelineMicros.load();
        const auto start = g_macroRecordStartMicros.load();
        event.timeMicros = now >= start ? now - start : 0;
        const auto frame = g_simulationFrame.load();
        const auto frameStart = g_macroRecordStartFrame.load();
        event.frameIndex = frame >= frameStart ? frame - frameStart : 0;
        event.type = kMacroReset;
        capturePlayerSnapshot(self, event);
        appendMacroEvent(event);
    }
    if (g_originalResetLevel) g_originalResetLevel(self);
    if (self == g_activePlayLayer.load()) { g_physicsAccumulator = 0.0; resetRunStats(); }
}

// Fires when the game itself decides the level was completed. If the
// attempt was assisted, the original function is deliberately NOT called:
// the game never transitions into its own level-complete state, so there is
// no end-level screen, no local best-percent write, and nothing downstream
// (including score/star submission, which in the real game only happens as
// a consequence of this call) gets a chance to run. If the attempt was
// clean, this hook is a pure passthrough and changes nothing.
void __fastcall hookLevelComplete(void* self) {
    if (self == g_activePlayLayer.load() && g_inLevel.load() && g_assistedThisAttempt.load()) {
        g_completionBlockedCount.fetch_add(1);
        return;
    }
    if (g_originalLevelComplete) g_originalLevelComplete(self);
}

void __fastcall hookOnQuit(void* self) {
    if (self == g_activePlayLayer.load()) {
        g_nativeMacroRecording.store(false);
        if (g_nativeMacroPlaying.load()) finishNativeMacroPlayback(self);
        g_inLevel.store(false);
        g_practiceMode.store(false);
        g_noclipEnabled.store(false);
        g_hitboxesEnabled.store(false);
        g_hitboxTrailEnabled.store(false);
        g_playerX.store(0.0f);
        g_playerY.store(0.0f);
        g_playerRotation.store(0.0f);
        g_frameStepEnabled.store(false);
        g_frameStepPaused.store(false);
        g_frameStepBudget.store(0);
        g_physicsAccumulator = 0.0;
        g_assistedThisAttempt.store(false);
    }
    if (g_originalOnQuit) g_originalOnQuit(self);
    if (self == g_activePlayLayer.load()) g_activePlayLayer.store(nullptr);
}

bool __fastcall hookShouldDrawHitboxes(void* self) {
    if (self == g_activePlayLayer.load() && g_inLevel.load() && g_hitboxesEnabled.load()) return true;
    return g_originalShouldDrawHitboxes ? g_originalShouldDrawHitboxes(self) : false;
}

bool __fastcall hookShouldDrawHitboxTrail(void* self) {
    if (self == g_activePlayLayer.load() && g_inLevel.load() && g_hitboxTrailEnabled.load()) return true;
    return g_originalShouldDrawHitboxTrail ? g_originalShouldDrawHitboxTrail(self) : false;
}

bool __fastcall hookShouldRecordHitboxTrail(void* self) {
    if (self == g_activePlayLayer.load() && g_inLevel.load() && g_hitboxTrailEnabled.load()) return true;
    return g_originalShouldRecordHitboxTrail ? g_originalShouldRecordHitboxTrail(self) : false;
}

void __fastcall hookTogglePractice(void* self, bool practice) {
    if (g_originalTogglePractice) g_originalTogglePractice(self, practice);
    if (self == g_activePlayLayer.load()) {
        g_practiceMode.store(practice);
        if (!practice) {
            g_nativeMacroRecording.store(false);
            if (g_nativeMacroPlaying.load()) g_nativeMacroStopRequested.store(true);
            // Custom fixed-step cadence is deliberately practice-only even if the UI safe-mode is disabled.
            g_physicsTps.store(0);
            g_physicsAccumulator = 0.0;
            g_lastUpdateTps = 0;
        }
    }
}

void __fastcall hookHandleButton(void* self, bool down, int button, bool isPlayer1) {
    const bool activeLayer = self == g_activePlayLayer.load() && g_inLevel.load();
    if (activeLayer && g_nativeMacroPlaying.load()) {
        // The replay is the input source while it runs; do not mix live input into it.
        return;
    }
    if (activeLayer && g_nativeMacroRecording.load() && macroAllowedNow()) {
        NativeMacroEvent event{};
        const auto now = g_gameTimelineMicros.load();
        const auto start = g_macroRecordStartMicros.load();
        event.timeMicros = now >= start ? now - start : 0;
        const auto frame = g_simulationFrame.load();
        const auto frameStart = g_macroRecordStartFrame.load();
        event.frameIndex = frame >= frameStart ? frame - frameStart : 0;
        event.type = kMacroButton;
        event.button = button;
        event.down = down ? 1 : 0;
        event.player1 = isPlayer1 ? 1 : 0;
        capturePlayerSnapshot(self, event);
        appendMacroEvent(event);
    }
    if (g_originalHandleButton) g_originalHandleButton(self, down, button, isPlayer1);
}

bool __fastcall hookIsIconUnlocked(void* self, int id, int type) {
    if (g_unlockIconsEnabled.load()) return true;
    return g_originalIsIconUnlocked ? g_originalIsIconUnlocked(self, id, type) : false;
}

bool __fastcall hookIsColorUnlocked(void* self, int id, int type) {
    if (g_unlockIconsEnabled.load()) return true;
    return g_originalIsColorUnlocked ? g_originalIsColorUnlocked(self, id, type) : false;
}

void __fastcall hookPlayerSetRotation(void* self, float rotation) {
    if (g_originalPlayerSetRotation) g_originalPlayerSetRotation(self, rotation);
    if (!g_build || !g_inLevel.load() || !std::isfinite(rotation)) return;
    auto* layer = reinterpret_cast<std::uint8_t*>(g_activePlayLayer.load());
    if (!layer) return;
    auto** playerSlot = reinterpret_cast<void**>(layer + g_build->gameLayerPlayer1Offset);
    if (!readableRange(playerSlot, sizeof(void*))) return;
    if (*playerSlot == self) g_playerRotation.store(rotation);
}
#endif // GDCLIENT_ENABLE_GAME_HOOKS

} // namespace

bool initialize() {
#if !GDCLIENT_ENABLE_GAME_HOOKS
    return false;
#else
    if (g_initialized.load()) return g_supported.load();
    g_initialized.store(true);
    g_gameModule = GetModuleHandleW(nullptr);
    g_build = detectBuild();
    if (!g_build) return false;

    const auto initStatus = MH_Initialize();
    if (initStatus != MH_OK && initStatus != MH_ERROR_ALREADY_INITIALIZED) {
        wchar_t msg[128]{};
        swprintf_s(msg, _countof(msg), L"GD Client: MinHook initialization failed (%d).\n", static_cast<int>(initStatus));
        OutputDebugStringW(msg);
        return false;
    }
    g_minhookInitialized = true;

    bool ok = true;
    ok &= installOne(g_build->gameLayerUpdateRva, reinterpret_cast<void*>(&hookLayerUpdate), reinterpret_cast<void**>(&g_originalLayerUpdate), L"GJBaseGameLayer::update", true);
    ok &= installOne(g_build->playLayerInitRva, reinterpret_cast<void*>(&hookPlayInit), reinterpret_cast<void**>(&g_originalPlayInit), L"PlayLayer::init", true);
    ok &= installOne(g_build->playLayerPostUpdateRva, reinterpret_cast<void*>(&hookPlayPostUpdate), reinterpret_cast<void**>(&g_originalPostUpdate), L"PlayLayer::postUpdate", true);
    ok &= installOne(g_build->playLayerDestroyPlayerRva, reinterpret_cast<void*>(&hookDestroyPlayer), reinterpret_cast<void**>(&g_originalDestroyPlayer), L"PlayLayer::destroyPlayer", true);
    ok &= installOne(g_build->playLayerResetLevelRva, reinterpret_cast<void*>(&hookResetLevel), reinterpret_cast<void**>(&g_originalResetLevel), L"PlayLayer::resetLevel", true);
    ok &= installOne(g_build->playLayerOnQuitRva, reinterpret_cast<void*>(&hookOnQuit), reinterpret_cast<void**>(&g_originalOnQuit), L"PlayLayer::onQuit", true);

    if (ok && g_build->playLayerLevelCompleteRva != 0) {
        if (installOne(g_build->playLayerLevelCompleteRva, reinterpret_cast<void*>(&hookLevelComplete),
                reinterpret_cast<void**>(&g_originalLevelComplete), L"PlayLayer::levelComplete", false)) {
            g_completionGuardAvailable.store(g_originalLevelComplete != nullptr);
        }
    }

    if (ok && g_build->playLayerShouldDrawHitboxesRva != 0) {
        if (installOne(g_build->playLayerShouldDrawHitboxesRva, reinterpret_cast<void*>(&hookShouldDrawHitboxes), reinterpret_cast<void**>(&g_originalShouldDrawHitboxes), L"PlayLayer::shouldDrawDebugHitboxes", false)) {
            g_hitboxHookAvailable.store(g_originalShouldDrawHitboxes != nullptr);
        }
    }
    if (ok && g_build->playLayerShouldDrawHitboxTrailRva != 0 && g_build->playLayerShouldRecordHitboxTrailRva != 0) {
        const bool drawInstalled = installOne(g_build->playLayerShouldDrawHitboxTrailRva,
            reinterpret_cast<void*>(&hookShouldDrawHitboxTrail), reinterpret_cast<void**>(&g_originalShouldDrawHitboxTrail),
            L"PlayLayer::shouldDrawHitboxTrail", false);
        const bool recordInstalled = installOne(g_build->playLayerShouldRecordHitboxTrailRva,
            reinterpret_cast<void*>(&hookShouldRecordHitboxTrail), reinterpret_cast<void**>(&g_originalShouldRecordHitboxTrail),
            L"PlayLayer::shouldRecordHitboxTrail", false);
        g_hitboxTrailHookAvailable.store(drawInstalled && recordInstalled &&
            g_originalShouldDrawHitboxTrail != nullptr && g_originalShouldRecordHitboxTrail != nullptr);
    }
    if (ok && g_build->playLayerTogglePracticeModeRva != 0) {
        if (installOne(g_build->playLayerTogglePracticeModeRva, reinterpret_cast<void*>(&hookTogglePractice), reinterpret_cast<void**>(&g_originalTogglePractice), L"PlayLayer::togglePracticeMode", false)) {
            g_practiceHookAvailable.store(g_originalTogglePractice != nullptr);
        }
    }
    if (ok && g_build->gameLayerHandleButtonRva != 0) {
        if (installOne(g_build->gameLayerHandleButtonRva, reinterpret_cast<void*>(&hookHandleButton), reinterpret_cast<void**>(&g_originalHandleButton), L"GJBaseGameLayer::handleButton", false)) {
            g_nativeMacroHookAvailable.store(g_originalHandleButton != nullptr);
        }
    }
    if (ok && g_build->playerSetRotationRva != 0) {
        installOne(g_build->playerSetRotationRva, reinterpret_cast<void*>(&hookPlayerSetRotation), reinterpret_cast<void**>(&g_originalPlayerSetRotation), L"PlayerObject::setRotation", false);
    }
    if (ok && g_build->gameManagerIsIconUnlockedRva != 0) {
        if (installOne(g_build->gameManagerIsIconUnlockedRva, reinterpret_cast<void*>(&hookIsIconUnlocked), reinterpret_cast<void**>(&g_originalIsIconUnlocked), L"GameManager::isIconUnlocked", false)) {
            g_unlockIconsHookAvailable.store(g_originalIsIconUnlocked != nullptr);
        }
    }
    if (ok && g_build->gameManagerIsColorUnlockedRva != 0) {
        if (installOne(g_build->gameManagerIsColorUnlockedRva, reinterpret_cast<void*>(&hookIsColorUnlocked), reinterpret_cast<void**>(&g_originalIsColorUnlocked), L"GameManager::isColorUnlocked", false)) {
            g_unlockColorsHookAvailable.store(g_originalIsColorUnlocked != nullptr);
        }
    }

    if (!ok) {
        MH_DisableHook(MH_ALL_HOOKS);
        for (auto* target : g_hookTargets) MH_RemoveHook(target);
        g_hookTargets.clear();
        MH_Uninitialize();
        g_minhookInitialized = false;
        g_originalLayerUpdate = nullptr;
        g_originalPlayInit = nullptr;
        g_originalPostUpdate = nullptr;
        g_originalDestroyPlayer = nullptr;
        g_originalResetLevel = nullptr;
        g_originalOnQuit = nullptr;
        g_originalShouldDrawHitboxes = nullptr;
        g_originalShouldDrawHitboxTrail = nullptr;
        g_originalShouldRecordHitboxTrail = nullptr;
        g_originalTogglePractice = nullptr;
        g_originalHandleButton = nullptr;
        g_originalIsIconUnlocked = nullptr;
        g_originalIsColorUnlocked = nullptr;
        g_originalPlayerSetRotation = nullptr;
        g_originalLevelComplete = nullptr;
        g_completionGuardAvailable.store(false);
        g_nativeMacroHookAvailable.store(false);
        g_unlockIconsHookAvailable.store(false);
        g_unlockColorsHookAvailable.store(false);
        return false;
    }

    g_speedHookAvailable.store(g_originalLayerUpdate != nullptr);
    g_noclipHookAvailable.store(g_originalDestroyPlayer != nullptr);
    g_supported.store(true);
    wchar_t ready[192]{};
    swprintf_s(ready, _countof(ready), L"GD Client: native game hooks enabled for %s.\n", g_build->name);
    OutputDebugStringW(ready);
    return true;
#endif
}

void shutdown() {
#if GDCLIENT_ENABLE_GAME_HOOKS
    if (g_gameThreadId.load() && g_originalPriorityCaptured.load()) {
        HANDLE thread = OpenThread(THREAD_SET_INFORMATION, FALSE, g_gameThreadId.load());
        if (thread) {
            SetThreadPriority(thread, g_originalGameThreadPriority.load());
            CloseHandle(thread);
        }
    }
    if (g_minhookInitialized) {
        // If the DLL is still loaded in the game, restoring the original entry
        // points is safe. Don't uninitialize MinHook from DllMain itself.
        MH_DisableHook(MH_ALL_HOOKS);
        for (auto* target : g_hookTargets) MH_RemoveHook(target);
        g_hookTargets.clear();
        MH_Uninitialize();
        g_minhookInitialized = false;
    }
#endif
    g_speedHookAvailable.store(false);
    g_noclipHookAvailable.store(false);
    g_hitboxHookAvailable.store(false);
    g_hitboxTrailHookAvailable.store(false);
    g_hitboxTrailEnabled.store(false);
    g_practiceHookAvailable.store(false);
    g_completionGuardAvailable.store(false);
    g_nativeMacroHookAvailable.store(false);
    g_nativeMacroRecording.store(false);
    g_nativeMacroPlaying.store(false);
    g_nativeMacroStopRequested.store(false);
    g_nativeMacroPracticeConfirmed.store(false);
    g_unlockIconsEnabled.store(false);
    g_unlockIconsHookAvailable.store(false);
    g_unlockColorsHookAvailable.store(false);
    g_supported.store(false);
    g_inLevel.store(false);
    g_practiceMode.store(false);
    g_noclipEnabled.store(false);
    g_hitboxesEnabled.store(false);
    g_frameStepEnabled.store(false);
    g_frameStepPaused.store(false);
    g_frameStepBudget.store(0);
    g_assistedThisAttempt.store(false);
    g_externalAssistActive.store(false);
    g_gameThreadId.store(0);
    g_appliedGameThreadPriority.store(THREAD_PRIORITY_ERROR_RETURN);
    g_originalPriorityCaptured.store(false);
    g_originalLevelComplete = nullptr;
#if GDCLIENT_ENABLE_GAME_HOOKS
    g_originalShouldDrawHitboxTrail = nullptr;
    g_originalShouldRecordHitboxTrail = nullptr;
#endif
    g_originalHandleButton = nullptr;
    g_originalIsIconUnlocked = nullptr;
    g_originalIsColorUnlocked = nullptr;
    g_originalPlayerSetRotation = nullptr;
    g_activePlayLayer.store(nullptr);
}

bool supportedBuild() { return g_supported.load(); }
const wchar_t* buildName() {
    if (!g_supported.load() || !g_build) return L"UNSUPPORTED / GAME HOOKS DISABLED";
    return g_build->name;
}
bool gameHooksReady() { return g_supported.load(); }
bool speedHookReady() { return g_speedHookAvailable.load(); }
bool noclipHookReady() { return g_noclipHookAvailable.load(); }
bool hitboxHookReady() { return g_hitboxHookAvailable.load(); }
bool hitboxTrailHookReady() { return g_hitboxTrailHookAvailable.load(); }
bool hitboxTrailEnabled() { return g_hitboxTrailEnabled.load(); }
void setHitboxTrailEnabled(bool enabled) {
    g_hitboxTrailEnabled.store(enabled && g_hitboxTrailHookAvailable.load() && g_inLevel.load());
}
bool completionGuardReady() { return g_completionGuardAvailable.load(); }
bool assistedThisAttempt() { return g_assistedThisAttempt.load(); }
std::uint64_t completionBlockedCount() { return g_completionBlockedCount.load(); }
void setExternalAssistActive(bool active) {
    g_externalAssistActive.store(active);
    if (active && g_inLevel.load()) g_assistedThisAttempt.store(true);
}
bool inLevel() { return g_inLevel.load(); }
bool practiceModeKnown() { return g_practiceHookAvailable.load(); }
bool practiceModeActive() { return g_practiceMode.load(); }
bool canUsePracticeFeatures(bool userConfirmedPractice) {
    if (!userConfirmedPractice || !g_inLevel.load()) return false;
    if (g_practiceHookAvailable.load() && !g_practiceMode.load()) return false;
    return true;
}
void setSpeedScale(double multiplier) { g_speed.store(std::clamp(multiplier, 0.05, 10.0)); }
double speedScale() { return g_speed.load(); }
bool setPhysicsTps(int hz, bool userConfirmedPractice) {
    if (hz != 0 && hz != 240 && hz != 360 && hz != 480 && hz != 1000) return false;
    if (hz == 0) { g_physicsTps.store(0); return true; }
    if (!g_supported.load() || !g_inLevel.load()) return false;
    if (!canUsePracticeFeatures(userConfirmedPractice)) return false;
    g_physicsTps.store(hz);
    return true;
}
int physicsTps() { return g_physicsTps.load(); }
void setNoclipEnabled(bool enabled) { g_noclipEnabled.store(enabled && g_noclipHookAvailable.load() && g_inLevel.load()); }
bool noclipEnabled() { return g_noclipEnabled.load(); }
void setNoclipDeathLimit(bool enabled, std::uint32_t contactLimit) {
    g_deathLimitEnabled.store(enabled);
    g_deathLimit.store(std::clamp<std::uint32_t>(contactLimit, 1, 100000));
}
void setNoclipAccuracyLimit(bool enabled, double minimumAccuracy) {
    g_accuracyLimitEnabled.store(enabled);
    g_minAccuracy.store(std::clamp(minimumAccuracy, 0.0, 100.0));
}
NoclipStats noclipStats() {
    return {g_frames.load(), g_collisionFrames.load(), g_contacts.load(), g_accuracy.load(), g_limitTriggered.load()};
}
PlayerSnapshot playerSnapshot() {
    if (!g_supported.load() || !g_inLevel.load()) return {};
    const float x = g_playerX.load();
    const float y = g_playerY.load();
    const float rotation = g_playerRotation.load();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(rotation)) return {};
    return {true, x, y, rotation};
}
void setHitboxesEnabled(bool enabled) { g_hitboxesEnabled.store(enabled && g_hitboxHookAvailable.load() && g_inLevel.load()); }
bool hitboxesEnabled() { return g_hitboxesEnabled.load(); }
void setFrameStepEnabled(bool enabled) {
    if (enabled && (!g_supported.load() || !g_inLevel.load())) return;
    g_frameStepEnabled.store(enabled);
    g_frameStepPaused.store(enabled);
    g_frameStepBudget.store(0);
}
bool frameStepEnabled() { return g_frameStepEnabled.load(); }
void setFrameStepPaused(bool paused) { if (g_frameStepEnabled.load()) g_frameStepPaused.store(paused); }
bool frameStepPaused() { return g_frameStepPaused.load(); }
bool advanceOneFrame() {
    if (!g_frameStepEnabled.load() || !g_frameStepPaused.load() || !g_inLevel.load()) return false;
    auto budget = g_frameStepBudget.load();
    while (budget < 1) {
        if (g_frameStepBudget.compare_exchange_weak(budget, budget + 1)) return true;
    }
    return true;
}
void setGameThreadPriorityLevel(int level) { g_gameThreadPriorityLevel.store(std::clamp(level, 0, 2)); }
int gameThreadPriorityLevel() { return g_gameThreadPriorityLevel.load(); }

bool nativeMacroHookReady() { return g_nativeMacroHookAvailable.load() && g_supported.load(); }

NativeMacroStats nativeMacroStats() {
    std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
    return {nativeMacroHookReady(), g_nativeMacroRecording.load(), g_nativeMacroPlaying.load(),
            static_cast<std::uint64_t>(g_nativeMacroEvents.size()), g_nativeMacroPlaybackRate.load()};
}

bool startNativeMacroRecording(bool userConfirmedPractice) {
    if (!nativeMacroHookReady() || !canUsePracticeFeatures(userConfirmedPractice) || g_nativeMacroPlaying.load()) return false;
    {
        std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
        g_nativeMacroEvents.clear();
        g_nativeMacroPlaybackIndex = 0;
    }
    g_nativeMacroStopRequested.store(false);
    g_nativeMacroPlaying.store(false);
    g_nativeMacroPracticeConfirmed.store(true);
    g_macroRecordStartMicros.store(g_gameTimelineMicros.load());
    g_macroRecordStartFrame.store(g_simulationFrame.load());
    g_nativeMacroRecording.store(true);
    return true;
}

void stopNativeMacroRecording() {
    g_nativeMacroRecording.store(false);
    if (!g_nativeMacroPlaying.load()) g_nativeMacroPracticeConfirmed.store(false);
}

bool saveNativeMacro() {
    if (g_nativeMacroRecording.load() || g_nativeMacroPlaying.load()) return false;
    std::vector<NativeMacroEvent> events;
    {
        std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
        events = g_nativeMacroEvents;
    }
    if (events.empty()) return false;
    const auto path = nativeMacroPath(true);
    if (path.empty()) return false;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    NativeMacroHeader header{{'G','D','N','3'}, 2, static_cast<std::uint64_t>(events.size())};
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (const auto& event : events) {
        NativeMacroDiskEvent disk{event.timeMicros, event.frameIndex, event.button, event.type, event.down, event.player1, 0,
                                  event.playerX, event.playerY, event.playerRotation};
        file.write(reinterpret_cast<const char*>(&disk), sizeof(disk));
        if (!file) return false;
    }
    file.flush();
    return static_cast<bool>(file);
}

bool loadNativeMacro() {
    if (g_nativeMacroRecording.load() || g_nativeMacroPlaying.load()) return false;
    const auto path = nativeMacroPath(false);
    if (path.empty()) return false;
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    NativeMacroHeader header{};
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file || std::memcmp(header.magic, "GDN3", 4) != 0 || header.version != 2 ||
        header.eventCount == 0 || header.eventCount > 5'000'000) return false;
    std::vector<NativeMacroEvent> events;
    events.reserve(static_cast<std::size_t>(header.eventCount));
    std::uint64_t previous = 0;
    std::uint64_t previousFrame = 0;
    for (std::uint64_t i = 0; i < header.eventCount; ++i) {
        NativeMacroDiskEvent disk{};
        file.read(reinterpret_cast<char*>(&disk), sizeof(disk));
        if (!file || disk.type > kMacroReset || disk.player1 > 1 || disk.down > 1 ||
            disk.timeMicros < previous || disk.frameIndex < previousFrame) return false;
        if (disk.type == kMacroButton && (disk.button < 0 || disk.button > 64)) return false;
        if (!std::isfinite(disk.playerX) || !std::isfinite(disk.playerY) || !std::isfinite(disk.playerRotation) ||
            std::abs(disk.playerX) > 1'000'000.0f || std::abs(disk.playerY) > 1'000'000.0f || std::abs(disk.playerRotation) > 1'000'000.0f) return false;
        events.push_back({disk.timeMicros, disk.frameIndex, disk.button, disk.type, disk.down, disk.player1, 0,
                          disk.playerX, disk.playerY, disk.playerRotation});
        previous = disk.timeMicros;
        previousFrame = disk.frameIndex;
    }
    char trailing{};
    if (file.read(&trailing, 1)) return false; // reject malformed/concatenated files
    {
        std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
        g_nativeMacroEvents = std::move(events);
        g_nativeMacroPlaybackIndex = 0;
    }
    return true;
}

bool startNativeMacroPlayback(bool userConfirmedPractice, bool loop) {
    if (!nativeMacroHookReady() || !canUsePracticeFeatures(userConfirmedPractice) || g_nativeMacroRecording.load()) return false;
    {
        std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
        if (g_nativeMacroEvents.empty()) {
            // Cannot call loadNativeMacro while holding the same mutex.
        } else {
            g_nativeMacroPlaybackIndex = 0;
            g_nativeMacroLoop.store(loop);
            g_nativeMacroPracticeConfirmed.store(true);
            g_macroPlaybackStartMicros.store(g_gameTimelineMicros.load());
            g_nativeMacroRecording.store(false);
            g_nativeMacroStopRequested.store(false);
            g_nativeMacroPlaying.store(true);
            return true;
        }
    }
    if (!loadNativeMacro()) return false;
    std::lock_guard<std::mutex> lock(g_nativeMacroMutex);
    if (g_nativeMacroEvents.empty()) return false;
    g_nativeMacroPlaybackIndex = 0;
    g_nativeMacroLoop.store(loop);
    g_nativeMacroPracticeConfirmed.store(true);
    g_macroPlaybackStartMicros.store(g_gameTimelineMicros.load());
    g_nativeMacroRecording.store(false);
    g_nativeMacroStopRequested.store(false);
    g_nativeMacroPlaying.store(true);
    return true;
}

void stopNativeMacroPlayback() {
    if (g_nativeMacroPlaying.load()) g_nativeMacroStopRequested.store(true);
}
void setNativeMacroPlaybackRate(double rate) { g_nativeMacroPlaybackRate.store(std::clamp(rate, 0.25, 4.0)); }
double nativeMacroPlaybackRate() { return g_nativeMacroPlaybackRate.load(); }

bool unlockIconsHookReady() { return g_unlockIconsHookAvailable.load() && g_supported.load(); }
bool unlockIconsEnabled() { return g_unlockIconsEnabled.load(); }
void setUnlockIconsEnabled(bool enabled) {
    g_unlockIconsEnabled.store(enabled && unlockIconsHookReady());
}

} // namespace gdclient::native
