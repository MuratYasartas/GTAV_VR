import ctypes as C
import ctypes.wintypes as W
import re, struct, math, sys, time

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100
PAGE_READWRITE = 0x04
PAGE_WRITECOPY = 0x08
PAGE_EXECUTE_READWRITE = 0x40
PAGE_EXECUTE_WRITECOPY = 0x80
WRITABLE_PROTECTS = (PAGE_READWRITE, PAGE_WRITECOPY,
                     PAGE_EXECUTE_READWRITE, PAGE_EXECUTE_WRITECOPY)

PAT = "48 8B C7 F3 0F 10 0D"
PATTERN_OFFSET = -0x1D
REL_OFFSETS = (0, 3)
POINTER_OFFSET = 0
MATRIX_OFFSET = 0x1F0

def find_process(name):
    snap = C.windll.kernel32.CreateToolhelp32Snapshot(2, 0)
    class E(C.Structure):
        _fields_ = [("dwSize", W.DWORD), ("cntUsage", W.DWORD), ("th32ProcessID", W.DWORD),
                    ("th32DefaultHeapID", C.POINTER(C.c_ulong)), ("th32ModuleID", W.DWORD),
                    ("cntThreads", W.DWORD), ("th32ParentProcessID", W.DWORD),
                    ("pcPriClassBase", C.c_long), ("dwFlags", W.DWORD), ("szExeFile", C.c_char * 260)]
    e = E(); e.dwSize = C.sizeof(E)
    ok = C.windll.kernel32.Process32First(snap, C.byref(e))
    while ok:
        if e.szExeFile.decode("ascii", "ignore").lower() == name.lower():
            C.windll.kernel32.CloseHandle(snap)
            return e.th32ProcessID
        ok = C.windll.kernel32.Process32Next(snap, C.byref(e))
    C.windll.kernel32.CloseHandle(snap)
    return None

class MODULEINFO(C.Structure):
    _fields_ = [("lpBaseOfDll", C.c_void_p), ("SizeOfImage", W.DWORD), ("EntryPoint", C.c_void_p)]

def read(h, addr, size):
    buf = C.create_string_buffer(size)
    n = C.c_size_t(0)
    if not C.windll.kernel32.ReadProcessMemory(h, C.c_void_p(addr), buf, size, C.byref(n)) or n.value != size:
        return None
    return buf.raw

class MEMORY_BASIC_INFORMATION(C.Structure):
    _fields_ = [("BaseAddress", C.c_void_p), ("AllocationBase", C.c_void_p),
                ("AllocationProtect", W.DWORD), ("RegionSize", C.c_size_t),
                ("State", W.DWORD), ("Protect", W.DWORD), ("Type", W.DWORD)]

def mod_is_writable(h, addr, size):
    """Mirror of GtaCameraHook::IsWritable (in-process VirtualQuery gates)."""
    mbi = MEMORY_BASIC_INFORMATION()
    if not C.windll.kernel32.VirtualQueryEx(h, C.c_void_p(addr), C.byref(mbi), C.sizeof(mbi)):
        return False, "VirtualQuery failed"
    if mbi.State != MEM_COMMIT:
        return False, "not committed"
    if mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS):
        return False, "guard/noaccess"
    if addr + size > (mbi.BaseAddress or 0) + mbi.RegionSize:
        return False, "crosses region end"
    if (mbi.Protect & 0xFF) not in WRITABLE_PROTECTS:
        return False, f"protect 0x{mbi.Protect:02X} not writable"
    return True, f"protect 0x{mbi.Protect:02X}"

def mod_score_gates(f):
    """Mirror of the REJECT gates in GtaCameraHook::ScoreMatrix (16 floats,
    GTA layout right/forward/up/position). Returns (name, passed) pairs."""
    def vec(i): return f[4*i:4*i+3]
    def length(v): return math.sqrt(sum(x*x for x in v))
    def dot(a, b): return abs(sum(x*y for x, y in zip(a, b)))
    r, fw, u = vec(0), vec(1), vec(2)
    lr, lf, lu = length(r), length(fw), length(u)
    cross = (r[1]*fw[2] - r[2]*fw[1],
             r[2]*fw[0] - r[0]*fw[2],
             r[0]*fw[1] - r[1]*fw[0])
    pos_abs = max(abs(f[12]), abs(f[13]), abs(f[14]))
    return [
        ("basis lengths finite & in [0.1,3]",
         all(math.isfinite(x) for x in (lr, lf, lu)) and
         all(0.1 <= x <= 3 for x in (lr, lf, lu))),
        ("mutual dots <= 0.95", max(dot(r, fw), dot(r, u), dot(fw, u)) <= 0.95),
        ("|cross(right,forward)| >= 0.1", length(cross) >= 0.1),
        ("max|pos.xyz| <= 1e7", pos_abs <= 1e7),  # NaN passes, as in the C++
        ("w row & posW finite",
         math.isfinite(abs(f[3]) + abs(f[7]) + abs(f[11])) and math.isfinite(f[15])),
    ]

def main():
    pid = find_process("GTA5.exe")
    if not pid:
        print("GTA5.exe not running"); return 3
    h = C.windll.kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
    if not h:
        print(f"OpenProcess failed err={C.GetLastError()} (run elevated)"); return 3

    mod = C.c_void_p()
    needed = W.DWORD()
    C.windll.psapi.EnumProcessModules(h, C.byref(mod), C.sizeof(mod), C.byref(needed))
    mi = MODULEINFO()
    C.windll.psapi.GetModuleInformation(h, mod, C.byref(mi), C.sizeof(mi))
    base, size = mi.lpBaseOfDll, mi.SizeOfImage
    print(f"image: base=0x{base:X} size=0x{size:X}")

    CHUNK = 1 << 20
    rx = re.compile(re.escape(bytes(int(t, 16) for t in PAT.split())))
    hits = []
    off = 0
    buf = C.create_string_buffer(CHUNK)
    while off < size:
        n = C.c_size_t(0)
        want = min(CHUNK, size - off)
        ok = C.windll.kernel32.ReadProcessMemory(h, C.c_void_p(base + off), buf, want, C.byref(n))
        if ok and n.value:
            data = buf.raw[:n.value]
            hits.extend(base + off + m.start() for m in rx.finditer(data))
            off += n.value
        else:
            off += want
    print(f"in-image hits: {[hex(x) for x in hits]}")
    if len(hits) != 1:
        print("FAIL: not a unique in-image match"); return 2

    ea = hits[0] + PATTERN_OFFSET
    print(f"after patternOffset: 0x{ea:X}")
    for ro in REL_OFFSETS:
        raw = read(h, ea + ro, 4)
        if raw is None:
            print(f"FAIL: cannot read disp32 at 0x{ea + ro:X}"); return 2
        disp = struct.unpack("<i", raw)[0]
        ea = ea + ro + 4 + disp
        print(f"after relative offset {ro}: 0x{ea:X}")

    raw = read(h, ea + POINTER_OFFSET, 8)
    if raw is None:
        print("FAIL: cannot read camera pointer"); return 2
    cam = struct.unpack("<Q", raw)[0]
    print(f"camera object: 0x{cam:X}")
    if cam < 0x10000 or cam > 0x00007FFF_FFFFFFFF:
        print("FAIL: camera pointer implausible"); return 2

    maddr = cam + MATRIX_OFFSET
    raw = read(h, maddr, 64)
    if raw is None:
        print(f"FAIL: cannot read matrix at 0x{maddr:X}"); return 2
    f = struct.unpack("<16f", raw)
    print(f"matrix @ 0x{maddr:X}:")
    names = ("right", "forward", "up", "position")
    for i, nm in enumerate(names):
        print(f"  {nm:9s} [{f[4*i]:+.4f} {f[4*i+1]:+.4f} {f[4*i+2]:+.4f} {f[4*i+3]:+.4f}]")

    def vec(i): return f[4*i:4*i+3]
    def length(v): return math.sqrt(sum(x*x for x in v))
    def dot(a, b): return abs(sum(x*y for x, y in zip(a, b)))
    lr, lf, lu = length(vec(0)), length(vec(1)), length(vec(2))
    checks = [
        ("basis lengths finite & in [0.1,3]", all(math.isfinite(x) and 0.1 <= x <= 3 for x in (lr, lf, lu))),
        ("mutual dots < 0.95", max(dot(vec(0), vec(1)), dot(vec(0), vec(2)), dot(vec(1), vec(2))) < 0.95),
        ("w row ~ 0", abs(f[3]) + abs(f[7]) + abs(f[11]) < 0.01),
        ("position w finite", math.isfinite(f[15])),
    ]
    ok = True
    for name, passed in checks:
        print(f"  [{'OK' if passed else 'FAIL'}] {name}")
        ok = ok and passed
    print("VERDICT:", "PLAUSIBLE CAMERA MATRIX" if ok else "NOT a camera matrix")

    # --- In-process gate mirror (what OVRInject's GtaCameraHook would do) ---
    # The mod validates a candidate with gates this script historically did
    # not check: IsWritable (VirtualQuery), the ScoreMatrix reject gates, and
    # a stability re-read ~40ms later (ValidateAndPublishCandidate). A chain
    # that resolves but trips one of these is exactly how "external tool OK,
    # mod stays mono" happens - mirror them so this tool catches it.
    mod_ok = ok
    writable, why = mod_is_writable(h, maddr, 64)
    print(f"  [{'OK' if writable else 'FAIL'}] mod IsWritable gate ({why})")
    mod_ok = mod_ok and writable

    for name, passed in mod_score_gates(f):
        print(f"  [{'OK' if passed else 'FAIL'}] mod ScoreMatrix gate: {name}")
        mod_ok = mod_ok and passed

    time.sleep(0.04)  # ValidateAndPublishCandidate stability sample #2
    raw2 = read(h, maddr, 64)
    stable = raw2 is not None
    if stable:
        stable = all(p for _, p in mod_score_gates(struct.unpack("<16f", raw2)))
    print(f"  [{'OK' if stable else 'FAIL'}] mod stability re-read (~40ms apart)")
    mod_ok = mod_ok and stable

    print("MOD GATE MIRROR:", "mod would ACCEPT this candidate" if mod_ok
          else "mod would REJECT this candidate (see FAILED gates above)")
    return 0 if mod_ok else 2

sys.exit(main())
