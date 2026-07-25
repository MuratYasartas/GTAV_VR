#!/usr/bin/env python3
"""GTAVR runtime pattern scanner (external, read-only).

Scans the RUNNING, unpacked GTA5.exe process for the known camera AOB
patterns and, on a unique match, prints a ready-to-paste [bNNNN] manifest
section. This is how a new game build gets supported: the on-disk binary is
packed (both .text sections at entropy 8.00), so patterns only exist in the
live process image.

USAGE:
  1. Rockstar Games Launcher -> Settings -> uncheck BattlEye (story-mode
     modding posture; the mod refuses to run with BattlEye active anyway).
  2. Launch GTA V into STORY MODE (main menu is enough; in-game is better).
     NEVER do this in GTA Online.
  3. python tools/scan_patterns.py            (from the repo root)

Exit codes: 0 = at least one unique pattern match; 2 = no unique match;
3 = game process not found / unreadable.

Reads process memory only. Writes nothing to the game. UNVERIFIED until a
match is confirmed to drive the camera correctly in-game.
"""
import ctypes as C
import ctypes.wintypes as W
import re
import sys

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

PATTERNS = [
    # (label, pattern, cameraPatternOffset, relativeOffsets|ripOffset, pointerOffsets, matrixOffset, source)
    ("manifest primary", "48 8B C7 F3 0F 10 0D",
     "-0x1D", "relativeOffsets=0,3", "0", "0x1F0",
     "gtav_legacy.ini primary (GTAForums legacy)"),
    ("manifest alt.1", "48 8B 05 ? ? ? ? 48 8B 98 ? ? ? ? 48 85 DB 74 30",
     "0", "ripOffset=3", "0", "0x60",
     "gtav_legacy.ini alt.1 (CViewPort::GetCamera)"),
    ("manifest alt.2", "48 8B 05 ? ? ? ? 48 8B 48 ? E8 ? ? ? ? 48 8B C8",
     "0", "ripOffset=3", "0", "0x1F0",
     "gtav_legacy.ini alt.2"),
    ("lukeross enhanced", "48 8B 41 10 F3 0F 10 00 F3 0F 10 48 08",
     "0", "ripOffset=0", "0x10", "0x1F0",
     "gtavr_camera.ini (LukeRoss R.E.A.L., Enhanced)"),
]


def find_process(name):
    snap = C.windll.kernel32.CreateToolhelp32Snapshot(2, 0)  # TH32CS_SNAPPROCESS
    if snap == -1:
        return None
    class PROCESSENTRY32(C.Structure):
        _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                    ("th32DefaultHeapID", C.POINTER(C.c_ulong)), ("th32ModuleID", W.DWORD),
                    ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                    ("pcPriClassBase", C.c_long), ("dwFlags", W.DWORD),
                    ("szExeFile", C.c_char * 260)]
    e = PROCESSENTRY32(); e.dwSize = C.sizeof(PROCESSENTRY32)
    ok = C.windll.kernel32.Process32First(snap, C.byref(e))
    while ok:
        if e.szExeFile.decode("ascii", "ignore").lower() == name.lower():
            C.windll.kernel32.CloseHandle(snap)
            return e.th32ProcessID
        ok = C.windll.kernel32.Process32Next(snap, C.byref(e))
    C.windll.kernel32.CloseHandle(snap)
    return None


class MEMORY_BASIC_INFORMATION(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", W.DWORD), ("RegionSize", C.c_size_t),
                ("State", W.DWORD), ("Protect", W.DWORD), ("Type", W.DWORD)]


def pattern_regex(pat):
    parts = []
    for t in pat.split():
        parts.append(b"." if t in ("?", "??") else re.escape(bytes([int(t, 16)])))
    return re.compile(b"".join(parts), re.DOTALL)


def main():
    pid = find_process("GTA5.exe")
    if not pid:
        print("GTA5.exe not running. Launch STORY MODE first (BattlEye off).", file=sys.stderr)
        return 3
    print(f"GTA5.exe pid={pid}")
    h = C.windll.kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        print(f"OpenProcess failed (err {C.GetLastError()}). Run this script elevated "
              f"(game may be running as admin).", file=sys.stderr)
        return 3

    # Determine the module range of GTA5.exe itself via its image base.
    base = C.c_void_p()
    needed = W.DWORD()
    C.windll.psapi.EnumProcessModules(h, C.byref(base), C.sizeof(base), C.byref(needed))
    print(f"image base (first module): 0x{base.value:X}" if base.value else "image base unknown")

    regs = []
    addr = C.c_void_p(0)
    mbi = MEMORY_BASIC_INFORMATION()
    while C.windll.kernel32.VirtualQueryEx(h, addr, C.byref(mbi), C.sizeof(mbi)):
        if (mbi.State == MEM_COMMIT and not (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
                and mbi.RegionSize >= 16):
            regs.append((mbi.BaseAddress, mbi.RegionSize))
        nxt = (mbi.BaseAddress or 0) + mbi.RegionSize
        addr = C.c_void_p(nxt if nxt else 1)
        if not nxt:
            break
    print(f"{len(regs)} readable committed regions; scanning...")

    matches = {label: [] for label, *_ in PATTERNS}
    buf = C.create_string_buffer(1 << 20)
    for base_addr, size in regs:
        off = 0
        while off < size:
            chunk = min(1 << 20, size - off)
            read = C.c_size_t(0)
            ok = C.windll.kernel32.ReadProcessMemory(h, C.c_void_p(base_addr + off), buf,
                                                     chunk, C.byref(read))
            if not ok or read.value == 0:
                break
            data = buf.raw[:read.value]
            for label, pat, *_ in PATTERNS:
                if len(matches[label]) > 8:
                    continue
                rx = pattern_regex(pat)
                for m in rx.finditer(data):
                    matches[label].append(base_addr + off + m.start())
            off += read.value

    print()
    unique = 0
    suggestions = []
    for label, pat, poff, chain, pointers, moff, source in PATTERNS:
        hits = matches[label]
        if len(hits) == 1:
            unique += 1
            print(f"UNIQUE MATCH  {label} @ 0x{hits[0]:X}   (source: {source})")
            suggestions.append((pat, poff, chain, pointers, moff, source, hits[0]))
        elif not hits:
            print(f"no match      {label}")
        else:
            print(f"AMBIGUOUS     {label}: {len(hits)}+ hits @ "
                  + ", ".join(f"0x{a:X}" for a in hits[:4]))
    print()
    if not unique:
        print("No unique match. The build needs new patterns derived (CE/IDA session).", file=sys.stderr)
        return 2

    # Read game version for the section name.
    print("Paste candidate(s) into manifests/gtav_legacy.ini "
          "(verify IN GAME before marking verified=1):\n")
    for pat, poff, chain, pointers, moff, source, addr in suggestions:
        chain_line = f"{chain}\n" if chain.startswith("relativeOffsets") else f"{chain}\n"
        print(f"[b3788]  ; TODO: confirm this is the build you scanned")
        print(f"version=1.0.3788.0")
        print(f"cameraPattern={pat}")
        print(f"cameraPatternOffset={poff}")
        print(chain_line.rstrip())
        print(f"pointerOffsets={pointers}")
        print(f"matrixOffset={moff}")
        print(f"source=runtime scan of running build 3788 ({source})")
        print(f"verified=0  ; UNVERIFIED until camera control is confirmed in-game\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
