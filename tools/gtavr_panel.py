#!/usr/bin/env python3
"""GTAVR control panel (tkinter, stdlib only).

One window to: start the game, pick the VR backend (OpenXR/Pimax vs
OpenVR/SteamVR), toggle verbose logging, inject the mod, and watch the mod
log live. Story Mode only - the mod hard-disables in GTA Online by design.

Run:  python tools/gtavr_panel.py   (elevated = same rights as the game)
"""
import ctypes as C
import ctypes.wintypes as W
import os
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

REPO = r"C:\Repos\GTA_VR\GTAV_VR"
BIN = os.path.join(REPO, "x64", "Release")
GTAVOVR = os.path.join(BIN, "GTAVOVR.exe")
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


class Panel(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("GTAVR control panel")
        self.resizable(True, True)
        self.injected_pid = None
        self._log_path = None
        self._log_pos = 0

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

        self.verbose = tk.BooleanVar(value=True)
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

    # ---------- helpers ----------
    def say(self, tag, text):
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
        self.log.see("end")
        self.log.configure(state="disabled")

    def run_bg(self, fn):
        threading.Thread(target=fn, daemon=True).start()

    def base_env(self):
        env = dict(os.environ)
        env.pop("GTAV_INSTALL_DIR", None)      # DLL must come from x64\Release (fresh)
        env["GTAV_EXE_PATH"] = GAME_EXE
        env["GTAVR_LOG_DIR"] = LOGDIR
        if self.verbose.get():
            env["GTAVR_VERBOSE"] = "1"
        else:
            env.pop("GTAVR_VERBOSE", None)
        env["GTAVR_BACKEND"] = self.backend.get()
        return env

    # ---------- actions ----------
    def preflight(self):
        def work():
            r = run_quiet([GTAVOVR, "--check"], env=self.base_env())
            self.say("info", (r.stdout or "") + "\n")
        self.run_bg(work)

    def start_game(self):
        self.say("info", f"starting {PLAY_GTAV} ...\n")
        subprocess.Popen([PLAY_GTAV], creationflags=_NO_WINDOW)

    def start_steamvr(self):
        vrserver = r"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe"
        if os.path.exists(vrserver):
            self.say("info", "starting SteamVR (vrserver)...\n")
            subprocess.Popen([vrserver], creationflags=_NO_WINDOW)
        else:
            self.say("info", "starting SteamVR via steam:// ...\n")
            os.startfile("steam://rungameid/250820")

    def inject(self):
        pid = game_pid()
        if not pid:
            messagebox.showwarning("GTAVR", "GTA5.exe is not running. Start the game first.")
            return
        if self.injected_pid == pid:
            messagebox.showinfo("GTAVR", "Already injected into this game instance.\nRestart the game to inject again.")
            return
        env = self.base_env()
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
        pid = game_pid()
        inj = f" | injected pid {self.injected_pid}" if self.injected_pid else ""
        self.status.set(f"game: {'RUNNING pid ' + str(pid) if pid else 'not running'}{inj}"
                        f"   backend: {self.backend.get()}")
        self.after(2000, self.tick_status)

    def tick_log(self):
        p = newest_log()
        try:
            if p and (p != self._log_path):
                self._log_path, self._log_pos = p, 0
                self.say("info", f"--- tailing {p} ---\n")
            if p:
                with open(p, "r", errors="ignore") as f:
                    f.seek(self._log_pos)
                    data = f.read()
                    self._log_pos = f.tell()
                if data:
                    self.say("info", data)
        except OSError:
            pass  # old DLL holds it read-denied; new DLL shares (rebuild + reinject)
        self.after(1000, self.tick_log)


if __name__ == "__main__":
    os.makedirs(LOGDIR, exist_ok=True)
    Panel().mainloop()
