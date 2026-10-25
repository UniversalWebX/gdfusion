#!/usr/bin/env python3
"""Custom Geometry Dash DLL client builder/injector (Windows x64).

Builds the local C++ DLL with CMake + Visual Studio Build Tools and injects it
into an already-running GeometryDash.exe using LoadLibraryW/CreateRemoteThread.
No Geode SDK or runtime is used.
"""
from __future__ import annotations
import argparse
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import sys
import time
from clientcore.profiles import export_profile, import_profile, settings_file_path

ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
GAME_NAMES = {"geometrydash.exe", "geometry dash.exe"}

if os.name == "nt":
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
else:
    kernel32 = None

TH32CS_SNAPPROCESS = 0x00000002
PROCESS_ALL_FOR_INJECT = 0x043A
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
MEM_RELEASE = 0x8000
PAGE_READWRITE = 0x04
WAIT_OBJECT_0 = 0

class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD), ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD), ("th32DefaultHeapID", ctypes.c_size_t),
        ("th32ModuleID", wintypes.DWORD), ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD), ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD), ("szExeFile", wintypes.WCHAR * 260),
    ]


def require_windows() -> None:
    if os.name != "nt":
        raise RuntimeError("This tool only runs on Windows 10/11.")
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError("Use 64-bit Python. Geometry Dash 2.2 requires an x64 DLL build.")


def find_game_processes() -> list[tuple[int, str]]:
    require_windows()
    kernel32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    kernel32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel32.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    kernel32.Process32FirstW.restype = wintypes.BOOL
    kernel32.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32W)]
    kernel32.Process32NextW.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel32.CloseHandle.restype = wintypes.BOOL
    snap = kernel32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    results: list[tuple[int, str]] = []
    entry = PROCESSENTRY32W()
    entry.dwSize = ctypes.sizeof(entry)
    try:
        ok = kernel32.Process32FirstW(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() in GAME_NAMES:
                results.append((int(entry.th32ProcessID), entry.szExeFile))
            ok = kernel32.Process32NextW(snap, ctypes.byref(entry))
    finally:
        kernel32.CloseHandle(snap)
    return results


def find_dll() -> Path:
    candidates = [
        BUILD_DIR / "Release" / "GDClient.dll",
        BUILD_DIR / "GDClient.dll",
        ROOT / "out" / "GDClient.dll",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    built = list(BUILD_DIR.rglob("GDClient.dll")) if BUILD_DIR.exists() else []
    built = [p for p in built if p.is_file()]
    if built:
        return max(built, key=lambda p: p.stat().st_mtime).resolve()
    raise FileNotFoundError("GDClient.dll not found. Build the project first with --build.")


def build_dll() -> Path:
    require_windows()
    if not (ROOT / "CMakeLists.txt").exists():
        raise FileNotFoundError("CMakeLists.txt missing. Run this script from the extracted GDClient folder.")
    cmake = __import__("shutil").which("cmake")
    if not cmake:
        raise RuntimeError("CMake was not found. Install CMake and Visual Studio's Desktop development with C++ workload.")
    generators = ["Visual Studio 18 2026", "Visual Studio 17 2022", "Visual Studio 16 2019"]
    # Reuse the generator cached by CMake first; switching generators in an existing
    # build directory causes a harmless-but-confusing cache mismatch on every run.
    cache_file = BUILD_DIR / "CMakeCache.txt"
    if cache_file.is_file():
        try:
            for line in cache_file.read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith("CMAKE_GENERATOR:INTERNAL="):
                    cached_generator = line.split("=", 1)[1].strip()
                    if cached_generator in generators:
                        generators.remove(cached_generator)
                        generators.insert(0, cached_generator)
                    break
        except OSError:
            pass
    last_error: Exception | None = None
    for generator in generators:
        cmd = [cmake, "-S", str(ROOT), "-B", str(BUILD_DIR), "-G", generator, "-A", "x64"]
        print("\n[BUILD]", " ".join(f'"{part}"' if " " in part else part for part in cmd))
        result = subprocess.run(cmd, cwd=ROOT, check=False)
        if result.returncode == 0:
            last_error = None
            break
        last_error = RuntimeError(f"CMake configuration failed using {generator} (exit {result.returncode}).")
        # Clear partial generator state before trying the next generator.
        cache = BUILD_DIR / "CMakeCache.txt"
        if cache.exists():
            try: cache.unlink()
            except OSError: pass
        for makefile in (BUILD_DIR / "Makefile", BUILD_DIR / "build.ninja"):
            if makefile.exists():
                try: makefile.unlink()
                except OSError: pass
    if last_error:
        raise RuntimeError(str(last_error) + " Install a compatible Visual Studio C++ toolchain and CMake.")
    cmd = [cmake, "--build", str(BUILD_DIR), "--config", "Release", "--target", "GDClient", "--parallel"]
    print("\n[BUILD]", " ".join(f'"{part}"' if " " in part else part for part in cmd))
    result = subprocess.run(cmd, cwd=ROOT, check=False)
    if result.returncode != 0:
        raise RuntimeError(f"C++ build failed (exit {result.returncode}). See output above.")
    dll = find_dll()
    print(f"\n[OK] Built: {dll}")
    return dll


def inject_dll(pid: int, dll: Path) -> None:
    require_windows()
    dll = dll.resolve()
    if not dll.is_file():
        raise FileNotFoundError(f"DLL not found: {dll}")

    # Explicit API signatures keep pointer sizes correct on 64-bit Python.
    kernel32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
    kernel32.GetModuleHandleW.restype = wintypes.HMODULE
    kernel32.GetProcAddress.argtypes = [wintypes.HMODULE, ctypes.c_char_p]
    kernel32.GetProcAddress.restype = ctypes.c_void_p
    kernel32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel32.GetExitCodeThread.restype = wintypes.BOOL
    kernel32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.VirtualAllocEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
    kernel32.VirtualAllocEx.restype = ctypes.c_void_p
    kernel32.WriteProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel32.WriteProcessMemory.restype = wintypes.BOOL
    kernel32.VirtualFreeEx.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
    kernel32.VirtualFreeEx.restype = wintypes.BOOL
    kernel32.CreateRemoteThread.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
    kernel32.CreateRemoteThread.restype = wintypes.HANDLE
    kernel32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel32.WaitForSingleObject.restype = wintypes.DWORD

    process = kernel32.OpenProcess(PROCESS_ALL_FOR_INJECT, False, pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error(), f"OpenProcess failed for PID {pid}. Try running Python at the same privilege level as Geometry Dash.")
    remote_mem = None
    thread = None
    try:
        path_buffer = ctypes.create_unicode_buffer(str(dll))
        byte_count = ctypes.sizeof(path_buffer)
        remote_mem = kernel32.VirtualAllocEx(process, None, byte_count, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)
        if not remote_mem:
            raise ctypes.WinError(ctypes.get_last_error(), "VirtualAllocEx failed")
        written = ctypes.c_size_t(0)
        if not kernel32.WriteProcessMemory(process, remote_mem, ctypes.byref(path_buffer), byte_count, ctypes.byref(written)) or written.value != byte_count:
            raise ctypes.WinError(ctypes.get_last_error(), "WriteProcessMemory failed")

        kernel = kernel32.GetModuleHandleW("kernel32.dll")
        load_library = kernel32.GetProcAddress(kernel, b"LoadLibraryW")
        if not load_library:
            raise ctypes.WinError(ctypes.get_last_error(), "GetProcAddress(LoadLibraryW) failed")
        thread = kernel32.CreateRemoteThread(process, None, 0, ctypes.c_void_p(load_library), remote_mem, 0, None)
        if not thread:
            raise ctypes.WinError(ctypes.get_last_error(), "CreateRemoteThread failed")
        wait_result = kernel32.WaitForSingleObject(thread, 15000)
        if wait_result != WAIT_OBJECT_0:
            raise RuntimeError("Remote LoadLibrary thread did not finish within 15 seconds.")
        exit_code = wintypes.DWORD(0)
        if kernel32.GetExitCodeThread(thread, ctypes.byref(exit_code)) and exit_code.value == 0:
            raise RuntimeError("LoadLibraryW returned NULL. Windows could not load GDClient.dll; check the DLL path and architecture.")
        print(f"[OK] DLL load thread completed for PID {pid}.")
        print("      In Geometry Dash: TAB menu, F2 speed, F3 preset, F4 clicker, F5 HUD, F6 screenshot, F7 record, F8 replay, F9 stop.")
    finally:
        if thread:
            kernel32.CloseHandle(thread)
        if remote_mem:
            kernel32.VirtualFreeEx(process, remote_mem, 0, MEM_RELEASE)
        kernel32.CloseHandle(process)


def enumerate_game_windows(pids: set[int]) -> list[dict[str, object]]:
    """Return visible top-level windows for candidate GD processes.

    Console windows are included as diagnostics but heavily de-prioritized. The
    native DLL independently applies the same Geometry Dash-title preference.
    """
    if os.name != "nt":
        return []
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetWindow.argtypes = [wintypes.HWND, wintypes.UINT]
    user32.GetWindow.restype = wintypes.HWND
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    user32.GetClassNameW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user32.GetClassNameW.restype = ctypes.c_int
    user32.GetClientRect.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.RECT)]
    user32.GetClientRect.restype = wintypes.BOOL

    windows: list[dict[str, object]] = []
    enum_proc_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    def callback(hwnd: int, _lparam: int) -> bool:
        if not user32.IsWindowVisible(hwnd):
            return True
        pid = wintypes.DWORD(0)
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if int(pid.value) not in pids:
            return True
        title_buffer = ctypes.create_unicode_buffer(512)
        class_buffer = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title_buffer, len(title_buffer))
        user32.GetClassNameW(hwnd, class_buffer, len(class_buffer))
        rect = wintypes.RECT()
        if not user32.GetClientRect(hwnd, ctypes.byref(rect)):
            return True
        width = max(0, int(rect.right - rect.left))
        height = max(0, int(rect.bottom - rect.top))
        title = title_buffer.value.strip()
        class_name = class_buffer.value.strip()
        lower_title = title.lower()
        lower_class = class_name.lower()
        is_console = lower_class == "consolewindowclass" or "console" in lower_title or "debug output" in lower_title
        score = width * height
        if "geometry dash" in lower_title:
            score += 10**12
        if is_console:
            score -= 10**11
        if width < 480 or height < 300:
            score -= 10**10
        windows.append({
            "pid": int(pid.value), "hwnd": int(hwnd), "title": title or "(untitled window)",
            "class": class_name, "width": width, "height": height,
            "score": score, "is_console": is_console,
        })
        return True

    callback_ref = enum_proc_type(callback)
    user32.EnumWindows.argtypes = [enum_proc_type, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.EnumWindows(callback_ref, 0)
    return windows


def choose_process() -> int:
    while True:
        found = find_game_processes()
        if found:
            pids = {pid for pid, _name in found}
            windows = enumerate_game_windows(pids)
            best_by_pid: dict[int, dict[str, object]] = {}
            for window in windows:
                pid = int(window["pid"])
                if pid not in best_by_pid or int(window["score"]) > int(best_by_pid[pid]["score"]):
                    best_by_pid[pid] = window

            print("\n[WINDOW SCAN] Candidate Geometry Dash processes:")
            for pid, name in found:
                win = best_by_pid.get(pid)
                if win:
                    marker = " [CONSOLE CANDIDATE]" if win["is_console"] else ""
                    print(f"  PID {pid:<7} {name:<20} Window={win['title']!r} "
                          f"Class={win['class']!r} Size={win['width']}x{win['height']}{marker}")
                else:
                    print(f"  PID {pid:<7} {name:<20} No visible game-sized window found")

            # If multiple same-named executables exist, pick the unique process
            # with a real Geometry Dash caption rather than a launcher/console.
            title_matches = {
                int(w["pid"]) for w in windows
                if "geometry dash" in str(w["title"]).lower()
                and not bool(w["is_console"])
                and int(w["width"]) >= 480 and int(w["height"]) >= 300
            }
            if len(found) == 1:
                print(f"[FOUND] Using PID {found[0][0]}; the injected DLL will bind to the best matching non-console game window.")
                return found[0][0]
            if len(title_matches) == 1:
                selected_pid = next(iter(title_matches))
                print(f"[FOUND] Automatically selected Geometry Dash window in PID {selected_pid}.")
                return selected_pid

            print("\nSelect the PID whose window title is the actual Geometry Dash game, not the Geode console:")
            for i, (pid, name) in enumerate(found, 1):
                win = best_by_pid.get(pid)
                desc = f" — {win['title']} ({win['width']}x{win['height']})" if win else " — no window"
                print(f"  {i}. {name} | PID {pid}{desc}")
            raw = input("Process number: ").strip()
            try:
                idx = int(raw) - 1
                if 0 <= idx < len(found):
                    return found[idx][0]
            except ValueError:
                pass
            print("Invalid selection. Try again.")
        else:
            input("[WAITING] Start Geometry Dash normally, leave it open, then press Enter here...")
            time.sleep(0.5)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and inject the custom Geometry Dash client (no Geode).")
    parser.add_argument("--build", action="store_true", help="Build the native DLL, then exit.")
    parser.add_argument("--inject-only", action="store_true", help="Skip compilation and inject an existing GDClient.dll.")
    parser.add_argument("--pid", type=int, help="Inject into a specific running Geometry Dash process ID.")
    profile_group = parser.add_mutually_exclusive_group()
    profile_group.add_argument("--export-profile", metavar="FILE.json", help="Export current settings.ini to a portable JSON profile.")
    profile_group.add_argument("--import-profile", metavar="FILE.json", help="Import a validated JSON profile into settings.ini.")
    args = parser.parse_args()
    try:
        if args.export_profile:
            destination = export_profile(settings_file_path(), args.export_profile)
            print(f"[OK] Profile exported: {destination}")
            return 0
        if args.import_profile:
            destination = import_profile(settings_file_path(), args.import_profile)
            print(f"[OK] Profile imported into: {destination}")
            print("Restart GD Client or relaunch Geometry Dash for all persisted settings to reload.")
            return 0
        require_windows()
        print("=" * 56)
        print(" GD CLIENT // CUSTOM NATIVE DLL // NO GEODE")
        print("=" * 56)
        if args.inject_only:
            dll = find_dll()
        else:
            dll = build_dll()
        if args.build:
            return 0
        pid = args.pid if args.pid else choose_process()
        inject_dll(pid, dll)
        return 0
    except KeyboardInterrupt:
        print("\nCancelled.")
        return 130
    except Exception as exc:
        print(f"\n[ERROR] {exc}")
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
