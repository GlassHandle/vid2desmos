from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import desmos_data

def _import_matplotlib(headless: bool):
    try:
        import matplotlib
    except ImportError:
        sys.exit(
            "matplotlib is not installed.\n"
            "  pip install -r loader/requirements.txt"
        )
    if headless:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.animation import FuncAnimation
    from matplotlib.collections import PolyCollection

    return plt, FuncAnimation, PolyCollection

def _colours(dark_ink: bool) -> tuple[str, str]:
    return ("white", "black") if dark_ink else ("black", "white")

def _rect_verts(regions: np.ndarray) -> np.ndarray:
    if len(regions) == 0:
        return np.zeros((0, 4, 2))
    x, y, w, h = (regions[:, i].astype(float) for i in range(4))
    return np.stack(
        [
            np.stack([x, y], axis=1),
            np.stack([x + w, y], axis=1),
            np.stack([x + w, y + h], axis=1),
            np.stack([x, y + h], axis=1),
        ],
        axis=1,
    )

def build_figure(anim, args, plt, PolyCollection):
    bg, ink = _colours(args.dark_ink)

    fig, ax = plt.subplots(figsize=(args.width_inches, args.width_inches * anim.grid_h / anim.grid_w))
    fig.patch.set_facecolor(bg)
    ax.set_facecolor(bg)
    ax.set_xlim(0, anim.grid_w)
    ax.set_ylim(0, anim.grid_h)
    ax.set_aspect("equal")
    ax.axis("off")
    fig.subplots_adjust(left=0, right=1, top=1, bottom=0)

    title = None
    if args.title:
        title = ax.set_title("", color=ink, fontsize=9, family="monospace")
        fig.subplots_adjust(top=0.94)

    if args.mode == "rects":

        collection = PolyCollection(
            _rect_verts(anim.regions(0)),
            facecolors=ink,
            edgecolors=(ink if not args.outline else "#ff3b30"),
            linewidths=(0.0 if not args.outline else 0.4),
        )
        ax.add_collection(collection)
        artists = [collection]

        def update(i: int):
            collection.set_verts(_rect_verts(anim.regions(i)))
            if title is not None:
                title.set_text(
                    f"frame {i}/{anim.frame_count - 1}   {len(anim.regions(i))} regions"
                )
                return [collection, title]
            return artists

    else:
        from matplotlib.colors import ListedColormap

        image = ax.imshow(
            anim.raster(0),
            cmap=ListedColormap([bg, ink]),
            origin="lower",
            interpolation="nearest",
            vmin=0,
            vmax=1,
            extent=(0, anim.grid_w, 0, anim.grid_h),
        )
        artists = [image]

        def update(i: int):
            image.set_data(anim.raster(i))
            if title is not None:
                title.set_text(
                    f"frame {i}/{anim.frame_count - 1}   {len(anim.regions(i))} regions"
                )
                return [image, title]
            return artists

    return fig, update

def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Preview vid2desmos output with matplotlib.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "examples:\n"
            "  python loader/view.py out/\n"
            "  python loader/view.py out/ --frame 42\n"
            "  python loader/view.py out/ --save clip.mp4\n"
            "  python loader/view.py out/ --outline\n"
        ),
    )
    p.add_argument("path", help="output directory, or a single frames_partNN.txt")
    p.add_argument("--frame", type=int, help="show one frame instead of animating")
    p.add_argument("--save", metavar="FILE", help="write the animation to .mp4/.gif and exit")
    p.add_argument("--info", action="store_true", help="print stats and exit")
    p.add_argument("--fps", type=float, help="override playback fps")
    p.add_argument(
        "--mode",
        choices=("raster", "rects"),
        default="raster",
        help="raster is fast; rects draws each encoded rectangle as a polygon",
    )
    p.add_argument(
        "--outline",
        action="store_true",
        help="outline each rectangle (implies --mode rects) to see the merging",
    )
    p.add_argument("--dark-ink", action="store_true", help="black regions on white")
    p.add_argument("--title", action="store_true", help="show frame number and region count")
    p.add_argument("--loop", action="store_true", help="repeat the animation")
    p.add_argument("--width-inches", type=float, default=7.0, help="figure width")
    p.add_argument("--dpi", type=int, default=110, help="figure dpi when saving")
    args = p.parse_args(argv)

    if args.outline:
        args.mode = "rects"

    try:
        anim = desmos_data.load(args.path)
    except (FileNotFoundError, desmos_data.ParseError) as exc:
        print(f"view.py: {exc}", file=sys.stderr)
        return 1

    if args.fps:
        anim.fps = args.fps

    print(desmos_data.describe(anim))
    if args.info:
        return 0

    headless = bool(args.save)
    plt, FuncAnimation, PolyCollection = _import_matplotlib(headless)

    if args.frame is not None:
        if not 0 <= args.frame < anim.frame_count:
            print(
                f"view.py: --frame {args.frame} out of range "
                f"(0..{anim.frame_count - 1})",
                file=sys.stderr,
            )
            return 1
        fig, update = build_figure(anim, args, plt, PolyCollection)
        update(args.frame)
        if args.save:
            fig.savefig(args.save, dpi=args.dpi, facecolor=fig.get_facecolor())
            print(f"\nwrote {args.save}")
        else:
            plt.show()
        return 0

    fig, update = build_figure(anim, args, plt, PolyCollection)
    interval = 1000.0 / max(anim.fps, 1e-6)
    animation = FuncAnimation(
        fig,
        update,
        frames=anim.frame_count,
        interval=interval,
        blit=not args.save,
        repeat=args.loop or not args.save,
        cache_frame_data=False,
    )

    if args.save:
        out = Path(args.save)
        writer = "pillow" if out.suffix.lower() == ".gif" else "ffmpeg"
        print(f"\nwriting {out} ({anim.frame_count} frames, {writer})...")
        try:
            animation.save(str(out), writer=writer, fps=anim.fps, dpi=args.dpi)
        except Exception as exc:
            print(f"view.py: could not write {out}: {exc}", file=sys.stderr)
            return 1
        print(f"wrote {out} ({out.stat().st_size / 1024:.0f} KB)")
        return 0

    plt.show()
    return 0

if __name__ == "__main__":
    sys.exit(main())
