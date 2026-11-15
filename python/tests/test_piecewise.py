import pytest

import saiopt as so

# Up, down, up again: nonconvex, so a plain convex combination of the
# breakpoints would undercut it (0.25 instead of 1 at x >= 1).
POINTS = [0.0, 2.0, 4.0, 6.0]
VALUES = [0.0, 4.0, 1.0, 6.0]


@pytest.mark.parametrize(
    "method, backend",
    [
        (so.PiecewiseMethod.Sos2, "scip"),
        (so.PiecewiseMethod.Incremental, "scip"),
        (so.PiecewiseMethod.Incremental, "highs"),
    ],
)
def test_minimizes_a_nonconvex_function(method, backend):
    model = so.Model()
    x = model.add_var("x", bounds=(1, so.inf))
    f = model.add_piecewise("f", x, POINTS, VALUES, method=method)
    model.set_objective(so.ObjectiveSense.Minimize, f.value)

    result = so.create_solver(model, backend).solve()
    assert result.objective_value == pytest.approx(1.0)
    assert result[x] == pytest.approx(4.0)


def test_the_default_method_works_with_highs():
    model = so.Model()
    x = model.add_var("x", bounds=(1, so.inf))
    f = model.add_piecewise("f", x, POINTS, VALUES)
    model.set_objective(so.ObjectiveSense.Minimize, f.value)
    assert f.block.var("delta[0]") is not None
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(1.0)


def test_values_may_be_a_callable_and_x_an_expression():
    model = so.Model()
    a = model.add_var("a", bounds=(1, 1))
    b = model.add_var("b", bounds=(0.5, 0.5))
    square = model.add_piecewise("square", a + b, [0, 1, 2, 3], lambda p: p * p)
    model.set_objective(so.ObjectiveSense.Minimize, square.value)
    assert so.create_solver(model, "scip").solve().objective_value == pytest.approx(2.5)


def test_a_repeated_point_makes_a_jump():
    model = so.Model()
    x = model.add_var("x", bounds=(1.5, 1.5))
    step = model.add_piecewise("step", x, [0, 1, 1, 2], [1, 1, 3, 3])
    model.set_objective(so.ObjectiveSense.Minimize, step.value)
    assert so.create_solver(model, "scip").solve().objective_value == pytest.approx(3.0)


def test_auxiliary_variables_live_in_the_returned_child_block():
    model = so.Model()
    x = model.add_var("x", bounds=(0, 6))
    f = model.add_piecewise("f", x, POINTS, VALUES, method=so.PiecewiseMethod.Sos2)
    g = model.add_piecewise("g", x, POINTS, VALUES)

    assert len(f.block.vars("lambda")) == 4
    assert len(model.block("f").vars("lambda")) == 4
    assert len(g.block.vars("delta")) == 3
    assert len(g.block.vars("full")) == 2
    assert model.constraint("f") is not None


def test_highs_rejects_sos2():
    model = so.Model()
    x = model.add_var("x", bounds=(0, 6))
    f = model.add_piecewise("f", x, POINTS, VALUES, method=so.PiecewiseMethod.Sos2)
    model.set_objective(so.ObjectiveSense.Minimize, f.value)
    with pytest.raises(RuntimeError, match="SOS2"):
        so.create_solver(model, "highs")


def test_bad_breakpoints_raise_and_change_nothing():
    model = so.Model()
    x = model.add_var("x")
    with pytest.raises(RuntimeError, match="non-decreasing"):
        model.add_piecewise("f", x, [0, 2, 1], [0, 1, 2])
    with pytest.raises(RuntimeError, match="2 points but 3 values"):
        model.add_piecewise("f", x, [0, 1], [0, 1, 2])
    assert model.block("f") is None
    assert model.constraint("f") is None


def test_x_must_be_linear():
    model = so.Model()
    x = model.add_var("x")
    with pytest.raises(TypeError):
        model.add_piecewise("f", x * x, POINTS, VALUES)


def test_add_sos2():
    model = so.Model()
    v = model.add_vars("v", range(3), bounds=(0, 1))
    model.add_sos2("pick", [v[0], v[1], v[2]])
    model.set_objective(so.ObjectiveSense.Maximize, 2 * v[0] + 3 * v[1] + v[2])
    result = so.create_solver(model, "scip").solve()
    assert result.objective_value == pytest.approx(5.0)
    assert result[v[2]] == pytest.approx(0.0)


def test_sos2_survives_serialization():
    region = so.Block()
    x = region.add_var("x", bounds=(1, so.inf))
    f = region.add_piecewise("f", x, POINTS, VALUES, method=so.PiecewiseMethod.Sos2)
    region.set_objective(f.value)

    model = so.Model.from_block(so.Block.deserialize(region.serialize()), so.ObjectiveSense.Minimize)
    assert so.create_solver(model, "scip").solve().objective_value == pytest.approx(1.0)
