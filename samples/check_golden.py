#!/usr/bin/env python3
"""Stereo-correctness checker for the D3D11Cube golden frames (GTAVR slice).

Stdlib only. Verifies, for a pair of golden runs produced by D3D11Cube.exe
with GTAVR_SLICE_GOLDEN=N:

  (a) L vs R frames of every pair differ (stereo is actually active).
  (b) Disparity ordering/direction: the horizontal centroid shift between
      L and R is positive (left eye at x=-0.0315 => scene shifts right in
      the left-eye image) and larger for the near cube (red, z=1m) than the
      mid cube (green, z=3m) than the far cube (blue, z=10m). Cubes are
      detected by their unique pure colours.
  (c) Determinism: two golden runs produce byte-identical BMPs.

Usage:
    python samples/check_golden.py [run_a_dir] [run_b_dir]

Defaults: run_a_dir = samples/D3D11Cube/golden_run_a
          run_b_dir = samples/D3D11Cube/golden_out
Stereo checks run on run_b_dir; run_a_dir is only used for determinism.

Exits 0 on success, 1 with a clear message on any failure.
"""

import os
import re
import struct
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SLICE_DIR = os.path.join(SCRIPT_DIR, "D3D11Cube")

# Expected golden-mode geometry (must match D3D11Cube.cpp).
EXPECTED_WIDTH = 1280
EXPECTED_HEIGHT = 720
EXPECTED_HALF_IPD = 0.0315  # metres; L eye at -, R eye at +
FOV_SCALE_X = 0.8036  # (1/tan(35 deg)) / (16/9); for sanity bounds only

# Unique cube colours (B, G, R byte triples as stored in 24-bit BMP).
CUBE_COLOURS = {
    "near(z=1m,red)": (0, 0, 255),
    "mid(z=3m,green)": (0, 255, 0),
    "far(z=10m,blue)": (255, 0, 0),
}

FRAME_RE = re.compile(r"^frame_(\d{4})_([LR])\.bmp$")

# Disparity thresholds in pixels (expected: ~32 near, ~11 mid, ~3.2 far).
MIN_FAR_DISPARITY = 1.0
MIN_MID_OVER_FAR = 2.0
MIN_NEAR_OVER_MID = 5.0
MIN_COLOUR_PIXELS = 100
MAX_VERTICAL_DRIFT = 2.0


class CheckFailure(Exception):
    pass


def read_bmp(path):
    """Parse an uncompressed 24-bit BMP. Returns (width, height, rows)
    where rows[y] is a bytes object of BGR triples, y=0 at the top."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise CheckFailure("%s: not a BMP file" % path)
    off = struct.unpack_from("<I", data, 10)[0]
    dib = struct.unpack_from("<I", data, 14)[0]
    if dib < 40:
        raise CheckFailure("%s: unsupported DIB header %d" % (path, dib))
    width = struct.unpack_from("<i", data, 18)[0]
    height = struct.unpack_from("<i", data, 22)[0]
    bpp = struct.unpack_from("<H", data, 28)[0]
    comp = struct.unpack_from("<I", data, 30)[0]
    if bpp != 24 or comp != 0:
        raise CheckFailure("%s: expected uncompressed 24-bit BMP, got bpp=%d comp=%d"
                           % (path, bpp, comp))
    top_down = height < 0
    height = abs(height)
    stride = ((width * 3 + 3) // 4) * 4
    if len(data) < off + stride * height:
        raise CheckFailure("%s: truncated pixel data" % path)
    rows = []
    for y in range(height):
        src_y = y if top_down else (height - 1 - y)
        start = off + src_y * stride
        rows.append(data[start:start + width * 3])
    return width, height, rows


def centroid_x(rows, width, height, colour_bgr):
    """Mean (x, y, count) of pixels exactly matching colour_bgr.

    Golden frames contain only flat exact colours (no AA/MSAA), so exact
    matching is precise. Uses bytes.find, which runs at C speed."""
    pattern = bytes(colour_bgr)
    total_x = 0
    total_y = 0
    count = 0
    for y in range(height):
        row = rows[y]
        i = row.find(pattern)
        while i != -1:
            if i % 3 == 0:
                total_x += i // 3
                total_y += y
                count += 1
            i = row.find(pattern, i + 1)
    if count == 0:
        return None
    return (total_x / count, total_y / count, count)


def collect_pairs(directory):
    """Return sorted [(frame_index, l_path, r_path)] for consecutive L/R pairs."""
    frames = {}
    for name in os.listdir(directory):
        m = FRAME_RE.match(name)
        if m:
            idx = int(m.group(1))
            eye = m.group(2)
            frames.setdefault(idx, {})[eye] = os.path.join(directory, name)
    pairs = []
    for idx in sorted(frames):
        if idx % 2 == 0 and "L" in frames[idx] and (idx + 1) in frames and \
                "R" in frames[idx + 1]:
            pairs.append((idx, frames[idx]["L"], frames[idx + 1]["R"]))
    return pairs


def fail(msg):
    print("FAIL: %s" % msg)
    sys.exit(1)


def check_stereo(directory):
    pairs = collect_pairs(directory)
    if not pairs:
        fail("no consecutive frame_XXXX_L/R pairs found in %s" % directory)
    print("Stereo checks on %s (%d L/R pairs)" % (directory, len(pairs)))

    # (a) L vs R frames differ.
    total_diff_bytes = 0
    for idx, l_path, r_path in pairs:
        with open(l_path, "rb") as f:
            l_bytes = f.read()
        with open(r_path, "rb") as f:
            r_bytes = f.read()
        if l_bytes == r_bytes:
            fail("pair %d: L and R frames are byte-identical (%s)" % (idx, l_path))
        total_diff_bytes += sum(1 for a, b in zip(l_bytes, r_bytes) if a != b)
    print("  (a) L vs R frames differ in every pair "
          "(%d pixel bytes differ on average)" % (total_diff_bytes // len(pairs)))

    # (b) Disparity per cube colour, averaged over all pairs.
    per_cube = {name: [] for name in CUBE_COLOURS}
    vertical_drifts = {name: [] for name in CUBE_COLOURS}
    for idx, l_path, r_path in pairs:
        lw, lh, l_rows = read_bmp(l_path)
        rw, rh, r_rows = read_bmp(r_path)
        if (lw, lh) != (EXPECTED_WIDTH, EXPECTED_HEIGHT) or (rw, rh) != (lw, lh):
            fail("pair %d: unexpected dimensions (%dx%d vs %dx%d)"
                 % (idx, lw, lh, rw, rh))
        for name, colour in CUBE_COLOURS.items():
            l_c = centroid_x(l_rows, lw, lh, colour)
            r_c = centroid_x(r_rows, rw, rh, colour)
            if l_c is None or r_c is None:
                fail("pair %d: cube %s not found (L=%s, R=%s pixels)"
                     % (idx, name,
                        "missing" if l_c is None else l_c[2],
                        "missing" if r_c is None else r_c[2]))
            if l_c[2] < MIN_COLOUR_PIXELS or r_c[2] < MIN_COLOUR_PIXELS:
                fail("pair %d: cube %s has too few pixels (L=%d, R=%d)"
                     % (idx, name, l_c[2], r_c[2]))
            per_cube[name].append(l_c[0] - r_c[0])
            vertical_drifts[name].append(abs(l_c[1] - r_c[1]))

    means = {}
    print("  (b) measured disparity (centroid X in L minus X in R, pixels):")
    for name in CUBE_COLOURS:
        vals = per_cube[name]
        means[name] = sum(vals) / len(vals)
        drift = sum(vertical_drifts[name]) / len(vals)
        print("      %-18s disparity %+7.2f px  (min %+.2f, max %+.2f, "
              "mean |dY| %.3f)" % (name, means[name], min(vals), max(vals), drift))
        if drift > MAX_VERTICAL_DRIFT:
            fail("cube %s: vertical drift %.2f px exceeds %.2f px "
                 "(cameras should differ only in X)" % (name, drift, MAX_VERTICAL_DRIFT))

    near = means["near(z=1m,red)"]
    mid = means["mid(z=3m,green)"]
    far = means["far(z=10m,blue)"]

    # Direction: with the left eye at x = -0.0315m the scene shifts right in
    # the left-eye image, so every disparity must be positive.
    for name, disp in means.items():
        if disp <= 0:
            fail("cube %s: disparity %.2f px has the wrong sign for IPD +/-%.4fm "
                 "(expected positive: X_L > X_R)" % (name, disp, EXPECTED_HALF_IPD))

    # Depth ordering: nearer cubes must show larger disparity.
    if far < MIN_FAR_DISPARITY:
        fail("far cube disparity %.2f px below minimum %.2f px "
             "(stereo separation too small to trust)" % (far, MIN_FAR_DISPARITY))
    if mid < far + MIN_MID_OVER_FAR:
        fail("mid cube disparity %.2f px not sufficiently larger than far %.2f px"
             % (mid, far))
    if near < mid + MIN_NEAR_OVER_MID:
        fail("near cube disparity %.2f px not sufficiently larger than mid %.2f px"
             % (near, mid))
    print("      OK: 0 < far(%.2f) < mid(%.2f) < near(%.2f), direction matches "
          "IPD sign" % (far, mid, near))


def check_determinism(dir_a, dir_b):
    names_a = sorted(n for n in os.listdir(dir_a) if FRAME_RE.match(n))
    names_b = sorted(n for n in os.listdir(dir_b) if FRAME_RE.match(n))
    if not names_a:
        fail("no golden frames in %s" % dir_a)
    if names_a != names_b:
        fail("frame sets differ between runs:\n  %s: %s\n  %s: %s"
             % (dir_a, names_a, dir_b, names_b))
    for name in names_a:
        with open(os.path.join(dir_a, name), "rb") as f:
            a = f.read()
        with open(os.path.join(dir_b, name), "rb") as f:
            b = f.read()
        if a != b:
            fail("determinism broken: %s differs between %s and %s"
                 % (name, dir_a, dir_b))
    print("  (c) determinism: %d frames byte-identical between %s and %s"
          % (len(names_a), dir_a, dir_b))


def main():
    run_a = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SLICE_DIR, "golden_run_a")
    run_b = sys.argv[2] if len(sys.argv) > 2 else os.path.join(SLICE_DIR, "golden_out")

    if not os.path.isdir(run_b):
        fail("golden output dir not found: %s (run D3D11Cube.exe with "
             "GTAVR_SLICE_GOLDEN=N first)" % run_b)
    if not os.path.isdir(run_a):
        fail("second golden run not found: %s (determinism needs two runs; "
             "see samples/README.md)" % run_a)

    check_stereo(run_b)
    check_determinism(run_a, run_b)
    print("PASS: all golden stereo checks succeeded.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
