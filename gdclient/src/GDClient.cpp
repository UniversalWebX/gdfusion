#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
// WIN32_LEAN_AND_MEAN omits some COM/OLE headers that GDI+ expects.
// Include these explicitly before gdiplus.h so IStream, PROPID, and RPC types exist.
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwctype>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <fstream>
#include <ctime>
#include <utility>
#include "generated_feature_catalog.hpp"
#include "native_game_hooks.hpp"

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdiplus.lib")

namespace {

HMODULE g_self = nullptr;
HWND g_gameWindow = nullptr;
HWND g_overlayWindow = nullptr;
// Persistent back buffer: drawing is completed off-screen and blitted once per frame.
HDC g_backBufferDC = nullptr;
HBITMAP g_backBufferBitmap = nullptr;
HGDIOBJ g_backBufferOriginalBitmap = nullptr;
int g_backBufferWidth = 0;
int g_backBufferHeight = 0;
std::atomic<bool> g_running{true};
std::atomic<bool> g_menuOpen{false};
std::atomic<bool> g_speedEnabled{false};
std::atomic<bool> g_autoClicker{false};
std::atomic<bool> g_clickerHoldToRun{false};
std::atomic<bool> g_showHud{true};
std::atomic<bool> g_animations{true};
std::atomic<double> g_speedMultiplier{1.5};
std::atomic<int> g_cps{12}; // Auto-clicker rate, separate from observed hardware CPS.
std::atomic<bool> g_inputOverlayVisible{true};
std::atomic<bool> g_unlockIconsPreference{false};
std::atomic<int> g_hardwareCps{0};
std::atomic<int> g_peakHardwareCps{0};
std::atomic<bool> g_spaceHeld{false}, g_upHeld{false};
std::atomic<bool> g_wHeld{false}, g_aHeld{false}, g_sHeld{false}, g_dHeld{false};
std::atomic<bool> g_leftMouseHeld{false}, g_rightMouseHeld{false};
std::atomic<ULONGLONG> g_spaceHeldSince{0}, g_upHeldSince{0};
std::atomic<ULONGLONG> g_wHeldSince{0}, g_aHeldSince{0}, g_sHeldSince{0}, g_dHeldSince{0};
std::atomic<ULONGLONG> g_leftMouseHeldSince{0}, g_rightMouseHeldSince{0};
std::deque<ULONGLONG> g_hardwareClickTimes;
bool g_prevSpaceHeld = false, g_prevUpHeld = false;
bool g_prevWHeld = false, g_prevAHeld = false, g_prevSHeld = false, g_prevDHeld = false;
bool g_prevLeftMouseHeld = false, g_prevRightMouseHeld = false;
std::atomic<int> g_accentIndex{1};
std::atomic<int> g_overlayAlpha{245};
constexpr COLORREF KEY_COLOR = RGB(255, 0, 255);
constexpr int MENU_W = 1000;
constexpr int MENU_H = 690;
// Navigation is a horizontal tab strip, not a sidebar.
constexpr int SIDEBAR_W = 0;
constexpr int PAGE_COUNT = 11;
enum Page : int { PAGE_DASHBOARD, PAGE_PLAYER, PAGE_PHYSICS, PAGE_PRACTICE, PAGE_LABELS, PAGE_VISUALS, PAGE_CREATOR, PAGE_BYPASS, PAGE_UTILITIES, PAGE_SETTINGS, PAGE_ABOUT };
std::atomic<int> g_page{PAGE_DASHBOARD};
int g_menuX = 22;
int g_menuY = 24;
float g_menuProgress = 0.0f;
float g_pageProgress = 1.0f;
int g_previousPage = PAGE_DASHBOARD;
bool g_dragging = false;
int g_dragOffsetX = 0;
int g_dragOffsetY = 0;
int g_drawnMenuW = MENU_W;
int g_drawnMenuH = MENU_H;
int g_pageScroll[PAGE_COUNT]{};
std::wstring g_toast;
ULONGLONG g_toastStarted = 0;
std::atomic<unsigned long long> g_sessionStarted{0};
std::atomic<unsigned long long> g_screenshotCount{0};
std::atomic<bool> g_macroRecording{false};
std::atomic<bool> g_macroReplaying{false};
std::atomic<bool> g_macroLoop{false};
std::atomic<int> g_macroCount{0};
// Macro times are stored on a game-time timeline: elapsed wall-clock time is
// integrated using the active speed scale while recording and while replaying.
std::atomic<bool> g_practiceGuard{false};
// Local safety guard: by default native gameplay-changing features require the practice guard.
// This does not inspect or filter outbound network requests.
std::atomic<bool> g_safeMode{true};
std::atomic<int> g_physicsTpsPreference{0};
// Configurable keyboard hotkeys. A capture action is local to the current session;
// the captured binding values themselves persist in settings.ini.
enum BindSlot : int { BIND_MENU = 1, BIND_SPEED, BIND_SPEED_CYCLE, BIND_CLICKER, BIND_HUD,
    BIND_SCREENSHOT, BIND_MACRO_RECORD, BIND_MACRO_PLAY, BIND_MACRO_STOP, BIND_FRAME_STEP, BIND_FRAME_STEP_ONCE };
std::atomic<int> g_bindMenu{VK_TAB}, g_bindSpeed{VK_F2}, g_bindSpeedCycle{VK_F3}, g_bindClicker{VK_F4};
std::atomic<int> g_bindHud{VK_F5}, g_bindScreenshot{VK_F6}, g_bindMacroRecord{VK_F7}, g_bindMacroPlay{VK_F8};
std::atomic<int> g_bindMacroStop{VK_F9}, g_bindFrameStep{VK_F10}, g_bindFrameStepOnce{VK_F11};
std::atomic<int> g_captureBindFor{0};
std::array<bool, 256> g_dynamicWasDown{};
std::array<bool, 256> g_captureWasDown{};
// These are UI preferences; the native game-hook module enforces them in-game.
std::atomic<bool> g_noclipDeathLimitOn{false};
std::atomic<int> g_noclipDeathThreshold{10};
std::atomic<bool> g_noclipAccuracyLimitOn{false};
std::atomic<double> g_noclipMinAccuracy{75.0};
std::atomic<bool> g_macroUsesGameTimeline{true};
double g_macroRecordTimelineMs = 0.0;
double g_macroPlaybackTimelineMs = 0.0;
ULONGLONG g_macroLastTimelineSample = 0;
ULONGLONG g_macroPlaybackLastSample = 0;

struct LevelLabelSettings {
    std::wstring text;
    bool enabled;
    int position; // 0 TL, 1 TR, 2 BL, 3 BR, 4 TC, 5 BC
    int fontSize;
    int colorIndex;
};
std::atomic<bool> g_levelLabelsVisible{true};
LevelLabelSettings g_levelLabels[3] = {
    {L"GD CLIENT  |  {SPEED}", true, 0, 12, 0},
    {L"MACRO: {MACRO}", true, 1, 12, 1},
    {L"{EVENTS} INPUT EVENTS  |  {SESSION}", false, 2, 11, 2}
};
HWND g_labelEditor = nullptr;
HWND g_labelTextEdits[3]{};
HWND g_labelVisibleChecks[3]{};
HWND g_labelPositionCombos[3]{};
HWND g_labelSizeCombos[3]{};
HWND g_labelColorCombos[3]{};
HWND g_labelMasterCheck = nullptr;
constexpr int IDC_LABEL_TEXT_BASE = 2100;
constexpr int IDC_LABEL_VISIBLE_BASE = 2110;
constexpr int IDC_LABEL_POSITION_BASE = 2120;
constexpr int IDC_LABEL_SIZE_BASE = 2130;
constexpr int IDC_LABEL_COLOR_BASE = 2140;
constexpr int IDC_LABEL_MASTER_VISIBLE = 2150;
constexpr int IDC_LABEL_APPLY = 2160;
constexpr int IDC_LABEL_CANCEL = 2161;

enum MacroEventType : DWORD { MACRO_KEY_DOWN = 1, MACRO_KEY_UP = 2, MACRO_MOUSE_LEFT_DOWN = 3, MACRO_MOUSE_LEFT_UP = 4, MACRO_MOUSE_RIGHT_DOWN = 5, MACRO_MOUSE_RIGHT_UP = 6 };
struct MacroEvent { DWORD elapsedMs; DWORD type; DWORD code; LONG x; LONG y; };
std::vector<MacroEvent> g_macroEvents;
ULONGLONG g_macroRecordStarted = 0;
ULONGLONG g_macroPlayStarted = 0;
size_t g_macroPlayIndex = 0;
std::vector<DWORD> g_replayKeysDown;
bool g_replayLeftDown = false;
bool g_replayRightDown = false;

struct RectI { int x, y, w, h; };

bool contains(const RectI& r, int x, int y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

std::wstring lowerString(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return value;
}

bool containsInsensitive(const std::wstring& value, const wchar_t* needle) {
    return lowerString(value).find(lowerString(needle)) != std::wstring::npos;
}

bool isGameWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return pid == GetCurrentProcessId();
}

struct GameWindowCandidate {
    HWND hwnd = nullptr;
    long long score = -1;
};

BOOL CALLBACK enumWindowProc(HWND hwnd, LPARAM param) {
    auto* best = reinterpret_cast<GameWindowCandidate*>(param);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (!isGameWindow(hwnd)) return TRUE;

    wchar_t className[256]{};
    wchar_t title[512]{};
    GetClassNameW(hwnd, className, static_cast<int>(_countof(className)));
    GetWindowTextW(hwnd, title, static_cast<int>(_countof(title)));
    const std::wstring cls(className);
    const std::wstring caption(title);
    const std::wstring lowerCaption = lowerString(caption);
    const std::wstring lowerClass = lowerString(cls);

    // Never attach the overlay to a console or to our own overlay window.
    if (lowerClass == L"consolewindowclass" || lowerClass == L"gdclientgdioverlay" || lowerClass == L"gdclientoverlaywin") return TRUE;
    if (lowerCaption.find(L"console") != std::wstring::npos || lowerCaption.find(L"debug output") != std::wstring::npos) return TRUE;
    if (lowerCaption.find(L"geode console") != std::wstring::npos) return TRUE;

    RECT client{};
    if (!GetClientRect(hwnd, &client)) return TRUE;
    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    if (width < 480 || height < 300) return TRUE;

    // Prefer the actual game caption decisively; otherwise use the largest usable
    // non-console top-level window belonging to this process.
    long long score = static_cast<long long>(width) * static_cast<long long>(height);
    if (lowerCaption.find(L"geometry dash") != std::wstring::npos) score += 1000000000000LL;
    if (lowerCaption.find(L"geode") != std::wstring::npos && lowerCaption.find(L"geometry dash") == std::wstring::npos) score -= 100000000000LL;
    if (lowerClass.find(L"console") != std::wstring::npos) score -= 100000000000LL;

    if (score > best->score) {
        best->score = score;
        best->hwnd = hwnd;
    }
    return TRUE;
}

HWND findGameWindow() {
    GameWindowCandidate best{};
    EnumWindows(enumWindowProc, reinterpret_cast<LPARAM>(&best));
    return best.hwnd;
}

void showToast(const wchar_t* message);
bool canUsePracticeFeatures();
bool gameplayModeAllowed(const wchar_t* feature, bool notify = true);

void setSpeedScale(double scale) {
    // Do not scale Windows clocks globally; the native hook scales only GD's
    // GJBaseGameLayer::update delta time for the active PlayLayer.
    gdclient::native::setSpeedScale(scale);
}

void toggleSpeed() {
    const bool turningOn = !g_speedEnabled.load();
    if (turningOn && !gameplayModeAllowed(L"Speedhack")) return;
    g_speedEnabled.store(turningOn);
    setSpeedScale(g_speedEnabled.load() ? g_speedMultiplier.load() : 1.0);
}

void cycleSpeed() {
    constexpr double choices[] = {0.25, 0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    double cur = g_speedMultiplier.load();
    size_t next = 0;
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); ++i) {
        if (std::abs(choices[i] - cur) < 0.01) { next = (i + 1) % (sizeof(choices) / sizeof(choices[0])); break; }
    }
    g_speedMultiplier.store(choices[next]);
    if (g_speedEnabled.load()) setSpeedScale(choices[next]);
}

void stepSpeed(int delta) {
    constexpr double choices[] = {0.25, 0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    int index = 0;
    for (int i = 0; i < static_cast<int>(_countof(choices)); ++i) {
        if (std::abs(choices[i] - g_speedMultiplier.load()) < 0.01) { index = i; break; }
    }
    index = (index + delta + static_cast<int>(_countof(choices))) % static_cast<int>(_countof(choices));
    g_speedMultiplier.store(choices[index]);
    if (g_speedEnabled.load()) setSpeedScale(choices[index]);
}

void clickAtCursor() {
    if (!g_gameWindow || GetForegroundWindow() != g_gameWindow || g_menuOpen.load() || !g_autoClicker.load()) return;
    POINT p{}; if (!GetCursorPos(&p)) return;
    POINT client = p;
    RECT rc{}; GetClientRect(g_gameWindow, &rc);
    if (!ScreenToClient(g_gameWindow, &client) || !PtInRect(&rc, client)) return;
    INPUT in[2]{};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

bool hasTimingHook() {
    // The Windows import hooks only provide diagnostics. Actual game speed is
    // controlled by the version-guarded GJBaseGameLayer::update hook.
    return gdclient::native::speedHookReady();
}

bool canUsePracticeFeatures() {
    return gdclient::native::canUsePracticeFeatures(g_practiceGuard.load());
}

bool gameplayModeAllowed(const wchar_t* feature, bool notify) {
    if (!g_safeMode.load() || canUsePracticeFeatures()) return true;
    if (notify) {
        std::wstring message = L"Safe Mode blocked ";
        message += feature;
        message += L". Arm the Practice Guard and use a verified Practice Mode level.";
        if (!gdclient::native::practiceModeKnown())
            message = L"Safe Mode blocked the feature. This build has manual Practice confirmation only; arm the Practice Guard before using it.";
        showToast(message.c_str());
    }
    return false;
}

void syncNativeSettings() {
    gdclient::native::setNoclipDeathLimit(g_noclipDeathLimitOn.load(),
        static_cast<std::uint32_t>(std::clamp(g_noclipDeathThreshold.load(), 1, 100000)));
    gdclient::native::setNoclipAccuracyLimit(g_noclipAccuracyLimitOn.load(),
        std::clamp(g_noclipMinAccuracy.load(), 0.0, 100.0));
    gdclient::native::setSpeedScale(g_speedEnabled.load() ? g_speedMultiplier.load() : 1.0);
    if (g_physicsTpsPreference.load() == 0 || !canUsePracticeFeatures()) {
        gdclient::native::setPhysicsTps(0, false);
    } else {
        gdclient::native::setPhysicsTps(g_physicsTpsPreference.load(), g_practiceGuard.load());
    }
    gdclient::native::setUnlockIconsEnabled(g_unlockIconsPreference.load());
}

std::atomic<int>& hotkeyRef(int slot) {
    switch (slot) {
        case BIND_MENU: return g_bindMenu;
        case BIND_SPEED: return g_bindSpeed;
        case BIND_SPEED_CYCLE: return g_bindSpeedCycle;
        case BIND_CLICKER: return g_bindClicker;
        case BIND_HUD: return g_bindHud;
        case BIND_SCREENSHOT: return g_bindScreenshot;
        case BIND_MACRO_RECORD: return g_bindMacroRecord;
        case BIND_MACRO_PLAY: return g_bindMacroPlay;
        case BIND_MACRO_STOP: return g_bindMacroStop;
        case BIND_FRAME_STEP: return g_bindFrameStep;
        case BIND_FRAME_STEP_ONCE: return g_bindFrameStepOnce;
        default: return g_bindMenu;
    }
}

const wchar_t* hotkeySlotName(int slot) {
    switch (slot) {
        case BIND_MENU: return L"Menu toggle";
        case BIND_SPEED: return L"Speedhack";
        case BIND_SPEED_CYCLE: return L"Speed preset";
        case BIND_CLICKER: return L"Auto clicker";
        case BIND_HUD: return L"Status HUD";
        case BIND_SCREENSHOT: return L"Screenshot";
        case BIND_MACRO_RECORD: return L"Macro record";
        case BIND_MACRO_PLAY: return L"Macro playback";
        case BIND_MACRO_STOP: return L"Stop macro";
        case BIND_FRAME_STEP: return L"Frame step";
        case BIND_FRAME_STEP_ONCE: return L"Step once";
        default: return L"Unknown";
    }
}

bool bindableKey(int key) {
    if (key <= VK_XBUTTON2 || key > 0xFE || key == VK_ESCAPE) return false;
    switch (key) {
        case VK_SHIFT: case VK_CONTROL: case VK_MENU:
        case VK_LSHIFT: case VK_RSHIFT: case VK_LCONTROL: case VK_RCONTROL:
        case VK_LMENU: case VK_RMENU: case VK_LWIN: case VK_RWIN:
        case VK_PAUSE: case VK_CAPITAL: return false;
        default: return true;
    }
}

std::wstring hotkeyName(int key) {
    if (key == VK_TAB) return L"TAB";
    if (key == VK_INSERT) return L"INSERT";
    if (key == VK_HOME) return L"HOME";
    if (key == VK_END) return L"END";
    if (key == VK_SPACE) return L"SPACE";
    if (key == VK_ESCAPE) return L"ESC";
    if (key == VK_RETURN) return L"ENTER";
    if (key == VK_PRIOR) return L"PAGE UP";
    if (key == VK_NEXT) return L"PAGE DOWN";
    if (key == VK_UP) return L"UP";
    if (key == VK_DOWN) return L"DOWN";
    if (key == VK_LEFT) return L"LEFT";
    if (key == VK_RIGHT) return L"RIGHT";
    if (key >= VK_F1 && key <= VK_F24) { wchar_t text[12]{}; swprintf_s(text, _countof(text), L"F%d", key - VK_F1 + 1); return text; }
    if (key >= 'A' && key <= 'Z') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= '0' && key <= '9') return std::wstring(1, static_cast<wchar_t>(key));
    if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9) { wchar_t text[16]{}; swprintf_s(text, _countof(text), L"NUM %d", key - VK_NUMPAD0); return text; }
    wchar_t text[16]{}; swprintf_s(text, _countof(text), L"VK %02X", key); return text;
}

bool isConfiguredHotkey(int key) {
    if (key <= 0 || key >= 256) return false;
    for (int slot = BIND_MENU; slot <= BIND_FRAME_STEP_ONCE; ++slot) {
        if (hotkeyRef(slot).load() == key) return true;
    }
    return false;
}

void normaliseHotkeys() {
    constexpr int defaults[] = {VK_TAB, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11};
    std::array<bool, 256> used{};
    bool valid = true;
    for (int slot = BIND_MENU; slot <= BIND_FRAME_STEP_ONCE; ++slot) {
        const int key = hotkeyRef(slot).load();
        if (!bindableKey(key) || used[static_cast<std::size_t>(key)]) { valid = false; break; }
        used[static_cast<std::size_t>(key)] = true;
    }
    if (!valid) {
        for (int i = 0; i < 11; ++i) hotkeyRef(i + 1).store(defaults[i]);
        OutputDebugStringW(L"GD Client: invalid or duplicate saved keybinds; restored default hotkeys.\n");
    }
}

void beginKeybindCapture(int slot) {
    if (slot < BIND_MENU || slot > BIND_FRAME_STEP_ONCE) return;
    for (int key = 1; key < 256; ++key) g_captureWasDown[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
    g_captureBindFor.store(slot);
    showToast(L"Press a keyboard key to bind this action. Escape cancels.");
}

COLORREF accentColor() {
    constexpr COLORREF accents[] = { RGB(67, 153, 255), RGB(163, 103, 255), RGB(37, 214, 180), RGB(255, 140, 80), RGB(255, 91, 151) };
    return accents[g_accentIndex.load() % _countof(accents)];
}

COLORREF blend(COLORREF a, COLORREF b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto channel = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t); };
    return RGB(channel(GetRValue(a), GetRValue(b)), channel(GetGValue(a), GetGValue(b)), channel(GetBValue(a), GetBValue(b)));
}

void rounded(HDC dc, int x, int y, int w, int h, int radius, COLORREF color, COLORREF border = RGB(255, 0, 255)) {
    HBRUSH brush = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, border == KEY_COLOR ? color : border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w, y + h, radius, radius);
    SelectObject(dc, oldBrush); SelectObject(dc, oldPen);
    DeleteObject(brush); DeleteObject(pen);
}

void solidRect(HDC dc, int x, int y, int w, int h, COLORREF color) {
    RECT r{x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(color);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void drawText(HDC dc, int x, int y, int w, int h, const wchar_t* value, COLORREF color, int size = 14, bool bold = false, UINT align = DT_LEFT) {
    HFONT font = CreateFontW(-size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color);
    RECT r{x, y, x + w, y + h};
    DrawTextW(dc, value, -1, &r, DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | align | DT_END_ELLIPSIS);
    SelectObject(dc, old); DeleteObject(font);
}

void drawGradient(HDC dc, const RectI& r, COLORREF left, COLORREF right) {
    const int strips = std::max(1, r.w / 3);
    for (int i = 0; i < strips; ++i) {
        const int x1 = r.x + (r.w * i) / strips;
        const int x2 = r.x + (r.w * (i + 1)) / strips;
        solidRect(dc, x1, r.y, std::max(1, x2 - x1), r.h, blend(left, right, static_cast<float>(i) / std::max(1, strips - 1)));
    }
}

void drawToggle(HDC dc, int x, int y, bool enabled, bool available = true) {
    COLORREF track = enabled ? accentColor() : RGB(63, 49, 73);
    if (!available) track = RGB(42, 36, 49);
    rounded(dc, x, y, 48, 25, 16, track, track);
    const int knobX = enabled ? x + 26 : x + 4;
    rounded(dc, knobX, y + 4, 17, 17, 12, available ? RGB(245, 248, 255) : RGB(130, 137, 150), available ? RGB(245, 248, 255) : RGB(130, 137, 150));
}

void drawButton(HDC dc, const RectI& r, const wchar_t* label, bool primary = false, bool enabled = true) {
    const COLORREF fill = !enabled ? RGB(32, 27, 38) : primary ? blend(accentColor(), RGB(46, 29, 59), 0.42f) : RGB(45, 36, 56);
    const COLORREF edge = !enabled ? RGB(49, 42, 56) : primary ? accentColor() : RGB(86, 69, 104);
    rounded(dc, r.x, r.y, r.w, r.h, 11, fill, edge);
    drawText(dc, r.x + 4, r.y, r.w - 8, r.h, label, enabled ? RGB(231, 240, 252) : RGB(110, 120, 136), 12, true, DT_CENTER);
}

void drawCard(HDC dc, const RectI& r, const wchar_t* title, const wchar_t* subtitle = nullptr) {
    rounded(dc, r.x, r.y, r.w, r.h, 12, RGB(22, 18, 31), RGB(76, 64, 100));
    drawText(dc, r.x + 15, r.y + 10, r.w - 28, 22, title, RGB(228, 237, 250), 14, true);
    if (subtitle) drawText(dc, r.x + 15, r.y + 31, r.w - 28, 18, subtitle, RGB(139, 155, 178), 11, false);
}

void drawStatCard(HDC dc, const RectI& r, const wchar_t* label, const wchar_t* value, const wchar_t* footnote, COLORREF indicator) {
    drawCard(dc, r, label, nullptr);
    rounded(dc, r.x + 15, r.y + 44, 5, 27, 3, indicator, indicator);
    drawText(dc, r.x + 29, r.y + 39, r.w - 121, 34, value, RGB(245, 248, 255), 22, true);
    drawText(dc, r.x + 15, r.y + r.h - 25, r.w - 110, 17, footnote, RGB(135, 152, 177), 10, false);
}

void showToast(const wchar_t* message) {
    g_toast = message;
    g_toastStarted = GetTickCount64();
    if (g_overlayWindow && IsWindow(g_overlayWindow))
        InvalidateRect(g_overlayWindow, nullptr, FALSE);
}

const wchar_t* pageTitle(int page) {
    switch (page) {
        case PAGE_DASHBOARD: return L"Dashboard";
        case PAGE_PLAYER: return L"Player";
        case PAGE_PHYSICS: return L"Physics";
        case PAGE_PRACTICE: return L"Practice";
        case PAGE_LABELS: return L"Level labels";
        case PAGE_VISUALS: return L"Visuals";
        case PAGE_CREATOR: return L"Creator tools";
        case PAGE_BYPASS: return L"Feature lab";
        case PAGE_UTILITIES: return L"Utilities";
        case PAGE_SETTINGS: return L"Settings";
        default: return L"About GD Client";
    }
}

const wchar_t* pageSubtitle(int page) {
    switch (page) {
        case PAGE_DASHBOARD: return L"Your tools, state, and quick actions in one place.";
        case PAGE_PLAYER: return L"Control timing and input helpers.";
        case PAGE_PHYSICS: return L"Native collision, noclip limits, hitbox debug, and validated build status.";
        case PAGE_PRACTICE: return L"Record and replay practice inputs on a speed-normalized timeline.";
        case PAGE_LABELS: return L"Custom screen-anchored labels displayed over the Geometry Dash window.";
        case PAGE_VISUALS: return L"Tune the overlay's appearance and status display.";
        case PAGE_CREATOR: return L"Editor feature roadmap and compatibility status.";
        case PAGE_BYPASS: return L"Browse 342 feature entries; implemented, experimental, and not-wired states are labeled honestly.";
        case PAGE_UTILITIES: return L"Screenshots, macros, local files, and session tools.";
        case PAGE_SETTINGS: return L"Personalize controls, appearance, and configuration.";
        default: return L"Independent custom native client • standalone overlay.";
    }
}

void drawNavigation(HDC dc, int x, int y, int width, int selected) {
    const wchar_t* labels[PAGE_COUNT] = {
        L"Home", L"Player", L"Physics", L"Practice", L"Labels", L"Visuals",
        L"Creator", L"Feature Lab", L"Tools", L"Settings", L"About"
    };
    const int left = x + 13;
    const int totalWidth = std::max(10, width - 26);
    const int tabWidth = totalWidth / PAGE_COUNT;
    for (int i = 0; i < PAGE_COUNT; ++i) {
        const int tabX = left + i * tabWidth;
        const int actualWidth = (i == PAGE_COUNT - 1) ? (x + width - 13 - tabX) : tabWidth;
        RectI r{tabX, y + 64, std::max(30, actualWidth - 3), 34};
        if (i == selected) {
            rounded(dc, r.x, r.y, r.w, r.h, 10, RGB(70, 48, 97), blend(accentColor(), RGB(40, 29, 57), 0.38f));
            rounded(dc, r.x + 12, r.y + r.h - 3, std::max(8, r.w - 24), 2, 3, accentColor(), accentColor());
        } else {
            rounded(dc, r.x, r.y, r.w, r.h, 10, RGB(31, 23, 38), RGB(80, 62, 90));
        }
        drawText(dc, r.x + 2, r.y + 1, r.w - 4, r.h - 2, labels[i],
            i == selected ? RGB(245, 248, 255) : RGB(150, 166, 190),
            actualWidth < 72 ? 8 : 10, i == selected, DT_CENTER);
    }
}

void drawPill(HDC dc, int x, int y, int w, const wchar_t* value, COLORREF color, COLORREF textColor = RGB(225,235,249)) {
    rounded(dc, x, y, w, 22, 10, blend(color, RGB(24,17,32), 0.77f), blend(color, RGB(24,17,32), 0.40f));
    drawText(dc, x + 3, y, w - 6, 22, value, textColor, 9, true, DT_CENTER);
}

void drawFeatureRow(HDC dc, const RectI& r, const wchar_t* title, const wchar_t* subtitle, const wchar_t* status, COLORREF stateColor, bool clickable = false) {
    drawCard(dc, r, title, nullptr);
    drawText(dc, r.x + 14, r.y + 34, r.w - 150, r.h - 39, subtitle, RGB(139, 155, 179), 10, false);
    drawPill(dc, r.x + r.w - 125, r.y + 12, 108, status, stateColor);
    if (clickable) drawText(dc, r.x + r.w - 125, r.y + r.h - 23, 108, 14, L"CLICK TO TOGGLE", accentColor(), 8, true, DT_CENTER);
}

std::wstring formatDuration(unsigned long long ms) {
    unsigned long long seconds = ms / 1000;
    const unsigned long long hours = seconds / 3600;
    const unsigned long long minutes = (seconds / 60) % 60;
    seconds %= 60;
    wchar_t buf[48]{};
    if (hours > 0) swprintf_s(buf, _countof(buf), L"%lluh %02llum %02llus", hours, minutes, seconds);
    else swprintf_s(buf, _countof(buf), L"%llum %02llus", minutes, seconds);
    return buf;
}

std::wstring replaceAll(std::wstring value, const std::wstring& token, const std::wstring& replacement) {
    if (token.empty()) return value;
    size_t at = 0;
    while ((at = value.find(token, at)) != std::wstring::npos) {
        value.replace(at, token.size(), replacement);
        at += replacement.size();
    }
    return value;
}

std::wstring expandLevelLabel(std::wstring value) {
    wchar_t speed[32]{};
    const double effective = (g_speedEnabled.load() && hasTimingHook()) ? g_speedMultiplier.load() : 1.0;
    swprintf_s(speed, _countof(speed), L"%.2fx", effective);
    const wchar_t* macroState = g_macroRecording.load() ? L"RECORDING" : (g_macroReplaying.load() ? L"PLAYING" : L"IDLE");
    wchar_t count[32]{}; swprintf_s(count, _countof(count), L"%d", g_macroCount.load());
    wchar_t cps[32]{}; swprintf_s(cps, _countof(cps), L"%d", g_cps.load());
    value = replaceAll(std::move(value), L"{SPEED}", speed);
    value = replaceAll(std::move(value), L"{SPEED_STATE}", g_speedEnabled.load() ? L"ON" : L"OFF");
    value = replaceAll(std::move(value), L"{EVENTS}", count);
    value = replaceAll(std::move(value), L"{CPS}", cps);
    const auto nc = gdclient::native::noclipStats();
    wchar_t ncAccuracy[32]{}; swprintf_s(ncAccuracy, _countof(ncAccuracy), L"%.2f", nc.accuracy);
    wchar_t ncContacts[32]{}; swprintf_s(ncContacts, _countof(ncContacts), L"%llu", static_cast<unsigned long long>(nc.contacts));
    value = replaceAll(std::move(value), L"{NOCLIP_ACCURACY}", ncAccuracy);
    value = replaceAll(std::move(value), L"{NOCLIP_CONTACTS}", ncContacts);
    const auto nativePlayer = gdclient::native::playerSnapshot();
    wchar_t px[32]{}, py[32]{}, rot[32]{};
    if (nativePlayer.available) {
        swprintf_s(px, _countof(px), L"%.1f", nativePlayer.x);
        swprintf_s(py, _countof(py), L"%.1f", nativePlayer.y);
        swprintf_s(rot, _countof(rot), L"%.1f", nativePlayer.rotation);
    } else {
        wcscpy_s(px, _countof(px), L"N/A"); wcscpy_s(py, _countof(py), L"N/A"); wcscpy_s(rot, _countof(rot), L"N/A");
    }
    const auto nativeMacro = gdclient::native::nativeMacroStats();
    wchar_t nativeEvents[32]{}; swprintf_s(nativeEvents, _countof(nativeEvents), L"%llu", static_cast<unsigned long long>(nativeMacro.eventCount));
    const wchar_t* effectiveMacroState = nativeMacro.recording ? L"NATIVE RECORDING" : (nativeMacro.playing ? L"NATIVE PLAYING" : macroState);
    value = replaceAll(std::move(value), L"{PLAYER_X}", px);
    value = replaceAll(std::move(value), L"{PLAYER_Y}", py);
    value = replaceAll(std::move(value), L"{ROTATION}", rot);
    value = replaceAll(std::move(value), L"{NATIVE_EVENTS}", nativeEvents);
    value = replaceAll(std::move(value), L"{MACRO}", effectiveMacroState);
    value = replaceAll(std::move(value), L"{SESSION}", formatDuration(g_sessionStarted.load() ? GetTickCount64() - g_sessionStarted.load() : 0));
    const wchar_t* practice = !g_practiceGuard.load() ? L"SAFE" :
        (gdclient::native::practiceModeKnown() ? (gdclient::native::practiceModeActive() ? L"PRACTICE" : L"NORMAL") : L"MANUAL");
    value = replaceAll(std::move(value), L"{PRACTICE}", practice);
    return value;
}

COLORREF levelLabelColor(int index) {
    constexpr COLORREF colors[] = {
        RGB(67, 153, 255), RGB(43, 205, 198), RGB(105, 220, 145),
        RGB(255, 104, 145), RGB(255, 166, 80)
    };
    return colors[std::clamp(index, 0, static_cast<int>(_countof(colors)) - 1)];
}

void drawLevelLabels(HDC dc, int width, int height) {
    if (!g_levelLabelsVisible.load() || !gdclient::native::inLevel() || width < 100 || height < 100) return;
    for (int i = 0; i < 3; ++i) {
        const auto& label = g_levelLabels[i];
        if (!label.enabled || label.text.empty()) continue;
        const std::wstring textValue = expandLevelLabel(label.text);
        const int fontSize = std::clamp(label.fontSize, 9, 24);
        const int textW = std::clamp(static_cast<int>(textValue.size() * fontSize * 0.62) + 25, 100, std::max(100, width - 24));
        const int boxW = std::min(textW, width - 24);
        const int boxH = fontSize + 17;
        int stack = 0;
        for (int j = 0; j < i; ++j) if (g_levelLabels[j].enabled && g_levelLabels[j].position == label.position) ++stack;
        int x = 12;
        int y = 52 + stack * (boxH + 5);
        switch (label.position) {
            case 1: x = width - boxW - 12; break;
            case 2: x = 12; y = height - boxH - 14 - stack * (boxH + 5); break;
            case 3: x = width - boxW - 12; y = height - boxH - 14 - stack * (boxH + 5); break;
            case 4: x = (width - boxW) / 2; break;
            case 5: x = (width - boxW) / 2; y = height - boxH - 14 - stack * (boxH + 5); break;
            default: break;
        }
        x = std::clamp(x, 4, std::max(4, width - boxW - 4));
        y = std::clamp(y, 4, std::max(4, height - boxH - 4));
        const COLORREF color = levelLabelColor(label.colorIndex);
        rounded(dc, x, y, boxW, boxH, 7, RGB(11, 16, 27), blend(color, RGB(22, 31, 46), 0.30f));
        rounded(dc, x + 5, y + 6, 3, boxH - 12, 2, color, color);
        drawText(dc, x + 14, y, boxW - 20, boxH, textValue.c_str(), RGB(237, 244, 255), fontSize, true,
            (label.position == 1 || label.position == 3) ? DT_RIGHT : DT_LEFT);
    }
}

void drawLabels(HDC dc, int x, int y, int width) {
    drawFeatureRow(dc, {x, y, width, 72}, L"Level labels overlay", L"Show customizable labels over the supported active PlayLayer. Unknown game builds fail closed, so labels remain hidden outside detected levels.", g_levelLabelsVisible.load() ? L"VISIBLE" : L"HIDDEN", g_levelLabelsVisible.load() ? RGB(54, 215, 157) : RGB(101, 121, 151), true);
    const int y2 = y + 84;
    drawCard(dc, {x, y2, width, 124}, L"Label editor", L"Configure up to three labels, screen position, size, color, and placeholder text.");
    drawButton(dc, {x + 16, y2 + 73, 154, 34}, L"Edit labels...", true);
    drawButton(dc, {x + 180, y2 + 73, 154, 34}, L"Reset labels", false);
    drawPill(dc, x + width - 125, y2 + 78, 106, g_levelLabelsVisible.load() ? L"OVERLAY ON" : L"OVERLAY OFF", g_levelLabelsVisible.load() ? RGB(54, 215, 157) : RGB(100, 118, 147));
    const int y3 = y2 + 136;
    drawCard(dc, {x, y3, width, 125}, L"Live placeholders", L"Use these tokens in any label's text. They update while the client runs.");
    drawText(dc, x + 16, y3 + 45, width - 32, 62,
        L"{SPEED} {SPEED_STATE} {MACRO} {EVENTS} {NATIVE_EVENTS} {CPS} {NOCLIP_ACCURACY} {NOCLIP_CONTACTS} {PLAYER_X} {PLAYER_Y} {ROTATION} {SESSION} {PRACTICE}",
        RGB(153, 203, 255), 11, true);
}

int heldMilliseconds(bool down, const std::atomic<ULONGLONG>& started, ULONGLONG now) {
    if (!down) return 0;
    const ULONGLONG began = started.load();
    if (began == 0 || now < began) return 0;
    return static_cast<int>(std::min<ULONGLONG>(now - began, 24ULL * 60ULL * 60ULL * 1000ULL));
}

void drawInputChip(HDC dc, int x, int y, int w, int h, const wchar_t* name,
                   bool down, const std::atomic<ULONGLONG>& started, ULONGLONG now) {
    const COLORREF fill = down ? RGB(32, 104, 88) : RGB(26, 35, 51);
    const COLORREF edge = down ? RGB(61, 218, 164) : RGB(47, 64, 88);
    rounded(dc, x, y, w, h, 6, fill, edge);
    drawText(dc, x + 1, y + 2, w - 2, 13, name,
             down ? RGB(196, 255, 233) : RGB(158, 178, 206), 8, true, DT_CENTER);
    wchar_t duration[24]{};
    const int ms = heldMilliseconds(down, started, now);
    if (!down) wcscpy_s(duration, _countof(duration), L"—");
    else if (ms < 1000) swprintf_s(duration, _countof(duration), L"%dms", ms);
    else swprintf_s(duration, _countof(duration), L"%.1fs", static_cast<double>(ms) / 1000.0);
    drawText(dc, x + 1, y + 15, w - 2, h - 16, duration,
             down ? RGB(230, 255, 245) : RGB(105, 126, 153), 7, false, DT_CENTER);
}

void drawInputOverlay(HDC dc, int width, int height) {
    if (!g_inputOverlayVisible.load() || width < 390 || height < 130) return;
    const int x = 12, y = 12, w = 420, h = 99;
    rounded(dc, x, y, w, h, 14, RGB(15, 19, 29), RGB(61, 80, 112));
    drawText(dc, x + 12, y + 5, 165, 18, L"INPUT MONITOR", RGB(147, 176, 216), 9, true);
    wchar_t cpsText[80]{};
    swprintf_s(cpsText, _countof(cpsText), L"CPS %d  /  PEAK %d", g_hardwareCps.load(), g_peakHardwareCps.load());
    drawText(dc, x + 166, y + 5, w - 178, 18, cpsText, RGB(228, 238, 252), 9, true, DT_RIGHT);
    const ULONGLONG now = GetTickCount64();
    const int chipW = 50, chipH = 28, gap = 5;
    const int row1Y = y + 27;
    struct KeyState { const wchar_t* name; bool down; const std::atomic<ULONGLONG>* started; };
    const KeyState keys[] = {
        {L"W", g_wHeld.load(), &g_wHeldSince}, {L"A", g_aHeld.load(), &g_aHeldSince},
        {L"S", g_sHeld.load(), &g_sHeldSince}, {L"D", g_dHeld.load(), &g_dHeldSince},
        {L"UP", g_upHeld.load(), &g_upHeldSince}, {L"SPACE", g_spaceHeld.load(), &g_spaceHeldSince}
    };
    for (int i = 0; i < static_cast<int>(_countof(keys)); ++i)
        drawInputChip(dc, x + 12 + i * (chipW + gap), row1Y, chipW, chipH, keys[i].name, keys[i].down, *keys[i].started, now);
    drawInputChip(dc, x + 12, y + 62, 88, 28, L"LMB", g_leftMouseHeld.load(), g_leftMouseHeldSince, now);
    drawInputChip(dc, x + 106, y + 62, 88, 28, L"RMB", g_rightMouseHeld.load(), g_rightMouseHeldSince, now);
    drawText(dc, x + 207, y + 67, w - 219, 18, L"hold time shown per key", RGB(105, 126, 153), 8, false);
}

void updateInputTelemetry() {
    const ULONGLONG now = GetTickCount64();
    const HWND foreground = GetForegroundWindow();
    const bool focused = g_gameWindow && IsWindow(g_gameWindow) &&
        (foreground == g_gameWindow || GetAncestor(foreground, GA_ROOT) == g_gameWindow) &&
        !g_menuOpen.load();
    const bool spaceDown = focused && ((GetAsyncKeyState(VK_SPACE) & 0x8000) != 0);
    const bool upDown = focused && ((GetAsyncKeyState(VK_UP) & 0x8000) != 0);
    const bool wDown = focused && ((GetAsyncKeyState('W') & 0x8000) != 0);
    const bool aDown = focused && ((GetAsyncKeyState('A') & 0x8000) != 0);
    const bool sDown = focused && ((GetAsyncKeyState('S') & 0x8000) != 0);
    const bool dDown = focused && ((GetAsyncKeyState('D') & 0x8000) != 0);
    const bool leftDown = focused && ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    const bool rightDown = focused && ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0);
    const auto updateStarted = [now](bool down, bool wasDown, std::atomic<ULONGLONG>& started) {
        if (down && !wasDown) started.store(now);
        else if (!down) started.store(0);
    };
    updateStarted(spaceDown, g_prevSpaceHeld, g_spaceHeldSince);
    updateStarted(upDown, g_prevUpHeld, g_upHeldSince);
    updateStarted(wDown, g_prevWHeld, g_wHeldSince);
    updateStarted(aDown, g_prevAHeld, g_aHeldSince);
    updateStarted(sDown, g_prevSHeld, g_sHeldSince);
    updateStarted(dDown, g_prevDHeld, g_dHeldSince);
    updateStarted(leftDown, g_prevLeftMouseHeld, g_leftMouseHeldSince);
    updateStarted(rightDown, g_prevRightMouseHeld, g_rightMouseHeldSince);

    // Do not treat clicks generated by the client's own clicker or macro player as hardware CPS.
    if (focused && !g_autoClicker.load() && !g_macroReplaying.load()) {
        if (leftDown && !g_prevLeftMouseHeld) g_hardwareClickTimes.push_back(now);
        if (rightDown && !g_prevRightMouseHeld) g_hardwareClickTimes.push_back(now);
    }
    g_prevSpaceHeld = spaceDown; g_prevUpHeld = upDown;
    g_prevWHeld = wDown; g_prevAHeld = aDown; g_prevSHeld = sDown; g_prevDHeld = dDown;
    g_prevLeftMouseHeld = leftDown; g_prevRightMouseHeld = rightDown;
    g_spaceHeld.store(spaceDown); g_upHeld.store(upDown);
    g_wHeld.store(wDown); g_aHeld.store(aDown); g_sHeld.store(sDown); g_dHeld.store(dDown);
    g_leftMouseHeld.store(leftDown); g_rightMouseHeld.store(rightDown);
    while (!g_hardwareClickTimes.empty() && now - g_hardwareClickTimes.front() >= 1000) g_hardwareClickTimes.pop_front();
    const int cps = static_cast<int>(g_hardwareClickTimes.size());
    g_hardwareCps.store(cps);
    if (cps > g_peakHardwareCps.load()) g_peakHardwareCps.store(cps);
}

void drawDashboard(HDC dc, int x, int y, int width) {
    const int gap = 12;
    const int col = (width - gap) / 2;
    const RectI speed{x, y, col, 94};
    const RectI click{x + col + gap, y, col, 94};
    drawFeatureRow(dc, speed, L"Speed scale", hasTimingHook() ? L"Version-guarded game delta-time hook; exact build fingerprint required." : L"No supported timing import found in the target process.", g_speedEnabled.load() ? L"ENABLED" : (hasTimingHook() ? L"READY" : L"UNAVAILABLE"), g_speedEnabled.load() ? RGB(54, 215, 157) : (hasTimingHook() ? RGB(80, 151, 255) : RGB(224, 160, 87)), true);
    drawFeatureRow(dc, click, L"Auto clicker", L"Sends input only while the game is active and the menu is closed.", g_autoClicker.load() ? L"RUNNING" : L"STOPPED", g_autoClicker.load() ? RGB(54, 215, 157) : RGB(102, 121, 150), true);

    const int y2 = y + 106;
    const int half = (width - gap) / 2;
    drawCard(dc, {x, y2, half, 106}, L"Speed multiplier", L"Choose a timing scale preset");
    wchar_t speedValue[32]{}; swprintf_s(speedValue, _countof(speedValue), L"%.2fx", g_speedMultiplier.load());
    drawText(dc, x + 16, y2 + 48, 124, 39, speedValue, RGB(247, 250, 255), 25, true);
    drawButton(dc, {x + half - 103, y2 + 58, 36, 30}, L"−");
    drawButton(dc, {x + half - 58, y2 + 58, 36, 30}, L"+");

    drawCard(dc, {x + half + gap, y2, half, 106}, L"Auto clicker rate", L"Adjust between 4 and 40 CPS");
    wchar_t cpsValue[32]{}; swprintf_s(cpsValue, _countof(cpsValue), L"%d CPS", g_cps.load());
    drawText(dc, x + half + gap + 16, y2 + 48, 124, 39, cpsValue, RGB(247, 250, 255), 23, true);
    drawButton(dc, {x + half + gap + half - 103, y2 + 58, 36, 30}, L"−");
    drawButton(dc, {x + half + gap + half - 58, y2 + 58, 36, 30}, L"+");

    const int y3 = y + 224;
    drawCard(dc, {x, y3, width, 83}, L"Practice workspace", L"Input macro records use a speed-normalized timeline when recorded in GDM2 format; this is not an internal deterministic replay bot.");
    const wchar_t* macroState = g_macroRecording.load() ? L"RECORDING" : (g_macroReplaying.load() ? L"PLAYING" : L"IDLE");
    drawPill(dc, x + width - 124, y3 + 12, 106, macroState, g_macroRecording.load() ? RGB(245, 94, 123) : (g_macroReplaying.load() ? RGB(54, 215, 157) : RGB(98, 123, 160)));
    wchar_t events[64]{}; swprintf_s(events, _countof(events), L"%d recorded events", g_macroCount.load());
    drawText(dc, x + 15, y3 + 54, 210, 18, events, RGB(190, 205, 226), 10, true);

    const int y4 = y + 318;
    const int third = (width - 24) / 3;
    drawStatCard(dc, {x, y4, third, 90}, L"Client session", formatDuration(g_sessionStarted.load() ? GetTickCount64() - g_sessionStarted.load() : 0).c_str(), L"Since DLL initialization", accentColor());
    wchar_t shot[32]{}; swprintf_s(shot, _countof(shot), L"%llu", g_screenshotCount.load());
    drawStatCard(dc, {x + third + 12, y4, third, 90}, L"Screenshots", shot, L"Captured this session", RGB(76, 202, 177));
    wchar_t ev[32]{}; swprintf_s(ev, _countof(ev), L"%d", g_macroCount.load());
    drawStatCard(dc, {x + (third + 12) * 2, y4, third, 90}, L"Macro events", ev, L"Locally stored in AppData", RGB(157, 126, 255));
}

void drawPlayer(HDC dc, int x, int y, int width) {
    drawFeatureRow(dc, {x, y, width, 94}, L"Timing scale", L"Scales GJBaseGameLayer update delta time on validated builds; audio pitch is not yet synchronized.", g_speedEnabled.load() ? L"ENABLED" : (hasTimingHook() ? L"AVAILABLE" : L"UNAVAILABLE"), g_speedEnabled.load() ? RGB(54,215,157) : RGB(85,140,207), true);
    const int y2 = y + 106;
    drawCard(dc, {x, y2, width, 104}, L"Multiplier preset", L"Cycle presets using the buttons or F3. Current multiplier applies when the timing hook is enabled.");
    wchar_t multiplier[32]{}; swprintf_s(multiplier, _countof(multiplier), L"%.2fx", g_speedMultiplier.load());
    drawText(dc, x + 18, y2 + 48, 120, 38, multiplier, RGB(244,248,255), 24, true);
    drawButton(dc, {x + width - 110, y2 + 53, 38, 30}, L"−");
    drawButton(dc, {x + width - 62, y2 + 53, 38, 30}, L"+");

    const int y3 = y2 + 116;
    drawFeatureRow(dc, {x, y3, width, 84}, L"Input clicker", L"External input helper with configurable rate and optional hold modifier.", g_autoClicker.load() ? L"RUNNING" : L"STOPPED", g_autoClicker.load() ? RGB(54,215,157) : RGB(101,121,151), true);
    const int y4 = y3 + 96;
    drawCard(dc, {x, y4, width, 74}, L"Preset safety", L"Speed is reset to 1.0x when disabled. Stop the game if a timing feature causes desynchronization.");
    drawPill(dc, x + width - 127, y4 + 24, 110, hasTimingHook() ? L"HOOK FOUND" : L"NO HOOK", hasTimingHook() ? RGB(54,215,157) : RGB(224,160,87));
}

void drawPhysics(HDC dc, int x, int y, int width) {
    const bool supported = gdclient::native::gameHooksReady();
    drawFeatureRow(dc, {x, y, width, 72}, L"Native game integration",
        gdclient::native::buildName(), supported ? L"VERSION VERIFIED" : L"HOOKS DISABLED",
        supported ? RGB(54, 215, 157) : RGB(224, 160, 87), false);

    const int colGap = 12;
    const int col = (width - colGap) / 2;
    const int y2 = y + 84;
    drawFeatureRow(dc, {x, y2, col, 80}, L"Noclip",
        gdclient::native::noclipHookReady() ? L"Suppresses collision-death handling on supported builds." : L"Requires a validated native PlayLayer hook.",
        gdclient::native::noclipEnabled() ? L"ENABLED" : (gdclient::native::noclipHookReady() ? L"READY" : L"UNAVAILABLE"),
        gdclient::native::noclipEnabled() ? RGB(54,215,157) : (gdclient::native::noclipHookReady() ? RGB(85,140,207) : RGB(224,160,87)), true);
    drawFeatureRow(dc, {x + col + colGap, y2, col, 80}, L"Show hitboxes",
        gdclient::native::hitboxHookReady() ? L"Uses the game's debug hitbox draw decision." : L"Not available for this verified build.",
        gdclient::native::hitboxesEnabled() ? L"VISIBLE" : (gdclient::native::hitboxHookReady() ? L"READY" : L"UNAVAILABLE"),
        gdclient::native::hitboxesEnabled() ? RGB(54,215,157) : (gdclient::native::hitboxHookReady() ? RGB(85,140,207) : RGB(224,160,87)), true);

    const int y3 = y2 + 92;
    drawCard(dc, {x, y3, width, 142}, L"Noclip accuracy limits", L"Limits are enforced by the game-side collision hook. A reached limit lets the current collision kill the player.");
    drawText(dc, x + 16, y3 + 47, 174, 26, g_noclipDeathLimitOn.load() ? L"Death/contact limit: ON" : L"Death/contact limit: OFF", g_noclipDeathLimitOn.load() ? RGB(245, 191, 104) : RGB(177,190,210), 10, true);
    wchar_t deathVal[40]{}; swprintf_s(deathVal, _countof(deathVal), L"%d contacts", g_noclipDeathThreshold.load());
    drawText(dc, x + 200, y3 + 47, width - 330, 26, deathVal, RGB(238,244,255), 12, true, DT_CENTER);
    drawButton(dc, {x + width - 112, y3 + 45, 38, 28}, L"−");
    drawButton(dc, {x + width - 64, y3 + 45, 38, 28}, L"+");

    drawText(dc, x + 16, y3 + 86, 174, 26, g_noclipAccuracyLimitOn.load() ? L"Accuracy limit: ON" : L"Accuracy limit: OFF", g_noclipAccuracyLimitOn.load() ? RGB(245, 191, 104) : RGB(177,190,210), 10, true);
    wchar_t accVal[40]{}; swprintf_s(accVal, _countof(accVal), L"%.1f%% minimum", g_noclipMinAccuracy.load());
    drawText(dc, x + 200, y3 + 86, width - 330, 26, accVal, RGB(238,244,255), 12, true, DT_CENTER);
    drawButton(dc, {x + width - 112, y3 + 84, 38, 28}, L"−");
    drawButton(dc, {x + width - 64, y3 + 84, 38, 28}, L"+");

    const int y4 = y3 + 154;
    const auto stats = gdclient::native::noclipStats();
    const int statCol = (width - 24) / 3;
    wchar_t frames[48]{}, contacts[48]{}, accuracy[48]{};
    swprintf_s(frames, _countof(frames), L"%llu", static_cast<unsigned long long>(stats.frames));
    swprintf_s(contacts, _countof(contacts), L"%llu", static_cast<unsigned long long>(stats.contacts));
    swprintf_s(accuracy, _countof(accuracy), L"%.2f%%", stats.accuracy);
    drawStatCard(dc, {x, y4, statCol, 80}, L"Noclip frames", frames, L"While enabled", accentColor());
    drawStatCard(dc, {x + statCol + 12, y4, statCol, 80}, L"Contacts", contacts, L"Collision callbacks", RGB(255,166,80));
    drawStatCard(dc, {x + (statCol + 12) * 2, y4, statCol, 80}, L"Accuracy", accuracy, stats.limitTriggered ? L"Limit triggered" : L"This attempt", RGB(54,215,157));

    const int y5 = y4 + 92;
    const int selectedTps = g_physicsTpsPreference.load();
    const int activeTps = gdclient::native::physicsTps();
    const wchar_t* tpsName = selectedTps == 0 ? L"Game default" : (selectedTps == 240 ? L"240 TPS" : (selectedTps == 360 ? L"360 TPS" : (selectedTps == 480 ? L"480 TPS" : L"1000 TPS")));
    const wchar_t* practiceState = gdclient::native::practiceModeKnown() ?
        (gdclient::native::practiceModeActive() ? L"practice detected" : L"normal mode detected") : L"manual practice check only";
    drawCard(dc, {x, y5, width, 66}, L"Physics TPS override (EXPERIMENTAL)", nullptr);
    drawText(dc, x + 16, y5 + 35, 145, 23, tpsName, RGB(240,246,255), 12, true);
    wchar_t tpsDetails[112]{};
    swprintf_s(tpsDetails, _countof(tpsDetails), L"Active: %s • %s", activeTps == 0 ? L"default" : (activeTps == 240 ? L"240" : (activeTps == 360 ? L"360" : (activeTps == 480 ? L"480" : L"1000"))), practiceState);
    drawText(dc, x + 164, y5 + 37, width - 300, 18, tpsDetails, RGB(156,178,205), 9, false);
    drawButton(dc, {x + width - 108, y5 + 34, 40, 27}, L"−");
    drawButton(dc, {x + width - 59, y5 + 34, 40, 27}, L"+");

    const int y6 = y5 + 78;
    std::wstring stepDescription = hotkeyName(g_bindFrameStep.load()) + L" enables/pauses the update-delta gate; " + hotkeyName(g_bindFrameStepOnce.load()) + L" advances one game-layer update. Experimental: some systems may still run dt-independent work while paused.";
    drawCard(dc, {x, y6, width, 76}, L"Frame stepper", stepDescription.c_str());
    const wchar_t* stepState = !gdclient::native::frameStepEnabled() ? L"DISABLED" : (gdclient::native::frameStepPaused() ? L"PAUSED" : L"RUNNING");
    drawText(dc, x + 15, y6 + 42, 190, 24, stepState, gdclient::native::frameStepEnabled() ? RGB(115,228,189) : RGB(170,184,205), 11, true);
    drawButton(dc, {x + width - 220, y6 + 39, 96, 29}, gdclient::native::frameStepEnabled() ? L"Disable" : L"Enable", true);
    drawButton(dc, {x + width - 112, y6 + 39, 96, 29}, L"Step once", true);

    const int y7 = y6 + 86;
    drawCard(dc, {x, y7, width, 72}, L"Instant complete", L"Requests GD's native level-completion routine on the game thread. Local testing only; requires a supported executable.");
    drawButton(dc, {x + width - 170, y7 + 36, 148, 28}, L"Complete level", gdclient::native::instantCompleteReady());

    const int y8 = y7 + 80;
    const bool trailReady = gdclient::native::hitboxTrailHookReady();
    const bool trailEnabled = gdclient::native::hitboxTrailEnabled();
    drawFeatureRow(dc, {x, y8, width, 72}, L"Native hitbox trail (experimental)",
        L"Uses GD's own hitbox-trail record/draw callbacks. This is not the exact death-frame freeze or per-category color editor.",
        trailEnabled ? L"ENABLED" : (trailReady ? L"READY" : L"UNAVAILABLE"),
        trailEnabled ? RGB(54,215,157) : (trailReady ? RGB(85,140,207) : RGB(224,160,87)), true);
}

void drawPractice(HDC dc, int x, int y, int width) {
    const wchar_t* practiceStatus = !g_practiceGuard.load() ? L"SAFE / NOT ARMED" :
        (!gdclient::native::inLevel() ? L"ARMED / NOT IN LEVEL" :
        (gdclient::native::practiceModeKnown() ? (gdclient::native::practiceModeActive() ? L"PRACTICE VERIFIED" : L"NORMAL MODE") : L"MANUAL CONFIRM"));
    drawFeatureRow(dc, {x, y, width, 64}, L"Practice-mode guard", L"The macro workflow requires an active supported level and an armed guard; known builds also check GD's actual Practice Mode flag.", practiceStatus, canUsePracticeFeatures() ? RGB(54,215,157) : RGB(224,160,87), true);
    const int recordY = y + 76;
    drawCard(dc, {x, recordY, width, 112}, L"Input macro recorder", L"Records gameplay keys and clicks on a speed-normalized timeline. Arm the practice guard before recording.");
    drawButton(dc, {x + 16, recordY + 65, 142, 31}, g_macroRecording.load() ? L"Stop recording" : L"Start recording", !g_macroRecording.load());
    drawButton(dc, {x + 169, recordY + 65, 142, 31}, L"Save macro", false, !g_macroRecording.load() && g_macroCount.load() > 0);
    drawPill(dc, x + width - 134, recordY + 20, 112, g_macroRecording.load() ? L"RECORDING" : L"READY", g_macroRecording.load() ? RGB(245,94,123) : RGB(88,130,177));
    wchar_t eventText[72]{}; swprintf_s(eventText, _countof(eventText), L"%d events in memory", g_macroCount.load());
    drawText(dc, x + 328, recordY + 70, width - 470, 24, eventText, RGB(181,198,222), 11, true);

    const int y2 = recordY + 124;
    drawCard(dc, {x, y2, width, 104}, L"Macro playback", L"Playback time follows the active speed scale for newly recorded GDM2 macros. A changed speed changes playback cadence to preserve game-time spacing.");
    drawButton(dc, {x + 16, y2 + 61, 124, 30}, L"Play", true, !g_macroRecording.load() && g_macroCount.load() > 0 && !g_macroReplaying.load());
    drawButton(dc, {x + 150, y2 + 61, 124, 30}, L"Stop", false, g_macroReplaying.load());
    drawText(dc, x + 290, y2 + 62, 110, 28, g_macroLoop.load() ? L"Loop: ON" : L"Loop: OFF", g_macroLoop.load() ? RGB(83,227,165) : RGB(174,188,209), 11, true);
    drawToggle(dc, x + 400, y2 + 63, g_macroLoop.load());
    drawPill(dc, x + width - 132, y2 + 61, 110, g_macroReplaying.load() ? L"PLAYING" : L"STOPPED", g_macroReplaying.load() ? RGB(54,215,157) : RGB(99,117,146));

    const int y3 = y2 + 116;
    drawCard(dc, {x, y3, width, 82}, L"Quick screenshot", L"Captures the visible game client area as PNG under %APPDATA%\GDClient\screenshots (not a swap-chain framebuffer capture).");
    drawButton(dc, {x + width - 154, y3 + 35, 132, 31}, L"Capture", true);

    const int y4 = y3 + 94;
    const auto nativeMacro = gdclient::native::nativeMacroStats();
    drawCard(dc, {x, y4, width, 148}, L"Native engine-tick macro", L"Records button/reset events by simulation time and tick, with Player 1 position/rotation snapshots as analysis metadata. Playback replays inputs; it does not teleport or restore full game state.");
    const wchar_t* nativeState = nativeMacro.recording ? L"RECORDING" : (nativeMacro.playing ? L"PLAYING" : (nativeMacro.available ? L"READY" : L"UNAVAILABLE"));
    drawPill(dc, x + width - 132, y4 + 15, 110, nativeState, nativeMacro.recording ? RGB(245,94,123) : (nativeMacro.playing ? RGB(54,215,157) : RGB(94,126,164)));
    wchar_t nativeEventText[80]{}; swprintf_s(nativeEventText, _countof(nativeEventText), L"%llu game input/reset events", static_cast<unsigned long long>(nativeMacro.eventCount));
    drawText(dc, x + 16, y4 + 35, width - 180, 18, nativeEventText, RGB(174,194,220), 10, true);
    drawButton(dc, {x + 16, y4 + 58, 124, 30}, nativeMacro.recording ? L"Stop record" : L"Record", nativeMacro.available && !nativeMacro.playing);
    drawButton(dc, {x + 148, y4 + 58, 124, 30}, L"Save", nativeMacro.eventCount > 0 && !nativeMacro.recording);
    drawButton(dc, {x + 280, y4 + 58, 124, 30}, L"Load", nativeMacro.available && !nativeMacro.recording && !nativeMacro.playing);
    drawButton(dc, {x + 412, y4 + 58, 124, 30}, L"Play", nativeMacro.available && nativeMacro.eventCount > 0 && !nativeMacro.recording && !nativeMacro.playing);
    drawButton(dc, {x + 544, y4 + 58, 124, 30}, L"Stop", nativeMacro.playing);
    wchar_t rateLabel[64]{}; swprintf_s(rateLabel, _countof(rateLabel), L"Playback rate: %.2fx", nativeMacro.playbackRate);
    drawText(dc, x + 16, y4 + 94, 240, 25, rateLabel, RGB(203,218,238), 11, true);
    drawButton(dc, {x + width - 108, y4 + 91, 42, 28}, L"−", !nativeMacro.playing);
    drawButton(dc, {x + width - 58, y4 + 91, 42, 28}, L"+", !nativeMacro.playing);
    const int y5 = y4 + 136;
    std::wstring frameStepSummary = L"Use the Physics tab or " + hotkeyName(g_bindFrameStep.load()) + L"/" + hotkeyName(g_bindFrameStepOnce.load()) + L". It gates the supported GJBaseGameLayer delta-time update and remains experimental.";
    drawFeatureRow(dc, {x, y5, width, 68}, L"Frame stepper", frameStepSummary.c_str(), gdclient::native::frameStepEnabled() ? (gdclient::native::frameStepPaused() ? L"PAUSED" : L"RUNNING") : L"READY", gdclient::native::frameStepEnabled() ? RGB(54,215,157) : RGB(93,128,169), false);
}

void drawVisuals(HDC dc, int x, int y, int width) {
    drawFeatureRow(dc, {x, y, width, 74}, L"Status HUD", L"Compact status chip at the top-right; customizable level labels live on the Labels tab.", g_showHud.load() ? L"VISIBLE" : L"HIDDEN", g_showHud.load() ? RGB(54,215,157) : RGB(101,121,151), true);
    const int y2 = y + 86;
    drawCard(dc, {x, y2, width, 100}, L"Accent theme", L"Choose a highlight color for cards, toggles, active tabs, and notification borders.");
    const COLORREF colors[] = { RGB(67,153,255), RGB(163,103,255), RGB(37,214,180), RGB(255,140,80), RGB(255,91,151) };
    const wchar_t* names[] = { L"Azure", L"Violet", L"Mint", L"Ember", L"Rose" };
    for (int i = 0; i < 5; ++i) {
        const int bx = x + 18 + i * 86;
        rounded(dc, bx, y2 + 50, 72, 31, 8, colors[i], i == g_accentIndex.load() ? RGB(245,248,255) : RGB(43,56,77));
        drawText(dc, bx, y2 + 50, 72, 31, names[i], RGB(255,255,255), 10, true, DT_CENTER);
        if (i == g_accentIndex.load()) drawText(dc, bx + 52, y2 + 51, 18, 28, L"✓", RGB(255,255,255), 11, true, DT_CENTER);
    }
    const int y3 = y2 + 112;
    drawCard(dc, {x, y3, width, 80}, L"Overlay opacity", L"Adjust how strong the panel appears. Color-key transparent pixels remain transparent.");
    wchar_t pct[24]{}; swprintf_s(pct, _countof(pct), L"%d%%", g_overlayAlpha.load() * 100 / 255);
    drawText(dc, x + 18, y3 + 40, 90, 28, pct, RGB(244,248,255), 18, true);
    drawButton(dc, {x + width - 112, y3 + 39, 40, 29}, L"−");
    drawButton(dc, {x + width - 61, y3 + 39, 40, 29}, L"+");
    const int y4 = y3 + 92;
    drawFeatureRow(dc, {x, y4, width, 72}, L"Motion and transitions", L"Smooth menu and page animations. Disable to use instant transitions.", g_animations.load() ? L"ENABLED" : L"REDUCED", g_animations.load() ? RGB(54,215,157) : RGB(101,121,151), true);
    const int y5 = y4 + 82;
    drawFeatureRow(dc, {x, y5, width, 72}, L"Input overlay and CPS meter", L"Shows W/A/S/D, UP, SPACE, LMB and RMB states with hold duration, plus rolling one-second CPS and peak.", g_inputOverlayVisible.load() ? L"VISIBLE" : L"HIDDEN", g_inputOverlayVisible.load() ? RGB(54,215,157) : RGB(101,121,151), true);
}

void drawCreator(HDC dc, int x, int y, int width) {
    const int gap = 12, col = (width - gap) / 2, rowH = 72, step = 82;
    struct Item { const wchar_t* title; const wchar_t* desc; };
    const Item items[] = {
        {L"Object / editor bypass", L"Extended editor limits require editor-specific hooks."},
        {L"Free scroll", L"Editor camera override is not wired."},
        {L"Z-order controls", L"Requires live editor object access."},
        {L"Place Over / Copy Hack", L"Editor placement and copy behaviors are not hooked."},
        {L"Object ID / scale bypass", L"No validated patch for this executable build."},
        {L"Smooth editor trail", L"Requires the native editor rendering lifecycle."},
        {L"Custom object type", L"Needs object creation/serialization hooks."},
        {L"Editor color picker", L"Needs editor UI integration."},
        {L"Force visibility / block type", L"Requires object field access."},
        {L"Editor layer modifier", L"Requires object layer controls."},
        {L"Free resize / zoom", L"Window/editor viewport integration is not implemented."},
        {L"Trigger inspector", L"Needs trigger-object introspection."},
    };
    for (int i = 0; i < static_cast<int>(_countof(items)); ++i) {
        const int cx = x + (i % 2) * (col + gap);
        const int cy = y + (i / 2) * step;
        drawFeatureRow(dc, {cx, cy, col, rowH}, items[i].title, items[i].desc, L"NOT WIRED", RGB(224,160,87));
    }
    drawCard(dc, {x, y + 6 * step + 2, width, 76}, L"Implementation policy", L"These controls remain unavailable until a build-specific editor hook is implemented and tested. They are not decorative toggles pretending to work.");
    drawPill(dc, x + width - 139, y + 6 * step + 30, 116, L"HONEST STATUS", RGB(87,131,187));
}

void drawBypass(HDC dc, int x, int y, int width) {
    wchar_t summary[128]{};
    swprintf_s(summary, _countof(summary), L"%llu entries  /  17 categories  /  status reflects actual wiring", static_cast<unsigned long long>(gdclient::kFeatureCatalogSize));
    drawCard(dc, {x, y, width, 50}, L"Feature catalogue", summary);

    struct CategoryBlock {
        const wchar_t* name;
        std::size_t first;
        std::size_t count;
        int column;
    };
    std::vector<CategoryBlock> categories;
    for (std::size_t i = 0; i < gdclient::kFeatureCatalogSize; ++i) {
        const auto& feature = gdclient::kFeatureCatalog[i];
        if (categories.empty() || std::wstring(categories.back().name) != feature.category) {
            categories.push_back({feature.category, i, 0, 0});
        }
        ++categories.back().count;
    }

    constexpr int columnGap = 9;
    constexpr int categoryHeight = 27;
    constexpr int rowHeight = 24;
    const int columnWidth = (width - columnGap * 2) / 3;
    std::array<int, 3> columnHeights{};
    for (auto& category : categories) {
        category.column = static_cast<int>(std::min_element(columnHeights.begin(), columnHeights.end()) - columnHeights.begin());
        columnHeights[category.column] += categoryHeight + static_cast<int>(category.count) * rowHeight + 5;
    }

    std::array<int, 3> cursor{};
    for (std::size_t column = 0; column < cursor.size(); ++column) cursor[column] = y + 60;
    for (const auto& category : categories) {
        const int columnX = x + category.column * (columnWidth + columnGap);
        int rowY = cursor[category.column];
        rounded(dc, columnX, rowY, columnWidth, categoryHeight - 3, 5, RGB(66, 39, 87), RGB(100, 62, 126));
        drawText(dc, columnX + 8, rowY, columnWidth - 16, categoryHeight - 3, category.name, RGB(244, 229, 255), 10, true);
        rowY += categoryHeight;

        for (std::size_t i = category.first; i < category.first + category.count; ++i) {
            const auto& feature = gdclient::kFeatureCatalog[i];
            const wchar_t* status = L"NOT WIRED";
            COLORREF stateColor = RGB(224, 160, 87);
            if (feature.status == gdclient::FeatureStatus::Implemented) {
                status = L"IMPLEMENTED";
                stateColor = RGB(54, 215, 157);
            } else if (feature.status == gdclient::FeatureStatus::Experimental) {
                status = L"EXPERIMENTAL";
                stateColor = RGB(177, 126, 255);
            }
            const COLORREF rowFill = (i % 2 == 0) ? RGB(34, 25, 46) : RGB(29, 22, 40);
            rounded(dc, columnX, rowY, columnWidth, rowHeight - 2, 3, rowFill, RGB(48, 36, 61));
            drawText(dc, columnX + 7, rowY, columnWidth - 93, rowHeight - 2, feature.name, RGB(226, 218, 237), 9, false);
            drawPill(dc, columnX + columnWidth - 84, rowY + 2, 78, status, stateColor, RGB(244, 239, 249));
            rowY += rowHeight;
        }
        cursor[category.column] = rowY + 5;
    }
}

void drawUtilities(HDC dc, int x, int y, int width) {
    const int gap = 12, col = (width - gap) / 2;
    drawCard(dc, {x, y, col, 112}, L"Capture game screenshot", L"Saves the visible game client area as a PNG to your GDClient screenshots folder; not a swap-chain framebuffer grab.");
    drawButton(dc, {x + 16, y + 65, 145, 31}, L"Capture now", true);
    wchar_t count[48]{}; swprintf_s(count, _countof(count), L"%llu saved", g_screenshotCount.load());
    drawText(dc, x + 175, y + 69, col - 188, 25, count, RGB(187,205,228), 10, true);

    drawCard(dc, {x + col + gap, y, col, 112}, L"Input macro", L"Record and replay input events with timing. Saved locally as last_macro.gdm.");
    drawButton(dc, {x + col + gap + 16, y + 65, 128, 31}, L"Open AppData", false);
    drawPill(dc, x + width - 124, y + 68, 102, g_macroRecording.load() ? L"RECORDING" : (g_macroReplaying.load() ? L"PLAYING" : L"IDLE"), g_macroRecording.load() ? RGB(245,94,123) : RGB(88,130,177));

    const int y2 = y + 124;
    drawCard(dc, {x, y2, width, 100}, L"Session", L"Elapsed time since the native DLL initialized; it is not the game's in-level timer.");
    const auto started = g_sessionStarted.load();
    const std::wstring duration = formatDuration(started ? GetTickCount64() - started : 0);
    drawText(dc, x + 17, y2 + 48, 230, 34, duration.c_str(), RGB(245,248,255), 23, true);
    drawText(dc, x + 270, y2 + 49, width - 290, 30, L"Local tools • no account data read or transmitted", RGB(138,157,183), 11);

    const int y3 = y2 + 112;
    drawFeatureRow(dc, {x, y3, width, 76}, L"Live input monitor", L"The macro recorder watches common gameplay keys and mouse buttons only while the game has focus.", L"LOCAL ONLY", RGB(76,202,177));
    const int y4 = y3 + 88;
    drawFeatureRow(dc, {x, y4, width, 76}, L"Game-specific hacks", L"See Feature Lab for items that require validated Geometry Dash internals.", L"VERSION-SPECIFIC", RGB(224,160,87));
    const int y5 = y4 + 88;
    const bool unlockReady = gdclient::native::unlockIconsHookReady();
    drawFeatureRow(dc, {x, y5, width, 76}, L"Unlock All Icons (local preview)", L"Overrides supported local icon/color unlock checks only. It does not unlock items on an account or server.",
        !unlockReady ? L"UNAVAILABLE" : (gdclient::native::unlockIconsEnabled() ? L"ENABLED" : L"DISABLED"),
        !unlockReady ? RGB(224,160,87) : (gdclient::native::unlockIconsEnabled() ? RGB(54,215,157) : RGB(101,121,151)), true);
}

void drawSettings(HDC dc, int x, int y, int width) {
    drawFeatureRow(dc, {x, y, width, 74}, L"Menu animations", L"Disable transitions if you prefer immediate page and panel changes.", g_animations.load() ? L"ENABLED" : L"DISABLED", g_animations.load() ? RGB(54,215,157) : RGB(101,121,151), true);
    const int y2 = y + 86;
    drawFeatureRow(dc, {x, y2, width, 76}, L"Auto clicker hold modifier", L"When enabled, hold Left Shift to allow the auto clicker to send clicks.", g_clickerHoldToRun.load() ? L"SHIFT REQUIRED" : L"ALWAYS READY", g_clickerHoldToRun.load() ? RGB(85,140,207) : RGB(101,121,151), true);
    const int y3 = y2 + 88;
    drawCard(dc, {x, y3, width, 80}, L"Overlay opacity", L"Adjust the opacity of the client overlay surfaces.");
    wchar_t pct[24]{}; swprintf_s(pct, _countof(pct), L"%d%%", g_overlayAlpha.load() * 100 / 255);
    drawText(dc, x + 18, y3 + 39, 90, 28, pct, RGB(244,248,255), 18, true);
    drawButton(dc, {x + width - 112, y3 + 37, 40, 29}, L"−");
    drawButton(dc, {x + width - 61, y3 + 37, 40, 29}, L"+");
    const int y4 = y3 + 92;
    drawFeatureRow(dc, {x, y4, width, 74}, L"Practice-only Safe Mode",
        L"Blocks native modifiers, macros, and auto-clicker outside the confirmed Practice Guard. Older builds require manual confirmation. This is not an online-request filter or a leaderboard guarantee.",
        g_safeMode.load() ? L"ON / PRACTICE ONLY" : L"OFF / UNGUARDED",
        g_safeMode.load() ? RGB(54,215,157) : RGB(245,166,87), true);
    const int y5 = y4 + 86;
    drawButton(dc, {x, y5, 156, 34}, L"Save settings", true);
    drawButton(dc, {x + 166, y5, 156, 34}, L"Reset defaults");
    drawText(dc, x + 337, y5, width - 340, 34, L"Stored locally in %APPDATA%\\GDClient", RGB(135,152,177), 10);
    const int y6 = y5 + 44;
    drawCard(dc, {x, y6, width, 70}, L"Game-thread priority", L"Applied from the verified game update hook; restored when the DLL shuts down.");
    const int selectedPriority = gdclient::native::gameThreadPriorityLevel();
    drawButton(dc, {x + width - 294, y6 + 37, 86, 27}, L"Normal", selectedPriority == 0);
    drawButton(dc, {x + width - 198, y6 + 37, 86, 27}, L"Above", selectedPriority == 1);
    drawButton(dc, {x + width - 102, y6 + 37, 86, 27}, L"High", selectedPriority == 2);

    const int y7 = y6 + 82;
    drawCard(dc, {x, y7, width, 260}, L"Keyboard shortcut manager", L"Click Bind, then press a key while GD is focused. Escape cancels. Duplicate bindings are rejected.");
    const int colWidth = (width - 28) / 2;
    for (int slot = BIND_MENU; slot <= BIND_FRAME_STEP_ONCE; ++slot) {
        const int index = slot - BIND_MENU;
        const int colIndex = index / 6;
        const int row = index % 6;
        const int colX = x + 10 + colIndex * (colWidth + 8);
        const int rowY = y7 + 35 + row * 35;
        drawText(dc, colX + 4, rowY + 1, 106, 28, hotkeySlotName(slot), RGB(195,210,233), 9, true);
        const std::wstring name = hotkeyName(hotkeyRef(slot).load());
        drawPill(dc, colX + 112, rowY + 3, 67, name.c_str(), accentColor());
        drawButton(dc, {colX + colWidth - 83, rowY, 78, 29}, g_captureBindFor.load() == slot ? L"Press key" : L"Bind", g_captureBindFor.load() == slot);
    }
}

void drawAbout(HDC dc, int x, int y, int width) {
    drawCard(dc, {x, y, width, 92}, L"GD Client", L"Custom standalone native Windows overlay • no Geode SDK dependency.");
    drawText(dc, x + 17, y + 53, width - 34, 22, L"Build: C++17 / MSVC x64    UI: Win32 + GDI    Menu: configurable", RGB(203,216,234), 12, true);
    const int y2 = y + 104;
    drawCard(dc, {x, y2, width, 94}, L"Runtime status", gdclient::native::gameHooksReady() ? gdclient::native::buildName() : L"No supported game fingerprint matched; native gameplay hooks remain disabled.");
    drawText(dc, x + 17, y2 + 55, width - 34, 20, g_gameWindow && IsWindow(g_gameWindow) ? L"Game window is bound to this process." : L"Game window is unavailable.", g_gameWindow && IsWindow(g_gameWindow) ? RGB(83,227,165) : RGB(255,144,121), 12, true);
    const int y3 = y2 + 106;
    drawCard(dc, {x, y3, width, 115}, L"Native integration", L"Version-guarded hooks cover game delta time, PlayLayer lifecycle, collision handling, and (on the 2.2091 hotfix) hitbox/practice-mode state.");
    drawText(dc, x + 17, y3 + 59, width - 34, 37, L"Password/copy-lock/verification/account-access bypasses are intentionally not implemented. Other editor, replay, FPS/TPS, audio, and cosmetic features still need their own verified hooks.", RGB(224,178,115), 10, false);
}

// --- Mandatory assist-disclosure banner -------------------------------------
// This is intentionally NOT wired to any setting, hotkey, Safe Mode state, or
// settings.ini key. If any gameplay-affecting hook below is active, the player
// (and anyone spectating or recording) can see that the run is assisted.
// Do not add a toggle, keybind, opacity setting, or "stream-safe" mode that
// hides this banner — that would defeat the point of having it. If you are
// adding a new gameplay-affecting hook, add it to activeAssistSummary() too.
std::wstring activeAssistSummary(bool& anyActive) {
    std::vector<std::wstring> parts;

    const double speed = (g_speedEnabled.load() && hasTimingHook()) ? g_speedMultiplier.load() : 1.0;
    if (std::abs(speed - 1.0) > 0.001) {
        wchar_t buf[32]{};
        swprintf_s(buf, _countof(buf), L"SPEED %.2fx", speed);
        parts.emplace_back(buf);
    }
    if (gdclient::native::noclipEnabled()) parts.emplace_back(L"NOCLIP");
    if (gdclient::native::frameStepEnabled()) parts.emplace_back(L"FRAME-STEP");
    if (gdclient::native::physicsTps() != 0) {
        wchar_t buf[32]{};
        swprintf_s(buf, _countof(buf), L"TPS %d", gdclient::native::physicsTps());
        parts.emplace_back(buf);
    }
    if (gdclient::native::nativeMacroStats().playing) parts.emplace_back(L"NATIVE MACRO PLAYBACK");
    if (g_macroReplaying.load()) parts.emplace_back(L"INPUT MACRO PLAYBACK");
    if (g_autoClicker.load()) parts.emplace_back(L"AUTO CLICKER");
    if (gdclient::native::hitboxesEnabled()) parts.emplace_back(L"DEBUG HITBOXES");
    if (gdclient::native::hitboxTrailEnabled()) parts.emplace_back(L"HITBOX TRAIL");

    anyActive = !parts.empty();
    std::wstring joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) joined += L"   •   ";
        joined += parts[i];
    }
    return joined;
}

// Always evaluated and drawn on every frame, independent of g_showHud,
// g_menuOpen, g_safeMode, g_levelLabelsVisible, or overlay opacity. There is
// no code path that can suppress this while an assist is active.
void drawAssistDisclosureBanner(HDC dc, int clientWidth) {
    bool anyActive = false;
    const std::wstring summary = activeAssistSummary(anyActive);
    if (!anyActive) return;

    const int bannerW = std::clamp(clientWidth - 24, 260, 680);
    const int bannerX = (clientWidth - bannerW) / 2;
    constexpr int bannerY = 8;
    constexpr int bannerH = 30;

    rounded(dc, bannerX, bannerY, bannerW, bannerH, 8, RGB(132, 20, 20), RGB(255, 96, 96));
    const std::wstring text = L"ASSIST ACTIVE — " + summary;
    drawText(dc, bannerX + 10, bannerY, bannerW - 20, bannerH, text.c_str(), RGB(255, 236, 236), 12, true, DT_CENTER);
}

void renderOverlay(HDC dc, int clientWidth, int clientHeight) {
    RECT client{0, 0, clientWidth, clientHeight};
    solidRect(dc, 0, 0, client.right, client.bottom, KEY_COLOR);

    // Drawn first and unconditionally so nothing later in this function — no
    // HUD toggle, no menu-closed state, no page switch — can cover or skip it.
    drawAssistDisclosureBanner(dc, clientWidth);

    const COLORREF accent = accentColor();
    if (g_showHud.load()) {
        const int hx = std::max(8, static_cast<int>(client.right) - 243);
        rounded(dc, hx, 10, 231, 32, 9, RGB(11, 16, 28), RGB(40, 61, 87));
        rounded(dc, hx + 10, 20, 6, 12, 3, g_speedEnabled.load() ? RGB(65, 226, 161) : accent, g_speedEnabled.load() ? RGB(65, 226, 161) : accent);
        const bool gameplayMod = g_speedEnabled.load() || gdclient::native::noclipEnabled() || gdclient::native::hitboxesEnabled() || gdclient::native::frameStepEnabled();
        drawText(dc, hx + 24, 10, 197, 31, gameplayMod ? L"GD CLIENT  /  MOD ACTIVE" : L"GD CLIENT  /  READY", RGB(209, 224, 243), 11, true);
    }
    drawLevelLabels(dc, clientWidth, clientHeight);
    drawInputOverlay(dc, clientWidth, clientHeight);

    const float progress = g_menuProgress;
    if (progress > 0.015f) {
        const int slide = g_animations.load() ? static_cast<int>((1.0f - progress) * 20.0f) : 0;
        const int vslide = g_animations.load() ? static_cast<int>((1.0f - progress) * 12.0f) : 0;
        const int px = g_menuX - slide;
        const int py = g_menuY + vslide;
        const int w = std::min(MENU_W, std::max(500, static_cast<int>(client.right) - px - 8));
        const int h = std::min(MENU_H, std::max(390, static_cast<int>(client.bottom) - py - 8));
        g_drawnMenuW = w;
        g_drawnMenuH = h;
        rounded(dc, px + 8, py + 9, w, h, 18, RGB(8, 5, 12), RGB(8, 5, 12));
        rounded(dc, px, py, w, h, 18, RGB(21, 17, 28), RGB(101, 76, 122));

        const int headerH = 60;
        rounded(dc, px + 1, py + 1, w - 2, headerH, 16, RGB(48, 31, 61), RGB(48, 31, 61));
        drawGradient(dc, {px + 2, py + headerH - 3, w - 4, 3}, blend(accent, RGB(82, 43, 107), 0.12f), blend(accent, RGB(37, 25, 50), 0.74f));
        rounded(dc, px + 16, py + 13, 34, 34, 12, accent, accent);
        drawText(dc, px + 16, py + 13, 34, 34, L"G", RGB(255,255,255), 19, true, DT_CENTER);
        drawText(dc, px + 60, py + 8, 260, 25, L"GD CLIENT", RGB(245, 248, 255), 17, true);
        drawText(dc, px + 61, py + 33, 340, 16, L"NATIVE GAMEPLAY TOOLKIT  /  BUILD 0.4 PRACTICE + LABELS", RGB(137, 158, 186), 9, true);
        drawPill(dc, px + w - 178, py + 19, 82, L"IN-GAME", RGB(62,116,181));
        drawPill(dc, px + w - 90, py + 19, 62, L"LIVE", RGB(54,153,126));
        rounded(dc, px + w - 31, py + 15, 21, 22, 9, RGB(44, 35, 49), RGB(91, 59, 72));
        drawText(dc, px + w - 31, py + 14, 21, 22, L"×", RGB(255, 176, 190), 16, true, DT_CENTER);

        const int page = g_page.load();
        drawNavigation(dc, px, py, w, page);
        drawText(dc, px + 19, py + 105, w - 38, 26, pageTitle(page), RGB(239, 246, 255), 21, true);
        drawText(dc, px + 20, py + 132, w - 40, 17, pageSubtitle(page), RGB(134, 153, 179), 9, false);
        const int slideContent = g_animations.load() ? static_cast<int>((1.0f - g_pageProgress) * 9.0f) : 0;
        const int contentX = px + 18 + slideContent;
        const int contentW = w - 36;
        const int contentY = py + 158 - g_pageScroll[page];
        const int clipSaved = SaveDC(dc);
        IntersectClipRect(dc, px + 10, py + 151, px + w - 10, py + h - 28);
        switch (page) {
            case PAGE_DASHBOARD: drawDashboard(dc, contentX, contentY, contentW); break;
            case PAGE_PLAYER: drawPlayer(dc, contentX, contentY, contentW); break;
            case PAGE_PHYSICS: drawPhysics(dc, contentX, contentY, contentW); break;
            case PAGE_PRACTICE: drawPractice(dc, contentX, contentY, contentW); break;
            case PAGE_LABELS: drawLabels(dc, contentX, contentY, contentW); break;
            case PAGE_VISUALS: drawVisuals(dc, contentX, contentY, contentW); break;
            case PAGE_CREATOR: drawCreator(dc, contentX, contentY, contentW); break;
            case PAGE_BYPASS: drawBypass(dc, contentX, contentY, contentW); break;
            case PAGE_UTILITIES: drawUtilities(dc, contentX, contentY, contentW); break;
            case PAGE_SETTINGS: drawSettings(dc, contentX, contentY, contentW); break;
            default: drawAbout(dc, contentX, contentY, contentW); break;
        }
        RestoreDC(dc, clipSaved);
        std::wstring hotkeyFooter = L"MENU " + hotkeyName(g_bindMenu.load()) + L"  SPEED " + hotkeyName(g_bindSpeed.load()) +
            L"  CLICKER " + hotkeyName(g_bindClicker.load()) + L"  SCREENSHOT " + hotkeyName(g_bindScreenshot.load()) +
            L"  MACRO " + hotkeyName(g_bindMacroRecord.load()) + L"/" + hotkeyName(g_bindMacroPlay.load());
        drawText(dc, contentX, py + h - 18, contentW - 8, 13, hotkeyFooter.c_str(), RGB(92, 112, 139), 8, false);

        if (!g_toast.empty() && GetTickCount64() - g_toastStarted < 2200) {
            const int toastW = std::min(350, w - 30);
            const int toastX = px + w - toastW - 14;
            const int toastY = py + h - 68;
            rounded(dc, toastX, toastY, toastW, 34, 12, RGB(20, 34, 49), blend(accent, RGB(20, 34, 49), 0.3f));
            drawText(dc, toastX + 12, toastY, toastW - 24, 34, g_toast.c_str(), RGB(225, 239, 252), 11, true);
        }
    }
}

void destroyBackBuffer() {
    if (g_backBufferDC) {
        if (g_backBufferOriginalBitmap) SelectObject(g_backBufferDC, g_backBufferOriginalBitmap);
        if (g_backBufferBitmap) DeleteObject(g_backBufferBitmap);
        DeleteDC(g_backBufferDC);
    }
    g_backBufferDC = nullptr;
    g_backBufferBitmap = nullptr;
    g_backBufferOriginalBitmap = nullptr;
    g_backBufferWidth = 0;
    g_backBufferHeight = 0;
}

bool ensureBackBuffer(HDC targetDC, int width, int height) {
    if (g_backBufferDC && g_backBufferBitmap && width == g_backBufferWidth && height == g_backBufferHeight)
        return true;

    destroyBackBuffer();
    g_backBufferDC = CreateCompatibleDC(targetDC);
    if (!g_backBufferDC) return false;
    g_backBufferBitmap = CreateCompatibleBitmap(targetDC, width, height);
    if (!g_backBufferBitmap) {
        destroyBackBuffer();
        return false;
    }
    g_backBufferOriginalBitmap = SelectObject(g_backBufferDC, g_backBufferBitmap);
    if (!g_backBufferOriginalBitmap || g_backBufferOriginalBitmap == HGDI_ERROR) {
        g_backBufferOriginalBitmap = nullptr;
        destroyBackBuffer();
        return false;
    }
    g_backBufferWidth = width;
    g_backBufferHeight = height;
    return true;
}

void paintOverlay(HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC targetDC = BeginPaint(hwnd, &ps);
    if (!targetDC) {
        EndPaint(hwnd, &ps);
        return;
    }
    RECT client{};
    if (GetClientRect(hwnd, &client)) {
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width > 0 && height > 0) {
            if (ensureBackBuffer(targetDC, width, height)) {
                renderOverlay(g_backBufferDC, width, height);
                BitBlt(targetDC, 0, 0, width, height, g_backBufferDC, 0, 0, SRCCOPY);
            } else {
                // Allocation failure should degrade gracefully rather than blank the overlay.
                renderOverlay(targetDC, width, height);
            }
        }
    }
    EndPaint(hwnd, &ps);
}

void saveSettings() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, _countof(appData));
    if (len == 0 || len >= _countof(appData)) return;
    std::wstring folder = std::wstring(appData) + L"\\GDClient";
    CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring file = folder + L"\\settings.ini";
    wchar_t value[64]{};
    swprintf_s(value, _countof(value), L"%d", g_showHud.load() ? 1 : 0); WritePrivateProfileStringW(L"UI", L"ShowHUD", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_animations.load() ? 1 : 0); WritePrivateProfileStringW(L"UI", L"Animations", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_accentIndex.load()); WritePrivateProfileStringW(L"UI", L"Accent", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_overlayAlpha.load()); WritePrivateProfileStringW(L"UI", L"Opacity", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_inputOverlayVisible.load() ? 1 : 0); WritePrivateProfileStringW(L"UI", L"InputOverlay", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", gdclient::native::gameThreadPriorityLevel()); WritePrivateProfileStringW(L"System", L"GameThreadPriority", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_unlockIconsPreference.load() ? 1 : 0); WritePrivateProfileStringW(L"Gameplay", L"UnlockIconsPreview", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_cps.load()); WritePrivateProfileStringW(L"Assist", L"CPS", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_clickerHoldToRun.load() ? 1 : 0); WritePrivateProfileStringW(L"Assist", L"HoldToRun", value, file.c_str());
    const struct { const wchar_t* name; int slot; } keybinds[] = {
        {L"Menu", BIND_MENU}, {L"Speed", BIND_SPEED}, {L"SpeedCycle", BIND_SPEED_CYCLE},
        {L"Clicker", BIND_CLICKER}, {L"HUD", BIND_HUD}, {L"Screenshot", BIND_SCREENSHOT},
        {L"MacroRecord", BIND_MACRO_RECORD}, {L"MacroPlay", BIND_MACRO_PLAY},
        {L"MacroStop", BIND_MACRO_STOP}, {L"FrameStep", BIND_FRAME_STEP}, {L"FrameStepOnce", BIND_FRAME_STEP_ONCE}
    };
    for (const auto& binding : keybinds) {
        swprintf_s(value, _countof(value), L"%d", hotkeyRef(binding.slot).load());
        WritePrivateProfileStringW(L"Keybinds", binding.name, value, file.c_str());
    }
    swprintf_s(value, _countof(value), L"%.2f", g_speedMultiplier.load()); WritePrivateProfileStringW(L"Player", L"SpeedMultiplier", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_noclipDeathLimitOn.load() ? 1 : 0); WritePrivateProfileStringW(L"Physics", L"DeathLimitEnabled", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_noclipDeathThreshold.load()); WritePrivateProfileStringW(L"Physics", L"DeathLimit", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_noclipAccuracyLimitOn.load() ? 1 : 0); WritePrivateProfileStringW(L"Physics", L"AccuracyLimitEnabled", value, file.c_str());
    swprintf_s(value, _countof(value), L"%.2f", g_noclipMinAccuracy.load()); WritePrivateProfileStringW(L"Physics", L"MinimumAccuracy", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_physicsTpsPreference.load()); WritePrivateProfileStringW(L"Physics", L"TPSPreference", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_safeMode.load() ? 1 : 0); WritePrivateProfileStringW(L"Security", L"PracticeOnlySafeMode", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_macroLoop.load() ? 1 : 0); WritePrivateProfileStringW(L"Practice", L"MacroLoop", value, file.c_str());
    swprintf_s(value, _countof(value), L"%.2f", gdclient::native::nativeMacroPlaybackRate()); WritePrivateProfileStringW(L"Practice", L"NativeMacroRate", value, file.c_str());
    swprintf_s(value, _countof(value), L"%d", g_levelLabelsVisible.load() ? 1 : 0); WritePrivateProfileStringW(L"Labels", L"Visible", value, file.c_str());
    for (int i = 0; i < 3; ++i) {
        wchar_t key[32]{}; swprintf_s(key, _countof(key), L"Label%dText", i + 1); WritePrivateProfileStringW(L"Labels", key, g_levelLabels[i].text.c_str(), file.c_str());
        swprintf_s(key, _countof(key), L"Label%dEnabled", i + 1); swprintf_s(value, _countof(value), L"%d", g_levelLabels[i].enabled ? 1 : 0); WritePrivateProfileStringW(L"Labels", key, value, file.c_str());
        swprintf_s(key, _countof(key), L"Label%dPosition", i + 1); swprintf_s(value, _countof(value), L"%d", g_levelLabels[i].position); WritePrivateProfileStringW(L"Labels", key, value, file.c_str());
        swprintf_s(key, _countof(key), L"Label%dSize", i + 1); swprintf_s(value, _countof(value), L"%d", g_levelLabels[i].fontSize); WritePrivateProfileStringW(L"Labels", key, value, file.c_str());
        swprintf_s(key, _countof(key), L"Label%dColor", i + 1); swprintf_s(value, _countof(value), L"%d", g_levelLabels[i].colorIndex); WritePrivateProfileStringW(L"Labels", key, value, file.c_str());
    }
}

void loadSettings() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, _countof(appData));
    if (len == 0 || len >= _countof(appData)) return;
    const std::wstring file = std::wstring(appData) + L"\\GDClient\\settings.ini";
    g_showHud.store(GetPrivateProfileIntW(L"UI", L"ShowHUD", 1, file.c_str()) != 0);
    g_animations.store(GetPrivateProfileIntW(L"UI", L"Animations", 1, file.c_str()) != 0);
    g_accentIndex.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI", L"Accent", 1, file.c_str())), 0, 4));
    g_overlayAlpha.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"UI", L"Opacity", 245, file.c_str())), 120, 255));
    g_inputOverlayVisible.store(GetPrivateProfileIntW(L"UI", L"InputOverlay", 1, file.c_str()) != 0);
    gdclient::native::setGameThreadPriorityLevel(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"System", L"GameThreadPriority", 0, file.c_str())), 0, 2));
    g_unlockIconsPreference.store(GetPrivateProfileIntW(L"Gameplay", L"UnlockIconsPreview", 0, file.c_str()) != 0);
    g_cps.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Assist", L"CPS", 12, file.c_str())), 4, 40));
    g_clickerHoldToRun.store(GetPrivateProfileIntW(L"Assist", L"HoldToRun", 0, file.c_str()) != 0);
    g_macroLoop.store(GetPrivateProfileIntW(L"Practice", L"MacroLoop", 0, file.c_str()) != 0);
    wchar_t macroRateText[32]{}; GetPrivateProfileStringW(L"Practice", L"NativeMacroRate", L"1.00", macroRateText, _countof(macroRateText), file.c_str());
    gdclient::native::setNativeMacroPlaybackRate(std::clamp(_wtof(macroRateText), 0.25, 4.0));
    g_noclipDeathLimitOn.store(GetPrivateProfileIntW(L"Physics", L"DeathLimitEnabled", 0, file.c_str()) != 0);
    g_noclipDeathThreshold.store(std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Physics", L"DeathLimit", 10, file.c_str())), 1, 100000));
    g_noclipAccuracyLimitOn.store(GetPrivateProfileIntW(L"Physics", L"AccuracyLimitEnabled", 0, file.c_str()) != 0);
    wchar_t storedAccuracy[32]{}; GetPrivateProfileStringW(L"Physics", L"MinimumAccuracy", L"75.00", storedAccuracy, _countof(storedAccuracy), file.c_str());
    g_noclipMinAccuracy.store(std::clamp(_wtof(storedAccuracy), 0.0, 100.0));
    const int storedTps = static_cast<int>(GetPrivateProfileIntW(L"Physics", L"TPSPreference", 0, file.c_str()));
    const int validTps = (storedTps == 240 || storedTps == 360 || storedTps == 480 || storedTps == 1000) ? storedTps : 0;
    g_physicsTpsPreference.store(validTps);
    g_safeMode.store(GetPrivateProfileIntW(L"Security", L"PracticeOnlySafeMode", 1, file.c_str()) != 0);
    // Never restore the practice guard from disk. The user must manually arm it for each session.
    g_practiceGuard.store(false);
    g_levelLabelsVisible.store(GetPrivateProfileIntW(L"Labels", L"Visible", 1, file.c_str()) != 0);
    for (int i = 0; i < 3; ++i) {
        wchar_t key[32]{}, stored[256]{};
        swprintf_s(key, _countof(key), L"Label%dText", i + 1);
        GetPrivateProfileStringW(L"Labels", key, g_levelLabels[i].text.c_str(), stored, _countof(stored), file.c_str());
        g_levelLabels[i].text = stored;
        swprintf_s(key, _countof(key), L"Label%dEnabled", i + 1); g_levelLabels[i].enabled = GetPrivateProfileIntW(L"Labels", key, g_levelLabels[i].enabled ? 1 : 0, file.c_str()) != 0;
        swprintf_s(key, _countof(key), L"Label%dPosition", i + 1); g_levelLabels[i].position = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Labels", key, g_levelLabels[i].position, file.c_str())), 0, 5);
        swprintf_s(key, _countof(key), L"Label%dSize", i + 1); g_levelLabels[i].fontSize = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Labels", key, g_levelLabels[i].fontSize, file.c_str())), 9, 24);
        swprintf_s(key, _countof(key), L"Label%dColor", i + 1); g_levelLabels[i].colorIndex = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Labels", key, g_levelLabels[i].colorIndex, file.c_str())), 0, 4);
    }
    const struct { const wchar_t* name; int slot; int fallback; } keybinds[] = {
        {L"Menu", BIND_MENU, VK_TAB}, {L"Speed", BIND_SPEED, VK_F2}, {L"SpeedCycle", BIND_SPEED_CYCLE, VK_F3},
        {L"Clicker", BIND_CLICKER, VK_F4}, {L"HUD", BIND_HUD, VK_F5}, {L"Screenshot", BIND_SCREENSHOT, VK_F6},
        {L"MacroRecord", BIND_MACRO_RECORD, VK_F7}, {L"MacroPlay", BIND_MACRO_PLAY, VK_F8},
        {L"MacroStop", BIND_MACRO_STOP, VK_F9}, {L"FrameStep", BIND_FRAME_STEP, VK_F10}, {L"FrameStepOnce", BIND_FRAME_STEP_ONCE, VK_F11}
    };
    for (const auto& binding : keybinds) {
        const int loaded = static_cast<int>(GetPrivateProfileIntW(L"Keybinds", binding.name, binding.fallback, file.c_str()));
        hotkeyRef(binding.slot).store(loaded);
    }
    normaliseHotkeys();
    wchar_t speed[32]{}; GetPrivateProfileStringW(L"Player", L"SpeedMultiplier", L"1.50", speed, _countof(speed), file.c_str());
    const double parsed = _wtof(speed);
    constexpr double choices[] = {0.25, 0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0};
    for (double choice : choices) if (std::abs(choice - parsed) < 0.01) { g_speedMultiplier.store(choice); break; }
}

void processKeybindCapture() {
    const int slot = g_captureBindFor.load();
    if (slot < BIND_MENU || slot > BIND_FRAME_STEP_ONCE) return;
    for (int key = 1; key < 256; ++key) {
        const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
        const bool wasDown = g_captureWasDown[static_cast<std::size_t>(key)];
        g_captureWasDown[static_cast<std::size_t>(key)] = down;
        if (!down || wasDown) continue;
        if (key == VK_ESCAPE) {
            g_captureBindFor.store(0);
            showToast(L"Keybind capture cancelled.");
            return;
        }
        if (!bindableKey(key)) continue;
        bool conflict = false;
        for (int other = BIND_MENU; other <= BIND_FRAME_STEP_ONCE; ++other) {
            if (other != slot && hotkeyRef(other).load() == key) { conflict = true; break; }
        }
        if (conflict) {
            showToast(L"That key is already assigned. Release it and press a different key.");
            return;
        }
        hotkeyRef(slot).store(key);
        // Avoid immediately triggering the newly-bound action from the same physical key press.
        g_dynamicWasDown[static_cast<std::size_t>(key)] = true;
        g_captureBindFor.store(0);
        saveSettings();
        std::wstring message = std::wstring(hotkeySlotName(slot)) + L" bound to " + hotkeyName(key) + L".";
        showToast(message.c_str());
        return;
    }
}

LRESULT CALLBACK labelEditorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
            CreateWindowExW(0, L"STATIC", L"Level labels are drawn over the game window. Placeholders update live.", WS_CHILD | WS_VISIBLE,
                16, 12, 650, 22, hwnd, nullptr, g_self, nullptr);
            CreateWindowExW(0, L"STATIC", L"Label text", WS_CHILD | WS_VISIBLE, 16, 42, 250, 20, hwnd, nullptr, g_self, nullptr);
            CreateWindowExW(0, L"STATIC", L"On", WS_CHILD | WS_VISIBLE, 385, 42, 35, 20, hwnd, nullptr, g_self, nullptr);
            CreateWindowExW(0, L"STATIC", L"Position", WS_CHILD | WS_VISIBLE, 425, 42, 95, 20, hwnd, nullptr, g_self, nullptr);
            CreateWindowExW(0, L"STATIC", L"Size", WS_CHILD | WS_VISIBLE, 525, 42, 55, 20, hwnd, nullptr, g_self, nullptr);
            CreateWindowExW(0, L"STATIC", L"Color", WS_CHILD | WS_VISIBLE, 586, 42, 65, 20, hwnd, nullptr, g_self, nullptr);
            for (int i = 0; i < 3; ++i) {
                const int rowY = 68 + i * 65;
                wchar_t name[32]{}; swprintf_s(name, _countof(name), L"Label %d", i + 1);
                CreateWindowExW(0, L"STATIC", name, WS_CHILD | WS_VISIBLE, 16, rowY + 6, 62, 23, hwnd, nullptr, g_self, nullptr);
                g_labelTextEdits[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_levelLabels[i].text.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 78, rowY, 300, 27, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_TEXT_BASE + i)), g_self, nullptr);
                g_labelVisibleChecks[i] = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                    388, rowY + 4, 28, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_VISIBLE_BASE + i)), g_self, nullptr);
                SendMessageW(g_labelVisibleChecks[i], BM_SETCHECK, g_levelLabels[i].enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                g_labelPositionCombos[i] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                    422, rowY, 98, 250, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_POSITION_BASE + i)), g_self, nullptr);
                const wchar_t* positions[] = {L"Top left", L"Top right", L"Bottom left", L"Bottom right", L"Top center", L"Bottom center"};
                for (const auto* item : positions) SendMessageW(g_labelPositionCombos[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
                SendMessageW(g_labelPositionCombos[i], CB_SETCURSEL, g_levelLabels[i].position, 0);
                g_labelSizeCombos[i] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                    524, rowY, 58, 240, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_SIZE_BASE + i)), g_self, nullptr);
                const wchar_t* sizes[] = {L"9", L"10", L"11", L"12", L"13", L"14", L"16", L"18", L"20", L"22", L"24"};
                int selectedSize = 3;
                for (int k = 0; k < static_cast<int>(_countof(sizes)); ++k) {
                    SendMessageW(g_labelSizeCombos[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(sizes[k]));
                    if (_wtoi(sizes[k]) == g_levelLabels[i].fontSize) selectedSize = k;
                }
                SendMessageW(g_labelSizeCombos[i], CB_SETCURSEL, selectedSize, 0);
                g_labelColorCombos[i] = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                    586, rowY, 78, 240, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_COLOR_BASE + i)), g_self, nullptr);
                const wchar_t* colors[] = {L"Blue", L"Cyan", L"Green", L"Pink", L"Orange"};
                for (const auto* item : colors) SendMessageW(g_labelColorCombos[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
                SendMessageW(g_labelColorCombos[i], CB_SETCURSEL, g_levelLabels[i].colorIndex, 0);
            }
            g_labelMasterCheck = CreateWindowExW(0, L"BUTTON", L"Show labels over game", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                16, 270, 220, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_MASTER_VISIBLE)), g_self, nullptr);
            SendMessageW(g_labelMasterCheck, BM_SETCHECK, g_levelLabelsVisible.load() ? BST_CHECKED : BST_UNCHECKED, 0);
            CreateWindowExW(0, L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                474, 268, 88, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_APPLY)), g_self, nullptr);
            CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                570, 268, 88, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LABEL_CANCEL)), g_self, nullptr);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            if (id == IDC_LABEL_CANCEL) { DestroyWindow(hwnd); return 0; }
            if (id == IDC_LABEL_APPLY) {
                for (int i = 0; i < 3; ++i) {
                    wchar_t textValue[256]{};
                    GetWindowTextW(g_labelTextEdits[i], textValue, _countof(textValue));
                    g_levelLabels[i].text = textValue;
                    if (g_levelLabels[i].text.size() > 120) g_levelLabels[i].text.resize(120);
                    g_levelLabels[i].enabled = SendMessageW(g_labelVisibleChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED;
                    const LRESULT pos = SendMessageW(g_labelPositionCombos[i], CB_GETCURSEL, 0, 0);
                    const LRESULT size = SendMessageW(g_labelSizeCombos[i], CB_GETCURSEL, 0, 0);
                    const LRESULT color = SendMessageW(g_labelColorCombos[i], CB_GETCURSEL, 0, 0);
                    g_levelLabels[i].position = std::clamp(static_cast<int>(pos < 0 ? 0 : pos), 0, 5);
                    const int sizes[] = {9,10,11,12,13,14,16,18,20,22,24};
                    g_levelLabels[i].fontSize = sizes[std::clamp(static_cast<int>(size < 0 ? 3 : size), 0, static_cast<int>(_countof(sizes))-1)];
                    g_levelLabels[i].colorIndex = std::clamp(static_cast<int>(color < 0 ? 0 : color), 0, 4);
                }
                g_levelLabelsVisible.store(SendMessageW(g_labelMasterCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                saveSettings();
                if (g_overlayWindow && IsWindow(g_overlayWindow)) InvalidateRect(g_overlayWindow, nullptr, FALSE);
                showToast(L"Level label settings applied.");
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
        case WM_DESTROY:
            if (hwnd == g_labelEditor) {
                g_labelEditor = nullptr;
                if (g_gameWindow && IsWindow(g_gameWindow)) SetForegroundWindow(g_gameWindow);
            }
            for (int i = 0; i < 3; ++i) { g_labelTextEdits[i] = nullptr; g_labelVisibleChecks[i] = nullptr; g_labelPositionCombos[i] = nullptr; g_labelSizeCombos[i] = nullptr; g_labelColorCombos[i] = nullptr; }
            g_labelMasterCheck = nullptr;
            return 0;
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void openLabelEditor() {
    if (g_labelEditor && IsWindow(g_labelEditor)) { ShowWindow(g_labelEditor, SW_SHOWNORMAL); SetForegroundWindow(g_labelEditor); return; }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = labelEditorWndProc; wc.hInstance = g_self;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = L"GDClientLabelEditorWindow";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) { showToast(L"Could not register label editor window."); return; }
        registered = true;
    }
    g_labelEditor = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"GDClientLabelEditorWindow", L"GD Client — Level Labels",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 700, 350, nullptr, nullptr, g_self, nullptr);
    if (!g_labelEditor) { showToast(L"Could not open label editor."); return; }
    ShowWindow(g_labelEditor, SW_SHOWNORMAL);
    UpdateWindow(g_labelEditor);
    SetForegroundWindow(g_labelEditor);
    SetFocus(g_labelTextEdits[0]);
}

std::wstring getClientFolder() {
    wchar_t appData[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, _countof(appData));
    if (len == 0 || len >= _countof(appData)) return L".";
    std::wstring folder = std::wstring(appData) + L"\\GDClient";
    CreateDirectoryW(folder.c_str(), nullptr);
    return folder;
}

void saveMacro() {
    if (g_macroEvents.size() > 500000) return;
    const std::wstring folder = getClientFolder();
    const std::wstring path = folder + L"\\last_macro.gdm";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) { showToast(L"Could not save macro file."); return; }
    struct HeaderPrefix { char magic[4]; std::uint32_t version; std::uint32_t count; } prefix{{'G','D','M','2'}, 2u, static_cast<std::uint32_t>(g_macroEvents.size())};
    struct HeaderTail { std::uint32_t flags; std::uint32_t reserved; } tail{g_macroUsesGameTimeline.load() ? 1u : 0u, 0u};
    file.write(reinterpret_cast<const char*>(&prefix), sizeof(prefix));
    file.write(reinterpret_cast<const char*>(&tail), sizeof(tail));
    if (!g_macroEvents.empty()) file.write(reinterpret_cast<const char*>(g_macroEvents.data()), static_cast<std::streamsize>(g_macroEvents.size() * sizeof(MacroEvent)));
    file.close();
    if (!file) showToast(L"Macro save failed.");
}

void loadMacro() {
    const std::wstring path = getClientFolder() + L"\\last_macro.gdm";
    std::ifstream file(path, std::ios::binary);
    if (!file) { g_macroEvents.clear(); g_macroCount.store(0); return; }
    struct HeaderPrefix { char magic[4]; std::uint32_t version; std::uint32_t count; } prefix{};
    file.read(reinterpret_cast<char*>(&prefix), sizeof(prefix));
    if (!file || prefix.count > 500000) { g_macroEvents.clear(); g_macroCount.store(0); return; }
    bool timelineNormalized = false;
    if (std::memcmp(prefix.magic, "GDM1", 4) == 0 && prefix.version == 1u) {
        // Legacy files use wall-clock event timestamps; preserve their old playback behavior.
        timelineNormalized = false;
    } else if (std::memcmp(prefix.magic, "GDM2", 4) == 0 && prefix.version == 2u) {
        struct HeaderTail { std::uint32_t flags; std::uint32_t reserved; } tail{};
        file.read(reinterpret_cast<char*>(&tail), sizeof(tail));
        if (!file) { g_macroEvents.clear(); g_macroCount.store(0); return; }
        timelineNormalized = (tail.flags & 1u) != 0;
    } else { g_macroEvents.clear(); g_macroCount.store(0); return; }
    std::vector<MacroEvent> loaded(prefix.count);
    if (!loaded.empty()) file.read(reinterpret_cast<char*>(loaded.data()), static_cast<std::streamsize>(loaded.size() * sizeof(MacroEvent)));
    if (!file && !loaded.empty()) { g_macroEvents.clear(); g_macroCount.store(0); return; }
    bool valid = true;
    DWORD previousTime = 0;
    for (const auto& e : loaded) {
        if (e.elapsedMs < previousTime || e.elapsedMs > 24u * 60u * 60u * 1000u || e.type < MACRO_KEY_DOWN || e.type > MACRO_MOUSE_RIGHT_UP) { valid = false; break; }
        if ((e.type == MACRO_KEY_DOWN || e.type == MACRO_KEY_UP) && (e.code > 255 || e.code == VK_LWIN || e.code == VK_RWIN || e.code == VK_TAB || (e.code >= VK_F2 && e.code <= VK_F9))) { valid = false; break; }
        previousTime = e.elapsedMs;
    }
    if (!valid) loaded.clear();
    g_macroEvents = std::move(loaded);
    g_macroUsesGameTimeline.store(timelineNormalized);
    g_macroCount.store(static_cast<int>(g_macroEvents.size()));
}

void stopMacroPlayback() {
    g_macroReplaying.store(false);
    // Never send cleanup key-ups into another foreground application after focus loss.
    const bool gameHasFocus = g_gameWindow && IsWindow(g_gameWindow) && GetForegroundWindow() == g_gameWindow;
    if (gameHasFocus) {
        for (DWORD vk : g_replayKeysDown) {
            INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = static_cast<WORD>(vk); in.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &in, sizeof(in));
        }
        if (g_replayLeftDown) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_LEFTUP; SendInput(1, &in, sizeof(in)); }
        if (g_replayRightDown) { INPUT in{}; in.type = INPUT_MOUSE; in.mi.dwFlags = MOUSEEVENTF_RIGHTUP; SendInput(1, &in, sizeof(in)); }
    }
    g_replayKeysDown.clear();
    g_replayLeftDown = false; g_replayRightDown = false;
}

void stopMacroRecording() {
    if (!g_macroRecording.exchange(false)) return;
    g_macroCount.store(static_cast<int>(g_macroEvents.size()));
    saveMacro();
    showToast(L"Input macro recording saved.");
}

double effectiveGameSpeedScale() {
    return (g_speedEnabled.load() && hasTimingHook()) ? std::clamp(g_speedMultiplier.load(), 0.05, 10.0) : 1.0;
}

void updateMacroRecordTimeline() {
    if (!g_macroRecording.load()) return;
    const ULONGLONG now = GetTickCount64();
    if (g_macroLastTimelineSample == 0) g_macroLastTimelineSample = now;
    // Do not count time spent in the client menu, editor dialog, or another app.
    if (g_menuOpen.load() || g_menuProgress > 0.025f || !g_gameWindow ||
        !IsWindow(g_gameWindow) || GetForegroundWindow() != g_gameWindow ||
        (g_labelEditor && IsWindow(g_labelEditor))) {
        g_macroLastTimelineSample = now;
        return;
    }
    const ULONGLONG delta = std::min<ULONGLONG>(now - g_macroLastTimelineSample, 1000);
    g_macroRecordTimelineMs += static_cast<double>(delta) * effectiveGameSpeedScale();
    g_macroLastTimelineSample = now;
    if (g_macroRecordTimelineMs > 24.0 * 60.0 * 60.0 * 1000.0) stopMacroRecording();
}

void startMacroRecording() {
    if (!canUsePracticeFeatures()) {
        showToast(gdclient::native::practiceModeKnown() && !gdclient::native::practiceModeActive()
            ? L"Enter Geometry Dash Practice Mode before recording."
            : L"Enter a supported level and arm the practice guard before recording.");
        return;
    }
    if (g_macroReplaying.load()) stopMacroPlayback();
    g_macroEvents.clear();
    g_macroRecordStarted = GetTickCount64();
    g_macroLastTimelineSample = g_macroRecordStarted;
    g_macroRecordTimelineMs = 0.0;
    g_macroUsesGameTimeline.store(true);
    g_macroRecording.store(true);
    g_macroCount.store(0);
    // Starting from the Practice tab should return control to the game immediately.
    // The transition is excluded from the record timeline and input scan below.
    g_menuOpen.store(false);
    std::wstring recordToast = L"Recording practice inputs on game-time timeline. " + hotkeyName(g_bindMacroRecord.load()) + L" stops.";
    showToast(recordToast.c_str());
}

void startMacroPlayback() {
    if (!canUsePracticeFeatures()) {
        showToast(gdclient::native::practiceModeKnown() && !gdclient::native::practiceModeActive()
            ? L"Enter Geometry Dash Practice Mode before playback."
            : L"Enter a supported level and arm the practice guard before playback.");
        return;
    }
    if (g_macroRecording.load()) stopMacroRecording();
    if (g_macroEvents.empty()) loadMacro();
    if (g_macroEvents.empty()) { showToast(L"No saved input macro found."); return; }
    stopMacroPlayback();
    g_macroPlayIndex = 0;
    g_macroPlayStarted = GetTickCount64();
    g_macroPlaybackLastSample = g_macroPlayStarted;
    g_macroPlaybackTimelineMs = 0.0;
    g_macroReplaying.store(true);
    // A Play click comes from the overlay; hide it so it cannot block gameplay inputs.
    g_menuOpen.store(false);
    showToast(L"Macro playback starting. The menu closes before the first event.");
}

void addMacroEvent(DWORD type, DWORD code = 0) {
    if (!g_macroRecording.load() || g_macroReplaying.load() || g_macroEvents.size() >= 500000 || !g_gameWindow) return;
    POINT pt{}; GetCursorPos(&pt); ScreenToClient(g_gameWindow, &pt);
    RECT rc{}; GetClientRect(g_gameWindow, &rc);
    if (type >= MACRO_MOUSE_LEFT_DOWN && type <= MACRO_MOUSE_RIGHT_UP && !PtInRect(&rc, pt)) return;
    const ULONGLONG elapsed = static_cast<ULONGLONG>(std::clamp(g_macroRecordTimelineMs, 0.0, 24.0 * 60.0 * 60.0 * 1000.0));
    g_macroEvents.push_back({ static_cast<DWORD>(std::min<ULONGLONG>(elapsed, 0xFFFFFFFFu)), type, code, pt.x, pt.y });
    g_macroCount.store(static_cast<int>(g_macroEvents.size()));
}

void scanMacroInput(bool keyState[256], bool& leftState, bool& rightState) {
    if (!g_macroRecording.load()) return;
    if (!canUsePracticeFeatures()) { stopMacroRecording(); showToast(L"Practice mode ended or practice guard disabled; recording stopped."); return; }
    if (g_menuOpen.load()) { stopMacroRecording(); showToast(L"Recording stopped because the client menu was opened."); return; }
    if (g_menuProgress > 0.025f || !g_gameWindow || !IsWindow(g_gameWindow) ||
        GetForegroundWindow() != g_gameWindow || (g_labelEditor && IsWindow(g_labelEditor))) return;
    constexpr int keys[] = { VK_SPACE, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_CONTROL, VK_SHIFT, VK_RETURN, 'Z', 'X', 'C', 'W', 'A', 'S', 'D' };
    for (int vk : keys) {
        // Client shortcuts must not record their own start/stop/toggle keypress as gameplay input.
        const bool down = !isConfiguredHotkey(vk) && ((GetAsyncKeyState(vk) & 0x8000) != 0);
        if (down != keyState[vk]) { addMacroEvent(down ? MACRO_KEY_DOWN : MACRO_KEY_UP, static_cast<DWORD>(vk)); keyState[vk] = down; }
    }
    const bool left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (left != leftState) { addMacroEvent(left ? MACRO_MOUSE_LEFT_DOWN : MACRO_MOUSE_LEFT_UP); leftState = left; }
    if (right != rightState) { addMacroEvent(right ? MACRO_MOUSE_RIGHT_DOWN : MACRO_MOUSE_RIGHT_UP); rightState = right; }
}

void emitMacroEvent(const MacroEvent& e) {
    if (!g_gameWindow || !IsWindow(g_gameWindow)) return;
    if (e.type == MACRO_KEY_DOWN || e.type == MACRO_KEY_UP) {
        INPUT in{}; in.type = INPUT_KEYBOARD; in.ki.wVk = static_cast<WORD>(e.code);
        if (e.type == MACRO_KEY_UP) in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(in));
        if (e.type == MACRO_KEY_DOWN) {
            if (std::find(g_replayKeysDown.begin(), g_replayKeysDown.end(), e.code) == g_replayKeysDown.end()) g_replayKeysDown.push_back(e.code);
        } else {
            g_replayKeysDown.erase(std::remove(g_replayKeysDown.begin(), g_replayKeysDown.end(), e.code), g_replayKeysDown.end());
        }
        return;
    }
    POINT origin{0,0}; ClientToScreen(g_gameWindow, &origin);
    SetCursorPos(origin.x + e.x, origin.y + e.y);
    INPUT in{}; in.type = INPUT_MOUSE;
    switch (e.type) {
        case MACRO_MOUSE_LEFT_DOWN: in.mi.dwFlags = MOUSEEVENTF_LEFTDOWN; g_replayLeftDown = true; break;
        case MACRO_MOUSE_LEFT_UP: in.mi.dwFlags = MOUSEEVENTF_LEFTUP; g_replayLeftDown = false; break;
        case MACRO_MOUSE_RIGHT_DOWN: in.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN; g_replayRightDown = true; break;
        case MACRO_MOUSE_RIGHT_UP: in.mi.dwFlags = MOUSEEVENTF_RIGHTUP; g_replayRightDown = false; break;
        default: return;
    }
    SendInput(1, &in, sizeof(in));
}

void advanceMacroPlayback() {
    if (!g_macroReplaying.load() || g_macroEvents.empty()) return;
    if (!canUsePracticeFeatures() || !g_gameWindow || !IsWindow(g_gameWindow) || GetForegroundWindow() != g_gameWindow || g_menuOpen.load()) { stopMacroPlayback(); return; }
    const ULONGLONG now = GetTickCount64();
    if (g_menuProgress > 0.025f) {
        // Don't replay the event that was clicked to start playback while the panel closes.
        g_macroPlaybackLastSample = now;
        return;
    }
    if (g_macroPlaybackLastSample == 0) g_macroPlaybackLastSample = now;
    const ULONGLONG delta = std::min<ULONGLONG>(now - g_macroPlaybackLastSample, 1000);
    const double scale = g_macroUsesGameTimeline.load() ? effectiveGameSpeedScale() : 1.0;
    g_macroPlaybackTimelineMs += static_cast<double>(delta) * scale;
    g_macroPlaybackLastSample = now;
    while (g_macroPlayIndex < g_macroEvents.size() && static_cast<double>(g_macroEvents[g_macroPlayIndex].elapsedMs) <= g_macroPlaybackTimelineMs) {
        emitMacroEvent(g_macroEvents[g_macroPlayIndex]);
        ++g_macroPlayIndex;
    }
    if (g_macroPlayIndex >= g_macroEvents.size()) {
        if (g_macroLoop.load()) {
            g_macroPlayIndex = 0; g_macroPlayStarted = now; g_macroPlaybackLastSample = now; g_macroPlaybackTimelineMs = 0.0;
        } else { stopMacroPlayback(); showToast(L"Macro playback finished."); }
    }
}

int findEncoderClsid(const WCHAR* mimeType, CLSID* clsid) {
    if (!mimeType || !clsid) return -1;
    UINT count = 0, bytes = 0;
    if (Gdiplus::GetImageEncodersSize(&count, &bytes) != Gdiplus::Ok || bytes == 0) return -1;
    std::vector<BYTE> buffer(bytes);
    auto* encoders = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
    if (Gdiplus::GetImageEncoders(count, bytes, encoders) != Gdiplus::Ok) return -1;
    for (UINT i = 0; i < count; ++i) {
        if (encoders[i].MimeType && wcscmp(encoders[i].MimeType, mimeType) == 0) {
            *clsid = encoders[i].Clsid;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void takeScreenshot() {
    if (!g_gameWindow || !IsWindow(g_gameWindow)) { showToast(L"Game window unavailable."); return; }
    if (GetForegroundWindow() != g_gameWindow) { showToast(L"Focus Geometry Dash before capturing a screenshot."); return; }
    RECT rc{}; GetClientRect(g_gameWindow, &rc);
    const int width = rc.right - rc.left, height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0 || width > 12000 || height > 12000) { showToast(L"Invalid game window dimensions."); return; }
    POINT origin{0,0}; ClientToScreen(g_gameWindow, &origin);
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = screen ? CreateCompatibleBitmap(screen, width, height) : nullptr;
    if (!screen || !memory || !bitmap) { if (bitmap) DeleteObject(bitmap); if (memory) DeleteDC(memory); if (screen) ReleaseDC(nullptr, screen); showToast(L"Screenshot allocation failed."); return; }
    HGDIOBJ old = SelectObject(memory, bitmap);
    const BOOL copied = BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
    if (!copied) { SelectObject(memory, old); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen); showToast(L"Screenshot capture failed."); return; }
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    SelectObject(memory, old);
    const int rows = GetDIBits(memory, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS);
    DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
    if (rows == 0) { showToast(L"Could not read screenshot pixels from GDI."); return; }
    // GDI's 32-bit DIB may leave the alpha byte undefined; force opaque pixels for PNG.
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0xFF;

    SYSTEMTIME st{}; GetLocalTime(&st);
    wchar_t filename[128]{}; swprintf_s(filename, _countof(filename), L"\\shot_%04u%02u%02u_%02u%02u%02u.png", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    const std::wstring folder = getClientFolder() + L"\\screenshots"; CreateDirectoryW(folder.c_str(), nullptr);
    const std::wstring path = folder + filename;

    Gdiplus::GdiplusStartupInput startupInput;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &startupInput, nullptr) != Gdiplus::Ok) {
        showToast(L"PNG encoder initialization failed."); return;
    }
    CLSID pngClsid{};
    bool ok = false;
    if (findEncoderClsid(L"image/png", &pngClsid) >= 0) {
        Gdiplus::Bitmap image(width, height, width * 4, PixelFormat32bppARGB, pixels.data());
        ok = image.GetLastStatus() == Gdiplus::Ok && image.Save(path.c_str(), &pngClsid, nullptr) == Gdiplus::Ok;
    }
    Gdiplus::GdiplusShutdown(token);
    if (ok) { g_screenshotCount.fetch_add(1); showToast(L"PNG screenshot saved to AppData\\GDClient\\screenshots."); }
    else showToast(L"PNG screenshot write failed.");
}

void openClientFolder() {
    const std::wstring folder = getClientFolder();
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void setOverlayClickThrough(bool clickThrough) {
    if (!g_overlayWindow || !IsWindow(g_overlayWindow)) return;
    LONG_PTR style = GetWindowLongPtrW(g_overlayWindow, GWL_EXSTYLE);
    const LONG_PTR wanted = clickThrough
        ? style | static_cast<LONG_PTR>(WS_EX_TRANSPARENT)
        : style & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
    if (wanted == style) return;
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtrW(g_overlayWindow, GWL_EXSTYLE, wanted);
    if (previous != 0 || GetLastError() == ERROR_SUCCESS) {
        SetWindowPos(g_overlayWindow, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

void changePage(int page) {
    if (page < 0 || page >= PAGE_COUNT) return;
    g_menuOpen.store(true);
    g_menuProgress = 1.0f;
    setOverlayClickThrough(false);
    if (page == g_page.load()) return;
    g_previousPage = g_page.load();
    g_page.store(page);
    g_pageProgress = g_animations.load() ? 0.0f : 1.0f;
}

void resetSettings() {
    if (g_macroRecording.load()) stopMacroRecording();
    if (g_macroReplaying.load()) stopMacroPlayback();
    g_showHud.store(true);
    g_inputOverlayVisible.store(true);
    g_animations.store(true);
    g_accentIndex.store(1);
    g_overlayAlpha.store(245);
    g_cps.store(12);
    g_clickerHoldToRun.store(false);
    g_macroLoop.store(false);
    gdclient::native::setNativeMacroPlaybackRate(1.0);
    g_practiceGuard.store(false);
    g_safeMode.store(true);
    g_physicsTpsPreference.store(0);
    gdclient::native::setPhysicsTps(0, false);
    g_bindMenu.store(VK_TAB); g_bindSpeed.store(VK_F2); g_bindSpeedCycle.store(VK_F3);
    g_bindClicker.store(VK_F4); g_bindHud.store(VK_F5); g_bindScreenshot.store(VK_F6);
    g_bindMacroRecord.store(VK_F7); g_bindMacroPlay.store(VK_F8); g_bindMacroStop.store(VK_F9);
    g_bindFrameStep.store(VK_F10); g_bindFrameStepOnce.store(VK_F11);
    g_captureBindFor.store(0); g_dynamicWasDown.fill(false);
    g_levelLabelsVisible.store(true);
    g_levelLabels[0] = {L"GD CLIENT  |  {SPEED}", true, 0, 12, 0};
    g_levelLabels[1] = {L"MACRO: {MACRO}", true, 1, 12, 1};
    g_levelLabels[2] = {L"{EVENTS} INPUT EVENTS  |  {SESSION}", false, 2, 11, 2};
    g_speedMultiplier.store(1.5);
    gdclient::native::setFrameStepEnabled(false);
    gdclient::native::setGameThreadPriorityLevel(0);
    if (g_speedEnabled.load()) setSpeedScale(1.0);
    g_speedEnabled.store(false);
    gdclient::native::setNoclipEnabled(false);
    gdclient::native::setHitboxesEnabled(false);
    syncNativeSettings();
    saveSettings();
    showToast(L"Settings reset to defaults.");
}

void handleMenuClick(int x, int y) {
    const int slide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 20.0f) : 0;
    const int vslide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 12.0f) : 0;
    const int panelX = g_menuX - slide;
    const int panelY = g_menuY + vslide;
    const int lx = x - panelX;
    const int ly = y - panelY;
    const int mw = g_drawnMenuW;
    if (lx < 0 || ly < 0 || lx >= mw || ly >= g_drawnMenuH) return;
    if (contains({mw - 39, 11, 30, 28}, lx, ly)) { g_menuOpen.store(false); return; }
    if (ly >= 0 && ly < 56 && lx < mw - 50) { if (g_menuProgress < 0.92f) return; g_dragging = true; g_dragOffsetX = lx; g_dragOffsetY = ly; SetCapture(g_overlayWindow); return; }

    if (ly >= 62 && ly < 100 && lx >= 13 && lx <= mw - 13) {
        const int totalWidth = std::max(10, mw - 26);
        const int tabWidth = totalWidth / PAGE_COUNT;
        const int p = std::clamp((lx - 13) / std::max(1, tabWidth), 0, PAGE_COUNT - 1);
        changePage(p);
        return;
    }

    if (ly < 150 || ly > g_drawnMenuH - 28) return;
    const int cx = 18;
    const int page = g_page.load();
    const int cy = 158 - g_pageScroll[page];
    const int cw = mw - 36;
    const int gap = 12;
    const int col = (cw - gap) / 2;
    auto toggleSpeed = [&]() {
        if (!hasTimingHook()) { showToast(L"No supported game delta-time hook is available for this build."); return; }
        const bool enable = !g_speedEnabled.load();
        if (enable && !gameplayModeAllowed(L"Speedhack")) return;
        setSpeedScale(enable ? g_speedMultiplier.load() : 1.0);
        g_speedEnabled.store(enable); saveSettings(); showToast(enable ? L"Speed scale enabled (experimental)." : L"Speed scale reset to 1.0x.");
    };
    auto toggleClicker = [&]() {
        const bool enable = !g_autoClicker.load();
        if (enable && !gameplayModeAllowed(L"Auto clicker")) return;
        g_autoClicker.store(enable);
        showToast(enable ? L"Auto clicker enabled." : L"Auto clicker disabled.");
    };
    auto adjustCps = [&](int d) { g_cps.store(std::clamp(g_cps.load() + d, 4, 40)); saveSettings(); showToast(L"Click rate updated."); };
    auto adjustOpacity = [&](int d) { g_overlayAlpha.store(std::clamp(g_overlayAlpha.load() + d, 120, 255)); saveSettings(); showToast(L"Overlay opacity updated."); };
    auto adjustSpeed = [&](int delta) { stepSpeed(delta); saveSettings(); showToast(L"Speed preset updated."); };

    if (page == PAGE_DASHBOARD) {
        if (contains({cx, cy, col, 94}, lx, ly)) { toggleSpeed(); return; }
        if (contains({cx + col + gap, cy, col, 94}, lx, ly)) { toggleClicker(); return; }
        if (contains({cx, cy + 106, col, 106}, lx, ly)) { adjustSpeed(lx >= cx + col - 70 ? 1 : -1); return; }
        if (contains({cx + col + gap, cy + 106, col, 106}, lx, ly)) { adjustCps(lx >= cx + col + gap + col - 70 ? 4 : -4); return; }
        if (contains({cx, cy + 318, cw, 90}, lx, ly)) { changePage(PAGE_UTILITIES); return; }
    } else if (page == PAGE_PLAYER) {
        if (contains({cx, cy, cw, 94}, lx, ly)) { toggleSpeed(); return; }
        if (contains({cx, cy + 106, cw, 104}, lx, ly)) { adjustSpeed(lx >= cx + cw - 105 ? 1 : -1); return; }
        if (contains({cx, cy + 222, cw, 84}, lx, ly)) { toggleClicker(); return; }
    } else if (page == PAGE_PHYSICS) {
        const int col = (cw - 12) / 2;
        if (contains({cx, cy + 84, col, 80}, lx, ly)) {
            if (!gdclient::native::noclipHookReady()) { showToast(L"Noclip requires a supported, fingerprint-verified game build."); return; }
            if (!gdclient::native::inLevel()) { showToast(L"Start a level before enabling noclip."); return; }
            const bool enable = !gdclient::native::noclipEnabled();
            if (enable && !gameplayModeAllowed(L"Noclip")) return;
            gdclient::native::setNoclipEnabled(enable);
            showToast(gdclient::native::noclipEnabled() ? L"Noclip enabled." : L"Noclip disabled.");
            return;
        }
        if (contains({cx + col + 12, cy + 84, col, 80}, lx, ly)) {
            if (!gdclient::native::hitboxHookReady()) { showToast(L"Hitbox debug is not available on this build."); return; }
            if (!gdclient::native::inLevel()) { showToast(L"Start a level before enabling hitbox debug."); return; }
            const bool enable = !gdclient::native::hitboxesEnabled();
            if (enable && !gameplayModeAllowed(L"Hitbox display")) return;
            gdclient::native::setHitboxesEnabled(enable);
            showToast(gdclient::native::hitboxesEnabled() ? L"Hitbox debug enabled." : L"Hitbox debug disabled.");
            return;
        }
        const int limitsY = cy + 176;
        if (contains({cx + 16, limitsY + 45, 174, 30}, lx, ly)) {
            g_noclipDeathLimitOn.store(!g_noclipDeathLimitOn.load()); syncNativeSettings(); saveSettings();
            showToast(g_noclipDeathLimitOn.load() ? L"Noclip contact limit enabled." : L"Noclip contact limit disabled."); return;
        }
        if (contains({cx + cw - 112, limitsY + 43, 38, 30}, lx, ly)) {
            g_noclipDeathThreshold.store(std::max(1, g_noclipDeathThreshold.load() - 1)); syncNativeSettings(); saveSettings(); return;
        }
        if (contains({cx + cw - 64, limitsY + 43, 38, 30}, lx, ly)) {
            g_noclipDeathThreshold.store(std::min(100000, g_noclipDeathThreshold.load() + 1)); syncNativeSettings(); saveSettings(); return;
        }
        if (contains({cx + 16, limitsY + 82, 174, 30}, lx, ly)) {
            g_noclipAccuracyLimitOn.store(!g_noclipAccuracyLimitOn.load()); syncNativeSettings(); saveSettings();
            showToast(g_noclipAccuracyLimitOn.load() ? L"Noclip accuracy limit enabled." : L"Noclip accuracy limit disabled."); return;
        }
        if (contains({cx + cw - 112, limitsY + 81, 38, 30}, lx, ly)) {
            g_noclipMinAccuracy.store(std::max(0.0, g_noclipMinAccuracy.load() - 1.0)); syncNativeSettings(); saveSettings(); return;
        }
        if (contains({cx + cw - 64, limitsY + 81, 38, 30}, lx, ly)) {
            g_noclipMinAccuracy.store(std::min(100.0, g_noclipMinAccuracy.load() + 1.0)); syncNativeSettings(); saveSettings(); return;
        }
        const int tpsY = cy + 422;
        if (contains({cx + cw - 108, tpsY + 34, 40, 27}, lx, ly) || contains({cx + cw - 59, tpsY + 34, 40, 27}, lx, ly)) {
            constexpr int rates[] = {0, 240, 360, 480, 1000};
            int idx = 0;
            for (int i = 0; i < static_cast<int>(_countof(rates)); ++i) if (rates[i] == g_physicsTpsPreference.load()) idx = i;
            const bool increase = lx >= cx + cw - 80;
            idx = (idx + (increase ? 1 : static_cast<int>(_countof(rates)) - 1)) % static_cast<int>(_countof(rates));
            g_physicsTpsPreference.store(rates[idx]);
            if (rates[idx] == 0) {
                gdclient::native::setPhysicsTps(0, false);
                showToast(L"Physics TPS restored to Geometry Dash default.");
            } else if (canUsePracticeFeatures()) {
                if (gdclient::native::setPhysicsTps(rates[idx], g_practiceGuard.load())) showToast(L"Experimental fixed-step TPS activated for this Practice session.");
                else showToast(L"TPS override unavailable; use a verified Practice session.");
            } else {
                showToast(L"TPS target saved. Arm the Practice Guard and enter a supported Practice Mode level to activate it.");
            }
            saveSettings(); return;
        }
        const int stepY = cy + 500;
        if (contains({cx + cw - 220, stepY + 39, 96, 29}, lx, ly)) {
            if (!gdclient::native::frameStepEnabled()) {
                if (!gdclient::native::inLevel()) { showToast(L"Start a level before enabling frame step."); return; }
                if (!gameplayModeAllowed(L"Frame stepping")) return;
                gdclient::native::setFrameStepEnabled(true);
                std::wstring stepToast = L"Frame step enabled and paused. " + hotkeyName(g_bindFrameStepOnce.load()) + L" advances one update.";
                showToast(gdclient::native::frameStepEnabled() ? stepToast.c_str() : L"Frame step could not be enabled.");
            } else {
                gdclient::native::setFrameStepEnabled(false);
                showToast(L"Frame step disabled.");
            }
            return;
        }
        if (contains({cx + cw - 112, stepY + 39, 96, 29}, lx, ly)) {
            if (!gdclient::native::advanceOneFrame()) showToast(L"Enable and pause frame step first.");
            else showToast(L"One update queued.");
            return;
        }
        const int completeY = stepY + 86;
        if (contains({cx + cw - 170, completeY + 36, 148, 28}, lx, ly)) {
            if (!gameplayModeAllowed(L"Instant complete")) return;
            if (!gdclient::native::requestInstantComplete()) showToast(L"Instant complete requires a supported build and active level.");
            else showToast(L"Instant completion queued on the game thread.");
            return;
        }
        const int trailY = completeY + 80;
        if (contains({cx, trailY, cw, 72}, lx, ly)) {
            if (!gdclient::native::hitboxTrailHookReady()) { showToast(L"Native hitbox trail is only available on the supported 2.2091 Windows build."); return; }
            if (!gdclient::native::inLevel()) { showToast(L"Start a level before enabling the native hitbox trail."); return; }
            const bool enable = !gdclient::native::hitboxTrailEnabled();
            gdclient::native::setHitboxTrailEnabled(enable);
            showToast(gdclient::native::hitboxTrailEnabled() ? L"Native hitbox trail enabled." : L"Native hitbox trail disabled.");
            return;
        }
    } else if (page == PAGE_PRACTICE) {
        if (contains({cx, cy, cw, 64}, lx, ly)) {
            const bool arm = !g_practiceGuard.load();
            g_practiceGuard.store(arm); syncNativeSettings(); saveSettings();
            if (!arm) { if (g_macroRecording.load()) stopMacroRecording(); if (g_macroReplaying.load()) stopMacroPlayback(); }
            showToast(arm ? L"Practice guard armed. Confirm you are in a local Practice Mode level." : L"Practice guard disarmed; macro activity stopped.");
            return;
        }
        const int recordY = cy + 76;
        if (contains({cx + 16, recordY + 65, 142, 31}, lx, ly)) {
            if (g_macroRecording.load()) stopMacroRecording(); else startMacroRecording();
            return;
        }
        if (contains({cx + 169, recordY + 65, 142, 31}, lx, ly)) {
            if (!g_macroRecording.load() && g_macroCount.load() > 0) { saveMacro(); showToast(L"Macro saved to AppData."); }
            return;
        }
        const int playbackY = cy + 200;
        if (contains({cx + 16, playbackY + 61, 124, 30}, lx, ly)) { startMacroPlayback(); return; }
        if (contains({cx + 150, playbackY + 61, 124, 30}, lx, ly)) { stopMacroPlayback(); return; }
        if (contains({cx + 400, playbackY + 63, 48, 25}, lx, ly)) {
            g_macroLoop.store(!g_macroLoop.load()); saveSettings(); showToast(g_macroLoop.load() ? L"Macro looping enabled." : L"Macro looping disabled."); return;
        }
        if (contains({cx + cw - 154, cy + 316 + 35, 132, 31}, lx, ly)) { takeScreenshot(); return; }
        const int nativeY = cy + 410;
        if (contains({cx + 16, nativeY + 58, 124, 30}, lx, ly)) {
            const auto state = gdclient::native::nativeMacroStats();
            if (!state.available) { showToast(L"Native macro needs the supported handleButton hook."); return; }
            if (state.recording) { gdclient::native::stopNativeMacroRecording(); showToast(L"Native macro recording stopped."); }
            else if (!canUsePracticeFeatures()) { showToast(L"Arm the practice guard and enter a confirmed Practice Mode level first."); }
            else if (!gdclient::native::startNativeMacroRecording(g_practiceGuard.load())) { showToast(L"Native macro could not start. Stop playback and verify Practice Mode."); }
            else showToast(L"Recording game input on the simulation-time timeline.");
            return;
        }
        if (contains({cx + 148, nativeY + 58, 124, 30}, lx, ly)) {
            if (gdclient::native::saveNativeMacro()) showToast(L"Native macro saved to AppData\\GDClient\\native_practice.gdm3.");
            else showToast(L"Save failed: record at least one input/reset event first.");
            return;
        }
        if (contains({cx + 280, nativeY + 58, 124, 30}, lx, ly)) {
            if (gdclient::native::loadNativeMacro()) showToast(L"Native macro loaded from AppData.");
            else showToast(L"Load failed: no valid native_practice.gdm3 was found.");
            return;
        }
        if (contains({cx + 412, nativeY + 58, 124, 30}, lx, ly)) {
            if (!canUsePracticeFeatures()) { showToast(L"Arm the practice guard and enter a confirmed Practice Mode level first."); return; }
            if (gdclient::native::startNativeMacroPlayback(g_practiceGuard.load(), g_macroLoop.load())) showToast(L"Native macro playback started.");
            else showToast(L"Playback failed: load a macro and verify the supported Practice Mode level.");
            return;
        }
        if (contains({cx + 544, nativeY + 58, 124, 30}, lx, ly)) {
            gdclient::native::stopNativeMacroPlayback(); showToast(L"Native macro stop requested."); return;
        }
        if (contains({cx + cw - 108, nativeY + 91, 42, 28}, lx, ly)) {
            const double rate = std::max(0.25, gdclient::native::nativeMacroPlaybackRate() - 0.25);
            gdclient::native::setNativeMacroPlaybackRate(rate); saveSettings(); showToast(L"Native macro playback rate updated."); return;
        }
        if (contains({cx + cw - 58, nativeY + 91, 42, 28}, lx, ly)) {
            const double rate = std::min(4.0, gdclient::native::nativeMacroPlaybackRate() + 0.25);
            gdclient::native::setNativeMacroPlaybackRate(rate); saveSettings(); showToast(L"Native macro playback rate updated."); return;
        }
    } else if (page == PAGE_LABELS) {
        if (contains({cx, cy, cw, 72}, lx, ly)) { g_levelLabelsVisible.store(!g_levelLabelsVisible.load()); saveSettings(); showToast(g_levelLabelsVisible.load() ? L"Level labels enabled." : L"Level labels hidden."); return; }
        const int labelCardY = cy + 84;
        if (contains({cx + 16, labelCardY + 73, 154, 34}, lx, ly)) { openLabelEditor(); return; }
        if (contains({cx + 180, labelCardY + 73, 154, 34}, lx, ly)) {
            g_levelLabels[0] = {L"GD CLIENT  |  {SPEED}", true, 0, 12, 0};
            g_levelLabels[1] = {L"MACRO: {MACRO}", true, 1, 12, 1};
            g_levelLabels[2] = {L"{EVENTS} INPUT EVENTS  |  {SESSION}", false, 2, 11, 2};
            g_levelLabelsVisible.store(true); saveSettings(); showToast(L"Default level labels restored."); return;
        }
    } else if (page == PAGE_VISUALS) {
        if (contains({cx, cy, cw, 74}, lx, ly)) { g_showHud.store(!g_showHud.load()); saveSettings(); showToast(g_showHud.load() ? L"Status HUD enabled." : L"Status HUD hidden."); return; }
        if (contains({cx, cy + 86, cw, 100}, lx, ly)) {
            g_accentIndex.store(std::clamp((lx - (cx + 18)) / 86, 0, 4)); saveSettings(); showToast(L"Accent theme changed."); return;
        }
        if (contains({cx, cy + 198, cw, 80}, lx, ly)) { adjustOpacity(lx >= cx + cw - 64 ? 15 : -15); return; }
        if (contains({cx, cy + 290, cw, 72}, lx, ly)) { g_animations.store(!g_animations.load()); saveSettings(); showToast(g_animations.load() ? L"Transitions enabled." : L"Transitions disabled."); return; }
        if (contains({cx, cy + 372, cw, 72}, lx, ly)) { g_inputOverlayVisible.store(!g_inputOverlayVisible.load()); saveSettings(); showToast(g_inputOverlayVisible.load() ? L"Input overlay enabled." : L"Input overlay hidden."); return; }
    } else if (page == PAGE_UTILITIES) {
        if (contains({cx, cy, col, 112}, lx, ly)) { takeScreenshot(); return; }
        if (contains({cx + col + gap, cy, col, 112}, lx, ly)) { openClientFolder(); return; }
        const int unlockY = cy + 412;
        if (contains({cx, unlockY, cw, 76}, lx, ly)) {
            if (!gdclient::native::unlockIconsHookReady()) { showToast(L"Unlock-icons preview is unavailable on this executable build."); return; }
            g_unlockIconsPreference.store(!g_unlockIconsPreference.load());
            gdclient::native::setUnlockIconsEnabled(g_unlockIconsPreference.load());
            saveSettings();
            showToast(gdclient::native::unlockIconsEnabled() ? L"Local icon/color unlock preview enabled." : L"Local icon/color unlock preview disabled.");
            return;
        }
    } else if (page == PAGE_SETTINGS) {
        if (contains({cx, cy, cw, 74}, lx, ly)) { g_animations.store(!g_animations.load()); saveSettings(); showToast(g_animations.load() ? L"Animations enabled." : L"Animations disabled."); return; }
        if (contains({cx, cy + 86, cw, 76}, lx, ly)) { g_clickerHoldToRun.store(!g_clickerHoldToRun.load()); saveSettings(); showToast(g_clickerHoldToRun.load() ? L"Hold-to-run enabled." : L"Hold-to-run disabled."); return; }
        if (contains({cx, cy + 174, cw, 80}, lx, ly)) { adjustOpacity(lx >= cx + cw - 64 ? 15 : -15); return; }
        if (contains({cx, cy + 266, cw, 74}, lx, ly)) {
            g_safeMode.store(!g_safeMode.load());
            if (g_safeMode.load()) showToast(L"Safe Mode enabled: native gameplay features are restricted to the Practice Guard.");
            else showToast(L"Safe Mode disabled. Gameplay changes can affect run legitimacy; no online filter is active.");
            syncNativeSettings(); saveSettings(); return;
        }
        const int yButtons = cy + 174 + 92 + 86;
        if (contains({cx, yButtons, 156, 34}, lx, ly)) { saveSettings(); showToast(L"Settings saved to AppData."); return; }
        if (contains({cx + 166, yButtons, 156, 34}, lx, ly)) { resetSettings(); return; }
        const int priorityY = yButtons + 44;
        if (contains({cx + cw - 294, priorityY + 37, 86, 27}, lx, ly)) { gdclient::native::setGameThreadPriorityLevel(0); saveSettings(); showToast(L"Game-thread priority set to Normal."); return; }
        if (contains({cx + cw - 198, priorityY + 37, 86, 27}, lx, ly)) { gdclient::native::setGameThreadPriorityLevel(1); saveSettings(); showToast(L"Game-thread priority set to Above Normal."); return; }
        if (contains({cx + cw - 102, priorityY + 37, 86, 27}, lx, ly)) { gdclient::native::setGameThreadPriorityLevel(2); saveSettings(); showToast(L"Game-thread priority set to High."); return; }
        const int keybindY = priorityY + 82;
        const int keybindColWidth = (cw - 28) / 2;
        for (int slot = BIND_MENU; slot <= BIND_FRAME_STEP_ONCE; ++slot) {
            const int index = slot - BIND_MENU;
            const int colIndex = index / 6;
            const int row = index % 6;
            const int colX = cx + 10 + colIndex * (keybindColWidth + 8);
            const int rowY = keybindY + 35 + row * 35;
            if (contains({colX + keybindColWidth - 83, rowY, 78, 29}, lx, ly)) {
                beginKeybindCapture(slot);
                return;
            }
        }
    }
}

int maxScrollForPage(int page) {
    switch (page) {
        case PAGE_CREATOR: return 80;
        case PAGE_PHYSICS: return 300;
        case PAGE_BYPASS: return static_cast<int>(gdclient::kFeatureCatalogSize) * 8 + 100;
        case PAGE_PRACTICE: return 204;
        case PAGE_SETTINGS: return 350;
        case PAGE_LABELS: return 0;
        default: return 0;
    }
}

LRESULT CALLBACK overlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: paintOverlay(hwnd); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCHITTEST: {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            if (g_menuOpen.load() && g_menuProgress > 0.015f) {
                const int slide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 20.0f) : 0;
                const int vslide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 12.0f) : 0;
                RectI panel{g_menuX - slide, g_menuY + vslide, g_drawnMenuW, g_drawnMenuH};
                if (contains(panel, p.x, p.y)) return HTCLIENT;
            }
            return HTTRANSPARENT;
        }
        case WM_LBUTTONDOWN: {
            if (g_menuOpen.load()) handleMenuClick(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            setOverlayClickThrough(!g_menuOpen.load());
            InvalidateRect(hwnd, nullptr, FALSE); return 0;
        }
        case WM_MOUSEWHEEL: {
            if (g_menuOpen.load()) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &pt);
                const int slide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 20.0f) : 0;
                const int vslide = g_animations.load() ? static_cast<int>((1.0f - g_menuProgress) * 12.0f) : 0;
                const int panelX = g_menuX - slide, panelY = g_menuY + vslide;
                const int page = g_page.load();
                if (pt.x >= panelX + 10 && pt.y >= panelY + 151 && pt.y <= panelY + g_drawnMenuH - 28) {
                    const int direction = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
                    g_pageScroll[page] = std::clamp(g_pageScroll[page] + direction * 54, 0, maxScrollForPage(page));
                    InvalidateRect(hwnd, nullptr, FALSE); return 0;
                }
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_MOUSEMOVE: {
            if (g_dragging && (wp & MK_LBUTTON)) {
                g_menuX = GET_X_LPARAM(lp) - g_dragOffsetX;
                g_menuY = GET_Y_LPARAM(lp) - g_dragOffsetY;
                RECT r{}; GetClientRect(hwnd, &r);
                g_menuX = std::clamp(g_menuX, 0, std::max(0, static_cast<int>(r.right) - MENU_W));
                g_menuY = std::clamp(g_menuY, 0, std::max(0, static_cast<int>(r.bottom) - MENU_H));
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: if (g_dragging) { g_dragging = false; ReleaseCapture(); } return 0;
        case WM_CAPTURECHANGED: g_dragging = false; return 0;
        default: return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool keyPressed(int key, bool& wasDown) {
    const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
    const bool pressed = down && !wasDown;
    wasDown = down;
    return pressed;
}

bool keyPressedDynamic(int key) {
    if (key <= 0 || key >= 256) return false;
    const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool& wasDown = g_dynamicWasDown[static_cast<std::size_t>(key)];
    const bool generatedByMacro = g_macroReplaying.load() &&
        std::find(g_replayKeysDown.begin(), g_replayKeysDown.end(), static_cast<DWORD>(key)) != g_replayKeysDown.end();
    const bool pressed = down && !wasDown && !generatedByMacro;
    wasDown = down;
    return pressed;
}

void clickerLoop() {
    while (g_running.load()) {
        // Use a keyboard modifier, not the physical mouse button: generated mouse-up
        // events would otherwise release the very button used as the hold condition.
        const bool holdSatisfied = !g_clickerHoldToRun.load() || ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0);
        // Generated clicker input must never feed back into a macro recording.
        const bool practicePermitted = !g_safeMode.load() || canUsePracticeFeatures();
        if (g_autoClicker.load() && practicePermitted && !g_macroRecording.load() && !g_macroReplaying.load() && holdSatisfied && !g_menuOpen.load() && GetForegroundWindow() == g_gameWindow) {
            clickAtCursor();
            int cps = g_cps.load(); if (cps < 1) cps = 1;
            Sleep(static_cast<DWORD>(1000 / cps));
        } else Sleep(25);
    }
}

DWORD WINAPI workerMain(void*) {
    loadSettings();
    loadMacro();
    g_sessionStarted.store(GetTickCount64());
    for (int i = 0; i < 150 && g_running.load(); ++i) {
        g_gameWindow = findGameWindow();
        if (g_gameWindow) break;
        Sleep(100);
    }
    if (!g_gameWindow) {
        OutputDebugStringW(L"GD Client: no suitable Geometry Dash top-level window found (console windows are excluded).\n");
        return 0;
    }
    wchar_t boundTitle[512]{}; GetWindowTextW(g_gameWindow, boundTitle, _countof(boundTitle));
    OutputDebugStringW((std::wstring(L"GD Client: bound overlay to window: ") + boundTitle + L"\n").c_str());

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = overlayWndProc;
    wc.hInstance = g_self; wc.lpszClassName = L"GDClientGDIOverlay";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        OutputDebugStringW(L"GD Client: RegisterClassEx failed.\n"); return 0;
    }
    g_overlayWindow = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"GD Client Overlay", WS_POPUP, 0, 0, 800, 600, nullptr, nullptr, g_self, nullptr);
    if (!g_overlayWindow) {
        OutputDebugStringW(L"GD Client: overlay window creation failed.\n"); return 0;
    }
    SetLayeredWindowAttributes(g_overlayWindow, KEY_COLOR, static_cast<BYTE>(g_overlayAlpha.load()), LWA_COLORKEY | LWA_ALPHA);
    const bool nativeReady = gdclient::native::initialize();
    if (nativeReady) {
        syncNativeSettings();
        showToast(L"Native game hooks installed for a verified build.");
    } else {
        showToast(L"Unsupported game build: native gameplay features are disabled.");
    }
    std::thread(clickerLoop).detach();

    bool highResolutionTimer = false;
    bool oldPageUp = false, oldPageDown = false;
    bool macroKeys[256]{}; bool macroLeft = false, macroRight = false;
    RECT lastRect{}; bool hasLastRect = false;
    ULONGLONG lastFrame = GetTickCount64();
    ULONGLONG lastInvalidate = 0;
    ULONGLONG lastLabelClockRefresh = 0;
    bool repaintPending = true;
    BYTE lastAlpha = static_cast<BYTE>(g_overlayAlpha.load());
    bool overlayVisible = false;
    bool wasMenuOpen = false;
    bool wasHudVisible = g_showHud.load();
    bool wasSpeedEnabled = g_speedEnabled.load();
    bool wasAutoClicker = g_autoClicker.load();
    bool wasMacroRecording = g_macroRecording.load();
    bool wasMacroReplaying = g_macroReplaying.load();
    bool wasMacroLoop = g_macroLoop.load();
    bool wasPracticeGuard = g_practiceGuard.load();
    bool wasLabelsVisible = g_levelLabelsVisible.load();
    int wasMacroCount = g_macroCount.load();
    int wasPage = g_page.load();
    int wasAccent = g_accentIndex.load();
    int wasCps = g_cps.load();
    int wasHardwareCps = g_hardwareCps.load();
    bool wasInputOverlayVisible = g_inputOverlayVisible.load();
    bool wasSpaceHeld = g_spaceHeld.load(), wasUpHeld = g_upHeld.load();
    bool wasWHeld = g_wHeld.load(), wasAHeld = g_aHeld.load(), wasSHeld = g_sHeld.load(), wasDHeld = g_dHeld.load();
    bool wasLeftMouseHeld = g_leftMouseHeld.load(), wasRightMouseHeld = g_rightMouseHeld.load();
    ULONGLONG lastInputOverlayRefresh = 0;
    bool wasUnlockIcons = gdclient::native::unlockIconsEnabled();
    bool wasNativeMacroRecording = gdclient::native::nativeMacroStats().recording;
    bool wasNativeMacroPlaying = gdclient::native::nativeMacroStats().playing;
    std::uint64_t wasNativeMacroEventCount = gdclient::native::nativeMacroStats().eventCount;
    double wasSpeedMultiplier = g_speedMultiplier.load();
    {
        std::wstring ready = L"GD Client: overlay ready. Menu=" + hotkeyName(g_bindMenu.load()) +
            L", speed=" + hotkeyName(g_bindSpeed.load()) + L", screenshot=" + hotkeyName(g_bindScreenshot.load()) +
            L". Configure shortcuts in Settings. Native practice macro requires a verified build.\n";
        OutputDebugStringW(ready.c_str());
    }

    while (g_running.load() && IsWindow(g_gameWindow)) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }

        HWND foreground = GetForegroundWindow();
        const bool gameActive = foreground == g_gameWindow || GetAncestor(foreground, GA_ROOT) == g_gameWindow;
        if (gameActive) {
            const bool wasCapturingKey = g_captureBindFor.load() != 0;
            if (wasCapturingKey) processKeybindCapture();
            if (!wasCapturingKey && keyPressedDynamic(g_bindMenu.load())) {
                g_menuOpen.store(!g_menuOpen.load());
                setOverlayClickThrough(!g_menuOpen.load());
                showToast(g_menuOpen.load() ? L"Menu opened." : L"Menu closed.");
                // Opening the menu mid-run must not silently corrupt the macro timeline.
                if (g_menuOpen.load() && g_macroRecording.load()) {
                    stopMacroRecording();
                    showToast(L"Macro recording stopped because the menu was opened.");
                }
                if (g_menuOpen.load() && g_macroReplaying.load()) {
                    stopMacroPlayback();
                    showToast(L"Macro playback stopped because the menu was opened.");
                }
            }
            if (!wasCapturingKey && keyPressedDynamic(g_bindSpeed.load())) {
                if (hasTimingHook()) { toggleSpeed(); saveSettings(); showToast(g_speedEnabled.load() ? L"Speedhack enabled." : L"Speedhack disabled."); }
                else showToast(L"No supported game delta-time hook; speedhack unavailable.");
            }
            if (!wasCapturingKey && keyPressedDynamic(g_bindSpeedCycle.load())) { cycleSpeed(); saveSettings(); showToast(L"Speed preset changed."); }
            if (!wasCapturingKey && keyPressedDynamic(g_bindClicker.load())) {
                const bool enable = !g_autoClicker.load();
                if (!enable || gameplayModeAllowed(L"Auto clicker")) {
                    g_autoClicker.store(enable);
                    showToast(enable ? L"Auto clicker enabled." : L"Auto clicker disabled.");
                }
            }
            if (!wasCapturingKey && keyPressedDynamic(g_bindHud.load())) { g_showHud.store(!g_showHud.load()); saveSettings(); showToast(g_showHud.load() ? L"Status chip enabled." : L"Status chip hidden."); }
            if (!wasCapturingKey && keyPressedDynamic(g_bindScreenshot.load())) takeScreenshot();
            if (!wasCapturingKey && keyPressedDynamic(g_bindMacroRecord.load())) { if (g_macroRecording.load()) stopMacroRecording(); else startMacroRecording(); }
            if (!wasCapturingKey && keyPressedDynamic(g_bindMacroPlay.load())) { if (g_macroReplaying.load()) stopMacroPlayback(); else startMacroPlayback(); }
            if (!wasCapturingKey && keyPressedDynamic(g_bindMacroStop.load())) { stopMacroPlayback(); showToast(L"Macro playback stopped."); }
            if (!wasCapturingKey && keyPressedDynamic(g_bindFrameStep.load())) {
                if (!gdclient::native::frameStepEnabled()) {
                    if (!gdclient::native::inLevel()) showToast(L"Frame step requires an active supported level.");
                    else if (!gameplayModeAllowed(L"Frame stepping")) { /* blocked by local Safe Mode */ }
                    else {
                        gdclient::native::setFrameStepEnabled(true);
                        std::wstring stepToast = L"Frame step enabled and paused. " + hotkeyName(g_bindFrameStepOnce.load()) + L" advances one update.";
                        showToast(stepToast.c_str());
                    }
                } else {
                    gdclient::native::setFrameStepPaused(!gdclient::native::frameStepPaused());
                    showToast(gdclient::native::frameStepPaused() ? L"Frame step paused." : L"Frame step running.");
                }
            }
            if (!wasCapturingKey && keyPressedDynamic(g_bindFrameStepOnce.load()) && !gdclient::native::advanceOneFrame()) {
                std::wstring stepHint = L"Enable and pause frame step with " + hotkeyName(g_bindFrameStep.load()) + L" first.";
                showToast(stepHint.c_str());
            }
            updateInputTelemetry();
            if (!wasCapturingKey && g_menuOpen.load()) {
                const int activePage = g_page.load();
                if (keyPressed(VK_NEXT, oldPageDown)) { g_pageScroll[activePage] = std::clamp(g_pageScroll[activePage] + 140, 0, maxScrollForPage(activePage)); InvalidateRect(g_overlayWindow, nullptr, FALSE); }
                if (keyPressed(VK_PRIOR, oldPageUp)) { g_pageScroll[activePage] = std::clamp(g_pageScroll[activePage] - 140, 0, maxScrollForPage(activePage)); InvalidateRect(g_overlayWindow, nullptr, FALSE); }
            } else {
                oldPageUp = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
                oldPageDown = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
            }
            if (!wasCapturingKey) {
                updateMacroRecordTimeline();
                scanMacroInput(macroKeys, macroLeft, macroRight);
                advanceMacroPlayback();
            }
        } else {
            if (g_macroRecording.load()) {
                stopMacroRecording();
                showToast(L"Macro recording stopped because Geometry Dash lost focus.");
            }
            if (g_captureBindFor.exchange(0) != 0) showToast(L"Keybind capture canceled because Geometry Dash lost focus.");
            for (int key = 1; key < 256; ++key)
                g_dynamicWasDown[static_cast<std::size_t>(key)] = (GetAsyncKeyState(key) & 0x8000) != 0;
            updateInputTelemetry();
            oldPageUp = (GetAsyncKeyState(VK_PRIOR) & 0x8000) != 0;
            oldPageDown = (GetAsyncKeyState(VK_NEXT) & 0x8000) != 0;
            if (g_macroReplaying.load()) stopMacroPlayback();
            updateMacroRecordTimeline();
        }

        // Enforce the local Practice-only guard continuously. When a supported build
        // reports that Practice Mode ended, clear active gameplay changes immediately.
        if (g_safeMode.load() && !canUsePracticeFeatures()) {
            bool turnedOff = false;
            if (g_speedEnabled.exchange(false)) { gdclient::native::setSpeedScale(1.0); turnedOff = true; }
            if (gdclient::native::noclipEnabled()) { gdclient::native::setNoclipEnabled(false); turnedOff = true; }
            if (gdclient::native::hitboxesEnabled()) { gdclient::native::setHitboxesEnabled(false); turnedOff = true; }
            if (gdclient::native::frameStepEnabled()) { gdclient::native::setFrameStepEnabled(false); turnedOff = true; }
            if (gdclient::native::physicsTps() != 0) { gdclient::native::setPhysicsTps(0, false); turnedOff = true; }
            if (g_autoClicker.exchange(false)) turnedOff = true;
            const auto nm = gdclient::native::nativeMacroStats();
            if (nm.recording) { gdclient::native::stopNativeMacroRecording(); turnedOff = true; }
            if (nm.playing) { gdclient::native::stopNativeMacroPlayback(); turnedOff = true; }
            if (g_macroRecording.load()) { stopMacroRecording(); turnedOff = true; }
            if (g_macroReplaying.load()) { stopMacroPlayback(); turnedOff = true; }
            if (turnedOff) { showToast(L"Safe Mode disabled active gameplay features outside confirmed Practice Mode."); saveSettings(); }
        }
        // The native TPS API always requires an active level and a manual Practice
        // confirmation. Re-apply the preference after PlayLayer resets it on level load.
        if (canUsePracticeFeatures() && g_physicsTpsPreference.load() > 0 &&
            gdclient::native::physicsTps() != g_physicsTpsPreference.load()) {
            gdclient::native::setPhysicsTps(g_physicsTpsPreference.load(), g_practiceGuard.load());
        }

        const ULONGLONG now = GetTickCount64();
        const float dt = static_cast<float>(std::min<ULONGLONG>(now - lastFrame, 100)) / 1000.0f;
        lastFrame = now;
        bool needsRepaint = false;
        const float oldMenuProgress = g_menuProgress;
        const float oldPageProgress = g_pageProgress;
        const float target = g_menuOpen.load() ? 1.0f : 0.0f;
        if (!g_animations.load()) {
            g_menuProgress = target;
            g_pageProgress = 1.0f;
        } else {
            const float blendFactor = std::min(1.0f, dt * 15.0f);
            g_menuProgress += (target - g_menuProgress) * blendFactor;
            if (std::abs(target - g_menuProgress) < 0.008f) g_menuProgress = target;
            if (g_pageProgress < 1.0f) g_pageProgress = std::min(1.0f, g_pageProgress + dt * 7.5f);
        }
        if (std::abs(oldMenuProgress - g_menuProgress) > 0.0005f ||
            std::abs(oldPageProgress - g_pageProgress) > 0.0005f)
            needsRepaint = true;
        if (!g_toast.empty() && now - g_toastStarted > 2200) {
            g_toast.clear();
            needsRepaint = true;
        }
        if (g_levelLabelsVisible.load() && now - lastLabelClockRefresh >= 1000) {
            for (const auto& label : g_levelLabels) {
                if (label.enabled && label.text.find(L"{SESSION}") != std::wstring::npos) {
                    needsRepaint = true;
                    break;
                }
            }
            lastLabelClockRefresh = now;
        }
        if (wasMenuOpen != g_menuOpen.load() || wasHudVisible != g_showHud.load() ||
            wasSpeedEnabled != g_speedEnabled.load() || wasAutoClicker != g_autoClicker.load() ||
            wasMacroRecording != g_macroRecording.load() || wasMacroReplaying != g_macroReplaying.load() ||
            wasMacroLoop != g_macroLoop.load() || wasPracticeGuard != g_practiceGuard.load() ||
            wasLabelsVisible != g_levelLabelsVisible.load() || wasMacroCount != g_macroCount.load() ||
            wasPage != g_page.load() || wasAccent != g_accentIndex.load() ||
            wasCps != g_cps.load() || wasHardwareCps != g_hardwareCps.load() ||
            wasInputOverlayVisible != g_inputOverlayVisible.load() ||
            wasSpaceHeld != g_spaceHeld.load() || wasUpHeld != g_upHeld.load() ||
            wasWHeld != g_wHeld.load() || wasAHeld != g_aHeld.load() || wasSHeld != g_sHeld.load() || wasDHeld != g_dHeld.load() ||
            wasLeftMouseHeld != g_leftMouseHeld.load() || wasRightMouseHeld != g_rightMouseHeld.load() ||
            wasUnlockIcons != gdclient::native::unlockIconsEnabled() ||
            wasNativeMacroRecording != gdclient::native::nativeMacroStats().recording ||
            wasNativeMacroPlaying != gdclient::native::nativeMacroStats().playing ||
            wasNativeMacroEventCount != gdclient::native::nativeMacroStats().eventCount ||
            std::abs(wasSpeedMultiplier - g_speedMultiplier.load()) > 0.001 ||
            (g_menuOpen.load() && g_page.load() == PAGE_PHYSICS))
            needsRepaint = true;
        wasMenuOpen = g_menuOpen.load();
        wasHudVisible = g_showHud.load();
        wasSpeedEnabled = g_speedEnabled.load();
        wasAutoClicker = g_autoClicker.load();
        wasMacroRecording = g_macroRecording.load();
        wasMacroReplaying = g_macroReplaying.load();
        wasMacroLoop = g_macroLoop.load();
        wasPracticeGuard = g_practiceGuard.load();
        wasLabelsVisible = g_levelLabelsVisible.load();
        wasMacroCount = g_macroCount.load();
        wasPage = g_page.load();
        wasAccent = g_accentIndex.load();
        wasCps = g_cps.load();
        wasHardwareCps = g_hardwareCps.load();
        wasInputOverlayVisible = g_inputOverlayVisible.load();
        wasSpaceHeld = g_spaceHeld.load(); wasUpHeld = g_upHeld.load();
        wasWHeld = g_wHeld.load(); wasAHeld = g_aHeld.load(); wasSHeld = g_sHeld.load(); wasDHeld = g_dHeld.load();
        wasLeftMouseHeld = g_leftMouseHeld.load(); wasRightMouseHeld = g_rightMouseHeld.load();
        wasUnlockIcons = gdclient::native::unlockIconsEnabled();
        const auto nativeMacroNow = gdclient::native::nativeMacroStats();
        wasNativeMacroRecording = nativeMacroNow.recording;
        wasNativeMacroPlaying = nativeMacroNow.playing;
        wasNativeMacroEventCount = nativeMacroNow.eventCount;
        wasSpeedMultiplier = g_speedMultiplier.load();

        RECT gameRect{}; POINT origin{0, 0};
        GetClientRect(g_gameWindow, &gameRect); ClientToScreen(g_gameWindow, &origin);
        const int width = gameRect.right - gameRect.left, height = gameRect.bottom - gameRect.top;
        if (width > 0 && height > 0) {
            RECT next{origin.x, origin.y, origin.x + width, origin.y + height};
            if (!hasLastRect || std::memcmp(&next, &lastRect, sizeof(RECT)) != 0) {
                SetWindowPos(g_overlayWindow, HWND_TOPMOST, next.left, next.top, width, height, SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                lastRect = next; hasLastRect = true;
                g_menuX = std::clamp(g_menuX, 0, std::max(0, width - MENU_W));
                g_menuY = std::clamp(g_menuY, 0, std::max(0, height - MENU_H));
                needsRepaint = true;
            }
            setOverlayClickThrough(!g_menuOpen.load());
        }
        if (gameActive && g_inputOverlayVisible.load() && (now - lastInputOverlayRefresh >= 50)) {
            needsRepaint = true;
            lastInputOverlayRefresh = now;
        }
        const BYTE alpha = static_cast<BYTE>(g_overlayAlpha.load());
        if (alpha != lastAlpha) {
            SetLayeredWindowAttributes(g_overlayWindow, KEY_COLOR, alpha, LWA_COLORKEY | LWA_ALPHA);
            lastAlpha = alpha;
            needsRepaint = true;
        }
        if (gameActive != overlayVisible) {
            ShowWindow(g_overlayWindow, gameActive ? SW_SHOWNOACTIVATE : SW_HIDE);
            overlayVisible = gameActive;
            if (gameActive) needsRepaint = true;
        }
        // Do not force a full paint every 10 ms. Animation frames are capped at ~60 Hz,
        // and an idle/closed overlay is repainted only after a real state or window event.
        repaintPending = repaintPending || needsRepaint;
        if (repaintPending && (now - lastInvalidate >= 16)) {
            InvalidateRect(g_overlayWindow, nullptr, FALSE);
            lastInvalidate = now;
            repaintPending = false;
        }
        const bool macroNeedsPrecision = g_macroRecording.load() || g_macroReplaying.load();
        if (macroNeedsPrecision && !highResolutionTimer) {
            if (timeBeginPeriod(1) == TIMERR_NOERROR) highResolutionTimer = true;
        } else if (!macroNeedsPrecision && highResolutionTimer) {
            timeEndPeriod(1);
            highResolutionTimer = false;
        }
        Sleep(macroNeedsPrecision ? 1 : 8);
    }
    if (highResolutionTimer) timeEndPeriod(1);
    if (g_labelEditor && IsWindow(g_labelEditor)) DestroyWindow(g_labelEditor);
    if (g_macroRecording.load()) stopMacroRecording();
    if (g_macroReplaying.load()) stopMacroPlayback();
    if (g_speedEnabled.load()) setSpeedScale(1.0);
    gdclient::native::shutdown();
    destroyBackBuffer();
    if (g_overlayWindow && IsWindow(g_overlayWindow)) DestroyWindow(g_overlayWindow);
    g_overlayWindow = nullptr;
    return 0;
}
} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(nullptr, 0, workerMain, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false);
    }
    return TRUE;
}
