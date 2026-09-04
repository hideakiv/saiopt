#pragma once

#include <mpi.h>

#include "saiopt/block.hpp"

namespace saiopt::mpi {

// Serializes `block` (see Block::serialize) and blocking-sends it to
// `dest` on `comm`, tagged `tag`. Pair with a matching recv_block call on
// the receiving rank.
void send_block(const Block& block, int dest, int tag, MPI_Comm comm = MPI_COMM_WORLD);

// Blocks until a message tagged `tag` arrives from `source` (which may be
// MPI_ANY_SOURCE), then deserializes it into a Block via Block::deserialize
// -- so the result gets fresh, locally-unique BlockIds regardless of what
// the sending rank's were.
Block recv_block(int source, int tag, MPI_Comm comm = MPI_COMM_WORLD);

}  // namespace saiopt::mpi
