import pytest

import saiopt as so


@pytest.fixture
def xy():
    model = so.Model()
    return model, model.add_var("x"), model.add_var("y")


def test_arithmetic_keeps_linear_expressions_linear(xy):
    _, x, y = xy
    for expr in (x + y, 2 * x, x * 2, x - 3, 3 - x, -x, +x, x / 2, sum([x, y]), so.quicksum([x, y, 1])):
        assert isinstance(expr, so.LinearExpression)

    e = 3 * x - y / 2 + 4
    assert e.coefficient(x) == 3
    assert e.coefficient(y) == -0.5
    assert e.constant == 4
    assert e.terms == {x: 3, y: -0.5}


def test_products_and_squares_are_quadratic(xy):
    _, x, y = xy
    q = x * y + 2 * (y * x) + x**2
    assert isinstance(q, so.QuadraticExpression)
    assert q.coefficient(x, y) == 3
    assert q.coefficient(y, x) == 3
    assert q.coefficient(x, x) == 1

    p = (x + 1) * (y - 2)
    assert p.coefficient(x, y) == 1
    assert p.linear.coefficient(x) == -2
    assert p.constant == -2

    assert isinstance(q + x, so.QuadraticExpression)
    assert isinstance(so.quicksum([x, x * y, 3]), so.QuadraticExpression)


def test_degree_above_two_is_rejected(xy):
    _, x, y = xy
    with pytest.raises(TypeError, match="degree 3"):
        x * y * x
    with pytest.raises(TypeError):
        x**3


def test_comparisons_build_constraints(xy):
    _, x, y = xy
    c = x + y <= 5
    assert isinstance(c, so.Constraint)
    assert c.upper == 5
    assert c.coefficient(x) == 1

    reflected = 3 <= x  # int.__le__ declines, so this is x >= 3
    assert reflected.lower == 3

    ranged = so.Constraint(x - y, 1, 4)
    assert (ranged.lower, ranged.upper) == (1, 4)

    with pytest.raises(TypeError, match="linear"):
        x * y <= 3


def test_chained_comparison_is_an_error_not_a_dropped_bound(xy):
    _, x, _ = xy
    with pytest.raises(TypeError, match="Constraint\\(expr, lower, upper\\)"):
        1 <= x <= 4


def test_variables_stay_usable_as_dict_keys(xy):
    model, x, y = xy
    # `==` on two Variables builds a constraint, but its truth value is
    # whether they're the same variable -- which is what a dict lookup
    # (comparing a fresh handle against the stored one) relies on.
    assert bool(x == model.var("x"))
    assert not bool(x == y)
    assert x != y
    assert x.is_same(model.var("x"))

    table = {x: "x", y: "y"}
    assert table[model.var("x")] == "x"
    assert len({x, model.var("x"), y}) == 2


def test_in_place_addition_accumulates(xy):
    _, x, y = xy
    total = so.LinearExpression()
    alias = total
    for _ in range(3):
        total += x
    total -= y
    assert total is alias
    assert total.coefficient(x) == 3
    assert total.coefficient(y) == -1


def test_expressions_are_unhashable(xy):
    _, x, y = xy
    with pytest.raises(TypeError):
        hash(x + y)
