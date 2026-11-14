"""Charts results.csv: time spent outside HiGHS, per setup, against problem size.

    python plot.py [results.csv]

Writes docs/benchmark-overhead-light.svg and docs/benchmark-overhead-dark.svg
(the README picks one by the reader's color scheme) and prints the same
medians as a Markdown table.
"""

import collections
import csv
import pathlib
import re
import statistics
import sys

import matplotlib

matplotlib.use("svg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.ticker import FixedLocator, FuncFormatter, NullFormatter, NullLocator  # noqa: E402

HERE = pathlib.Path(__file__).resolve().parent
DOCS = HERE.parents[1] / "docs"

# Series in fixed categorical-slot order: saiopt's setups take the first
# three slots. Each also gets its own marker, so identity never rests on
# color alone.
SERIES = [
    ("saiopt-blocks-6", "saiopt, blocks, 6 MPI ranks", "o"),
    ("saiopt-blocks-1", "saiopt, blocks, 1 process", "s"),
    ("saiopt", "saiopt, flat", "D"),
    ("linopy", "linopy", "^"),
    ("pyoptinterface", "PyOptInterface", "v"),
    ("pulp", "PuLP", "p"),
    ("pyomo", "Pyomo", "h"),
]

# The reference palette's categorical slots 1-7 and chart chrome, per mode
# (validated: every adjacent-pair and band check passes in both modes;
# three light-mode slots sit below 3:1 on the light surface, which is why
# the README carries the table alongside the chart).
THEMES = {
    "light": {
        "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100", "#e87ba4", "#008300", "#6250d6"],
        "surface": "#fcfcfb",
        "primary": "#0b0b0b",
        "secondary": "#52514e",
        "muted": "#898781",
        "grid": "#e1e0d9",
        "axis": "#c3c2b7",
    },
    "dark": {
        "series": ["#3987e5", "#d95926", "#199e70", "#c98500", "#d55181", "#0ca30c", "#9085e9"],
        "surface": "#1a1a19",
        "primary": "#f0efec",
        "secondary": "#c3c2b7",
        "muted": "#898781",
        "grid": "#2c2c2a",
        "axis": "#383835",
    },
}

FONT_STACK = "system-ui, -apple-system, 'Segoe UI', sans-serif"


def load(path):
    samples = collections.defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f):
            # Floored at 1 ms: HiGHS logs its run time to the nearest 10 ms,
            # so on a tiny model the difference can round to zero or below,
            # which a log axis can't show.
            other_s = max((float(row["total_ms"]) - float(row["highs_ms"])) / 1000, 0.001)
            samples[row["setup"], int(row["variables"])].append(other_s)
    return {key: statistics.median(values) for key, values in samples.items()}


def count_label(n):
    return f"{n / 1e6:.3g}M" if n >= 1e6 else f"{n / 1e3:.0f}k"


def seconds_label(s):
    return f"{s:g} s"


def draw(medians, sizes, mode):
    t = THEMES[mode]
    plt.rcParams.update({
        "svg.fonttype": "none",
        "font.family": "sans-serif",
        "font.size": 11,
        "axes.unicode_minus": False,
    })
    fig, ax = plt.subplots(figsize=(10.8, 5.6), dpi=96)
    fig.patch.set_facecolor(t["surface"])
    ax.set_facecolor(t["surface"])

    for (setup, label, marker), color in zip(SERIES, t["series"]):
        xs = [n for n in sizes if (setup, n) in medians]
        ys = [medians[setup, n] for n in xs]
        ax.plot(xs, ys, color=color, linewidth=1.5, marker=marker, markersize=8, markeredgewidth=1.5,
                markeredgecolor=t["surface"], label=label, solid_capstyle="round", zorder=3)

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_locator(FixedLocator(sizes))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: count_label(v)))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.xaxis.set_minor_formatter(NullFormatter())
    lo = min(medians.values())
    hi = max(medians.values())
    y_ticks = [v for v in (0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100) if lo / 1.5 <= v <= hi * 1.5]
    ax.yaxis.set_major_locator(FixedLocator(y_ticks))
    ax.yaxis.set_major_formatter(FuncFormatter(lambda v, _: seconds_label(v)))
    ax.yaxis.set_minor_locator(NullLocator())
    ax.yaxis.set_minor_formatter(NullFormatter())
    ax.set_xlim(sizes[0] / 1.15, sizes[-1] * 1.15)
    ax.set_ylim(lo / 1.5, hi * 1.5)

    ax.grid(True, which="major", color=t["grid"], linewidth=0.75, linestyle="-", zorder=0)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    for side in ("left", "bottom"):
        ax.spines[side].set_color(t["axis"])
        ax.spines[side].set_linewidth(0.75)
    ax.tick_params(colors=t["muted"], labelcolor=t["secondary"], length=0, pad=6)

    ax.set_xlabel("Variables (log scale)", color=t["secondary"], labelpad=8)
    ax.set_ylabel("Time outside HiGHS (log scale)", color=t["secondary"], labelpad=8)
    fig.text(0.012, 0.965, "Modeling overhead vs. problem size", color=t["primary"], fontsize=14,
             fontweight="semibold", ha="left", va="top")
    fig.text(0.012, 0.915,
             "Wall time minus HiGHS's own run time, same LP in every library. Median of 2 runs; lower is better.",
             color=t["secondary"], fontsize=10.5, ha="left", va="top")

    legend = ax.legend(loc="upper left", bbox_to_anchor=(1.02, 1.0), frameon=False, labelcolor=t["primary"],
                       handlelength=2.2, borderaxespad=0)
    for handle in legend.legend_handles:
        handle.set_markeredgecolor(t["surface"])

    fig.subplots_adjust(left=0.08, right=0.69, top=0.84, bottom=0.12)
    out = DOCS / f"benchmark-overhead-{mode}.svg"
    fig.savefig(out, facecolor=t["surface"])
    plt.close(fig)

    # svg.fonttype "none" keeps text as text; point it at the system UI
    # face rather than whichever font matplotlib happened to resolve here.
    svg = out.read_text()
    # (Single-quoted family names only: the declarations sit inside
    # double-quoted style="..." attributes.)
    svg = re.sub(r"font-family:[^;\"]*;", f"font-family: {FONT_STACK};", svg)
    out.write_text(svg)
    return out


def table(medians, sizes):
    header = "| Setup | " + " | ".join(count_label(n) for n in sizes) + " |"
    rule = "|---|" + "---:|" * len(sizes)
    rows = [header, rule]
    for setup, label, _ in SERIES:
        cells = [f"{medians[setup, n]:.2f}" if (setup, n) in medians else "--" for n in sizes]
        rows.append(f"| {label} | " + " | ".join(cells) + " |")
    return "\n".join(rows)


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else HERE / "results.csv"
    medians = load(path)
    sizes = sorted({n for _, n in medians})
    DOCS.mkdir(exist_ok=True)
    for mode in THEMES:
        print(f"wrote {draw(medians, sizes, mode)}")
    print()
    print(table(medians, sizes))


if __name__ == "__main__":
    main()
