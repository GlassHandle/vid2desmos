from __future__ import annotations

import json
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import desmos_data
from desmos_data import ParseError

GW, GH = 16, 8

def pack(x: int, y: int, w: int, h: int, gw: int = GW, gh: int = GH) -> int:
    return ((y * gw + x) * (gw + 1) + w) * (gh + 1) + h

def write_part(path: Path, frames: list[list[tuple]], chunk: int | None = None) -> None:
    data: list[int] = []
    starts = [1]
    for regions in frames:
        data.extend(pack(*r) for r in (regions or [(0, 0, 0, 0)]))
        starts.append(len(data) + 1)

    lines = [f"G_{{w}}={GW}", f"G_{{h}}={GH}"]
    if chunk and len(data) > chunk:
        names = []
        for i in range(0, len(data), chunk):
            name = f"D_{{{i // chunk + 1}}}"
            names.append(name)
            lines.append(f"{name}=[" + ",".join(str(v) for v in data[i:i + chunk]) + "]")
        lines.append("D=join(" + ",".join(names) + ")")
    else:
        lines.append("D=[" + ",".join(str(v) for v in data) + "]")

    lines += [
        "S=[" + ",".join(str(v) for v in starts) + "]",
        "f=0",
        "I=[S[f+1]...S[f+2]-1]",
        "R=D[I]",
        "h_{r}=mod(R,G_{h}+1)",
        "polygon((x_{r},y_{r}),(x_{r}+w_{r},y_{r}))",
    ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

class Checker:
    def __init__(self) -> None:
        self.checks = 0
        self.failures = 0

    def __call__(self, ok: bool, what: str) -> None:
        self.checks += 1
        if not ok:
            self.failures += 1
            print(f"  FAIL: {what}")

check = Checker()

def test_unpack_roundtrip() -> None:
    print("unpack")
    rng = np.random.default_rng(7)
    n = 5000
    x = rng.integers(0, GW, n)
    y = rng.integers(0, GH, n)
    w = rng.integers(0, GW + 1, n)
    h = rng.integers(0, GH + 1, n)
    packed = np.array([pack(*t) for t in zip(x, y, w, h)])

    out = desmos_data.unpack(packed, GW, GH)
    check(np.array_equal(out[:, 0], x), "unpack recovers x")
    check(np.array_equal(out[:, 1], y), "unpack recovers y")
    check(np.array_equal(out[:, 2], w), "unpack recovers w")
    check(np.array_equal(out[:, 3], h), "unpack recovers h")
    check(desmos_data.unpack(np.array([]), GW, GH).shape == (0, 4), "empty unpack is (0,4)")

def test_single_part(tmp: Path) -> None:
    print("single part")
    frames = [
        [(1, 2, 4, 3)],
        [(0, 0, 2, 2), (5, 5, 1, 1)],
        [],
    ]
    path = tmp / "one_part01.txt"
    write_part(path, frames)

    anim = desmos_data.load(path)
    check(anim.frame_count == 3, "frame count")
    check((anim.grid_w, anim.grid_h) == (GW, GH), "grid size is read from the file")

    r0 = anim.regions(0)
    check(len(r0) == 1 and tuple(r0[0]) == (1, 2, 4, 3), "frame 0 regions round-trip")
    check(len(anim.regions(1)) == 2, "frame 1 has both regions")

    r2 = anim.regions(2)
    check(len(r2) == 1 and r2[0][2] == 0 and r2[0][3] == 0, "empty frame is a zero-size region")
    check(anim.raster(2).sum() == 0, "empty frame rasters blank")

    img = anim.raster(0)
    check(img.shape == (GH, GW), "raster shape is (h, w)")
    check(img[2:5, 1:5].all() and img.sum() == 12, "raster places the region y-up")

    check(len(list(anim)) == 3, "iterating yields every frame")

def test_join_chunks(tmp: Path) -> None:
    print("join chunking")
    frames = [[(i % GW, 0, 1, 1)] for i in range(20)]
    path = tmp / "chunked_part01.txt"
    write_part(path, frames, chunk=3)

    anim = desmos_data.load(path)
    check(anim.frame_count == 20, "join: every frame present")
    ok = all(tuple(anim.regions(i)[0]) == (i % GW, 0, 1, 1) for i in range(20))
    check(ok, "join: chunks reassemble in order")

def test_multipart(tmp: Path) -> None:
    print("multi-part")
    out = tmp / "multi"
    out.mkdir()
    write_part(out / "frames_part01.txt", [[(0, 0, 1, 1)], [(1, 0, 1, 1)]])
    write_part(out / "frames_part02.txt", [[(2, 0, 1, 1)], [(3, 0, 1, 1)], [(4, 0, 1, 1)]])
    (out / "manifest.json").write_text(
        json.dumps(
            {
                "gridWidth": GW,
                "gridHeight": GH,
                "fps": 12,
                "parts": [
                    {"index": 1, "expressions": "frames_part01.txt"},
                    {"index": 2, "expressions": "frames_part02.txt"},
                ],
            }
        ),
        encoding="utf-8",
    )

    anim = desmos_data.load(out)
    check(anim.frame_count == 5, "parts are stitched into one sequence")
    check(anim.fps == 12, "fps comes from the manifest")
    check(len(anim.parts) == 2, "both parts loaded")
    ok = all(tuple(anim.regions(i)[0]) == (i, 0, 1, 1) for i in range(5))
    check(ok, "frames stay in order across the part boundary")
    check(tuple(anim.regions(-1)[0]) == (4, 0, 1, 1), "negative indexing reaches the last frame")

    (out / "manifest.json").unlink()
    anim2 = desmos_data.load(out)
    check(anim2.frame_count == 5, "parts load without a manifest")

    raised = False
    try:
        anim.regions(5)
    except IndexError:
        raised = True
    check(raised, "out-of-range frame raises IndexError")

def test_errors(tmp: Path) -> None:
    print("errors")

    def expect_parse_error(text: str, what: str) -> None:
        p = tmp / "bad_part01.txt"
        p.write_text(text, encoding="utf-8")
        raised = False
        try:
            desmos_data.load(p)
        except ParseError:
            raised = True
        check(raised, what)

    expect_parse_error("G_{w}=16\nG_{h}=8\nS=[1,2]\n", "missing D is reported")
    expect_parse_error("D=[0]\nS=[1,2]\n", "missing grid size is reported")

    expect_parse_error("G_{w}=16\nG_{h}=8\nD=[0]\nS=[1,9]\n", "truncated data is reported")
    expect_parse_error(
        "G_{w}=16\nG_{h}=8\nD=join(D_{1},D_{2})\nD_{1}=[0]\nS=[1,2]\n",
        "join referring to a missing chunk is reported",
    )

    raised = False
    try:
        desmos_data.load(tmp / "does_not_exist")
    except FileNotFoundError:
        raised = True
    check(raised, "a missing path raises FileNotFoundError")

def main() -> int:
    print("vid2desmos loader tests\n")
    tmp = Path(tempfile.mkdtemp(prefix="v2d_loader_"))
    try:
        test_unpack_roundtrip()
        test_single_part(tmp)
        test_join_chunks(tmp)
        test_multipart(tmp)
        test_errors(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print(f"\n{check.checks - check.failures}/{check.checks} checks passed")
    return 1 if check.failures else 0

if __name__ == "__main__":
    sys.exit(main())
