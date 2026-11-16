import itertools

import pytest

import saiopt as so

BACKENDS = ["highs", "scip"]


@pytest.mark.parametrize("backend", BACKENDS)
def test_diet_lp(backend):
    model = so.Model()
    bread = model.add_var("bread", bounds=(0, so.inf))
    milk = model.add_var("milk", bounds=(0, so.inf))
    model.set_objective(so.ObjectiveSense.Minimize, 2 * bread + 3 * milk)
    model.add_constraint("protein", 4 * bread + 8 * milk >= 20)
    model.add_constraint("calories", 100 * bread + 150 * milk >= 400)

    result = so.create_solver(model, backend).solve()
    assert result.optimal
    assert result.status == so.SolveStatus.Optimal
    assert result.objective_value == pytest.approx(8.0)
    # Optimal at (1, 2) and (0, 8/3) alike, so check feasibility, not the point.
    b, m = result[bread], result.value(milk)
    assert 4 * b + 8 * m >= 20 - 1e-6
    assert 100 * b + 150 * m >= 400 - 1e-6


def test_indexed_groups_name_items_like_cpp_and_keep_declaration_order(tmp_path):
    model = so.Model()
    arcs = list(itertools.product(["W2", "W1"], [1, 2]))
    caps = {"W1": 3.0, "W2": 4.0}
    ship = model.add_vars("ship", arcs, bounds=lambda arc: (0, caps[arc[0]]))
    demand = model.add_constraints(
        "demand", [1, 2], lambda c: ship["W1", c] + ship["W2", c] >= 5
    )
    model.set_objective(so.ObjectiveSense.Minimize, so.quicksum(ship.values()))

    assert list(ship) == arcs
    assert model.var("ship[(W2, 1)]").is_same(ship["W2", 1])
    assert model.constraint("demand[2]") == demand[2]
    assert list(model.vars("ship")) == arcs
    assert model.vars("ship")["W1", 2].is_same(ship["W1", 2])
    assert list(model.constraints("demand")) == [1, 2]

    solver = so.create_solver(model, "highs")
    result = solver.solve()
    assert result.objective_value == pytest.approx(10.0)
    values = result.values(ship)
    assert values["W1", 1] + values["W2", 1] == pytest.approx(5.0)
    assert result.values([ship["W1", 1]]) == [values["W1", 1]]

    path = tmp_path / "ship.lp"
    solver.write_model(path)
    assert "ship((W1,1))" in path.read_text()


def test_duplicate_names_raise():
    model = so.Model()
    model.add_var("x")
    with pytest.raises(RuntimeError, match="already exists"):
        model.add_var("x")


def test_names_cant_be_empty_or_contain_a_dot():
    model = so.Model()
    x = model.add_var("whole wheat")  # spaces are fine
    with pytest.raises(RuntimeError, match="a name can't be empty"):
        model.add_var("")
    with pytest.raises(RuntimeError, match=r"can't name a constraint \"a\.b\""):
        model.add_constraint("a.b", x >= 0)
    with pytest.raises(RuntimeError, match="can't name a group of child blocks"):
        model.add_blocks("a.b", [1], lambda k: so.Block())
    # A key may contain one, as a float does.
    model.add_vars("y", [2.5])
    model.add_vars("z", ["n.1"])
    assert model.var("y[2.5]") is not None and model.var("z[n.1]") is not None


def test_binary_and_integer_variables():
    model = so.Model()
    pick = model.add_vars("pick", range(4), kind=so.VarKind.Binary)
    count = model.add_var("count", kind=so.VarKind.Integer, bounds=(0, 10))
    model.add_constraint("cap", so.quicksum(pick.values()) <= 2.5)
    model.add_constraint("link", count == so.quicksum(pick.values()))
    model.set_objective(so.ObjectiveSense.Maximize, so.quicksum((i + 1) * pick[i] for i in range(4)))

    result = so.create_solver(model, "highs").solve()
    assert result.objective_value == pytest.approx(7.0)
    assert result[count] == pytest.approx(2.0)


def test_blocks_scale_objectives_and_reach_child_variables():
    model = so.Model()
    sub = so.Block()
    y = sub.add_var("y", bounds=(0, 10))
    sub.add_constraint("floor", y >= 2)
    sub.set_objective(y)
    handle = model.add_block("sub", sub, multiplier=3.0)

    x = model.add_var("x", bounds=(0, 10))
    model.add_constraint("link", x - y >= 1)
    model.set_objective(so.ObjectiveSense.Minimize, x)

    result = so.create_solver(model, "highs").solve()
    assert result.objective_value == pytest.approx(3 * 2 + 3)
    assert handle.var("y").is_same(y)
    assert model.block("sub").constraint("floor") is not None
    assert model.block("missing") is None


def test_child_handles_survive_sibling_additions():
    model = so.Model()
    first = model.add_block("first", so.Block())
    # Enough siblings to force the parent's child storage to reallocate,
    # which would leave a raw pointer to "first" dangling.
    for i in range(64):
        model.add_block(f"sibling{i}", so.Block())
    v = first.add_var("v", bounds=(0, 1))
    assert model.block("first").var("v").is_same(v)

    nested = first.add_block("nested", so.Block())
    for i in range(64):
        first.add_block(f"n{i}", so.Block())
    nested.add_var("w")
    assert model.block("first").block("nested").var("w") is not None


def test_add_blocks_with_per_key_multiplier():
    model = so.Model()

    def region(k):
        b = so.Block()
        z = b.add_var("z", bounds=(1, 5))
        b.set_objective(z)
        return b

    regions = model.add_blocks("region", ["b", "a"], region, multiplier=lambda k: 2.0 if k == "a" else 1.0)
    model.set_objective(so.ObjectiveSense.Minimize, 0)

    assert list(regions) == ["b", "a"]
    assert list(model.blocks("region")) == ["b", "a"]
    assert model.block("region[a]").var("z") is not None
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(3.0)


def test_a_moved_block_cant_be_used_or_added_twice():
    model = so.Model()
    sub = so.Block()
    model.add_block("sub", sub)
    with pytest.raises(RuntimeError, match="moved into another block"):
        sub.add_var("late")
    with pytest.raises(RuntimeError):
        model.add_block("again", sub)
    # The attached child is still reachable, and isn't itself addable.
    with pytest.raises(RuntimeError, match="standalone"):
        model.add_block("copy", model.block("sub"))


def test_a_block_cant_be_added_into_itself():
    outer = so.Block()
    inner = outer.add_block("inner", so.Block())
    with pytest.raises(RuntimeError, match="itself"):
        outer.add_block("self", outer)
    with pytest.raises(RuntimeError, match="descendants"):
        inner.add_block("loop", outer)
    # A failed add leaves the would-be child intact.
    outer.add_var("still_usable")


def test_duplicate_child_name_leaves_the_child_intact():
    model = so.Model()
    model.add_block("sub", so.Block())
    orphan = so.Block()
    with pytest.raises(RuntimeError, match="already exists"):
        model.add_block("sub", orphan)
    orphan.add_var("ok")


def test_serialize_round_trip_and_from_block():
    block = so.Block()
    x = block.add_var("x", bounds=(0, 6))
    y = block.add_var("y", bounds=(0, so.inf))
    block.add_constraint("total", x + y >= 5)
    block.set_objective(2 * x + 3 * y + x * x)

    rebuilt = so.Block.deserialize(block.serialize())
    model = so.Model.from_block(rebuilt, so.ObjectiveSense.Minimize)
    with pytest.raises(RuntimeError):
        rebuilt.add_var("late")

    result = so.create_solver(model, "highs").solve()
    # min x^2 + 2x + 3y over x + y >= 5: the marginal cost 2x + 2 of x
    # reaches y's 3 at x = 0.5.
    assert result.objective_value == pytest.approx(0.25 + 1 + 13.5)
    assert result[model.var("x")] == pytest.approx(0.5)


def test_print_shows_the_folded_objective():
    model = so.Model()
    x = model.add_var("x")
    model.set_objective(so.ObjectiveSense.Minimize, 2 * x * x - x + 1)
    text = str(model)
    assert text.startswith("model (minimize)")
    assert "2 x^2 - x + 1" in text


def test_objective_must_be_set_before_solving():
    model = so.Model()
    model.add_var("x")
    with pytest.raises(RuntimeError, match="no objective"):
        so.create_solver(model, "highs")
