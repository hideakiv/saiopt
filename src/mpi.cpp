#include "saiopt/mpi.hpp"

namespace saiopt::mpi {

void send_block(const Block& block, int dest, int tag, MPI_Comm comm) {
    std::vector<std::byte> bytes = block.serialize();
    MPI_Send(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, dest, tag, comm);
}

Block recv_block(int source, int tag, MPI_Comm comm) {
    MPI_Status status;
    MPI_Probe(source, tag, comm, &status);

    int count = 0;
    MPI_Get_count(&status, MPI_BYTE, &count);

    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
    MPI_Recv(bytes.data(), count, MPI_BYTE, status.MPI_SOURCE, status.MPI_TAG, comm,
             MPI_STATUS_IGNORE);

    return Block::deserialize(bytes);
}

}  // namespace saiopt::mpi
