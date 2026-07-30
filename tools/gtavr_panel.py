#!/usr/bin/env python3
"""GTAVR control panel (tkinter, stdlib only).

One window to: start the game, pick the VR backend (OpenXR/Pimax vs
OpenVR/SteamVR), toggle verbose logging, inject the mod, and watch the mod
log live. Story Mode only - the mod hard-disables in GTA Online by design.

Run:  python tools/gtavr_panel.py   (elevated = same rights as the game)
"""
import ctypes as C
import ctypes.wintypes as W
import filecmp
import os
import queue
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

REPO = r"C:\Repos\GTA_VR\GTAV_VR"
BIN = os.path.join(REPO, "x64", "Release")
GTAVOVR = os.path.join(BIN, "GTAVOVR.exe")
RUNTIME_UPDATER = os.path.join(REPO, "tools", "update_runtime.bat")
GAME_EXE = r"D:\Rockstar Games\Grand Theft Auto V Legacy\GTA5.exe"
PLAY_GTAV = r"D:\Rockstar Games\Grand Theft Auto V Legacy\PlayGTAV.exe"
GAME_DIR = os.path.dirname(GAME_EXE)
LOGDIR = r"C:\Users\MY\AppData\Local\Temp\gtavr_logs"
TEMP = os.environ.get("TEMP", r"C:\Users\MY\AppData\Local\Temp")
LOG_CANDIDATES = [
    os.path.join(LOGDIR, "gtavrInjectLog.txt"),
    os.path.join(TEMP, "gtavrInjectLog.txt"),
    os.path.join(GAME_DIR, "gtavrInjectLog.txt"),
]
RUNTIME_FILE_PAIRS = [
    (os.path.join(BIN, "OVRInject.dll"), os.path.join(GAME_DIR, "OVRInject.dll")),
    (os.path.join(REPO, "GTAVRBridge", "x64", "Release", "GTAVRBridge.asi"),
     os.path.join(GAME_DIR, "GTAVRBridge.asi")),
]

# The injected mod log can become very large after a noisy/failed run.  The
# panel is a live tail, not a full-file viewer: never read an existing log from
# byte zero or keep unbounded text in the Tk widget.
_LOG_INITIAL_TAIL_BYTES = 256 * 1024
_LOG_READ_CHUNK_BYTES = 128 * 1024
_LOG_MAX_BACKLOG_BYTES = 2 * 1024 * 1024
_LOG_MAX_WIDGET_LINES = 2500

# Never spawn a visible console from pythonw (that flashing cmd window).
_NO_WINDOW = 0x08000000  # CREATE_NO_WINDOW


def run_quiet(cmd, **kw):
    """subprocess.run without a console window flash."""
    kw.setdefault("creationflags", _NO_WINDOW)
    kw.setdefault("capture_output", True)
    kw.setdefault("text", True)
    kw.setdefault("errors", "ignore")
    return subprocess.run(cmd, **kw)


def _pid_of(name):
    """Process id by exe name via Toolhelp32 snapshot - no subprocess at all."""
    name = name.lower()
    snap = C.windll.kernel32.CreateToolhelp32Snapshot(2, 0)
    if snap == -1:
        return None

    class E(C.Structure):
        _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                    ("th32DefaultHeapID", C.POINTER(C.c_ulong)), ("th32ModuleID", W.DWORD),
                    ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                    ("pcPriClassBase", C.c_long), ("dwFlags", W.DWORD),
                    ("szExeFile", C.c_char * 260)]

    e = E()
    e.dwSize = C.sizeof(E)
    ok = C.windll.kernel32.Process32First(snap, C.byref(e))
    while ok:
        if e.szExeFile.decode("ascii", "ignore").lower() == name:
            C.windll.kernel32.CloseHandle(snap)
            return e.th32ProcessID
        ok = C.windll.kernel32.Process32Next(snap, C.byref(e))
    C.windll.kernel32.CloseHandle(snap)
    return None


def game_pid():
    return _pid_of("gta5.exe")


def newest_log():
    best, best_m = None, 0.0
    for p in LOG_CANDIDATES:
        try:
            m = os.path.getmtime(p)
            if m > best_m:
                best, best_m = p, m
        except OSError:
            pass
    return best


def runtime_files_current():
    try:
        return all(
            os.path.isfile(source)
            and os.path.isfile(target)
            and filecmp.cmp(source, target, shallow=False)
            for source, target in RUNTIME_FILE_PAIRS
        )
    except OSError:
        return False


def format_size(byte_count):
    value = float(byte_count)
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if value < 1024.0 or unit == "TB":
            return f"{value:.1f} {unit}"
        value /= 1024.0


def upsert_ini_value(text, section, key, value):
    """Update one INI key without deleting unrelated keys or comments."""
    lines = text.splitlines()
    section_header = f"[{section}]"
    section_start = None
    section_end = len(lines)
    for index, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if section_start is not None:
                section_end = index
                break
            if stripped.lower() == section_header.lower():
                section_start = index

    assignment = f"{key}={value}"
    if section_start is None:
        while lines and not lines[-1].strip():
            lines.pop()
        if lines:
            lines.append("")
        lines.extend([section_header, assignment])
    else:
        key_lower = key.lower()
        for index in range(section_start + 1, section_end):
            candidate = lines[index].strip()
            if "=" in candidate and candidate.split("=", 1)[0].strip().lower() == key_lower:
                lines[index] = assignment
                break
        else:
            lines.insert(section_end, assignment)
    return "\n".join(lines).rstrip() + "\n"


class Panel(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("GTAVR control panel")
        self.resizable(True, True)
        self.injected_pid = None
        self._ui_queue = queue.SimpleQueue()
        self._log_path = None
        self._log_pos = 0
        self._drop_partial_log_line = False

        top = ttk.Frame(self, padding=8)
        top.pack(fill="x")

        self.status = tk.StringVar(value="game: checking...")
        ttk.Label(top, textvariable=self.status, font=("Segoe UI", 10, "bold")).grid(
            row=0, column=0, columnspan=6, sticky="w", pady=(0, 6))

        ttk.Label(top, text="Backend:").grid(row=1, column=0, sticky="w")
        self.backend = tk.StringVar(value="openvr")
        ttk.Radiobutton(top, text="OpenVR (SteamVR)", value="openvr",
                        variable=self.backend).grid(row=1, column=1, sticky="w")
        ttk.Radiobutton(top, text="OpenXR (Pimax)", value="openxr",
                        variable=self.backend).grid(row=1, column=2, sticky="w")

        # Normal logging is enough for routine use. Verbose mode is opt-in
        # because camera/runtime diagnostics can be extremely chatty.
        self.verbose = tk.BooleanVar(value=False)
        ttk.Checkbutton(top, text="Verbose logging", variable=self.verbose).grid(
            row=1, column=3, sticky="w", padx=(12, 0))

        ttk.Button(top, text="Preflight --check", command=self.preflight).grid(
            row=1, column=4, sticky="e", padx=(12, 0))
        ttk.Button(top, text="Collect log bundle", command=self.collect).grid(
            row=1, column=5, sticky="e", padx=(6, 0))

        mid = ttk.Frame(self, padding=(8, 0, 8, 4))
        mid.pack(fill="x")
        self.btn_game = ttk.Button(mid, text="1. Start game (PlayGTAV)", command=self.start_game)
        self.btn_game.pack(side="left")
        self.btn_steamvr = ttk.Button(mid, text="Start SteamVR", command=self.start_steamvr)
        self.btn_steamvr.pack(side="left", padx=(8, 0))
        self.btn_inject = ttk.Button(mid, text="2. INJECT MOD", command=self.inject)
        self.btn_inject.pack(side="left", padx=(16, 0))
        self.btn_install = ttk.Button(mid, text="Update DLL + bridge", command=self.install_update)
        self.btn_install.pack(side="left", padx=(8, 0))
        self.btn_log = ttk.Button(mid, text="Open log folder", command=self.open_log_folder)
        self.btn_log.pack(side="left", padx=(8, 0))

        self.log = tk.Text(self, height=26, bg="#101010", fg="#d0d0d0",
                           font=("Consolas", 9), state="disabled", wrap="none")
        self.log.pack(fill="both", expand=True, padx=8, pady=(4, 8))
        self.log.tag_config("warn", foreground="#e0c060")
        self.log.tag_config("err", foreground="#e06060")
        self.log.tag_config("verdict", foreground="#70e070")

        self.after(1000, self.tick_status)
        self.after(1500, self.tick_log)
        self.say("verdict", "Panel ready. Flow: Start game -> wait for Story Mode -> INJECT MOD.\n")
        self.say("verdict", "If the headset shows nothing with OpenXR (Pimax), quit the game and retry with OpenVR (SteamVR).\n")
        self.say("info", "Runtime posture: if OpenXR session stays pre-READY (log shows 'waiting for session READY'), the Pimax home holds the display - set Pimax Play to 'no default environment', or use OpenVR.\n")
        self.say("info", "In-game overlay: Delete/Insert/F10 (or controller menu). Perf CSV: F11. Logs stream below.\n")

    # ---------- helpers ----------
    def say(self, tag, text):
        if threading.current_thread() is not threading.main_thread():
            self._ui_queue.put((tag, text))
            return
        self.log.configure(state="normal")
        for line in text.splitlines():
            t = tag
            if "WARN" in line:
                t = "warn"
            if " ERR " in line or "error" in line.lower():
                t = "err"
            if any(k in line for k in ("MOD ACTIVE", "SUCCESS!", "MOD INERT", "HARD-DISABLED")):
                t = "verdict"
            self.log.insert("end", line + "\n", t)
        line_count = int(self.log.index("end-1c").split(".", 1)[0])
        excess = line_count - _LOG_MAX_WIDGET_LINES
        if excess > 0:
            self.log.delete("1.0", f"{excess + 1}.0")
        self.log.see("end")
        self.log.configure(state="disabled")

    def run_bg(self, fn):
        threading.Thread(target=fn, daemon=True).start()

    def base_env(self):
        env = dict(os.environ)
        env.pop("GTAV_INSTALL_DIR", None)      # DLL must come from x64\Release (fresh)
        env["GTAV_EXE_PATH"] = GAME_EXE
        env["GTAVR_LOG_DIR"] = LOGDIR
        # A game launched by this panel inherits a deterministic writable
        # settings path. Injection into an already-running game cannot change
        # its environment, so the DLL retains its documented game-dir fallback.
        env["GTAVR_SETTINGS_DIR"] = BIN
        env["GTAVR_SETTINGS_PATH"] = os.path.join(BIN, "gtavr_settings.ini")
        env["GTAVR_CAMERA_PATH"] = os.path.join(REPO, "gtavr_camera.ini")
        if self.verbose.get():
            env["GTAVR_VERBOSE"] = "1"
        else:
            env.pop("GTAVR_VERBOSE", None)
        env["GTAVR_BACKEND"] = self.backend.get()
        return env

    def write_backend_ini(self):
        """Persist the chosen backend and verbose flag where the INJECTED DLL
        actually reads them.

        Env vars only reach a process we spawn - when injecting into an
        already-running game, GTAVR_BACKEND / GTAVR_VERBOSE never arrive. The
        DLL reads gtavr_settings.ini ([Runtime] backend= and [Debug]
        verbose=) from its own directory.
        """
        ini = os.path.join(BIN, "gtavr_settings.ini")
        text = ""
        if os.path.exists(ini):
            try:
                with open(ini, "r", errors="ignore") as f:
                    text = f.read()
            except OSError:
                text = ""
        text = upsert_ini_value(text, "Runtime", "backend", self.backend.get())
        text = upsert_ini_value(
            text, "Debug", "verbose", "1" if self.verbose.get() else "0")
        try:
            with open(ini, "w", newline="\n") as f:
                f.write(text)
        except OSError as e:
            self.say("err", f"cannot write settings ini: {e}\n")

    # ---------- actions ----------
    def preflight(self):
        def work():
            r = run_quiet([GTAVOVR, "--check"], env=self.base_env())
            self.say("info", (r.stdout or "") + "\n")
        self.run_bg(work)

    def start_game(self):
        self.say("info", f"starting {PLAY_GTAV} ...\n")
        self.write_backend_ini()
        subprocess.Popen([PLAY_GTAV], creationflags=_NO_WINDOW, env=self.base_env())

    def start_steamvr(self):
        vrserver = r"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe"
        if os.path.exists(vrserver):
            self.say("info", "starting SteamVR (vrserver)...\n")
            subprocess.Popen([vrserver], creationflags=_NO_WINDOW)
        else:
            self.say("info", "starting SteamVR via steam:// ...\n")
            os.startfile("steam://rungameid/250820")

    def install_update(self):
        if game_pid():
            messagebox.showwarning(
                "GTAVR",
                "Close GTA5.exe before updating the runtime files.")
            return
        if not os.path.exists(RUNTIME_UPDATER):
            messagebox.showerror("GTAVR", f"Updater not found:\n{RUNTIME_UPDATER}")
            return
        args = f'/c ""{RUNTIME_UPDATER}" "{GAME_DIR}""'
        result = C.windll.shell32.ShellExecuteW(
            None, "runas", "cmd.exe", args, REPO, 1)
        if result <= 32:
            self.say("err", f"installer elevation failed (ShellExecute {result}).\n")
        else:
            self.say("info", "runtime updater opened; approve UAC and wait for DONE.\n")

    def inject(self):
        if not runtime_files_current():
            messagebox.showwarning(
                "GTAVR",
                "The game DLL and bridge do not match this build.\n"
                "Close GTA, click 'Update DLL + bridge', approve UAC, then restart GTA.")
            return
        pid = game_pid()
        if not pid:
            messagebox.showwarning("GTAVR", "GTA5.exe is not running. Start the game first.")
            return
        if self.injected_pid == pid:
            messagebox.showinfo("GTAVR", "Already injected into this game instance.\nRestart the game to inject again.")
            return
        env = self.base_env()
        self.write_backend_ini()
        self.say("verdict", f"--- injecting backend={env['GTAVR_BACKEND']} verbose={self.verbose.get()} into pid {pid} ---\n")

        def work():
            r = run_quiet([GTAVOVR], env=env, timeout=120)
            out = (r.stdout or "") + (r.stderr or "")
            self.say("info", out + "\n")
            if "Injection successful" in out:
                self.injected_pid = pid
                self.say("verdict", "injected OK - mod log should stream below.\n")
            else:
                self.say("err", "injection FAILED - see output above.\n")
        self.run_bg(work)

    def open_log_folder(self):
        p = newest_log() or LOGDIR
        subprocess.Popen(["explorer", os.path.dirname(p)])

    def collect(self):
        self.run_bg(lambda: self.say(
            "info", (run_quiet(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                "-File", os.path.join(REPO, "tools", "collect_logs.ps1")]).stdout or "") + "\n"))

    # ---------- ticks ----------
    def tick_status(self):
        while True:
            try:
                tag, message = self._ui_queue.get_nowait()
            except queue.Empty:
                break
            self.say(tag, message)
        pid = game_pid()
        inj = f" | injected pid {self.injected_pid}" if self.injected_pid else ""
        runtime_ready = runtime_files_current()
        self.btn_inject.configure(state="normal" if runtime_ready else "disabled")
        pair = "runtime files: READY" if runtime_ready else "runtime files: UPDATE REQUIRED"
        self.status.set(f"game: {'RUNNING pid ' + str(pid) if pid else 'not running'}{inj}"
                        f"   backend: {self.backend.get()}   {pair}")
        self.after(2000, self.tick_status)

    def tick_log(self):
        p = newest_log()
        try:
            if p and (p != self._log_path):
                size = os.path.getsize(p)
                self._log_path = p
                self._log_pos = max(0, size - _LOG_INITIAL_TAIL_BYTES)
                self._drop_partial_log_line = self._log_pos > 0
                if self._log_pos:
                    self.say(
                        "warn",
                        f"--- tailing recent output from {p} "
                        f"({format_size(size)} total; older content is not loaded) ---\n")
                else:
                    self.say("info", f"--- tailing {p} ---\n")
            if p:
                size = os.path.getsize(p)
                if size < self._log_pos:
                    # The writer rotated or truncated the file.
                    self._log_pos = 0
                    self._drop_partial_log_line = False
                    self.say("info", "--- log was reset; following from the start ---\n")
                elif size - self._log_pos > _LOG_MAX_BACKLOG_BYTES:
                    skipped = size - self._log_pos - _LOG_INITIAL_TAIL_BYTES
                    self._log_pos = max(0, size - _LOG_INITIAL_TAIL_BYTES)
                    self._drop_partial_log_line = self._log_pos > 0
                    self.say(
                        "warn",
                        f"--- log produced too much data; skipped "
                        f"{format_size(max(0, skipped))} to keep the panel responsive ---\n")

                with open(p, "rb") as f:
                    f.seek(self._log_pos)
                    data = f.read(_LOG_READ_CHUNK_BYTES)
                    self._log_pos = f.tell()

                if data and self._drop_partial_log_line:
                    newline = data.find(b"\n")
                    if newline >= 0:
                        data = data[newline + 1:]
                        self._drop_partial_log_line = False
                    else:
                        data = b""
                if data:
                    self.say("info", data.decode("utf-8", "replace"))
        except OSError:
            pass  # old DLL holds it read-denied; new DLL shares (rebuild + reinject)
        self.after(1000, self.tick_log)


if __name__ == "__main__":
    os.makedirs(LOGDIR, exist_ok=True)
    Panel().mainloop()
