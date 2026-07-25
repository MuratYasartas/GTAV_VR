#!/usr/bin/env python3
"""GTAVR control panel (tkinter, stdlib only).

One window to: start the game, pick the VR backend (OpenXR/Pimax vs
OpenVR/SteamVR), toggle verbose logging, inject the mod, and watch the mod
log live. Story Mode only - the mod hard-disables in GTA Online by design.

Run:  python tools/gtavr_panel.py   (elevated = same rights as the game)
"""
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


def game_pid():
    out = subprocess.run(["tasklist", "/FO", "CSV", "/NH"], capture_output=True,
                         text=True, errors="ignore").stdout.lower()
    for line in out.splitlines():
        if line.startswith('"gta5.exe"'):
            parts = line.split('","')
            if len(parts) > 1 and parts[1].strip('"').isdigit():
                return int(parts[1].strip('"'))
    return None


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
            r = subprocess.run([GTAVOVR, "--check"], env=self.base_env(),
                               capture_output=True, text=True, errors="ignore")
            self.say("info", r.stdout + "\n")
        self.run_bg(work)

    def start_game(self):
        self.say("info", f"starting {PLAY_GTAV} ...\n")
        subprocess.Popen(["cmd", "/c", "start", "", PLAY_GTAV], shell=False)

    def start_steamvr(self):
        vrserver = r"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\vrserver.exe"
        if os.path.exists(vrserver):
            self.say("info", "starting SteamVR (vrserver)...\n")
            subprocess.Popen(["cmd", "/c", "start", "", vrserver], shell=False)
        else:
            self.say("info", "starting SteamVR via steam:// ...\n")
            subprocess.Popen(["cmd", "/c", "start", "", "steam://rungameid/250820"], shell=False)

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
            r = subprocess.run([GTAVOVR], env=env, capture_output=True, text=True,
                               errors="ignore", timeout=120)
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
            "info", subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                    "-File", os.path.join(REPO, "tools", "collect_logs.ps1")],
                                   capture_output=True, text=True, errors="ignore").stdout + "\n"))

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
