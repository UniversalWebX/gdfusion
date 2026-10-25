# Feature implementation status

The catalog contains **342 entries across 17 categories**. Current registry counts are **18 implemented**, **8 experimental**, and **316 not wired**. Status means source availability, not proof that the feature works in the target game at runtime.

- `implemented`: the described local utility/profile/UI behavior is present in the source. Native integration still needs runtime validation.
- `experimental`: a native hook or guard exists for the exact verified Windows x64 builds, but behavior is risky/version-sensitive or has not been runtime-validated here.
- `not-wired`: the catalog entry is a roadmap/schema item; no working gameplay implementation is supplied. The Python runtime must not pretend the game feature is enabled.

## Implemented local utilities

- Horizontal tabbed GDI/Win32 overlay with a persistent back buffer, animations, themes, opacity control, page navigation, search/feature lab, toasts, and settings persistence.
- Three editable screen-anchored GDI label overlays, live placeholders, input monitor, hold-duration indicators, CPS and peak-CPS meter.
- Visible-client-area PNG screenshot tool (not a swap-chain capture).
- External keyboard/mouse macro tooling and playback safety guards.
- Settings profile export/import using an allow-listed, schema-versioned JSON format.
- Configurable keyboard keybinds for 11 client actions, with capture, duplicate prevention, persistence, and default reset. Mouse-button bindings and modifiers are not implemented.

## Experimental native hooks

Only the two exact game builds listed in README are supported by the native hook guard. Other builds must report unsupported and keep native hooks disabled.

- Game delta-time speed scale via the supported game-layer update function. It does not synchronize FMOD pitch or every internal timer.
- Noclip collision-death suppression, contact counts, collision-frame accuracy, and real threshold behavior. A reached threshold lets GD process the collision normally.
- Game-provided hitbox debug decision override where `shouldDrawDebugHitboxes` exists.
- Experimental native hitbox-trail decision override on the exact 2.2091 build. It forces GD's own `shouldDrawHitboxTrail` and `shouldRecordHitboxTrail` callbacks to return true while active; this does not implement the custom per-category collision shapes or exact frozen death-frame wireframes.
- Bounded fixed-step substepping at 240/360/480/1000 update steps. This is not an FPS/presentation bypass and may change game timing; local Practice confirmation is required.
- Frame-step update gate and one-step budget.
- Native input macro recording/playback using `GJBaseGameLayer::handleButton`, simulation-time timestamps, reset events, and player state snapshots. Only local practice use is intended.
- Thread priority controls limited to Normal, Above Normal, and Highest.
- Local icon/color unlock preview in the icon picker only; not server-side unlocks.
- Practice-only Safe Mode is a client-side guard. It stops local gameplay changes, macros and clicker when the level/guard is not eligible; it does not filter outbound HTTP requests or enforce leaderboard rules.

## Explicitly not wired

The following entries remain catalog/roadmap items and are **not implemented as working cheats** in this version:

- Visual frame extrapolation, true FPS/presentation bypass, global engine-wide TPS modification, and pitch-corrected FMOD DSP speedhack.
- Smart StartPos switching, portal-state reconstruction for arbitrary StartPos locations, and frame-exact StartPos import.
- Custom collision polygon/hitbox rendering, per-category hitbox color picker, and hitbox freeze-on-death trails outside the game's own debug draw implementation.
- Editor scale/rotation/object-count/zoom/free-scroll bypasses; copy/password/verify bypasses; custom/default song bypasses.
- Cocos2d world-space labels, real level/menu/editor scene-state detection for the external label overlay, and full in-level UI nodes.
- Layout/render-pass mode, particle/portal-effect suppression, FMOD practice music/pitch hacks, checkpoint cooldown changes, auto-checkpoint disable, RGB player/wave rendering, wave mesh/pulse controls, and ball rotation bug fix.
- Best-run HUD logic integrated into the game's level-completion state; frame-buffer/swap-chain screenshots.
- Online HTTP/score submission interception, authoritative Demon List rules enforcement, account/password bypasses, or any guarantee of leaderboard compliance.
- Jump force modification, verified auto-complete, and replay-bot/editor automation features that require more game-specific hooks than this build contains.

Do not label a not-wired catalog row as active in UI. Do not remove executable fingerprint checks or add guessed offsets to make unsupported features appear available.

## Known limitations and safety notes

- The overlay is a separate GDI window so the menu does not use the game's OpenGL render path. Its labels are screen-anchored overlays, not objects attached to Cocos2d's world.
- The per-launch manual Practice guard is a precaution. Where a supported Practice-mode hook is available, the client also checks GD's flag; otherwise the user must confirm Practice manually.
- Native C++ compilation and runtime tests inside Geometry Dash were not possible in this environment. All native hook features remain unverified in the user's actual game process until tested on the target Windows installation.
- Feature catalog names are organized from public feature descriptions and original metadata; no proprietary MegaHack source/assets are copied.

## Test/size accounting

- 28,728 generated feature/configuration contracts in 48 batched modules.
- Python tests are run as part of the release validation; see `BUILD_MANIFEST.json` for the current count.
- The project's 100k+ total code/test lines include generated test code; they are not 100k lines of handwritten game-hook implementations.
