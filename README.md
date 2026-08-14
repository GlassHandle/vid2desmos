# vid2desmos

Turns a video into a black-and-white animation made of filled rectangles, played
back in Desmos with a frame slider.

```
video → ffmpeg decode → resize → grayscale → convolution → threshold
      → binary frame → merged rectangles → packed Desmos expressions
```

A 75-frame clip at 160×90 comes out as **17 Desmos expressions in total** — not
17 per frame. The whole animation is a single vectorised `polygon()` call over a
packed list, sliced per frame by a slider.

```bash
vid2desmos clip.mp4 --width 160 --fps 15 --output out/
python loader/serve.py out/          # preview in a browser, no Desmos needed
```

---

## Contents

- [Build](#build) · [Usage](#usage) · [Getting it into Desmos](#getting-it-into-desmos)
- [Previewing locally](#previewing-locally) · [Image processing](#image-processing)
- [How the encoding works](#how-the-encoding-works) · [Desmos's limits](#designing-around-desmoss-limits)
- [Layout](#layout) · [Implementation notes](#implementation-notes) · [Tests](#tests)

---

## Build

Requirements:

- CMake 3.16+ and a C++17 compiler (tested with MSVC 14.51; GCC 9+/Clang 10+ work)
- `ffmpeg` and `ffprobe` on `PATH` at **runtime** — not needed to build

There are no build-time third-party dependencies. PNG writing and all image
processing are implemented in-tree.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

The binary lands in `build/bin/Release/vid2desmos`, or `build/bin/vid2desmos` on
single-config generators.

## Usage

```bash
vid2desmos input.mp4 --width 160 --fps 15 --threshold 128 --output out/
```

`--help` lists every flag. The ones that matter most:

| Flag | Meaning | Default |
| --- | --- | --- |
| `--width` / `--height` | Output grid size. Give one and the aspect ratio is preserved | `--width 160` |
| `--fps` | Frames per second to sample | source fps |
| `--max-frames` | Stop after N frames | all |
| `--threshold` | Fixed cut-off, 0–255 | `128` |
| `--threshold-mode` | `fixed`, `otsu`, `adaptive`, `dither`, `bayer` | `fixed` |
| `--kernel` / `--chain` | Image processing, see [below](#image-processing) | none |
| `--encode` | `pixels`, `runs`, `rects` | `rects` |
| `--preview <file>` | Also write a video of the result | off |
| `--dark-ink` | Black regions on white instead of white on black | off |
| `--debug-frames N` | Write the first N processed frames as PNGs | `0` |

### Output

`--output out/` produces:

- **`frames_partNN.txt`** — the expressions, one per line. This is the file you
  paste into Desmos.
- `frames_partNN.json` — a Desmos graph state with colours and slider settings
  already applied, for anyone scripting Desmos via `calc.setState(...)`.
  `--no-json` skips it.
- `manifest.json` — frame counts, region counts, and part boundaries.

## Getting it into Desmos

1. Open a blank graph at [desmos.com/calculator](https://www.desmos.com/calculator).
2. Open `out/frames_part01.txt`, select all, copy, and paste into the first
   expression box. Desmos splits the pasted lines into separate expressions.
3. Colour the last two expressions — click the coloured circle beside each. The
   first `polygon` is the backdrop (black), the second is the picture (white).
4. Press play on the `f` slider.

Only step 3 is manual: pasted text cannot carry styling. The `.json` graph state
has colours baked in if you are driving Desmos programmatically instead.

Step 4 works immediately at whatever slider range Desmos supplies. The graph
derives its frame index as `n = mod(floor(f), N_f)`, so a pasted `f` with the
default −10…10 range still lands on a real frame and loops on its own. For an
exact one-pass loop, click the slider and set min `0`, max `N_f − 1`, step `1`.

If a run produced several parts, each is a separate graph — paste them one at a
time. The tool prints these steps with your actual numbers filled in at the end
of every run.

> **Note** — the `D=[…]` line is tens of kilobytes of digits on one line. Desmos
> handles it, but the paste takes a moment and the expression panel scrolls
> sluggishly. A lower `--width` or `--fps` shrinks it quickly.

## Previewing locally

Two viewers, both parsing the emitted expressions and evaluating them the way
Desmos does — so they show what the graph will actually draw, not a parallel
rendering path.

```bash
python loader/serve.py out/    # browser viewer, Desmos-like UI
python loader/view.py  out/    # matplotlib window, video export
```

`serve.py` needs nothing beyond the standard library and numpy; matplotlib is
only required by `view.py`. Both stitch multi-part output back into one
continuous sequence. See [loader/README.md](loader/README.md).

### Preview video

```bash
vid2desmos clip.mp4 --width 160 --fps 15 --preview preview.mp4
```

Writes a normal video file — `.mp4`, `.gif`, `.webm`, `.mkv` all work, since
ffmpeg picks the encoder from the extension. Frames are rendered **from the
encoded rectangles**, not from the mask that went into the encoder, so the video
reflects what Desmos will draw.

`--preview-scale <n>` sets an integer nearest-neighbour magnification so a grid
cell becomes a crisp `n × n` block. The default lands near 720px wide (160×90
becomes 800×450 at 5×). Odd results are rounded up to even dimensions because
most encoders reject odd sizes; with nearest-neighbour sampling that repeats the
last row or column rather than adding a visible border.

## Image processing

`--chain` is a comma-separated list applied in order:

```bash
vid2desmos clip.mp4 --chain "gaussian3,sobel,normalize" --threshold 60
vid2desmos clip.mp4 --chain "blur5,laplacian,normalize,gamma:0.8"
```

| | Steps |
| --- | --- |
| Gradients (X/Y pair combined by magnitude) | `sobel`, `prewitt`, `scharr` |
| Single kernels | `gaussian<N>`, `blur<N>`, `box<N>`, `sobelx`, `sobely`, `prewittx`, `prewitty`, `scharrx`, `scharry`, `laplacian`, `sharpen`, `emboss` |
| Point operations | `invert`, `normalize`, `abs`, `gamma:<x>`, `contrast:<mul>[/<offset>]` |

`--kernel sobel` is shorthand for a one-step chain. Gradient and
second-derivative kernels get a `normalize` step appended automatically, since
their output does not fit in 0–255.

Adding a kernel means one function in `kernels::` in
[src/convolution.cpp](src/convolution.cpp) and one line in `kernelByName`.
`convolve()` takes arbitrary kernel dimensions plus a divisor and bias, so
nothing else changes.

### Choosing a threshold mode

`fixed` and `otsu` give clean, highly compressible frames. `dither` and `bayer`
preserve apparent shading at roughly 20× the region count, because dithering is
specifically designed to break up the flat areas the encoder merges. Measured on
the test clip at 120×67:

| Mode | Avg regions/frame |
| --- | --- |
| `otsu` | 69 |
| `adaptive` | 136 |
| `dither` | 2041 |
| `bayer` | 2355 |

## How the encoding works

### Rectangle merging

Each binary frame is scanned into maximal horizontal runs. A run is then
extended downwards for as long as the row below holds a run with exactly the
same start and length. Runs within a row are sorted by x, so matching against
the previous row is a linear merge — the whole pass is O(pixels).

Measured on the test clip at 160×90:

| Strategy | Avg regions/frame | vs. raw pixels | Output |
| --- | --- | --- | --- |
| `pixels` | 7454 | 1× | 5.1 MB |
| `runs` | 292 | 25.5× | 202 KB |
| `rects` | 109 | **68.6×** | 75 KB |

This is a greedy partition, not a minimum one. Computing the true minimum
rectangle partition costs far more and buys little on real video frames, so the
greedy pass is the default and the only one implemented.

### Packing

Each rectangle becomes a single number:

```
p = ((y · W + x) · (W + 1) + w) · (H + 1) + h
```

so one list holds a whole frame instead of four. Desmos numbers are doubles,
exact to 2⁵³; even a 4096×4096 grid peaks near 2.8 × 10¹⁴, well inside that. The
graph unpacks with `mod` and `floor`, which vectorise over the list for free.
Image rows are flipped to Desmos's y-up convention at encode time, so nothing
downstream deals with orientation.

### Frame selection

`D` holds every rectangle of every frame, concatenated. `S` holds the 1-based
start index of each frame plus a tail entry, so frame `n` is
`D[[S[n+1]...S[n+2]-1]]`. Moving the slider re-slices one list.

An all-black frame would produce an empty index range, which `polygon()`
rejects. Such a frame emits a single zero-size rectangle instead: it unpacks to a
degenerate polygon and draws nothing.

Encoding strategies sit behind `EncodeStrategy` in
[include/desmos.hpp](include/desmos.hpp). `encodeFrame()` is a pure
mask-to-rectangles function, so adding a strategy does not touch the writer.

## Designing around Desmos's limits

Desmos generates list *ranges* up to 10000 elements, and a graph slows
noticeably once it carries a few tens of thousands of list entries. Two knobs
handle this:

- `--max-list` (default 10000) splits long literal lists into
  `D_{1}, D_{2}, …` stitched back together with `join()`.
- `--part-budget` (default 40000) caps the packed regions in one graph. Past it
  the encoder starts a new self-contained part with its own slider, so a long
  video becomes several graphs rather than one unusable one. Parts never
  straddle a frame.

Because frames are streamed, the encoder cannot know the total up front; it
fills each part to the budget and the last one takes whatever is left. That
would routinely leave a three-frame final part, useless as a separate graph, so
a tail shorter than a quarter of the budget is folded back into the previous
part — overshooting the budget slightly in exchange for not emitting a runt. One
completed part is held in memory to make this possible, so peak memory is about
two parts regardless of video length.

If a clip lands just over the budget and you would rather have a single graph,
raise `--part-budget`; to force more, smaller graphs, lower it.

If the busiest frame exceeds 10000 regions the tool warns you, because that
frame's index range will not render. Lower the resolution, blur before
thresholding, or stay on `--encode rects`.

## Layout

```
include/            src/
  frame.hpp           frame.cpp        Plane<T>, conversions
  filter.hpp          filter.cpp       point operations, area resize
  convolution.hpp     convolution.cpp  kernels and generic convolve
  threshold.hpp       threshold.cpp    fixed / otsu / adaptive / dither / bayer
  pipeline.hpp        pipeline.cpp     --chain parsing and execution
  desmos.hpp          desmos.cpp       rectangle merging, packing, output
  video.hpp           video.cpp        ffprobe metadata, ffmpeg in/out pipes
  png.hpp             png.cpp          dependency-free PNG writer
  cli.hpp             cli.cpp          argument parsing, help
                      main.cpp         orchestration and reporting

tests/                                 C++ test suite
loader/
  desmos_data.py                       expression parser (no matplotlib)
  serve.py                             local web server for the browser viewer
  view.py                              matplotlib viewer and video export
  test_loader.py                       loader test suite
  web/                                 browser UI: html, css, canvas renderer
```

Decoding, image processing, and Desmos encoding do not know about each other.
`encodeFrame()` takes a mask and returns rectangles; `runChain()` takes an image
and returns an image; `VideoReader` yields grayscale frames.

## Implementation notes

### FFmpeg is driven as a subprocess, not linked

`VideoReader` spawns `ffmpeg … -f rawvideo -pix_fmt gray -` and reads frames off
the pipe. Linking `libavcodec`/`libavformat` would be the conventional choice,
but on Windows it means shipping or vendoring the dev libraries, and the widely
installed FFmpeg builds — including the winget/gyan.dev "essentials" build this
was developed against — contain **binaries only, no headers or import
libraries**. The pipe gets the same decoder with zero build-time dependencies.

The consequences:

- `fps`, `scale`, and grayscale conversion run inside FFmpeg's filter graph, so
  only already-reduced frames cross the pipe and full-resolution data is never
  copied into this process. Scaling uses swscale's `area` filter, the right
  choice for heavy downscaling anyway.
- No intermediate video is encoded on the way to Desmos; `-f rawvideo` is a pipe
  format, not a file. (`--preview` does encode a video, but that is an
  explicitly requested output produced from the same single decode pass, not a
  step in the chain.)
- Per-frame presentation timestamps are unavailable. Nothing here needs them:
  `fps=` produces a constant-rate stream, exactly what a frame-indexed animation
  wants.
- Frames stream one at a time and are discarded, so memory is independent of
  video length.

Swapping in a libav-linked decoder means reimplementing `VideoReader`'s two
methods; nothing else depends on how frames arrive.

### Non-obvious details

A few things that look arbitrary but are not:

- **`degreeMode` is omitted from the graph state.** Desmos expects a boolean
  there and silently coerces anything else to degrees. Nothing here uses trig,
  but the coercion is confusing to debug.
- **Otsu returns `bestT + 1`.** The search yields the last level of the dark
  class; callers compare with `>=`, so the first level of the bright class is
  the correct cut.
- **Empty frames still cost one list element.** The zero-size sentinel is what
  keeps `polygon()` from choking on an empty index range, so the part budget
  accounts for it.
- **The browser viewer's playback advances by whole steps with an epsilon.**
  `1000/15` is not exact in binary, so a frame boundary can land a hair under
  the step and silently drop a frame.
- **The viewer auto-fits until the user pans or zooms.** A canvas can be
  measured before layout settles, and a fit computed against a zero-width
  element sticks forever otherwise.

### Performance

Frames are processed sequentially with buffers hoisted out of the loop.
`Plane<T>` is a flat contiguous buffer, so the convolution inner loop walks
contiguous memory and a future GPU port is a straight upload. The convolution
row loop is OpenMP-parallel when OpenMP is found — `-DV2D_USE_OPENMP=OFF`
disables it. The test clip runs at ~670 frames/sec at 160×90 with no filter
chain.

## Tests

```bash
ctest --test-dir build -C Release
```

Runs both suites. `v2d_tests` and `python loader/test_loader.py` also run
directly.

77 C++ checks cover convolution (identity, border handling, edge response),
thresholding, filters, pack/unpack round-trips across the whole grid, chain
parsing, CLI parsing, part splitting and tail merging, and output generation.
28 Python checks cover the loader.

The load-bearing one: for every encoding strategy, the emitted rectangles are
rasterised back onto a blank mask and compared against the source frame, so an
encoding that loses or misplaces a pixel fails the build.

There are four independent implementations of the rectangle geometry — the C++
encoder, the C++ test's own rasteriser, the Python loader's unpack, and the
browser renderer's unpack — checked against each other. A subtle packing or
offset bug has nowhere to hide.
