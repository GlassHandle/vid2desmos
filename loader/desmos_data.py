from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterator, List, Sequence

import numpy as np

_NAME = r"[A-Za-z](?:_\{[^}]*\})?"
_LIST_RE = re.compile(rf"^({_NAME})=\[([-\d.,eE+\s]*)\]$")
_JOIN_RE = re.compile(rf"^({_NAME})=join\((.*)\)$")
_SCALAR_RE = re.compile(rf"^({_NAME})=(-?[\d.]+(?:[eE][-+]?\d+)?)$")

class ParseError(RuntimeError):
    pass

@dataclass
class Part:
    grid_w: int
    grid_h: int

    data: np.ndarray

    starts: np.ndarray
    path: Path | None = None
    first_frame: int = 0

    @property
    def frame_count(self) -> int:
        return len(self.starts) - 1

    def packed(self, index: int) -> np.ndarray:
        if not 0 <= index < self.frame_count:
            raise IndexError(f"frame {index} out of range (0..{self.frame_count - 1})")

        lo = int(self.starts[index]) - 1
        hi = int(self.starts[index + 1]) - 1
        return self.data[lo:hi]

    def regions(self, index: int) -> np.ndarray:
        return unpack(self.packed(index), self.grid_w, self.grid_h)

@dataclass
class Animation:

    grid_w: int
    grid_h: int
    parts: List[Part] = field(default_factory=list)
    fps: float = 15.0
    source: Path | None = None

    @property
    def frame_count(self) -> int:
        return sum(p.frame_count for p in self.parts)

    def _locate(self, index: int) -> tuple[Part, int]:
        if index < 0:
            index += self.frame_count
        if not 0 <= index < self.frame_count:
            raise IndexError(f"frame {index} out of range (0..{self.frame_count - 1})")
        for part in self.parts:
            if index < part.frame_count:
                return part, index
            index -= part.frame_count
        raise AssertionError("unreachable: frame index within range but not found")

    def packed(self, index: int) -> np.ndarray:
        part, local = self._locate(index)
        return part.packed(local)

    def regions(self, index: int) -> np.ndarray:
        part, local = self._locate(index)
        return part.regions(local)

    def flatten(self) -> tuple[np.ndarray, np.ndarray]:
        chunks = [self.packed(i) for i in range(self.frame_count)]
        starts = np.empty(self.frame_count + 1, dtype=np.int64)
        starts[0] = 1
        for i, chunk in enumerate(chunks):
            starts[i + 1] = starts[i] + len(chunk)
        data = (
            np.concatenate(chunks) if chunks else np.zeros(0, dtype=np.int64)
        )
        return data, starts

    def raster(self, index: int) -> np.ndarray:
        out = np.zeros((self.grid_h, self.grid_w), dtype=np.uint8)
        for x, y, w, h in self.regions(index):
            if w and h:
                out[y : y + h, x : x + w] = 1
        return out

    def region_counts(self) -> List[int]:
        return [len(self.regions(i)) for i in range(self.frame_count)]

    def __len__(self) -> int:
        return self.frame_count

    def __iter__(self) -> Iterator[np.ndarray]:
        for i in range(self.frame_count):
            yield self.raster(i)

def unpack(packed: np.ndarray, grid_w: int, grid_h: int) -> np.ndarray:
    p = np.asarray(packed, dtype=np.int64)
    if p.size == 0:
        return np.zeros((0, 4), dtype=np.int64)

    h = p % (grid_h + 1)
    t = p // (grid_h + 1)
    w = t % (grid_w + 1)
    q = t // (grid_w + 1)
    x = q % grid_w
    y = q // grid_w
    return np.stack([x, y, w, h], axis=1)

def _parse_numbers(body: str) -> np.ndarray:
    body = body.strip()
    if not body:
        return np.zeros(0, dtype=np.int64)
    try:
        return np.fromstring(body, dtype=np.int64, sep=",")
    except (ValueError, DeprecationWarning):
        return np.array([int(tok) for tok in body.split(",")], dtype=np.int64)

def parse_expressions(text: str) -> Dict[str, object]:
    lists: Dict[str, np.ndarray] = {}
    joins: Dict[str, List[str]] = {}
    scalars: Dict[str, float] = {}

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        m = _LIST_RE.match(line)
        if m:
            lists[m.group(1)] = _parse_numbers(m.group(2))
            continue
        m = _JOIN_RE.match(line)
        if m:
            joins[m.group(1)] = [c.strip() for c in m.group(2).split(",")]
            continue
        m = _SCALAR_RE.match(line)
        if m:
            scalars[m.group(1)] = float(m.group(2))

    for name, chunks in joins.items():
        missing = [c for c in chunks if c not in lists]
        if missing:
            raise ParseError(f"{name}=join(...) refers to missing list(s): {', '.join(missing)}")
        lists[name] = np.concatenate([lists[c] for c in chunks])

    return {"lists": lists, "scalars": scalars}

def load_part(path: str | Path) -> Part:
    path = Path(path)
    parsed = parse_expressions(path.read_text(encoding="utf-8"))
    lists = parsed["lists"]
    scalars = parsed["scalars"]

    for key in ("G_{w}", "G_{h}"):
        if key not in scalars:
            raise ParseError(f"{path.name}: missing grid size '{key}'")
    for key in ("D", "S"):
        if key not in lists:
            raise ParseError(f"{path.name}: missing list '{key}'")

    part = Part(
        grid_w=int(scalars["G_{w}"]),
        grid_h=int(scalars["G_{h}"]),
        data=lists["D"],
        starts=lists["S"],
        path=path,
    )

    if part.frame_count < 1:
        raise ParseError(f"{path.name}: offset list S has no frames")
    if int(part.starts[-1]) != len(part.data) + 1:
        raise ParseError(
            f"{path.name}: S ends at {int(part.starts[-1])} but D holds "
            f"{len(part.data)} values; the file looks truncated"
        )
    return part

def load(path: str | Path) -> Animation:
    path = Path(path)

    if path.is_file():
        part = load_part(path)
        return Animation(
            grid_w=part.grid_w, grid_h=part.grid_h, parts=[part], source=path
        )

    if not path.is_dir():
        raise FileNotFoundError(f"No such file or directory: {path}")

    fps = 15.0
    files: List[Path] = []
    manifest_path = path / "manifest.json"

    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        fps = float(manifest.get("fps", fps)) or fps
        for entry in manifest.get("parts", []):
            candidate = path / entry["expressions"]
            if not candidate.exists():
                raise ParseError(
                    f"manifest.json lists '{entry['expressions']}', which is missing"
                )
            files.append(candidate)

    if not files:
        files = sorted(path.glob("*_part*.txt")) or sorted(path.glob("*.txt"))
    if not files:
        raise FileNotFoundError(f"No vid2desmos expression files found in {path}")

    parts = [load_part(f) for f in files]
    grids = {(p.grid_w, p.grid_h) for p in parts}
    if len(grids) > 1:
        raise ParseError(f"parts disagree on grid size: {sorted(grids)}")

    first = 0
    for p in parts:
        p.first_frame = first
        first += p.frame_count

    return Animation(
        grid_w=parts[0].grid_w,
        grid_h=parts[0].grid_h,
        parts=parts,
        fps=fps,
        source=path,
    )

def describe(anim: Animation) -> str:
    counts: Sequence[int] = anim.region_counts()
    total = sum(counts)
    lines = [
        f"source:  {anim.source}",
        f"grid:    {anim.grid_w}x{anim.grid_h}",
        f"frames:  {anim.frame_count} at {anim.fps:g} fps "
        f"({anim.frame_count / anim.fps:.1f}s)",
        f"parts:   {len(anim.parts)}",
        f"regions: {total} total, {total / max(1, len(counts)):.0f} avg, "
        f"{max(counts, default=0)} max",
    ]
    cells = anim.grid_w * anim.grid_h
    lit = sum(int(w) * int(h) for i in range(anim.frame_count) for _, _, w, h in anim.regions(i))
    lines.append(
        f"fill:    {lit / max(1, anim.frame_count):.0f} of {cells} cells per frame "
        f"({100.0 * lit / max(1, anim.frame_count * cells):.1f}%)"
    )
    for p in anim.parts:
        lines.append(
            f"  part {p.first_frame:>5}..{p.first_frame + p.frame_count - 1:<5} "
            f"{p.frame_count:>4} frames, {len(p.data):>6} regions  "
            f"{p.path.name if p.path else ''}"
        )
    return "\n".join(lines)
