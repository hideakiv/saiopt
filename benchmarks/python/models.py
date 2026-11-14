"""One benchmark LP, built in saiopt and in other Python modeling libraries.

    python models.py LIBRARY K N REPS

The model: K regions, each an N x N transportation sub-model --

    x[k,i,j] >= 0
    cap[k,i]:      sum_j x[k,i,j] <= 2N
    dem[k,j]:      sum_i x[k,i,j] >= 1
    used_def[k]:   used[k] == sum_ij x[k,i,j]
    link:          sum_k used[k] <= 1e9
    minimize       sum (1 + (7i + 13j + k) % 10) * x[k,i,j]

-- K*N*N + K variables, optimal objective K*N. Each library builds it
flat, the way that library is meant to be used, then solves it with
HiGHS with the solver log on. For each repetition this prints the HiGHS
log, then one line:

    TIMING <build ms> <total ms> <objective>

where build covers constructing the model and total adds getting it to
HiGHS, solving, and getting the result back. run.py subtracts the HiGHS
run time the log reports from total.

PyOptInterface needs a HiGHS shared library: set SAIOPT_BENCH_HIGHS_LIB,
or it falls back to the one the C++ build fetched (build/lib/libhighs.so).
"""

import itertools
import os
import pathlib
import sys
import time

REPO = pathlib.Path(__file__).resolve().parents[2]


def cost(k, i, j):
    return 1 + (i * 7 + j * 13 + k) % 10


def saiopt_flat(K, N):
    import saiopt as so

    model = so.Model()
    keys = list(itertools.product(range(K), range(N), range(N)))
    x = model.add_vars("x", keys, bounds=(0, so.inf))
    used = model.add_vars("used", range(K), bounds=(0, so.inf))
    model.add_constraints("cap", itertools.product(range(K), range(N)),
                          lambda ki: so.quicksum(x[ki[0], ki[1], j] for j in range(N)) <= 2.0 * N)
    model.add_constraints("dem", itertools.product(range(K), range(N)),
                          lambda kj: so.quicksum(x[kj[0], i, kj[1]] for i in range(N)) >= 1)
    model.add_constraints("used_def", range(K),
                          lambda k: used[k] == so.quicksum(x[k, i, j] for i in range(N) for j in range(N)))
    model.add_constraint("link", so.quicksum(used.values()) <= 1e9)
    model.set_objective(so.ObjectiveSense.Minimize, so.quicksum(cost(*key) * x[key] for key in keys))

    def solve():
        return so.create_solver(model, "highs", {"output_flag": True}).solve().objective_value

    return solve


def pyoptinterface(K, N):
    import pyoptinterface as poi
    from pyoptinterface import highs

    if not highs.is_library_loaded():
        lib = os.environ.get("SAIOPT_BENCH_HIGHS_LIB", str(REPO / "build" / "lib" / "libhighs.so"))
        if not highs.load_library(lib):
            raise RuntimeError(f"PyOptInterface couldn't load HiGHS from {lib}; set SAIOPT_BENCH_HIGHS_LIB")
    model = highs.Model()
    x = poi.make_tupledict(range(K), range(N), range(N), rule=lambda k, i, j: model.add_variable(lb=0))
    used = poi.make_tupledict(range(K), rule=lambda k: model.add_variable(lb=0))
    for k in range(K):
        for i in range(N):
            model.add_linear_constraint(poi.quicksum(x[k, i, j] for j in range(N)), poi.Leq, 2.0 * N)
        for j in range(N):
            model.add_linear_constraint(poi.quicksum(x[k, i, j] for i in range(N)), poi.Geq, 1)
        model.add_linear_constraint(used[k] - poi.quicksum(x[k, i, j] for i in range(N) for j in range(N)),
                                    poi.Eq, 0)
    model.add_linear_constraint(poi.quicksum(used[k] for k in range(K)), poi.Leq, 1e9)
    model.set_objective(poi.quicksum(cost(k, i, j) * x[k, i, j] for k in range(K) for i in range(N) for j in range(N)))

    def solve():
        model.optimize()
        return model.get_model_attribute(poi.ModelAttribute.ObjectiveValue)

    return solve


def linopy(K, N):
    import linopy as lp
    import numpy as np
    import pandas as pd
    import xarray as xr

    Kc, I, J = pd.RangeIndex(K, name="k"), pd.RangeIndex(N, name="i"), pd.RangeIndex(N, name="j")
    model = lp.Model()
    x = model.add_variables(lower=0, coords=[Kc, I, J], name="x")
    used = model.add_variables(lower=0, coords=[Kc], name="used")
    model.add_constraints(x.sum("j") <= 2.0 * N, name="cap")
    model.add_constraints(x.sum("i") >= 1, name="dem")
    model.add_constraints(used - x.sum(["i", "j"]) == 0, name="used_def")
    model.add_constraints(used.sum() <= 1e9, name="link")
    k, i, j = np.ix_(np.arange(K), np.arange(N), np.arange(N))
    c = xr.DataArray(1 + (7 * i + 13 * j + k) % 10, coords=[Kc, I, J])
    model.add_objective((c * x).sum())

    def solve():
        model.solve("highs", io_api="direct")
        return model.objective.value

    return solve


def pulp(K, N):
    import pulp as pl

    prob = pl.LpProblem("regions", pl.LpMinimize)
    x = prob.add_variable_dict("x", (range(K), range(N), range(N)), lowBound=0)
    used = prob.add_variable_dict("used", range(K), lowBound=0)
    prob += pl.lpSum(cost(k, i, j) * x[k, i, j] for k in range(K) for i in range(N) for j in range(N))
    for k in range(K):
        for i in range(N):
            prob += pl.lpSum(x[k, i, j] for j in range(N)) <= 2.0 * N
        for j in range(N):
            prob += pl.lpSum(x[k, i, j] for i in range(N)) >= 1
        prob += used[k] == pl.lpSum(x[k, i, j] for i in range(N) for j in range(N))
    prob += pl.lpSum(used[k] for k in range(K)) <= 1e9

    def solve():
        prob.solve(pl.HiGHS(msg=True))
        return pl.value(prob.objective)

    return solve


def pyomo(K, N):
    import pyomo.environ as pyo
    from pyomo.contrib import appsi

    m = pyo.ConcreteModel()
    m.K = pyo.RangeSet(0, K - 1)
    m.I = pyo.RangeSet(0, N - 1)
    m.J = pyo.RangeSet(0, N - 1)
    m.x = pyo.Var(m.K, m.I, m.J, within=pyo.NonNegativeReals)
    m.used = pyo.Var(m.K, within=pyo.NonNegativeReals)
    m.cap = pyo.Constraint(m.K, m.I, rule=lambda m, k, i: pyo.quicksum(m.x[k, i, j] for j in m.J) <= 2.0 * N)
    m.dem = pyo.Constraint(m.K, m.J, rule=lambda m, k, j: pyo.quicksum(m.x[k, i, j] for i in m.I) >= 1)
    m.used_def = pyo.Constraint(
        m.K, rule=lambda m, k: m.used[k] == pyo.quicksum(m.x[k, i, j] for i in m.I for j in m.J))
    m.link = pyo.Constraint(expr=pyo.quicksum(m.used[k] for k in m.K) <= 1e9)
    m.obj = pyo.Objective(expr=pyo.quicksum(cost(k, i, j) * m.x[k, i, j] for k in m.K for i in m.I for j in m.J))

    def solve():
        solver = appsi.solvers.Highs()
        solver.config.stream_solver = True
        return solver.solve(m).best_feasible_objective

    return solve


LIBRARIES = {
    "saiopt": saiopt_flat,
    "pyoptinterface": pyoptinterface,
    "linopy": linopy,
    "pulp": pulp,
    "pyomo": pyomo,
}

if __name__ == "__main__":
    library, K, N, reps = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    build = LIBRARIES[library]
    build(2, 3)()  # untimed warm-up: imports, first-call setup, solver library loading
    for _ in range(reps):
        t0 = time.perf_counter()
        solve = build(K, N)
        t1 = time.perf_counter()
        objective = solve()
        t2 = time.perf_counter()
        if abs(objective - K * N) > 1e-6 * K * N:
            raise SystemExit(f"{library}: wrong objective {objective}, expected {K * N}")
        print(f"\nTIMING {1000 * (t1 - t0):.1f} {1000 * (t2 - t0):.1f} {objective}", flush=True)
