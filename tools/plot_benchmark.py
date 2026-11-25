#!/usr/bin/env python3
"""Graphs benchmark recordings (the Profiler panel's Record button, docs/wave-2-benchmarking.md).

    python3 tools/plot_benchmark.py build/gram-engine/macos/release/benchmarks/2026-11-25_15-50-57.csv
    python3 tools/plot_benchmark.py build/gram-engine/macos/release/benchmarks/          # every .csv in it
    python3 tools/plot_benchmark.py a.csv b.csv --output comparison.png --show

For each recording, a graph saved next to its .csv (<name>.png): the passes' GPU ms stacked (the band's top is
the GPU's whole frame) and the FPS, against time. Given more than one recording (several files, or a folder),
also a comparison: every recording's FPS on the same axes, and one stacked bar per recording with each pass's
mean ms. The comparison's labels are the settings that differ between the recordings (from the .json next to
each .csv); what they share goes into the title. Saved as comparison.png in the first recording's folder,
unless --output says otherwise.

Needs matplotlib: `python3 -m pip install matplotlib`.
"""

import argparse
import csv
import json
import sys
import textwrap
from pathlib import Path

# Keys that say how a run was made but never tell two runs apart usefully in a label.
IGNORED_IN_LABELS = {"driver_info", "bvh_tree"}


class Recording:
    def __init__(self, path: Path, skip: int):
        self.path = path
        with path.open(newline="") as f:
            rows = list(csv.DictReader(f))[skip:]
        if not rows:
            sys.exit(f"{path}: no rows left (stopped before its first interval, or --skip too large)")

        def column(name):
            return [float(row[name]) if row[name] else None for row in rows]

        self.time = column("time_s")
        self.fps = column("fps")
        self.frame_ms = column("frame_ms")
        # The passes, in the order they ran: every "<name> ms" column but the frame's own.
        self.passes = {name[:-3]: column(name) for name in rows[0] if name.endswith(" ms")}

        settings_path = path.with_suffix(".json")
        self.settings = json.loads(settings_path.read_text()) if settings_path.exists() else {}

    def mean(self, values):
        measured = [v for v in values if v is not None]
        return sum(measured) / len(measured) if measured else 0.0


def zeros_for_missing(values):
    """A stack needs a number everywhere: a pass not measured in an interval adds nothing to it."""
    return [v if v is not None else 0.0 for v in values]


def describe(settings: dict, keys) -> str:
    return ", ".join(f"{key.replace('_', ' ')} {settings[key]}" for key in keys if key in settings)


def title(text: str) -> str:
    """Long settings lists wrapped instead of running off the image."""
    return textwrap.fill(text, width=150)


def fps_axis_from_zero(axis, recordings):
    """0 at the bottom, a tenth of headroom at the top so the highest line isn't drawn on the frame."""
    highest = max(v for r in recordings for v in r.fps if v is not None)
    axis.set_ylim(0, highest * 1.1)


def plot_one(recording: Recording, zero: bool, plt):
    figure, (passes_axis, fps_axis) = plt.subplots(2, 1, sharex=True, figsize=(11, 8))

    names = list(recording.passes)
    passes_axis.stackplot(recording.time, *[zeros_for_missing(recording.passes[n]) for n in names], labels=names)
    passes_axis.plot(recording.time, recording.frame_ms, "k--", linewidth=1, label="whole frame (CPU + GPU + waiting)")
    passes_axis.set_ylabel("ms per frame (mean per interval)")
    passes_axis.legend(fontsize="small", loc="lower right")
    passes_axis.grid(True, alpha=0.3)

    fps_axis.plot(recording.time, recording.fps)
    fps_axis.set_ylabel("FPS (frames / interval)")
    fps_axis.set_xlabel("time (s)")
    fps_axis.grid(True, alpha=0.3)
    if zero:
        fps_axis_from_zero(fps_axis, [recording])

    keys = ("gpu", "driver", "configuration", "resolution", "vsync", "render_scale", "fsr", "samples_per_frame", "max_bounces", "bvh")
    figure.suptitle(f"{recording.path.stem}\n{title(describe(recording.settings, keys))}", fontsize="small")
    figure.tight_layout()
    return figure


def plot_comparison(recordings, zero: bool, plt):
    # The settings that differ between the recordings label them; the ones they share make the title.
    keys = [k for k in recordings[0].settings if k not in IGNORED_IN_LABELS]
    for r in recordings[1:]:
        keys += [k for k in r.settings if k not in keys and k not in IGNORED_IN_LABELS]
    differing = [k for k in keys if len({r.settings.get(k) for r in recordings}) > 1]
    shared = [k for k in keys if k not in differing]
    labels = [describe(r.settings, differing) or r.path.stem for r in recordings]

    figure, (fps_axis, bars_axis) = plt.subplots(2, 1, figsize=(11, 9), gridspec_kw={"height_ratios": [3, 2]})

    for recording, label in zip(recordings, labels):
        fps_axis.plot(recording.time, recording.fps, label=label)
    fps_axis.set_ylabel("FPS (frames / interval)")
    fps_axis.set_xlabel("time (s)")
    fps_axis.legend(fontsize="small")
    fps_axis.grid(True, alpha=0.3)
    if zero:
        fps_axis_from_zero(fps_axis, recordings)

    # One bar per recording, each pass's mean ms stacked: every pass that appears in any of them.
    names = []
    for r in recordings:
        names += [n for n in r.passes if n not in names]
    positions = range(len(recordings))
    bottoms = [0.0] * len(recordings)
    for name in names:
        means = [r.mean(r.passes[name]) if name in r.passes else 0.0 for r in recordings]
        bars_axis.bar(positions, means, bottom=bottoms, label=name)
        bottoms = [b + m for b, m in zip(bottoms, means)]
    for position, total in zip(positions, bottoms):
        bars_axis.annotate(f"{total:.2f} ms", (position, total), ha="center", va="bottom", fontsize="small")
    bars_axis.set_xticks(list(positions), labels, fontsize="small")
    bars_axis.set_ylabel("mean GPU ms per pass")
    bars_axis.legend(fontsize="small", loc="upper left", bbox_to_anchor=(1.0, 1.0))
    bars_axis.grid(True, axis="y", alpha=0.3)

    figure.suptitle(title(describe(recordings[0].settings, [k for k in shared if k in recordings[0].settings])), fontsize="small")
    figure.tight_layout()
    return figure


def find_recordings(paths):
    """The .csv files given, and those inside the folders given, in name (= date) order."""
    found = []
    for path in paths:
        if path.is_dir():
            found += sorted(path.glob("*.csv"))
        elif path.suffix == ".csv":
            found.append(path)
        else:
            sys.exit(f"{path}: not a .csv nor a folder")
    if not found:
        sys.exit("no recordings found")
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", type=Path, help=".csv recordings, or folders holding them")
    parser.add_argument("--skip", type=int, default=1, help="rows to drop at the start of each recording (warm-up; default 1)")
    parser.add_argument("--zero", action="store_true", help="start the FPS axis at 0 (honest scale for side-by-side graphs)")
    parser.add_argument("--output", type=Path, help="where to save the comparison (default: comparison.png next to the first recording)")
    parser.add_argument("--show", action="store_true", help="also open the graphs in a window")
    args = parser.parse_args()

    try:
        import matplotlib
        if not args.show:
            matplotlib.use("Agg")   # files only: no window, works without a display
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit("matplotlib is missing: python3 -m pip install matplotlib")

    recordings = [Recording(path, args.skip) for path in find_recordings(args.paths)]

    for recording in recordings:
        image = recording.path.with_suffix(".png")
        plot_one(recording, args.zero, plt).savefig(image, dpi=150)
        print(f"saved {image}")

    if len(recordings) > 1:
        image = args.output or recordings[0].path.parent / "comparison.png"
        plot_comparison(recordings, args.zero, plt).savefig(image, dpi=150)
        print(f"saved {image}")

    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
