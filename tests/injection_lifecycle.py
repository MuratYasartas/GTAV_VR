#!/usr/bin/env python3
"""Injection-lifecycle harness for the GTAVR mod (test-matrix A4, Phase 9).

Loops N times (default 20; --count 100 for the full mission run):

  1. start the D3D11Cube slice (samples/D3D11Cube/x64/Release/D3D11Cube.exe)
     with GTAVR_LOG_DIR=<fresh temp dir for this run>
  2. run GTAVOVR/x64/Release/GTAVOVR.exe with GTAVR_TARGET_PROCESS=D3D11Cube.exe
     (it CreateRemoteThread-injects OVRInject.dll from the staging dir)
  3. watch the injected mod's log stream for a terminal marker:
       - "Hooked Present at vtable index"          -> hook-success
         (followed by a VR-init outcome line; on a machine with no live
         headset the expected outcome is "Failed to initialize VR", i.e.
         hooked + inert-VR pass-through, NOT a crash)
       - "No D3D11 swapchain hooked within 30 s"   -> clean inert
         (late injection: the cube's swapchain predates the mod hooks)
       - "Failed to hook Present - mod stays inert"-> clean inert (flagged)
  4. terminate the cube process tree and confirm it dies within 10 s
     (a hang on exit is a failure).

Modes (--mode):
  cube-first     (default, mission spec) harness starts the cube, then the
                 launcher injects into the running process. The cube usually
                 creates its swapchain first, so runs commonly end as
                 clean-inert via the late-injection watchdog - a valid,
                 production-relevant terminal state.
  launcher-first the launcher itself starts the cube (production GTA V flow:
                 GTAV_INSTALL_DIR staged per run with the cube exe), then
                 injects before the swapchain exists -> exercises the
                 hook-success path.
  shim           dxgi-proxy load path (ADR-0005, the shipped mechanism): the
                 OVRInjectShim is staged beside a cube copy as dxgi.dll and
                 loads the mod during the cube's DLL resolution, before any
                 swapchain exists. The shim only injects into GTA5.exe, so
                 the cube copy is renamed; the shim's LOGWNDF MessageBox is
                 auto-dismissed. Deterministic hook-success path.
  mixed          cycles cube-first / launcher-first / shim per run.

Log capture note: OVRInject keeps gtavrInjectLog.txt open without read
sharing, so it cannot be tailed while the game lives. Every log line is
also mirrored to OutputDebugStringA (see OVRInject/Log.cpp), which this
harness captures live via the Win32 DBWIN protocol (ctypes only). After
the cube dies the on-disk log is read again as a fallback/consistency
check. Note: only one DBWIN listener can exist per Windows session - do
not run DebugView (or a second harness) at the same time.

A run is a failure if the cube crashes (CrashDump fatal-exception line or a
gtavr_crash_*.dmp file), hangs on exit, times out without a terminal marker,
or the launcher fails to inject. Exit code is 0 only when every run is
hook-success or clean-inert with zero crashes/hangs/timeouts.

Results (per-run status + summary) are written to tests/lifecycle_report.txt.

Prerequisites: Release|x64 builds of GTAVOVR, OVRInject and the D3D11Cube
sample. Python 3.8+, standard library only.
"""

import argparse
import ctypes
from ctypes import wintypes
import datetime
import glob
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

CUBE_EXE = os.path.join(ROOT, "samples", "D3D11Cube", "x64", "Release", "D3D11Cube.exe")
CUBE_NAME = "D3D11Cube.exe"
LAUNCHER_CANDIDATES = [
    os.path.join(ROOT, "x64", "Release", "GTAVOVR.exe"),
    os.path.join(ROOT, "GTAVOVR", "x64", "Release", "GTAVOVR.exe"),
]
OPENVR_DLL = os.path.join(ROOT, "ThirdParty", "openvr", "bin", "x64", "openvr_api.dll")
OPENXR_DLL = os.path.join(ROOT, "ThirdParty", "openxr", "bin", "x64", "openxr_loader.dll")
OVRINJECT_CANDIDATES = [
    os.path.join(ROOT, "build_solution_out", "OVRInject.dll"),
    os.path.join(ROOT, "x64", "Release", "OVRInject.dll"),
]
SHIM_CANDIDATES = [
    os.path.join(ROOT, "build_solution_out", "OVRInjectShim.dll"),
    os.path.join(ROOT, "x64", "Release", "OVRInjectShim.dll"),
]
REPORT_PATH = os.path.join(ROOT, "tests", "lifecycle_report.txt")

LOG_FILE_NAME = "gtavrInjectLog.txt"

# Terminal markers (see OVRInject/D3DHook/D3DHooks_VRManager.hpp, OVRInject/dllmain.cpp).
MARK_HOOKED = "Hooked Present at vtable index"
MARK_HOOK_FAILED = "Failed to hook Present - mod stays inert"
MARK_LATE_INERT = "No D3D11 swapchain hooked within"
MARK_VR_FAILED = "Failed to initialize VR"
MARK_VR_OK = "VR initialized with"
MARK_CRASH = "CrashDump: fatal exception"
MARK_INIT_EXCEPTION = "OVRInject init failed"

# Verdict buckets.
HOOK_SUCCESS = "hook-success"
INERT_CLEAN = "inert-clean"
CRASH = "crash"
HANG = "hang"
TIMEOUT = "timeout"
INJECT_FAILURE = "inject-failure"

EXIT_KILL_TIMEOUT_S = 10.0


# ---------------------------------------------------------------------------
# Win32 interop (ctypes, stdlib only).
# ---------------------------------------------------------------------------

_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_k32.CreateEventW.argtypes = [wintypes.LPVOID, wintypes.BOOL, wintypes.BOOL, wintypes.LPCWSTR]
_k32.CreateEventW.restype = wintypes.HANDLE
_k32.CreateFileMappingW.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD,
                                    wintypes.DWORD, wintypes.DWORD, wintypes.LPCWSTR]
_k32.CreateFileMappingW.restype = wintypes.HANDLE
_k32.MapViewOfFile.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.DWORD,
                               wintypes.DWORD, ctypes.c_size_t]
_k32.MapViewOfFile.restype = wintypes.LPVOID
_k32.UnmapViewOfFile.argtypes = [wintypes.LPCVOID]
_k32.UnmapViewOfFile.restype = wintypes.BOOL
_k32.SetEvent.argtypes = [wintypes.HANDLE]
_k32.SetEvent.restype = wintypes.BOOL
_k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
_k32.WaitForSingleObject.restype = wintypes.DWORD
_k32.CloseHandle.argtypes = [wintypes.HANDLE]
_k32.CloseHandle.restype = wintypes.BOOL
_k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
_k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
_k32.Process32FirstW.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
_k32.Process32FirstW.restype = wintypes.BOOL
_k32.Process32NextW.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
_k32.Process32NextW.restype = wintypes.BOOL

_DBWIN_SIZE = 4096  # DWORD source pid + NUL-terminated text
_WAIT_OBJECT_0 = 0
_PAGE_READWRITE = 0x04
_FILE_MAP_READ = 0x0004
_INVALID_HANDLE = wintypes.HANDLE(-1)


class DbwinListener(threading.Thread):
    """Captures OutputDebugStringA lines via the session-local DBWIN protocol.

    Only one DBWIN consumer can exist per Windows session. Lines are tagged
    with the source PID; pass pid=None to accept every process (used when the
    launcher spawns the cube and its PID is not known up front).
    """

    def __init__(self, pid):
        super().__init__(daemon=True, name="dbwin-%s" % (pid if pid else "all"))
        self.pid = pid
        self.lines = queue.Queue()
        self.error = None
        self._stop = threading.Event()

    def run(self):
        buffer_ready = None
        data_ready = None
        mapping = None
        view = None
        try:
            buffer_ready = _k32.CreateEventW(None, False, False, "DBWIN_BUFFER_READY")
            data_ready = _k32.CreateEventW(None, False, False, "DBWIN_DATA_READY")
            if not buffer_ready or not data_ready:
                raise OSError("CreateEventW failed (gle=%d)" % ctypes.get_last_error())
            mapping = _k32.CreateFileMappingW(_INVALID_HANDLE, None, _PAGE_READWRITE,
                                              0, _DBWIN_SIZE, "DBWIN_BUFFER")
            if not mapping:
                raise OSError("CreateFileMappingW failed (gle=%d)" % ctypes.get_last_error())
            view = _k32.MapViewOfFile(mapping, _FILE_MAP_READ, 0, 0, _DBWIN_SIZE)
            if not view:
                raise OSError("MapViewOfFile failed (gle=%d)" % ctypes.get_last_error())

            while not self._stop.is_set():
                _k32.SetEvent(buffer_ready)
                rc = _k32.WaitForSingleObject(data_ready, 250)
                if rc != _WAIT_OBJECT_0:
                    continue
                src_pid = ctypes.c_uint32.from_address(view).value
                raw = ctypes.string_at(view + 4, _DBWIN_SIZE - 4)
                text = raw.split(b"\x00", 1)[0].decode("mbcs", errors="replace").rstrip("\r\n")
                if (self.pid is None or src_pid == self.pid) and text:
                    self.lines.put(text)
        except OSError as exc:
            self.error = str(exc)
        finally:
            if view:
                _k32.UnmapViewOfFile(view)
            for handle in (mapping, buffer_ready, data_ready):
                if handle:
                    _k32.CloseHandle(handle)

    def stop(self):
        self._stop.set()
        self.join(timeout=5)


_user32 = ctypes.WinDLL("user32", use_last_error=True)
_WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
_BM_CLICK = 0x00F5


class MessageBoxKiller(threading.Thread):
    """Auto-dismisses the modal MessageBoxA that OVRInjectShim's LOGWNDF pops
    ("Error Logged") before it loads OVRInject.dll - without this, shim-mode
    runs would stall on the dialog forever. Strictly limited to top-level
    windows owned by the cube process, and only clicks their OK button."""

    def __init__(self, pid, titles=("Error Logged", "Fatal Error Logged")):
        super().__init__(daemon=True, name="msgbox-killer-%d" % pid)
        self.pid = pid
        self.titles = titles
        self.clicks = 0
        self._stop = threading.Event()

    def _window_text(self, hwnd):
        length = _user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        _user32.GetWindowTextW(hwnd, buf, length + 1)
        return buf.value

    def _dismiss_once(self):
        hits = []

        def on_top(hwnd, _lparam):
            if _user32.IsWindowVisible(hwnd):
                owner = wintypes.DWORD()
                _user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
                if owner.value == self.pid and self._window_text(hwnd) in self.titles:
                    hits.append(hwnd)
            return True

        callback = _WNDENUMPROC(on_top)
        _user32.EnumWindows(callback, 0)
        for hwnd in hits:
            self._click_ok(hwnd)

    def _click_ok(self, dialog):
        buttons = []

        def on_child(child, _lparam):
            class_name = ctypes.create_unicode_buffer(64)
            _user32.GetClassNameW(child, class_name, 64)
            if class_name.value == "Button" and self._window_text(child).replace("&", "").upper() == "OK":
                buttons.append(child)
            return True

        callback = _WNDENUMPROC(on_child)
        _user32.EnumChildWindows(dialog, callback, 0)
        for button in buttons:
            _user32.SendMessageW(button, _BM_CLICK, 0, 0)
            self.clicks += 1

    def run(self):
        while not self._stop.is_set():
            self._dismiss_once()
            self._stop.wait(0.05)

    def stop(self):
        self._stop.set()
        self.join(timeout=5)


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [("dwSize", wintypes.DWORD),
                ("cntUsage", wintypes.DWORD),
                ("th32ProcessID", wintypes.DWORD),
                ("th32DefaultHeapID", ctypes.c_size_t),
                ("th32ModuleID", wintypes.DWORD),
                ("cntThreads", wintypes.DWORD),
                ("th32ParentProcessID", wintypes.DWORD),
                ("pcPriClassBase", ctypes.c_long),
                ("dwFlags", wintypes.DWORD),
                ("szExeFile", wintypes.WCHAR * 260)]


def find_pid_by_name(image_name):
    """First PID whose exe name matches (case-insensitive), or None."""
    snapshot = _k32.CreateToolhelp32Snapshot(0x00000002, 0)  # TH32CS_SNAPPROCESS
    if not snapshot or snapshot == _INVALID_HANDLE.value:
        return None
    try:
        entry = PROCESSENTRY32W()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32W)
        ok = _k32.Process32FirstW(snapshot, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.lower() == image_name.lower():
                return entry.th32ProcessID
            ok = _k32.Process32NextW(snapshot, ctypes.byref(entry))
        return None
    finally:
        _k32.CloseHandle(snapshot)


# ---------------------------------------------------------------------------
# Harness mechanics.
# ---------------------------------------------------------------------------

def log_line(report_lines, text):
    print(text, flush=True)
    report_lines.append(text)


def find_newest(paths):
    best = None
    for path in paths:
        if os.path.isfile(path):
            mtime = os.path.getmtime(path)
            if best is None or mtime > best[1]:
                best = (path, mtime)
    return best[0] if best else None


def sanitized_env():
    """Environment shared by cube and launcher, minus vars that would skew the run."""
    env = dict(os.environ)
    # GTAVR_LOG_PATH would override the per-run GTAVR_LOG_DIR (Log.cpp order).
    env.pop("GTAVR_LOG_PATH", None)
    # Golden mode renders fixed frames and exits - not what we are testing.
    env.pop("GTAVR_SLICE_GOLDEN", None)
    return env


def kill_stray_cubes():
    """Make sure no leftover D3D11Cube.exe from a previous run steals the
    launcher's process lookup (it injects the first match by name)."""
    subprocess.run(["taskkill", "/IM", CUBE_NAME, "/F"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def stage_mod_dlls(stage_dir, ovrinject_path):
    """Place OVRInject.dll where the launcher expects it (GTAV_INSTALL_DIR),
    plus its import dependencies, which the cube resolves via PATH."""
    for src in (ovrinject_path, OPENVR_DLL, OPENXR_DLL):
        shutil.copy2(src, os.path.join(stage_dir, os.path.basename(src)))


def read_log(run_dir):
    path = os.path.join(run_dir, LOG_FILE_NAME)
    if not os.path.isfile(path):
        return ""
    try:
        with open(path, "r", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def crash_dumps(run_dir):
    return glob.glob(os.path.join(run_dir, "gtavr_crash_*.dmp"))


def terminate_cube(cube):
    """Terminate the cube process tree and confirm it dies within
    EXIT_KILL_TIMEOUT_S. Returns "ok", "hang", or "died-earlier"."""
    pid = cube.pid if cube is not None else find_pid_by_name(CUBE_NAME)
    if pid is None:
        return "died-earlier"
    subprocess.run(["taskkill", "/PID", str(pid), "/T", "/F"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + EXIT_KILL_TIMEOUT_S
    while time.time() < deadline:
        if find_pid_by_name(CUBE_NAME) is None:
            if cube is not None:
                try:
                    cube.wait(timeout=max(0.1, deadline - time.time()))
                except subprocess.TimeoutExpired:
                    return "hang"
            return "ok"
        time.sleep(0.1)
    # Still alive past the deadline: force-kill and report the hang.
    subprocess.run(["taskkill", "/IM", CUBE_NAME, "/F"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return "hang"


def classify_text(text):
    """Terminal-marker classification of one log text (live DBWIN stream or
    the post-mortem file). Returns (verdict, detail) or None."""
    if MARK_CRASH in text or MARK_INIT_EXCEPTION in text:
        return CRASH, "crash marker in mod log"
    if MARK_HOOK_FAILED in text:
        return INERT_CLEAN, "Present hook install failed; mod inert by design"
    if MARK_HOOKED in text:
        if MARK_VR_FAILED in text:
            return HOOK_SUCCESS, "VR init failed cleanly (no runtime/headset) -> pass-through"
        if MARK_VR_OK in text:
            return HOOK_SUCCESS, "VR initialized (a live runtime answered)"
        return None  # hooked but VR outcome not logged yet - keep waiting
    if MARK_LATE_INERT in text:
        return INERT_CLEAN, "late injection: swapchain predates hooks, watchdog kept mod inert"
    return None


def run_one_iteration(index, args, stage_dir, report_lines):
    run_dir = tempfile.mkdtemp(prefix="gtavr_lifecycle_run%03d_" % index)
    t_start = time.time()
    detail = []
    verdict = None

    mode = args.mode
    if mode == "mixed":
        mode = ("cube-first", "launcher-first", "shim")[index % 3 - 1]
    detail.append("mode=%s" % mode)

    # Stray-kill by image name is only used for the D3D11Cube modes; in shim
    # mode the cube runs AS "GTA5.exe" and a global taskkill on that name
    # could hit a real game session, so shim cleanup is strictly PID-based.
    if mode != "shim":
        kill_stray_cubes()

    launcher_env = sanitized_env()
    launcher_env.pop("GTAV_EXE_PATH", None)
    launcher_env.pop("GTAV_PROCESS_NAME", None)
    launcher_env.pop("GTAV_LAUNCHER_PATH", None)
    launcher_env["GTAVR_TARGET_PROCESS"] = CUBE_NAME

    cube = None
    listener = None
    box_killer = None
    try:
        if mode == "cube-first":
            # Harness starts the cube (per-run GTAVR_LOG_DIR); the launcher
            # then injects into the already-running process.
            cube_env = sanitized_env()
            cube_env["GTAVR_LOG_DIR"] = run_dir
            cube_env["PATH"] = stage_dir + os.pathsep + cube_env.get("PATH", "")
            console_log_path = os.path.join(run_dir, "cube_console.txt")
            console_log = open(console_log_path, "w")
            try:
                cube = subprocess.Popen(
                    [CUBE_EXE], env=cube_env, stdout=console_log, stderr=subprocess.STDOUT,
                    creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
            finally:
                console_log.close()
            listener = DbwinListener(cube.pid)
            launcher_env["GTAV_INSTALL_DIR"] = stage_dir
            launcher_env["PATH"] = stage_dir + os.pathsep + launcher_env.get("PATH", "")
        elif mode == "launcher-first":
            # Production flow: the launcher starts the cube itself from the
            # staged install dir, then injects before the swapchain exists.
            # The launcher forces GTAVR_LOG_DIR=<install dir> for its child,
            # so the per-run dir doubles as the install dir here.
            stage_mod_dlls(run_dir, args.ovrinject_resolved)
            shutil.copy2(CUBE_EXE, os.path.join(run_dir, CUBE_NAME))
            launcher_env["GTAV_INSTALL_DIR"] = run_dir
            launcher_env["PATH"] = run_dir + os.pathsep + launcher_env.get("PATH", "")
            listener = DbwinListener(None)
        else:
            # shim: dxgi-proxy load path (the shipped mechanism, ADR-0005).
            # The shim only injects into a process named GTA5.exe, so the
            # cube copy is renamed; its LOGWNDF MessageBox is auto-dismissed
            # by MessageBoxKiller or the mod would never load.
            stage_mod_dlls(run_dir, args.ovrinject_resolved)
            shutil.copy2(args.shim_resolved, os.path.join(run_dir, "dxgi.dll"))
            cube_path = os.path.join(run_dir, "GTA5.exe")
            shutil.copy2(CUBE_EXE, cube_path)
            cube_env = sanitized_env()
            cube_env["GTAVR_LOG_DIR"] = run_dir
            cube_env["PATH"] = run_dir + os.pathsep + cube_env.get("PATH", "")
            console_log_path = os.path.join(run_dir, "cube_console.txt")
            console_log = open(console_log_path, "w")
            try:
                cube = subprocess.Popen([cube_path], env=cube_env,
                                        stdout=console_log, stderr=subprocess.STDOUT)
            finally:
                console_log.close()
            listener = DbwinListener(cube.pid)
            box_killer = MessageBoxKiller(cube.pid)
            box_killer.start()

        listener.start()

        # Redirect the launcher to a file, not pipes: in launcher-first mode
        # the cube inherits its parent's handles, and a grandchild holding a
        # pipe write-end open would block subprocess.run's communicate()
        # forever (no EOF after the launcher itself exits).
        # (No launcher at all in shim mode - the dxgi proxy is the loader.)
        launcher_output_path = os.path.join(run_dir, "launcher_output.txt")
        if mode != "shim":
            try:
                with open(launcher_output_path, "w") as launcher_out:
                    launcher = subprocess.run(
                        [args.launcher_resolved], env=launcher_env, timeout=60,
                        stdout=launcher_out, stderr=subprocess.STDOUT,
                        creationflags=subprocess.ABOVE_NORMAL_PRIORITY_CLASS)
            except subprocess.TimeoutExpired:
                verdict = INJECT_FAILURE
                detail.append("launcher did not finish within 60 s")
                launcher = None

            if launcher is not None and launcher.returncode != 0:
                try:
                    with open(launcher_output_path, "r", errors="replace") as f:
                        launcher_text = f.read()
                except OSError:
                    launcher_text = ""
                verdict = INJECT_FAILURE
                detail.append("launcher rc=%d: %s" % (
                    launcher.returncode, launcher_text.strip().replace("\n", " | ")))

        # Watch the live debug-string stream for a terminal marker.
        if verdict is None:
            hook_deadline = time.time() + args.hook_timeout
            vr_deadline = None
            hooked = False
            stream = []
            while True:
                try:
                    while True:
                        stream.append(listener.lines.get_nowait())
                except queue.Empty:
                    pass
                text = "\n".join(stream)
                now = time.time()

                if crash_dumps(run_dir):
                    verdict = CRASH
                    detail.append("minidump written (gtavr_crash_*.dmp)")
                    break
                outcome = classify_text(text)
                if outcome is not None:
                    verdict, why = outcome
                    if verdict == HOOK_SUCCESS:
                        detail.append("hooked Present in %.1f s; %s" % (now - t_start, why))
                    else:
                        detail.append(why)
                    break
                if not hooked and MARK_HOOKED in text:
                    hooked = True
                    vr_deadline = now + args.vr_timeout
                    detail.append("hooked Present in %.1f s; awaiting VR outcome" % (now - t_start))
                if hooked and vr_deadline is not None and now > vr_deadline:
                    verdict = HOOK_SUCCESS
                    detail.append("no VR outcome line within %d s of hook (pass-through assumed)"
                                  % args.vr_timeout)
                    break
                if mode != "launcher-first" and cube.poll() is not None and not hooked:
                    verdict = CRASH
                    detail.append("cube exited on its own (rc=%s) before any terminal marker"
                                  % cube.returncode)
                    break
                if mode == "launcher-first" and find_pid_by_name(CUBE_NAME) is None and not hooked:
                    verdict = CRASH
                    detail.append("cube vanished before any terminal marker")
                    break
                if now > hook_deadline:
                    verdict = TIMEOUT
                    detail.append("no terminal marker within %d s" % args.hook_timeout)
                    break
                time.sleep(0.1)

        # Terminate and confirm a timely death (no hang on exit).
        kill_result = terminate_cube(cube)
        if listener is not None:
            listener.stop()
        if box_killer is not None:
            box_killer.stop()
            if box_killer.clicks:
                detail.append("dismissed %d shim MessageBox(es)" % box_killer.clicks)
        if kill_result == "hang":
            verdict = HANG
            detail.append("cube did not die within %.0f s of taskkill" % EXIT_KILL_TIMEOUT_S)
        elif kill_result == "died-earlier" and verdict in (HOOK_SUCCESS, INERT_CLEAN):
            detail.append("cube had already exited")

        # A crash can also happen during teardown (e.g. inside DllMain detach).
        if verdict in (HOOK_SUCCESS, INERT_CLEAN) and crash_dumps(run_dir):
            verdict = CRASH
            detail.append("minidump appeared during/after shutdown")

        # Fallback/consistency: the on-disk log is readable once the cube is
        # dead. If live capture missed the verdict, classify from the file.
        if verdict == TIMEOUT:
            post = read_log(run_dir)
            outcome = classify_text(post) if post else None
            if outcome is not None:
                verdict, why = outcome
                detail.append("classified from post-mortem log file: %s" % why)
            elif not post:
                detail.append("post-mortem log file missing or unreadable")
            else:
                tail = post.strip().splitlines()[-1] if post.strip() else ""
                detail.append("post-mortem log had no terminal marker (last: %s)" % tail[:120])
    finally:
        if listener is not None and listener.is_alive():
            listener.stop()
        if box_killer is not None and box_killer.is_alive():
            box_killer.stop()
        if cube is not None and cube.poll() is None:
            cube.kill()
        if mode != "shim":
            kill_stray_cubes()

    if listener is not None and listener.error:
        detail.append("DBWIN listener error: %s" % listener.error)

    elapsed = time.time() - t_start
    keep = args.keep_logs or verdict not in (HOOK_SUCCESS, INERT_CLEAN)
    if not keep:
        shutil.rmtree(run_dir, ignore_errors=True)
        location = "(log dir discarded)"
    else:
        location = run_dir

    log_line(report_lines, "run %3d: %-14s %6.1f s  %s  %s"
             % (index, verdict, elapsed, "; ".join(detail) if detail else "-", location))
    return verdict


def main():
    parser = argparse.ArgumentParser(
        description="GTAVR injection-lifecycle harness (inject -> hook/inert -> terminate, xN).")
    parser.add_argument("--count", type=int, default=20,
                        help="number of inject/terminate iterations (default 20)")
    parser.add_argument("--mode", choices=["cube-first", "launcher-first", "shim", "mixed"],
                        default="cube-first",
                        help="cube-first (default, mission spec): harness starts the cube, "
                             "launcher injects into it; launcher-first: launcher starts the "
                             "cube from a staged dir (production launch flow); shim: dxgi-proxy "
                             "load path, deterministic hook-success; mixed: cycle all three")
    parser.add_argument("--hook-timeout", type=float, default=40.0,
                        help="seconds to wait for a hook/inert terminal marker per run "
                             "(default 40; the late-injection watchdog fires at 30 s)")
    parser.add_argument("--vr-timeout", type=float, default=20.0,
                        help="seconds to wait for the VR-init outcome once Present is hooked "
                             "(default 20)")
    parser.add_argument("--ovrinject", default=None,
                        help="explicit path to OVRInject.dll (default: newest known build output)")
    parser.add_argument("--keep-logs", action="store_true",
                        help="keep every per-run log dir (default: keep only failed runs)")
    args = parser.parse_args()

    report_lines = []
    started = datetime.datetime.now()
    log_line(report_lines, "GTAVR injection-lifecycle report - %s" % started.strftime("%Y-%m-%d %H:%M:%S"))
    log_line(report_lines, "count=%d mode=%s hook-timeout=%.0fs vr-timeout=%.0fs" %
             (args.count, args.mode, args.hook_timeout, args.vr_timeout))

    # Preconditions: all binaries must exist before the first run.
    launcher = find_newest(LAUNCHER_CANDIDATES)
    missing = [p for p in (CUBE_EXE, OPENVR_DLL, OPENXR_DLL) if not os.path.isfile(p)]
    if launcher is None:
        missing.append("GTAVOVR.exe (searched: %s)" % ", ".join(LAUNCHER_CANDIDATES))
    ovrinject = args.ovrinject or find_newest(OVRINJECT_CANDIDATES)
    if ovrinject is None:
        missing.append("OVRInject.dll (searched: %s)" % ", ".join(OVRINJECT_CANDIDATES))
    shim = find_newest(SHIM_CANDIDATES)
    if shim is None and args.mode in ("shim", "mixed"):
        missing.append("OVRInjectShim.dll (searched: %s)" % ", ".join(SHIM_CANDIDATES))
    if missing:
        for path in missing:
            log_line(report_lines, "ERROR: missing prerequisite: %s" % path)
        log_line(report_lines, "Build Release|x64 of GTAVOVR.sln (and the D3D11Cube sample) first.")
        with open(REPORT_PATH, "w") as f:
            f.write("\n".join(report_lines) + "\n")
        return 1
    args.ovrinject_resolved = ovrinject
    args.launcher_resolved = launcher
    args.shim_resolved = shim

    log_line(report_lines, "cube=%s" % CUBE_EXE)
    log_line(report_lines, "launcher=%s" % launcher)
    log_line(report_lines, "OVRInject.dll=%s" % ovrinject)
    if shim:
        log_line(report_lines, "OVRInjectShim.dll=%s" % shim)
    log_line(report_lines, "")

    stage_dir = tempfile.mkdtemp(prefix="gtavr_lifecycle_stage_")
    counts = {HOOK_SUCCESS: 0, INERT_CLEAN: 0, CRASH: 0, HANG: 0, TIMEOUT: 0, INJECT_FAILURE: 0}
    try:
        stage_mod_dlls(stage_dir, ovrinject)
        for i in range(1, args.count + 1):
            verdict = run_one_iteration(i, args, stage_dir, report_lines)
            counts[verdict] = counts.get(verdict, 0) + 1
    finally:
        shutil.rmtree(stage_dir, ignore_errors=True)
        kill_stray_cubes()

    failures = counts[CRASH] + counts[HANG] + counts[TIMEOUT] + counts[INJECT_FAILURE]
    log_line(report_lines, "")
    log_line(report_lines, "summary: %d runs - %d hook-success, %d inert-clean, "
             "%d crash, %d hang, %d timeout, %d inject-failure"
             % (args.count, counts[HOOK_SUCCESS], counts[INERT_CLEAN],
                counts[CRASH], counts[HANG], counts[TIMEOUT], counts[INJECT_FAILURE]))
    ok = failures == 0 and (counts[HOOK_SUCCESS] + counts[INERT_CLEAN]) == args.count
    log_line(report_lines, "verdict: %s (0 crashes/hangs/timeouts required; "
             "hook-success and clean-inert both pass)" % ("PASS" if ok else "FAIL"))
    log_line(report_lines, "note: on a machine with no live VR runtime the expected hooked "
             "outcome is 'Failed to initialize VR' (inert pass-through), not a crash.")

    with open(REPORT_PATH, "w") as f:
        f.write("\n".join(report_lines) + "\n")
    print("\nreport written to %s" % REPORT_PATH, flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
