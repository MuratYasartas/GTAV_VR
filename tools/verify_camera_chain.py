import ctypes as C
import ctypes.wintypes as W
import re, struct, math, sys

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

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
    return 0 if ok else 2

sys.exit(main())
