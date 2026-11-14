"""Send Blocks between MPI ranks, via mpi4py.

Build a model's Block tree across ranks in parallel -- each rank builds
its own Block(s) independently, then sends them to a coordinating rank
to attach and solve::

    from mpi4py import MPI
    import saiopt as so
    import saiopt.mpi

    region = so.Block()
    ...
    regions = saiopt.mpi.gather_blocks(region)  # a list on rank 0, None elsewhere

A Block travels as its Block.serialize() bytes, sent as a plain MPI_BYTE
message -- the same wire format the C++ saiopt::mpi::send_block and
recv_block use, so Python and C++ ranks can exchange Blocks within one
MPI job. The receiving side rebuilds it with Block.deserialize, which
assigns fresh, locally-unique ids regardless of the sender's.

Requires mpi4py (``pip install saiopt[mpi]``), built against the same MPI
implementation the job runs under. Importing this module imports
mpi4py's MPI, which initializes MPI -- after which some MPI builds (e.g.
MPICH over libfabric) abort the process on any fork(), including the one
behind subprocess.run. Launch subprocesses before importing it, or from
a process that never does.
"""

from __future__ import annotations

try:
    from mpi4py import MPI
except ImportError as error:  # pragma: no cover - depends on the environment
    raise ImportError(
        "saiopt.mpi needs mpi4py: install it with `pip install saiopt[mpi]` "
        "(or `pip install mpi4py`), built against your MPI implementation"
    ) from error

from ._saiopt import Block

__all__ = ["send_block", "recv_block", "gather_blocks"]


def send_block(block: Block, dest: int, tag: int = 0, comm: MPI.Comm | None = None) -> None:
    """Serialize `block` and blocking-send it to rank `dest` on `comm`.

    Pair with a matching recv_block on the receiving rank. `block` itself is
    left as it was -- it's copied into the message, not moved.
    """
    comm = MPI.COMM_WORLD if comm is None else comm
    comm.Send([block.serialize(), MPI.BYTE], dest=dest, tag=tag)


def recv_block(source: int = MPI.ANY_SOURCE, tag: int = MPI.ANY_TAG, comm: MPI.Comm | None = None) -> Block:
    """Block until a Block tagged `tag` arrives from rank `source`, and return it.

    `source` and `tag` default to any. The result is a standalone Block,
    ready to attach to a parent with add_block.
    """
    comm = MPI.COMM_WORLD if comm is None else comm
    status = MPI.Status()
    comm.Probe(source=source, tag=tag, status=status)
    buffer = bytearray(status.Get_count(MPI.BYTE))
    comm.Recv([buffer, MPI.BYTE], source=status.Get_source(), tag=status.Get_tag())
    return Block.deserialize(bytes(buffer))


def gather_blocks(block: Block, root: int = 0, tag: int = 0, comm: MPI.Comm | None = None) -> list[Block] | None:
    """Collect every rank's `block` on rank `root`, ordered by rank.

    Collective: every rank in `comm` must call it. Returns the list on
    `root` -- `root`'s own entry is `block` itself, the others freshly
    deserialized -- and None on every other rank.
    """
    comm = MPI.COMM_WORLD if comm is None else comm
    rank = comm.Get_rank()
    if rank != root:
        send_block(block, root, tag, comm)
        return None
    return [block if source == root else recv_block(source, tag, comm) for source in range(comm.Get_size())]
