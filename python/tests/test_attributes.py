"""Declaring a block's contents by attribute assignment (model.x = so.Var())."""

import copy

import pytest

import saiopt as so


def diet_model():
    model = so.Model()
    model.bread = so.Var(bounds=(0, so.inf))
    model.milk = so.Var(bounds=(0, so.inf))
    model.protein = so.Constraint(4 * model.bread + 8 * model.milk >= 20)
    model.calories = so.Constraint(100 * model.bread + 150 * model.milk >= 400)
    model.set_objective(so.ObjectiveSense.Minimize, 2 * model.bread + 3 * model.milk)
    return model


def test_a_model_declared_by_attributes_solves():
    model = diet_model()
    result = so.create_solver(model, "highs").solve()
    assert result.objective_value == pytest.approx(8.0)
    assert result[model.bread] == pytest.approx(1.0)
    assert result[model.milk] == pytest.approx(2.0)


def test_attributes_are_the_same_items_as_the_add_methods_look_up():
    model = diet_model()
    assert model.bread.is_same(model.var("bread"))
    assert model.protein == model.constraint("protein")


def test_single_constraint_forms():
    model = so.Model()
    model.x = so.Var(bounds=(0, 10))
    model.y = so.Var(bounds=(0, 10))
    model.ranged = so.Constraint(model.x - model.y, 1, 4)
    model.wrapped = so.Constraint(model.x <= 5)
    model.bare = model.x + model.y >= 2  # the object so.Constraint(...) copies, so it's accepted too
    model.set_objective(so.ObjectiveSense.Maximize, model.x + model.y)
    assert all(model.constraint(name) is not None for name in ("ranged", "wrapped", "bare"))
    # x = 5 and y = x - 1 = 4.
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(9.0)
    # Copying keeps what a comparison of two variables knows: its truth value.
    assert so.Constraint(model.x == model.x)
    assert not so.Constraint(model.x == model.y)


def test_var_kind_and_bounds():
    model = so.Model()
    model.n = so.Var(kind=so.VarKind.Integer, bounds=(0, 10))
    model.flag = so.Var(kind=so.VarKind.Binary)
    model.set_objective(so.ObjectiveSense.Maximize, model.n + model.flag)
    result = so.create_solver(model, "highs").solve()
    assert result.objective_value == pytest.approx(11.0)


def test_vars_and_constraints_groups():
    model = so.Model()
    caps = {"a": 2.0, "b": 3.0}
    model.x = so.Vars(caps, bounds=lambda k: (0, caps[k]))
    model.cap = so.Constraints(caps, lambda k: model.x[k] <= caps[k] - 1)
    model.set_objective(so.ObjectiveSense.Maximize, so.quicksum(model.x.values()))

    assert list(model.x) == ["a", "b"]
    assert set(model.cap) == {"a", "b"}
    assert model.cap["a"] == model.constraint("cap[a]")
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(3.0)


def build_site(cost):
    site = so.Block()
    site.make = so.Var(bounds=(0, 10))
    site.set_objective(cost * site.make)
    return site


def test_child_blocks_plain_scaled_and_grouped():
    model = so.Model()
    model.north = build_site(1.0)
    model.south = so.Scaled(build_site(1.0), 3.0)
    model.region = so.Blocks(["e", "w"], lambda k: build_site(2.0), multiplier=lambda k: 1.0 if k == "e" else 0.5)
    total = model.north.make + model.south.make + model.region["e"].make + model.region["w"].make
    model.demand = so.Constraint(total >= 25)
    model.set_objective(so.ObjectiveSense.Minimize, 0)

    result = so.create_solver(model, "highs").solve()
    # Unit costs after multipliers: north 1, south 3, e 2, w 1 -- fill north
    # and w (10 each), then 5 from e.
    assert result.objective_value == pytest.approx(10 + 10 + 2 * 5)
    assert result[model.region["e"].make] == pytest.approx(5.0)


def test_attributes_can_be_declared_and_read_through_a_child_view():
    model = so.Model()
    model.site = so.Block()
    model.site.extra = so.Var(bounds=(0, 1))
    assert model.site.extra.is_same(model.block("site").var("extra"))


def test_piecewise_linear():
    model = so.Model()
    model.x = so.Var(bounds=(1, so.inf))
    model.f = so.PiecewiseLinear(model.x, [0, 2, 4, 6], [0, 4, 1, 6])
    model.set_objective(so.ObjectiveSense.Minimize, model.f.value)
    assert isinstance(model.f, so.Piecewise)
    assert model.f.block.var("delta[0]") is not None
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(1.0)

    model.g = so.PiecewiseLinear(model.x, [0, 6], lambda p: 2 * p, method=so.PiecewiseMethod.Sos2)
    assert model.g.block.var("lambda[0]") is not None
    assert model.piecewise("g") is not None
    assert model.piecewise("x") is None


def test_attributes_survive_serialization():
    region = so.Block()
    region.x = so.Var(bounds=(1, so.inf))
    region.f = so.PiecewiseLinear(region.x, [0, 2, 4, 6], [0, 4, 1, 6])
    region.set_objective(region.f.value)

    model = so.Model.from_block(so.Block.deserialize(region.serialize()), so.ObjectiveSense.Minimize)
    assert model.x.is_same(model.var("x"))
    assert so.create_solver(model, "highs").solve().objective_value == pytest.approx(1.0)
    # Attached as a child: the block brings its own objective, f(x).
    model2 = so.Model()
    model2.region = so.Block.deserialize(region.serialize())
    model2.set_objective(so.ObjectiveSense.Minimize, 0)
    assert isinstance(model2.region.f, so.Piecewise)
    assert so.create_solver(model2, "highs").solve().objective_value == pytest.approx(1.0)


@pytest.mark.parametrize("name", ["vars", "add_var", "set_objective", "block"])
def test_method_names_cant_be_declared(name):
    with pytest.raises(AttributeError, match="method"):
        setattr(so.Model(), name, so.Var())


def test_underscore_names_are_reserved():
    model = so.Model()
    with pytest.raises(AttributeError, match="reserved"):
        model._x = so.Var()


def test_a_name_can_only_be_declared_once_whatever_its_kind():
    model = so.Model()
    model.x = so.Var()
    with pytest.raises(AttributeError, match="already a variable"):
        model.x = so.Var()
    with pytest.raises(AttributeError, match="already a variable"):
        model.x = model.x >= 0
    model.f = so.PiecewiseLinear(model.x, [0, 1], [0, 1])
    with pytest.raises(AttributeError, match="already a piecewise function"):
        model.f = so.Block()


@pytest.mark.parametrize("value, hint", [(3, "int"), ("x", "str"), (None, "NoneType")])
def test_unsupported_values_raise(value, hint):
    with pytest.raises(TypeError, match=hint):
        so.Model().y = value


def test_assigning_a_variable_hints_at_var():
    model = so.Model()
    model.x = so.Var()
    with pytest.raises(TypeError, match=r"so\.Var\(\.\.\.\)"):
        model.y = model.x


def test_reading_an_undeclared_name_raises_attribute_error():
    model = so.Model()
    with pytest.raises(AttributeError, match="nothing named 'nope'"):
        model.nope
    assert not hasattr(model, "nope")
    assert not hasattr(so.Block(), "nope")


def test_a_name_used_twice_through_add_methods_is_ambiguous():
    model = so.Model()
    x = model.add_var("x")
    model.add_constraint("x", x >= 0)
    with pytest.raises(AttributeError, match="variable, constraint"):
        model.x
    assert model.var("x").is_same(x)


def test_dunder_lookups_still_behave():
    block = so.Block()
    block.x = so.Var()
    # copy asks for __deepcopy__/__reduce_ex__ etc.; those must not be
    # mistaken for declared names.
    with pytest.raises((TypeError, AttributeError, RuntimeError)) as info:
        copy.deepcopy(block)
    assert "nothing named" not in str(info.value)


def test_reading_a_group_again_gives_the_same_dict_without_rebuilding_it():
    # A read is cached on the object, so model.x[i] in a loop doesn't
    # rebuild the whole group every time.
    model = so.Model()
    model.x = so.Vars(range(20000), bounds=(0, 1))
    assert model.x is model.x
    model.c = so.Constraints(range(3), lambda i: model.x[i] <= 1)
    assert model.c is model.c
    # Child blocks aren't cached (their handles refer back to the root),
    # but each read still reaches the same block.
    model.site = so.Block()
    model.site.y = so.Var()
    assert model.site.y.is_same(model.site.y)


def test_a_block_moved_into_a_parent_cant_be_read_through_its_old_handle():
    site = so.Block()
    site.x = so.Var()
    assert site.x is not None
    model = so.Model()
    model.site = site
    with pytest.raises(RuntimeError, match="moved into another block"):
        site.x
    assert model.site.x.is_same(model.block("site").var("x"))


def test_groups_keep_their_keys_through_serialization():
    block = so.Block()
    block.x = so.Vars([("a", 1), ("b", 2.5)], bounds=(0, 1))
    block.y = so.Vars([3, 1, 2])
    block.cap = so.Constraints(["e", "w"], lambda k: block.y[3] <= 1)
    block.region = so.Blocks([10, 20], lambda k: so.Block())

    # Twice, so keys restored by the first deserialize are written again.
    rebuilt = so.Block.deserialize(so.Block.deserialize(block.serialize()).serialize())

    assert list(rebuilt.x) == [("a", 1), ("b", 2.5)]
    assert rebuilt.x["b", 2.5].is_same(rebuilt.var("x[(b, 2.5)]"))
    assert list(rebuilt.y) == [3, 1, 2]  # declaration order, not sorted
    assert set(rebuilt.cap) == {"e", "w"}
    assert set(rebuilt.region) == {10, 20}
    assert rebuilt.vars("y") == rebuilt.y


def test_keys_of_other_types_drop_on_serialization_but_items_stay_reachable():
    block = so.Block()
    block.flag = so.Vars([True, False])  # bool would come back as int, so it isn't written
    rebuilt = so.Block.deserialize(block.serialize())
    with pytest.raises(RuntimeError, match="without its keys"):
        rebuilt.flag
    assert rebuilt.var("flag[True]") is not None
    assert list(rebuilt.vars("flag", [False, True])) == [False, True]


def test_lookup_by_keys_finds_items_by_full_name_in_the_order_given():
    model = so.Model()
    model.x = so.Vars(range(5))
    model.c = so.Constraints(range(3), lambda i: model.x[i] >= 0)
    model.region = so.Blocks(["a", "b"], lambda k: so.Block())

    picked = model.vars("x", [4, 0])
    assert list(picked) == [4, 0]
    assert picked[4].is_same(model.x[4])
    assert list(model.constraints("c", [2])) == [2]
    assert list(model.blocks("region", ["b"])) == ["b"]
    with pytest.raises(RuntimeError, match=r"x\[9\]"):
        model.vars("x", [9])


def test_a_group_declared_in_cpp_round_trips_with_python_keys():
    region = so.Block()
    region.v = so.Var(bounds=(0, 6))
    region.f = so.PiecewiseLinear(region.v, [0, 2, 4, 6], [0, 4, 1, 6])
    rebuilt = so.Block.deserialize(region.serialize())
    # add_piecewise's "delta" group is keyed by C++ std::size_t.
    assert list(rebuilt.f.block.delta) == [0, 1, 2]
