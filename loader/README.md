# vid2desmos loader

Reads `vid2desmos` output back and previews it — no Desmos account, no Desmos
API, no internet. Two front ends over one parser:

```bash
pip install -r loader/requirements.txt

python loader/serve.py out/     # browser viewer with a Desmos-like UI
python loader/view.py  out/     # matplotlib window, video export
```

`serve.py` needs nothing beyond the standard library and numpy. matplotlib is
required only by `view.py`.

## Why it parses the expressions

Both viewers read the emitted `frames_partNN.txt` — the same text you paste into
Desmos — and re-implement the graph's arithmetic: 1-indexed lists, the
`[S[n+1]...S[n+2]-1]` range slice, the `mod`/`floor` unpack, and `join()` chunk
reassembly.

They deliberately do not read a private side format. That makes them an
independent check on the C++ encoder: if the preview is right, the expressions
are right. Verified against the encoder's own `--debug-frames` PNGs at zero
mismatched pixels.

They also stitch parts back together. Splitting output into parts is a Desmos
expression-count limit, not a viewing limit, so `out/` plays as one continuous
sequence.

## serve.py — browser viewer

```bash
python loader/serve.py out/              # opens http://127.0.0.1:8000
python loader/serve.py out/ --port 9000
python loader/serve.py out/ --no-browser
```

Serves a page laid out like Desmos — expression list down the left, graph paper
filling the rest, frame slider with a play button inside the `f=` row — but the
resemblance stops at the layout. The renderer is canvas `fillRect` calls in
[web/app.js](web/app.js) that unpack the same packed numbers Desmos would.
Nothing loads from the network. If the port is busy it steps to the next free
one.

The expression panel shows the real emitted expressions with subscripts
rendered, and abbreviates the huge literal lists — `D=[203062953,…]` plus a
"2,941 values" note — so the panel stays usable.

| Control | |
| --- | --- |
| Drag / wheel | Pan and zoom |
| Space | Play / pause |
| ← → | Step one frame |
| Home / End | First / last frame |
| `+` `−` `⌂` | Zoom in, out, reset view |
| `#` | Toggle grid and axes |
| `◑` | Swap black/white (matches `--dark-ink`) |
| `↓` | Save the current frame as a PNG |

Frames are sent once as two binary `Float64Array` blobs (`/api/d.bin`,
`/api/s.bin`) rather than JSON, so startup is a single small transfer and
scrubbing the slider needs no further requests.

## view.py — matplotlib

```bash
python loader/view.py out/ --info            # stats only, works headless
python loader/view.py out/ --frame 42        # a single frame
python loader/view.py out/ --outline         # show the rectangle merging
python loader/view.py out/ --save clip.mp4   # write a video
```

| Flag | Meaning |
| --- | --- |
| `--info` | Print stats and exit |
| `--frame N` | Render one frame instead of animating |
| `--save FILE` | Write `.mp4` (ffmpeg) or `.gif` (pillow) and exit |
| `--mode raster\|rects` | `raster` fills an array and uses `imshow` (fast, default). `rects` draws each encoded rectangle as a real polygon |
| `--outline` | Outline every rectangle in red; implies `--mode rects` |
| `--fps N` | Override playback speed (default comes from `manifest.json`) |
| `--dark-ink` | Black regions on white |
| `--title` | Show frame number and region count |
| `--loop` | Repeat the animation |

`--outline` is the useful debugging view: a flat area collapses to one tall
rectangle while dithered or diagonal content fragments into many small ones. It
shows directly why `--threshold-mode dither` costs ~20× the regions.

`.gif` export needs pillow, which is not in `requirements.txt` since it is
optional.

## As a library

`desmos_data` has no matplotlib dependency and is usable headless:

```python
import sys; sys.path.insert(0, "loader")
import desmos_data

anim = desmos_data.load("out/")
print(anim.frame_count, anim.grid_w, anim.grid_h, anim.fps)

frame = anim.raster(0)      # (grid_h, grid_w) uint8 of 0/1, row 0 = bottom
regions = anim.regions(0)   # (N, 4) array of x, y, w, h in Desmos coords
print(desmos_data.describe(anim))
```

Coordinates are Desmos's: origin bottom-left, y increasing upwards. `raster()`
returns rows bottom-up to match, so display it with `origin="lower"`.

## Tests

```bash
python loader/test_loader.py
```

Self-contained — it builds its own expression files rather than needing
fixtures, and covers the unpack round-trip, `join()` chunk reassembly, frame
slicing, raster orientation, multi-part stitching, and the parse errors for
truncated or malformed files. `ctest` runs it automatically when numpy is
present.

The browser renderer is JavaScript and outside this suite. It was verified
in-browser against the same data — identical region counts, region totals, and
lit-cell totals to the C++ encoder and the Python parser.
