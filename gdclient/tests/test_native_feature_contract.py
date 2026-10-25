"""Portable source-contract checks for the Windows-only native client.

These checks validate the intended architecture but do not replace an MSVC build
or a real Geometry Dash run on each supported executable fingerprint.
"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "GDClient.cpp").read_text(encoding="utf-8")
HOOKS = (ROOT / "src" / "native_game_hooks.cpp").read_text(encoding="utf-8")
HOOKS_H = (ROOT / "src" / "native_game_hooks.hpp").read_text(encoding="utf-8")
CMAKE = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
CATALOG = json.loads((ROOT / "data" / "features.json").read_text(encoding="utf-8"))


def test_native_menu_is_an_eleven_page_horizontal_tab_ui():
    assert "constexpr int PAGE_COUNT = 11;" in SOURCE
    assert "void drawNavigation" in SOURCE
    assert 'L"Home", L"Player", L"Physics", L"Practice"' in SOURCE
    assert "case PAGE_PHYSICS: drawPhysics" in SOURCE
    assert "case PAGE_LABELS: drawLabels" in SOURCE


def test_overlay_tabs_stay_open_and_ui_hit_testing_owns_clicks():
    change_page = SOURCE.split("void changePage(int page) {", 1)[1].split("\n}", 1)[0]
    assert "g_menuOpen.store(true);" in change_page
    assert "setOverlayClickThrough(false);" in change_page
    assert "g_menuProgress = 1.0f;" in change_page
    assert "g_menuProgress > 0.015f" in SOURCE
    assert "setOverlayClickThrough(!g_menuOpen.load());" in SOURCE


def test_native_hook_target_is_strictly_fingerprint_guarded():
    assert "peTimestamp(GetModuleHandleW(nullptr))" in HOOKS
    assert 'GetModuleHandleW(L"libcocos2d.dll")' in HOOKS
    assert 'GetModuleHandleW(L"libExtensions.dll")' in HOOKS
    assert "if (!g_build) return false;" in HOOKS
    assert "unknown build" in HOOKS.lower() or "unsupported executable fingerprint" in HOOKS.lower()
    assert "0x6ace3633u" in HOOKS and "0x6ad2d987u" in HOOKS


def test_native_noclip_has_actual_collision_suppression_and_limits():
    assert "hookDestroyPlayer" in HOOKS
    assert "g_originalDestroyPlayer(self, player, object);" in HOOKS
    assert "if (deathLimitHit || accuracyLimitHit)" in HOOKS
    assert "g_noclipEnabled.store(false);" in HOOKS
    assert "g_accuracy.store(accuracy);" in HOOKS
    assert "contact-count and accuracy limits" in CATALOG_TEXT()


def CATALOG_TEXT():
    return "\n".join(f["description"] for f in CATALOG["features"])


def test_game_speed_uses_game_delta_time_not_global_windows_clock_scaling():
    assert "hookLayerUpdate" in HOOKS
    assert "const double scaledDt = static_cast<double>(dt) * multiplier;" in HOOKS
    assert "runOneLayerTick(self, static_cast<float>(scaledDt));" in HOOKS
    assert "GJBaseGameLayer::update" in HOOKS
    assert "QueryPerformanceCounter" not in SOURCE
    assert "setSpeedScale" in SOURCE


def test_hitbox_toggle_is_only_available_when_its_hook_was_installed():
    assert "hookShouldDrawHitboxes" in HOOKS
    assert "g_hitboxHookAvailable.store(g_originalShouldDrawHitboxes != nullptr);" in HOOKS
    assert "if (!gdclient::native::hitboxHookReady())" in SOURCE


def test_practice_macro_checks_level_state_and_practice_hook_when_supported():
    assert "canUsePracticeFeatures()" in SOURCE
    assert "g_practiceGuard.store(false);" in SOURCE
    assert "bool canUsePracticeFeatures(bool userConfirmedPractice)" in HOOKS_H
    assert "if (g_practiceHookAvailable.load() && !g_practiceMode.load()) return false;" in HOOKS
    assert "g_menuOpen.store(false);" in SOURCE
    assert "g_menuProgress > 0.025f" in SOURCE


def test_macro_format_supports_speed_normalized_gdm2_and_legacy_gdm1():
    assert "{'G','D','M','2'}" in SOURCE
    assert 'std::memcmp(prefix.magic, "GDM2", 4)' in SOURCE
    assert 'std::memcmp(prefix.magic, "GDM1", 4)' in SOURCE
    assert "g_macroRecordTimelineMs += static_cast<double>(delta) * effectiveGameSpeedScale();" in SOURCE
    assert "g_macroPlaybackTimelineMs += static_cast<double>(delta) * scale;" in SOURCE


def test_clicker_does_not_feed_back_into_macro_recording_or_playback():
    assert "!g_macroRecording.load() && !g_macroReplaying.load()" in SOURCE


def test_level_labels_only_draw_when_a_supported_play_layer_is_active():
    assert "!gdclient::native::inLevel()" in SOURCE
    assert "bool __fastcall hookPlayInit" in HOOKS
    assert "void __fastcall hookOnQuit" in HOOKS
    assert "{PRACTICE}" in SOURCE


def test_ui_uses_separate_gdi_backbuffer_without_opengl_swapbuffers_hook():
    assert "BitBlt(targetDC" in SOURCE
    assert "ensureBackBuffer" in SOURCE
    assert "SwapBuffers" not in SOURCE
    assert "image/png" in SOURCE
    assert "shot_%04u%02u%02u_%02u%02u%02u.png" in SOURCE


def test_native_build_downloads_minhook_but_does_not_depend_on_geode():
    assert "GDCLIENT_ENABLE_GAME_HOOKS" in CMAKE
    assert "TsudaKageyu/minhook" in CMAKE
    assert "GEODE_SDK" not in CMAKE


def test_features_with_native_hooks_have_explicit_experimental_status():
    by_id = {f["id"]: f for f in CATALOG["features"]}
    for feature_id in (
        "player-collision.noclip",
        "player-collision.noclip-limits",
        "speed-timing.speedhack",
        "visual-performance.show-hitboxes",
    ):
        assert by_id[feature_id]["implementation_status"] == "experimental"
        assert by_id[feature_id]["activation_type"] == "native-hook"


def test_fixed_step_tps_is_bounded_and_practice_confirmed():
    assert "bool setPhysicsTps(int hz, bool userConfirmedPractice)" in HOOKS_H
    assert "hz != 0 && hz != 240 && hz != 360 && hz != 480 && hz != 1000" in HOOKS
    assert "kMaxSubstepsPerRenderUpdate = 32" in HOOKS
    assert "canUsePracticeFeatures(userConfirmedPractice)" in HOOKS
    assert "g_physicsAccumulator > maxAccumulated" in HOOKS


def test_practice_only_safe_mode_is_persisted_and_fails_closed():
    assert "g_safeMode{true}" in SOURCE
    assert "PracticeOnlySafeMode" in SOURCE
    assert "gameplayModeAllowed(L\"Noclip\")" in SOURCE
    assert "not an online-request filter" in SOURCE


def test_settings_profile_cli_is_separate_from_injector():
    launcher = (ROOT / "gd_client.py").read_text(encoding="utf-8")
    assert "--export-profile" in launcher
    assert "--import-profile" in launcher
    assert "import_profile(settings_file_path(), args.import_profile)" in launcher


def test_practice_guard_also_gates_external_clicker_and_native_tps_reapplies_after_level_reset():
    assert 'const bool practicePermitted = !g_safeMode.load() || canUsePracticeFeatures();' in SOURCE
    assert 'if (g_autoClicker.exchange(false)) turnedOff = true;' in SOURCE
    assert 'if (canUsePracticeFeatures() && g_physicsTpsPreference.load() > 0' in SOURCE
    assert 'gdclient::native::setPhysicsTps(g_physicsTpsPreference.load(), g_practiceGuard.load());' in SOURCE


def test_customizable_keyboard_hotkeys_are_saved_captured_and_deduplicated():
    assert 'enum BindSlot : int' in SOURCE
    assert 'void beginKeybindCapture(int slot)' in SOURCE
    assert 'void processKeybindCapture()' in SOURCE
    assert 'void normaliseHotkeys()' in SOURCE
    assert 'WritePrivateProfileStringW(L"Keybinds", binding.name' in SOURCE
    assert 'GetPrivateProfileIntW(L"Keybinds", binding.name' in SOURCE
    assert 'g_dynamicWasDown[static_cast<std::size_t>(key)] = true;' in SOURCE
    assert 'Duplicate bindings are rejected' in SOURCE


def test_input_overlay_shows_wasd_and_per_key_hold_duration():
    assert 'g_wHeld.load()' in SOURCE and 'g_aHeld.load()' in SOURCE
    assert 'g_sHeld.load()' in SOURCE and 'g_dHeld.load()' in SOURCE
    assert 'heldMilliseconds(bool down' in SOURCE
    assert 'hold time shown per key' in SOURCE


def test_catalog_statuses_match_real_profile_tps_and_safe_mode_support():
    by_id = {f['id']: f for f in CATALOG['features']}
    assert len(CATALOG['features']) == 342
    assert by_id['speed-timing.physics-tps-bypass']['implementation_status'] == 'experimental'
    assert by_id['safety-accessibility.safe-mode']['implementation_status'] == 'experimental'
    assert by_id['safety-accessibility.auto-safe-mode']['implementation_status'] == 'experimental'
    assert by_id['interface-themes.settings-profiles']['implementation_status'] == 'implemented'
    assert 'does not intercept outbound http requests' in by_id['safety-accessibility.safe-mode']['description'].lower()


def test_dynamic_bindings_do_not_record_or_retrigger_their_own_macro_hotkeys():
    assert "bool isConfiguredHotkey(int key)" in SOURCE
    assert "const bool down = !isConfiguredHotkey(vk)" in SOURCE
    assert "generatedByMacro" in SOURCE
    assert "std::find(g_replayKeysDown.begin(), g_replayKeysDown.end()" in SOURCE


def test_frame_step_and_macro_hints_follow_rebound_key_names():
    assert "hotkeyName(g_bindFrameStep.load())" in SOURCE
    assert "hotkeyName(g_bindFrameStepOnce.load())" in SOURCE
    assert "hotkeyName(g_bindMacroRecord.load()) + L\" stops.\"" in SOURCE


def test_native_hitbox_trail_is_separate_from_death_freeze_and_fail_closed():
    by_id = {f["id"]: f for f in CATALOG["features"]}
    feature = by_id["visual-performance.native-hitbox-trail"]
    assert feature["implementation_status"] == "experimental"
    assert feature["activation_type"] == "native-hook"
    assert "not a frozen exact-death-frame wireframe" in feature["description"]
    assert "0x505bf0" in HOOKS and "0x505c20" in HOOKS
    assert "g_hitboxTrailHookAvailable.store(drawInstalled && recordInstalled" in HOOKS
    assert "g_originalShouldDrawHitboxTrail ? g_originalShouldDrawHitboxTrail(self) : false" in HOOKS
    assert "g_originalShouldRecordHitboxTrail ? g_originalShouldRecordHitboxTrail(self) : false" in HOOKS
    assert "hitboxTrailHookReady()" in HOOKS_H
    assert "setHitboxTrailEnabled(enable)" in SOURCE

