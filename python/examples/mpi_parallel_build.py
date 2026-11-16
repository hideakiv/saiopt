"""Build one Model's Blocks across MPI ranks in parallel, then solve on rank 0.

Each rank builds one "region" Block -- its own capacity and per-unit
cost -- and sends it to rank 0, which attaches every region to one Model,
ties them together with a demand constraint, and solves. The Python port
of examples/mpi_parallel_build.cpp.

Run with, e.g.: mpiexec -n 4 python python/examples/mpi_parallel_build.py
"""

from mpi4py import MPI

import saiopt as so
import saiopt.mpi

DEMAND = 25.0

comm = MPI.COMM_WORLD
rank, size = comm.Get_rank(), comm.Get_size()

# Every rank builds its own region independently -- no coordination
# needed until it's time to send.
capacity = 10.0 + rank
cost_per_unit = 5.0 - 0.5 * rank

region = so.Block()
region.produce = so.Var(bounds=(0, capacity))
region.set_objective(cost_per_unit * region.produce)

regions = saiopt.mpi.gather_blocks(region)

if rank == 0:
    model = so.Model()
    model.region = so.Blocks(range(size), lambda i: regions[i])
    produced = {i: block.produce for i, block in model.region.items()}
    model.meet_demand = so.Constraint(so.quicksum(produced.values()) >= DEMAND)
    model.set_objective(so.ObjectiveSense.Minimize, 0)

    result = so.create_solver(model, "highs").solve()
    if result.status == so.SolveStatus.Infeasible:
        print(f"built from {size} ranks: infeasible (not enough total capacity for demand {DEMAND:.2f})")
    elif not result.optimal:
        print(f"built from {size} ranks: no optimal solution ({result.status})")
    else:
        print(f"built from {size} ranks, minimum cost = {result.objective_value:.2f}")
        for i, var in produced.items():
            print(f"  region{i} produced {result[var]:.2f}")
