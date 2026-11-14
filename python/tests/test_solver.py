import pytest

import saiopt as so

BACKENDS = ["highs", "scip"]


def knapsack(n=80):
    model = so.Model()
    x = model.add_vars("x", range(n), kind=so.VarKind.Binary)
    model.set_objective(so.ObjectiveSense.Maximize, so.quicksum((1 + i * 53 % 29) * x[i] for i in range(n)))
    for c in range(5):
        weight = so.quicksum((1 + (i * 37 + c * 17) % 23) * x[i] for i in range(n))
        model.add_constraint(f"cap{c}", weight <= 130 + c * 11)
    return model, x


@pytest.mark.parametrize("backend", BACKENDS)
def test_infeasible_status(backend):
    model = so.Model()
    x = model.add_var("x")
    model.add_constraint("floor", x >= 5)
    model.add_constraint("ceiling", x <= 3)
    model.set_objective(so.ObjectiveSense.Minimize, x)

    options = {"presolve": "off"} if backend == "highs" else None
    result = so.create_solver(model, backend, options).solve()
    assert result.status == so.SolveStatus.Infeasible
    assert not result.has_solution
    with pytest.raises(KeyError):
        result[x]


def test_options_are_typed_by_python_value():
    model, _ = knapsack()
    result = so.create_solver(model, "highs", {"mip_max_improving_sols": 1, "time_limit": 100.0, "output_flag": False}).solve()
    assert result.status == so.SolveStatus.SolutionLimit
    assert result.has_solution

    with pytest.raises(RuntimeError):
        so.create_solver(model, "highs", {"not_an_option": 1})
    with pytest.raises(TypeError, match="bool, int, float, or str"):
        so.create_solver(model, "highs", {"time_limit": [1]})


def test_unknown_backend():
    model, _ = knapsack(4)
    with pytest.raises(RuntimeError, match="no solver registered"):
        so.create_solver(model, "cplex")


@pytest.mark.parametrize("backend", BACKENDS)
def test_progress_callback_can_interrupt(backend):
    model, _ = knapsack()
    calls = []
    solver = so.create_solver(model, backend)
    solver.set_progress_callback(lambda p: calls.append(p.elapsed_seconds) or False)
    result = solver.solve()
    assert calls
    assert result.status == so.SolveStatus.Interrupted


@pytest.mark.parametrize("backend", BACKENDS)
def test_progress_callback_returning_none_keeps_going(backend):
    model, _ = knapsack()
    seen = []
    solver = so.create_solver(model, backend)
    solver.set_progress_callback(seen.append)
    assert solver.solve().optimal
    assert all(isinstance(p, so.SolverProgress) for p in seen)


@pytest.mark.parametrize("backend", BACKENDS)
def test_progress_callback_exception_is_reraised(backend):
    model, _ = knapsack()
    solver = so.create_solver(model, backend)

    def explode(progress):
        raise ValueError("stop right there")

    solver.set_progress_callback(explode)
    with pytest.raises(ValueError, match="stop right there"):
        solver.solve()


@pytest.mark.parametrize("backend", BACKENDS)
def test_initial_solution_seeds_the_search(backend):
    model, x = knapsack()
    optimum = so.create_solver(model, backend).solve()

    first_only = {"mip_max_improving_sols": 1} if backend == "highs" else {"limits/solutions": 1}
    solver = so.create_solver(model, backend, first_only)
    solver.set_initial_solution(optimum.variable_values)
    assert solver.solve().objective_value == pytest.approx(optimum.objective_value)

    with pytest.raises(RuntimeError, match="outside its bounds"):
        solver.set_initial_solution({x[0]: 2.0})


def test_solver_setters_act_on_the_backend():
    model = so.Model()
    x = model.add_var("x", bounds=(0, 4))
    y = model.add_var("y", bounds=(0, 4))
    cap = model.add_constraint("cap", x + y <= 5)
    model.set_objective(so.ObjectiveSense.Maximize, x + 2 * y)

    solver = so.create_solver(model, "highs")
    assert solver.solve().objective_value == pytest.approx(9.0)

    solver.set_objective_coefficient(x, 3)
    solver.set_constraint_upper(cap, so.inf)
    solver.set_variable_upper(y, 1)
    assert solver.objective_coefficient(x) == 3
    assert solver.constraint_upper(cap) == so.inf
    assert solver.solve().objective_value == pytest.approx(14.0)


def test_lp_duals_and_ranging():
    model = so.Model()
    x = model.add_var("x", bounds=(0, so.inf))
    y = model.add_var("y", bounds=(0, so.inf))
    total = model.add_constraint("total", x + y >= 10)
    model.set_objective(so.ObjectiveSense.Minimize, 2 * x + 3 * y)

    result = so.create_solver(model, "highs").solve()
    assert result.dual(total) == pytest.approx(2.0)
    assert result.reduced_cost(y) == pytest.approx(1.0)
    rng = result.objective_coefficient_range(x)
    assert rng.lower <= 2 <= rng.upper
    assert total in result.duals


@pytest.mark.parametrize("backend", BACKENDS)
def test_quadratic_objective(backend):
    model = so.Model()
    x = model.add_var("x")
    y = model.add_var("y")
    model.add_constraint("total", x + y >= 2)
    model.set_objective(so.ObjectiveSense.Minimize, x**2 + y**2)

    result = so.create_solver(model, backend).solve()
    assert result.objective_value == pytest.approx(2.0)
    assert result[x] == pytest.approx(1.0, abs=1e-3)


def test_highs_refuses_a_mixed_integer_quadratic_objective():
    model = so.Model()
    x = model.add_var("x", kind=so.VarKind.Integer, bounds=(0, 5))
    model.set_objective(so.ObjectiveSense.Minimize, (x - 2.4) ** 2)
    with pytest.raises(RuntimeError, match="scip"):
        so.create_solver(model, "highs")
    result = so.create_solver(model, "scip").solve()
    assert result[x] == pytest.approx(2.0)
