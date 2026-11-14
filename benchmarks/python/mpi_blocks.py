"""The models.py LP built as K saiopt Blocks, spread across MPI ranks.

    mpiexec -n RANKS python mpi_blocks.py K N REPS

Each rank builds its share of the K regions (rank r takes r, r + RANKS,
...) as child blocks of one Block, then saiopt.mpi.gather_blocks
collects them on rank 0, which attaches them, adds the linking
constraint, and solves with HiGHS (log on). For each repetition, rank 0
prints the HiGHS log, then one line:

    TIMING <build ms> <total ms> <objective>

where build is the slowest rank's time to build its blocks, and total
runs from that start through gathering, attaching, create_solver, and
solve. With one rank nothing is serialized: gather_blocks just returns
the rank's own block.
"""

import itertools
import sys

from mpi4py import MPI

import saiopt as so
import saiopt.mpi

K, N, REPS = map(int, sys.argv[1:4])
comm = MPI.COMM_WORLD
rank, size = comm.Get_rank(), comm.Get_size()


def region(k):
    b = so.Block()
    arcs = list(itertools.product(range(N), range(N)))
    x = b.add_vars("x", arcs, bounds=(0, so.inf))
    b.add_constraints("cap", range(N), lambda i: so.quicksum(x[i, j] for j in range(N)) <= 2.0 * N)
    b.add_constraints("dem", range(N), lambda j: so.quicksum(x[i, j] for i in range(N)) >= 1)
    used = b.add_var("used", bounds=(0, so.inf))
    b.add_constraint("used_def", used == so.quicksum(x.values()))
    b.set_objective(so.quicksum((1 + (i * 7 + j * 13 + k) % 10) * x[i, j] for i, j in arcs))
    return b


for _ in range(REPS):
    comm.Barrier()
    t0 = MPI.Wtime()
    chunk = so.Block()
    for k in range(rank, K, size):
        chunk.add_block(f"region{k}", region(k))
    comm.Barrier()
    t1 = MPI.Wtime()

    chunks = saiopt.mpi.gather_blocks(chunk)
    if rank == 0:
        model = so.Model()
        used = []
        for r, c in enumerate(chunks):
            attached = model.add_block(f"rank{r}", c)
            used += [attached.block(f"region{k}").var("used") for k in range(r, K, size)]
        model.add_constraint("link", so.quicksum(used) <= 1e9)
        model.set_objective(so.ObjectiveSense.Minimize, 0)
        result = so.create_solver(model, "highs", {"output_flag": True}).solve()
        t2 = MPI.Wtime()
        if not result.optimal or abs(result.objective_value - K * N) > 1e-6 * K * N:
            print(f"wrong result: {result}", flush=True)
            comm.Abort(1)
        print(f"\nTIMING {1000 * (t1 - t0):.1f} {1000 * (t2 - t0):.1f} {result.objective_value}", flush=True)
    comm.Barrier()
