"""Run by test_mpi.py under mpiexec (3 ranks); exits nonzero if a check fails."""

import sys

from mpi4py import MPI

import saiopt as so
import saiopt.mpi

comm = MPI.COMM_WORLD
rank, size = comm.Get_rank(), comm.Get_size()
assert size == 3, size


def build(rank):
    block = so.Block()
    x = block.add_var("x", bounds=(0, 10 + rank))
    block.add_constraint("floor", x >= rank)
    child = so.Block()
    y = child.add_var("y", bounds=(0, 1))
    child.set_objective(y * y)
    block.add_block("child", child, multiplier=2.0)
    block.set_objective((rank + 1) * x)
    return block


# gather_blocks onto a non-zero root, and the structure survives the trip.
gathered = saiopt.mpi.gather_blocks(build(rank), root=1)
if rank == 1:
    assert len(gathered) == 3
    model = so.Model()
    for i, block in enumerate(gathered):
        model.add_block(f"r{i}", block)
        assert model.block(f"r{i}").block("child").var("y") is not None
    model.set_objective(so.ObjectiveSense.Minimize, 0)
    result = so.create_solver(model, "highs").solve()
    # Each region pins x to its floor: 1*0 + 2*1 + 3*2.
    assert abs(result.objective_value - 8.0) < 1e-6, result
else:
    assert gathered is None

# Point-to-point with explicit tags, received from any source, in
# whatever order they arrive.
if rank == 0:
    received = {}
    for _ in range(size - 1):
        block = saiopt.mpi.recv_block(tag=5)
        received[block.var("x") is not None] = block
    assert list(received) == [True]
else:
    saiopt.mpi.send_block(build(rank), dest=0, tag=5)

# A non-default communicator: ranks 1 and 2 on their own.
sub = comm.Split(color=0 if rank > 0 else MPI.UNDEFINED, key=rank)
if sub != MPI.COMM_NULL:
    if sub.Get_rank() == 0:
        block = saiopt.mpi.recv_block(source=1, comm=sub)
        assert block.constraint("floor") is not None
    else:
        saiopt.mpi.send_block(build(rank), dest=0, comm=sub)
    sub.Free()

comm.Barrier()
if rank == 0:
    print("mpi worker ok")
sys.stdout.flush()
