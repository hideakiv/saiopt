"""Runs models.py and mpi_blocks.py over a range of problem sizes and writes results.csv.

    python run.py [--reps 2] [--sizes 50,71,100,141,200,283] [--ranks 6]

Every configuration runs in its own process (MPI ones under mpiexec).
Each TIMING line is paired with the HiGHS run time logged just before
it, so results.csv records, per repetition, the total wall time and how
much of it was HiGHS itself -- plot.py charts the difference.

Needs saiopt[mpi] plus the libraries being compared (linopy, pulp,
pyomo, pyoptinterface) installed in the running interpreter.
"""

import argparse
import csv
import pathlib
import re
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
K = 24
LIBRARIES = ["saiopt", "pyoptinterface", "linopy", "pulp", "pyomo"]


def timed_runs(cmd):
    done = subprocess.run(cmd, capture_output=True, text=True, timeout=7200, cwd=HERE)
    if done.returncode != 0:
        raise SystemExit(f"{' '.join(cmd)} failed:\n{done.stdout[-2000:]}\n{done.stderr[-2000:]}")
    highs_ms, rows = None, []
    for line in done.stdout.splitlines():
        found = re.search(r"HiGHS run time\s*:\s*([0-9.]+)", line)
        if found:
            highs_ms = 1000 * float(found.group(1))
        elif line.startswith("TIMING"):
            _, build_ms, total_ms, _ = line.split()
            rows.append((float(build_ms), float(total_ms), highs_ms))
            highs_ms = None
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--reps", type=int, default=2)
    parser.add_argument("--sizes", default="50,71,100,141,200,283", help="values of N (each region is N x N)")
    parser.add_argument("--ranks", default="6", help="MPI rank counts to run besides 1, comma-separated")
    parser.add_argument("--out", default=str(HERE / "results.csv"))
    args = parser.parse_args()

    mpiexec = shutil.which("mpiexec") or shutil.which("mpirun")
    ranks = [1] + [int(r) for r in args.ranks.split(",") if r]
    with open(args.out, "w", newline="") as f:
        out = csv.writer(f)
        out.writerow(["setup", "variables", "rep", "build_ms", "total_ms", "highs_ms"])
        for n in (int(s) for s in args.sizes.split(",")):
            variables = K * n * n + K
            configs = [(lib, [sys.executable, "-I", "models.py", lib, str(K), str(n), str(args.reps)])
                       for lib in LIBRARIES]
            configs += [(f"saiopt-blocks-{p}", [mpiexec, "-n", str(p), sys.executable, "-I", "mpi_blocks.py",
                                                  str(K), str(n), str(args.reps)]) for p in ranks]
            for setup, cmd in configs:
                for rep, (build_ms, total_ms, highs_ms) in enumerate(timed_runs(cmd)):
                    out.writerow([setup, variables, rep, f"{build_ms:.1f}", f"{total_ms:.1f}", f"{highs_ms:.1f}"])
                    f.flush()
                    print(f"{variables:>9} {setup:<18} total {total_ms:8.0f} ms  HiGHS {highs_ms:7.0f} ms  "
                          f"other {total_ms - highs_ms:8.0f} ms", flush=True)


if __name__ == "__main__":
    main()
