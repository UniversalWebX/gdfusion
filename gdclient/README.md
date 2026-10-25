# GD Client Ultimate — Standalone MegaHack-style Development Client

GD Client is a custom Windows x64 Geometry Dash companion/injected DLL project. It does not use the Geode SDK or require Geode to be installed. The injected UI is a separate Win32/GDI overlay, deliberately kept out of Geometry Dash's OpenGL `SwapBuffers` / Cocos2d `drawScene` render path because a prior render-hook prototype crashed in Intel's graphics driver.

This is a development prototype, not a verified drop-in MegaHack replacement. Some gameplay hooks are implemented for exact executable fingerprints; some are experimental; most entries in the feature catalog are roadmap metadata and are not wired to game behavior. See `FEATURE_STATUS.md` before relying on a feature.

## Current concrete features

- **Mandatory assist-disclosure banner.** A red banner at the top-center of the screen appears automatically whenever any gameplay-affecting hook is active (speed scale, noclip, frame-step, the physics TPS experiment, native or input-level macro playback, the auto-clicker, debug hitboxes, or the hitbox-trail override), naming exactly which ones. It has no setting, hotkey, opacity control, or "stream-safe" mode to hide it, is not written to `settings.ini`, and is not affected by Safe Mode, the HUD toggle, or menu state. It is deliberately not optional: if a run was assisted, that should be visible to the player and to anyone watching or recording. Do not add a way to suppress it; if you add a new gameplay-affecting hook, add it to the banner's trigger list too.
- **Horizontal tabbed UI** with Dashboard, Player, Physics, Practice, Labels, Visuals, Creator, Feature Lab, Tools, Settings, and About pages; persistent GDI back buffer; reduced-motion toggle; accent and opacity settings.
- **Strict game build guard** using the PE timestamps of `GeometryDash.exe`, `libcocos2d.dll`, and `libExtensions.dll`. Native hooks fail closed on unknown builds.
- **Game delta-time speed scaling** through the supported `GJBaseGameLayer::update` hook. This is experimental and does not currently synchronize FMOD pitch or every timer in the game.
- **Noclip / collision-death suppression** with contact-count and collision-frame accuracy thresholds. The limit deliberately lets a collision kill the player once the threshold is crossed.
- **Hitbox debug toggle** using the game's supported `PlayLayer::shouldDrawDebugHitboxes` decision hook where available.
- **Native hitbox-trail toggle (experimental)** on exact GD 2.2091: overrides both `PlayLayer::shouldDrawHitboxTrail` and `shouldRecordHitboxTrail` so GD uses its own trail implementation. This is not the custom per-category collision renderer or an exact freeze of hitboxes at death.
- **Fixed-step physics TPS experiment** at 240, 360, 480, or 1000 update steps per second. It uses a bounded 32-substep catch-up loop, changes simulation update cadence rather than presentation FPS, and requires a manually armed Practice guard.
- **Frame-step gate**, queued one-update advance, priority options (Normal / Above Normal / Highest), and game-thread integration.
- **Native practice macro record/playback** from `GJBaseGameLayer::handleButton`, with button edges, simulation-time timestamps, frame indices, player-position snapshots, loop and speed controls. Native macro files are local `GDM3` data under `%APPDATA%\GDClient` and require a manually armed Practice guard. The separate external-input recorder supports common keyboard/mouse transitions.
- **Three customizable level-screen labels**, with text, visibility, position, font size, and color options; placeholders include `{SPEED}`, `{SPEED_STATE}`, `{MACRO}`, `{EVENTS}`, `{NATIVE_EVENTS}`, `{CPS}`, `{NOCLIP_ACCURACY}`, `{NOCLIP_CONTACTS}`, `{PLAYER_X}`, `{PLAYER_Y}`, `{ROTATION}`, `{SESSION}`, and `{PRACTICE}`. They are GDI overlay labels over the detected game window, not Cocos2d world-space labels.
- **Input monitor / CPS meter** for W/A/S/D, Up, Space, LMB, RMB, per-key hold durations, rolling CPS and peak CPS.
- **Screenshot utility** that writes PNGs from the visible game client area using GDI+/GDI; it does not grab the DirectX/OpenGL swap-chain framebuffer.
- **Local icon/color unlock preview** in the local UI only, when the corresponding native hooks are available. This does not unlock anything on servers.
- **Configurable keyboard hotkeys** for menu, speed, speed preset, clicker, HUD, screenshot, macro record/play/stop, frame-step and single-step. Duplicate keybinds are rejected; Escape cancels capture. Macro recording skips configured client shortcut keys, and injected macro presses do not retrigger those shortcuts. Mouse-button keybinds are not supported yet.
- **Safe Mode** enabled by default. It locally gates gameplay-changing actions, macro input, and the clicker behind an active level and manually armed Practice guard; it disables active features when the guard is no longer confirmed. It is **not** an outbound HTTP filter and cannot guarantee a run is accepted by a leaderboard.
- **Portable JSON settings profiles**, with validated allow-listed sections and scalar values. Importing a profile changes `settings.ini`; restart GD Client to reload the imported settings.
- **342-entry feature catalog**, searchable desktop browser, generated C++ feature catalog, and a generated contract test suite.

## Exact native build support

Native hook functions are only enabled if all three PE timestamps match one of these known builds:

| Game build | `GeometryDash.exe` timestamp | `libcocos2d.dll` timestamp | `libExtensions.dll` timestamp |
|---|---:|---:|---:|
| 2.209 Steam build 25909607 (2026-10-13) | `0x6ACE3633` | `0x6ACBFF38` | `0x6ACBEFC5` |
| 2.2091 Steam build 26002420 (2026-10-17) | `0x6AD2D987` | `0x6AD2D6FC` | `0x6AD2D956` |

A version string alone is not enough because executable hotfixes can move function addresses while keeping a similar version label. Do not remove this guard or run these offsets on an unverified binary. The native DLL and the hooks have not been compiled or runtime-tested against the owner's installation in this environment.

## Build on Windows

Requirements: 64-bit Python 3, CMake, and Visual Studio Build Tools with **Desktop development with C++** and an x64 MSVC toolchain.

1. Extract this project to a clean directory.
2. Open Command Prompt in the project root.
3. Build the DLL:

   ```cmd
   python gd_client.py --build
   ```

4. Start Geometry Dash normally.
5. Inject the freshly built DLL:

   ```cmd
   python gd_client.py --inject-only
   ```

6. Press **Tab** by default to open the UI. Change keyboard shortcuts under Settings.

If the build fails, keep the terminal open and share the full output. If the native hook initializer reports an unsupported fingerprint, the game-internal feature hooks intentionally remain disabled.

## Config and profile commands

```cmd
python gd_client.py --export-profile MyPracticeProfile.json
python gd_client.py --import-profile MyPracticeProfile.json
python tools/feature_catalog_browser.py
python -m pytest -q tests
```

Profiles contain only allow-listed scalar preferences. They do not include game saves, macros, account data, executable paths, or DLLs.

## Feature implementation status

`FEATURE_STATUS.md` separates implemented utilities from experimental native hooks and entries that are still only catalog/documentation. Do not treat a feature row or UI catalog entry as proof that the game behavior is implemented. Current catalog counts are 18 marked implemented, 8 experimental, and 316 not wired. These statuses describe code availability, not proof of runtime correctness.

## Validation in this environment

- Python modules compile successfully.
- Full test suite: **82 passed**.
- 28,728 feature/configuration contracts execute in 48 batched modules.
- Catalog generator reports 342 native catalog entries.
- ZIP packaging/integrity can be checked automatically.
- **Not performed:** MSVC native compilation, injection into a local Geometry Dash process, runtime gameplay verification, audio sync verification, FPS presentation bypass, or validation of unsupported hooks/features.

The generated contract tests account for over 115,000 lines of the requested 100,000+ line count. They test distinct configuration contracts; they are not 115,000 lines of gameplay-cheat implementation. The codebase intentionally prioritizes honest feature status and safe failure over fake switches or unverified offsets.
