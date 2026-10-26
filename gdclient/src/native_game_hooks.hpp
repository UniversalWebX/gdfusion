#pragma once

#include <windows.h>
#include <cstdint>

namespace gdclient::native {

struct NoclipStats {
    std::uint64_t frames = 0;
    std::uint64_t collisionFrames = 0;
    std::uint64_t contacts = 0;
    double accuracy = 100.0;
    bool limitTriggered = false;
};

struct PlayerSnapshot {
    bool available = false;
    float x = 0.0f;
    float y = 0.0f;
    float rotation = 0.0f;
};

struct NativeMacroStats {
    bool available = false;
    bool recording = false;
    bool playing = false;
    std::uint64_t eventCount = 0;
    double playbackRate = 1.0;
};

// Installs native hooks only after matching Geometry Dash.exe, libcocos2d.dll,
// and libExtensions.dll PE timestamps against a known supported build.
// Unknown builds fail closed: no game function is patched.
bool initialize();
void shutdown();

bool supportedBuild();
const wchar_t* buildName();
bool gameHooksReady();
bool speedHookReady();
bool noclipHookReady();
bool hitboxHookReady();
bool hitboxTrailHookReady();
bool hitboxTrailEnabled();
void setHitboxTrailEnabled(bool enabled);
bool inLevel();
bool practiceModeKnown();
bool practiceModeActive();
bool canUsePracticeFeatures(bool userConfirmedPractice);

void setSpeedScale(double multiplier);
double speedScale();
// Fixed-step substepping for local Practice Mode experiments. 0 restores GD's normal update cadence.
// Only the supported rates 0/240/360/480/1000 are accepted; the loop is capped to 32 steps per render update.
bool setPhysicsTps(int hz, bool userConfirmedPractice);
int physicsTps();
void setNoclipEnabled(bool enabled);
bool noclipEnabled();
void setNoclipDeathLimit(bool enabled, std::uint32_t contactLimit);
void setNoclipAccuracyLimit(bool enabled, double minimumAccuracy);
NoclipStats noclipStats();
PlayerSnapshot playerSnapshot();
void setHitboxesEnabled(bool enabled);
bool hitboxesEnabled();

// Scheduler-adjacent controls are intentionally limited to the verified game
// update hook. Frame-step is an experimental dt gate, not a deterministic replay.
void setFrameStepEnabled(bool enabled);
bool frameStepEnabled();
void setFrameStepPaused(bool paused);
bool frameStepPaused();
bool advanceOneFrame();

// Game-thread priority is only Normal / Above Normal / Highest; realtime is
// deliberately excluded to avoid starving Windows or other applications.
void setGameThreadPriorityLevel(int level);
int gameThreadPriorityLevel();

// Completion guard: if any gameplay-affecting hook (native or external) was
// active at any point since the attempt started, the game's own
// PlayLayer::levelComplete is intercepted and NOT forwarded, so the attempt
// never registers as completed in the game. completionBlockedCount() goes up
// each time this happens; the UI polls it to show "Level Completed with
// Cheats" instead of a normal completion. This has no setting to disable it.
bool completionGuardReady();
bool assistedThisAttempt();
std::uint64_t completionBlockedCount();
// Called by the UI layer for assists that live outside this file (the
// input-level macro player, the auto-clicker) so they count toward the
// completion guard the same as native hooks do.
void setExternalAssistActive(bool active);

// Engine-input macro: events are intercepted at GJBaseGameLayer::handleButton
// and replayed against the supported game's simulation-time timeline. Use only
// in a locally confirmed Practice Mode level.
bool nativeMacroHookReady();
NativeMacroStats nativeMacroStats();
bool startNativeMacroRecording(bool userConfirmedPractice);
void stopNativeMacroRecording();
bool saveNativeMacro();
bool loadNativeMacro();
bool startNativeMacroPlayback(bool userConfirmedPractice, bool loop);
void stopNativeMacroPlayback();
void setNativeMacroPlaybackRate(double rate);
double nativeMacroPlaybackRate();

// Local icon-picker preview only; these hooks do not unlock server-side items.
bool unlockIconsHookReady();
bool unlockIconsEnabled();
void setUnlockIconsEnabled(bool enabled);

} // namespace gdclient::native
