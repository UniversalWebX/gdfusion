# Research and implementation references

The catalog uses original feature descriptions and status metadata informed by publicly documented Geometry Dash mod-menu conventions. It does not copy MegaHack source code, proprietary assets, or present itself as affiliated with MegaHack/Eclipse.

Public references reviewed for feature taxonomy and Windows-native Geometry Dash hooking conventions:
- https://github.com/prevter/OpenHack
- https://github.com/TheBearodactyl/openhack-but-updated
- https://github.com/prevter/gd.hpp
- https://github.com/geode-sdk/bindings
- https://github.com/TsudaKageyu/MinHook

Implementation notes:
- The standalone native hook address/RVA set is restricted to exact PE timestamps from two known Windows x64 builds. A GD version string alone is insufficient, because a hotfix may move functions while retaining the visible version number.
- The client does not depend on Geode or use its SDK; public bindings were used only to verify method names/signatures and to identify build-specific ABI risks.
- The GDI overlay remains outside the game's OpenGL/Cocos2d rendering pipeline because an earlier rendering-hook prototype crashed through Intel's driver.
- Native speed scaling, noclip limits, hitbox-debug override, fixed-step TPS, frame-step, thread priority and input macros are experimental until compiled and exercised against the matching executable in Windows.
- The practice guard is local safety behavior, not an online-request filter. No server-side unlock, account/password bypass, or leaderboard integrity guarantee is claimed.
- Current source and UI explicitly mark not-wired features rather than shipping inert switches that imply they work.

- `geode-sdk/bindings` GeometryDash 2.2091 binding metadata: PlayLayer::shouldDrawHitboxTrail and PlayLayer::shouldRecordHitboxTrail addresses are used only for the exact 2.2091 PE fingerprint; runtime hook behavior remains unverified.
