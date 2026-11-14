#include <interfaces/highs_c_api.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "saiopt/cross.hpp"
#include "saiopt/macros.hpp"
#include "saiopt/model.hpp"
#include "saiopt/solver.hpp"
#include "saiopt/sparse_lp.hpp"

using namespace saiopt;
using Catch::Approx;

namespace {

// A small MILP that exercises every kind of name write_model has to
// rewrite: tuple-indexed variables (brackets, a space after the comma),
// an indexed constraint group, and a sub-block's qualified names.
Model build_shipping() {
    Model model;
    std::vector<std::string> warehouses{"W1", "W2"};
    std::vector<int> customers{1, 2};
    auto arcs = cross(warehouses, customers);
    SAIOPT_VARS(model, ship, arcs, .bounds = {0.0, 10.0});
    SAIOPT_VAR(model, open, .kind = VarKind::Binary);

    auto meets_demand = [&](int c) { return ship[{"W1", c}] + ship[{"W2", c}] >= 4.0; };
    SAIOPT_CONSTRAINTS(model, demand, customers, meets_demand);
    LinearExpression total;
    for (const auto& [arc, v] : ship) total += v;
    SAIOPT_CONSTRAINT(model, link, total <= 20.0 * open);

    Block sub;
    SAIOPT_VAR(sub, y, .bounds = {1.0, 5.0});
    SAIOPT_CONSTRAINT(sub, ylim, y <= 3.0);
    sub.set_objective(2.0 * y);
    model.add_block("sub", std::move(sub));

    SAIOPT_OBJECTIVE(model, ObjectiveSense::Minimize,
                     1.0 * ship[{"W1", 1}] + 2.0 * ship[{"W1", 2}] + 3.0 * ship[{"W2", 1}] +
                         1.0 * ship[{"W2", 2}] + 5.0 * open + 1.5);
    return model;
}

std::filesystem::path scratch_dir() {
    auto dir = std::filesystem::temp_directory_path() / "saiopt_test_write_model";
    std::filesystem::create_directories(dir);
    return dir;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

// Reads `path` into a fresh HiGHS instance and solves it, independently
// of saiopt -- the point is that the written file stands on its own.
double solve_file_with_highs(const std::filesystem::path& path) {
    void* highs = Highs_create();
    Highs_setBoolOptionValue(highs, "output_flag", 0);
    HighsInt read_status = Highs_readModel(highs, path.c_str());
    Highs_run(highs);
    HighsInt model_status = Highs_getModelStatus(highs);
    double objective = Highs_getObjectiveValue(highs);
    Highs_destroy(highs);
    REQUIRE(read_status != kHighsStatusError);
    REQUIRE(model_status == kHighsModelStatusOptimal);
    return objective;
}

}  // namespace

TEST_CASE("write_model writes a file that reads back to the same model", "[write]") {
    std::string backend = GENERATE("highs", "scip");
    std::string extension = GENERATE(".lp", ".mps");
    CAPTURE(backend, extension);

    Model model = build_shipping();
    auto solver = create_solver(model, backend);
    SolveResult result = solver->solve();
    REQUIRE(result.optimal());

    auto path = scratch_dir() / (backend + extension);
    solver->write_model(path.string());

    std::string contents = read_file(path);
    REQUIRE(contents.find("ship((W1,1))") != std::string::npos);
    REQUIRE(contents.find("demand(2)") != std::string::npos);
    REQUIRE(contents.find("sub.ylim") != std::string::npos);
    REQUIRE(solve_file_with_highs(path) == Approx(result.objective_value));
}

TEST_CASE("write_model reflects changes made through the Solver", "[write]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    Model model = build_shipping();
    Variable w2_1 = model.vars<std::tuple<std::string, int>>("ship")[{"W2", 1}];
    auto solver = create_solver(model, backend);
    solver->set_objective_coefficient(w2_1, 0.5);
    SolveResult result = solver->solve();
    REQUIRE(result.optimal());

    auto path = scratch_dir() / (backend + "_mutated.lp");
    solver->write_model(path.string());
    REQUIRE(solve_file_with_highs(path) == Approx(result.objective_value));
}

TEST_CASE("write_model rejects an unknown extension without touching the file", "[write]") {
    std::string backend = GENERATE("highs", "scip");
    CAPTURE(backend);

    Model model = build_shipping();
    auto solver = create_solver(model, backend);
    auto path = scratch_dir() / (backend + ".not_a_format");
    std::filesystem::remove(path);

    REQUIRE_THROWS_AS(solver->write_model(path.string()), std::runtime_error);
    REQUIRE_FALSE(std::filesystem::exists(path));
}

TEST_CASE("file_safe_names rewrites names into distinct file identifiers", "[write]") {
    std::vector<std::string> names{"x[(a b, c)]", "1st", ".x", "a+b", "a-b", "a_b", "sub.y"};
    REQUIRE(file_safe_names(names) ==
            std::vector<std::string>{"x((ab,c))", "_1st", "_.x", "a_b", "a_b~2", "a_b~3", "sub.y"});
}
