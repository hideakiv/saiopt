"""saiopt.mpi, exercised under real mpiexec runs.

Nothing here imports mpi4py's MPI into the pytest process itself: once MPI
is initialized, some MPI builds (e.g. MPICH over libfabric) abort any
later fork() -- which is what launching each mpiexec below needs.
"""

import importlib.util
import os
import pathlib
import shutil
import signal
import subprocess
import sys

import pytest

if importlib.util.find_spec("mpi4py") is None:
    pytest.skip("mpi4py isn't installed", allow_module_level=True)
MPIEXEC = shutil.which("mpiexec") or shutil.which("mpirun")
if MPIEXEC is None:
    pytest.skip("no mpiexec/mpirun on PATH", allow_module_level=True)

HERE = pathlib.Path(__file__).parent
EXAMPLE = HERE.parent / "examples" / "mpi_parallel_build.py"


def run_ranks(n, *args, timeout=120):
    """Run `python *args` on `n` ranks, killing the whole job if it hangs."""
    proc = subprocess.Popen(
        [MPIEXEC, "-n", str(n), sys.executable, *args],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        stdout, stderr = proc.communicate()
        pytest.fail(f"mpiexec -n {n} timed out\n{stdout}\n{stderr}")
    assert proc.returncode == 0, stdout + stderr
    return stdout


def test_gather_on_a_single_rank_returns_the_block_itself():
    script = (
        "import saiopt as so, saiopt.mpi\n"
        "b = so.Block()\n"
        "b.add_var('x')\n"
        "assert saiopt.mpi.gather_blocks(b) == [b]\n"
        "print('ok')\n"
    )
    assert run_ranks(1, "-c", script).split() == ["ok"]


def test_send_recv_and_gather_across_ranks():
    assert "mpi worker ok" in run_ranks(3, str(HERE / "mpi_worker.py"))


def test_parallel_build_example():
    # Capacities 10, 11, 12 at costs 5, 4.5, 4: the cheapest two regions
    # fill up first, and region0 covers the last 2 of the 25 units.
    assert run_ranks(3, str(EXAMPLE)).splitlines() == [
        "built from 3 ranks, minimum cost = 107.50",
        "  region0 produced 2.00",
        "  region1 produced 11.00",
        "  region2 produced 12.00",
    ]
