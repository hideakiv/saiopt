#include <cstdio>
#include <string>
#include <vector>

#include <mpi.h>

#include "saiopt/mpi.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"

// Each MPI rank builds one "region" Block in parallel -- its own capacity
// and per-unit cost -- and sends it to rank 0, which gathers every
// region's Block, attaches them all to one Model, ties them together with
// a demand constraint, and solves.
//
// Run with, e.g.: mpirun -n 4 ./mpi_parallel_build
int main(int argc, char** argv) {
    using namespace saiopt;

    MPI_Init(&argc, &argv);
    int rank = 0;
    int size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const double demand = 25.0;
    const int build_tag = 0;

    // Every rank builds its own region's Block independently and in
    // parallel -- no coordination needed until it's time to send.
    double capacity = 10.0 + rank;
    double cost_per_unit = 5.0 - 0.5 * rank;

    Block region;
    Variable produce = region.add_var(VariableDef::continuous().bound(0.0, capacity).name("produce"));
    region.set_objective(cost_per_unit * produce);

    if (rank != 0) {
        mpi::send_block(region, /*dest=*/0, build_tag);
    } else {
        std::vector<Block> regions;
        regions.push_back(std::move(region));
        for (int source = 1; source < size; ++source) {
            regions.push_back(mpi::recv_block(source, build_tag));
        }

        Model model;
        std::vector<Variable> produce_of;
        produce_of.reserve(regions.size());
        for (std::size_t i = 0; i < regions.size(); ++i) {
            std::string name = "region" + std::to_string(i);
            model.add_block(name, std::move(regions[i]));
            produce_of.push_back(*model.child(name)->var("produce"));
        }

        LinearExpression total_production;
        for (Variable v : produce_of) total_production += v;
        model.add_constraint("meet_demand", total_production >= demand);
        model.set_objective(ObjectiveSense::Minimize, LinearExpression{});

        auto solver = create_solver(model, "highs");
        SolveResult result = solver->solve();
        if (!result.optimal) {
            std::printf("built from %d ranks: infeasible (not enough total capacity for demand %.2f)\n",
                        size, demand);
        } else {
            std::printf("built from %d ranks, minimum cost = %.2f\n", size, result.objective_value);
            for (std::size_t i = 0; i < produce_of.size(); ++i) {
                std::printf("  region%zu produced %.2f\n", i, result.value(produce_of[i]));
            }
        }
    }

    MPI_Finalize();
    return 0;
}
