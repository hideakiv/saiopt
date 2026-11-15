Writing a solver backend
========================

saiopt's core library knows nothing about HiGHS or SCIP. It depends only
on the abstract ``Solver`` class in ``saiopt/solver.hpp`` and a registry
of named factories. A backend is a class that implements ``Solver``, plus
one line that registers it under a name. ``create_solver(model, "name")``
then finds it, with no change to saiopt and no backend header in your
modeling code.

Step 1: implement Solver
------------------------

A backend implements these pure virtual functions:

* ``solve()``, returning a ``SolveResult``
* the getters and setters from :doc:`modifying`
  (``objective_coefficient``, ``set_constraint_lower``, and so on)
* ``set_progress_callback``, ``set_sensitivity_ranging``,
  ``write_model``, and ``set_initial_solution``

Its constructor takes the ``Model`` and the ``SolverOptions``. A real
backend translates the model into its solver's native form there, once.
``saiopt/sparse_lp.hpp`` provides ``flatten(model)``, which turns a
model's whole block tree into a ``SparseLp``: a column-wise sparse
matrix with costs, bounds, kinds, an optional quadratic objective, and
the ``Variable`` and ``ConstraintRef`` each column and row came from,
for mapping results back. Most solver C APIs can take that directly.
``src/solvers/highs_solver.cpp`` is a complete example.

To keep this page short, the backend below doesn't talk to a solver
itself. It wraps the ``"highs"`` backend and counts how often
``solve()`` is called. Every other function forwards to HiGHS.

.. check: file

.. code-block:: cpp

   class CountingSolver : public Solver {
   public:
       CountingSolver(const Model& model, const SolverOptions& options)
           : inner_(create_solver(model, "highs", options)) {}

       SolveResult solve() override {
           ++solves_;
           std::printf("solve #%d\n", solves_);
           return inner_->solve();
       }

       double objective_coefficient(Variable v) const override { return inner_->objective_coefficient(v); }
       void set_objective_coefficient(Variable v, double c) override { inner_->set_objective_coefficient(v, c); }
       double constraint_coefficient(ConstraintRef c, Variable v) const override {
           return inner_->constraint_coefficient(c, v);
       }
       void set_constraint_coefficient(ConstraintRef c, Variable v, double a) override {
           inner_->set_constraint_coefficient(c, v, a);
       }
       double constraint_lower(ConstraintRef c) const override { return inner_->constraint_lower(c); }
       void set_constraint_lower(ConstraintRef c, double lo) override { inner_->set_constraint_lower(c, lo); }
       double constraint_upper(ConstraintRef c) const override { return inner_->constraint_upper(c); }
       void set_constraint_upper(ConstraintRef c, double hi) override { inner_->set_constraint_upper(c, hi); }
       double variable_lower(Variable v) const override { return inner_->variable_lower(v); }
       void set_variable_lower(Variable v, double lo) override { inner_->set_variable_lower(v, lo); }
       double variable_upper(Variable v) const override { return inner_->variable_upper(v); }
       void set_variable_upper(Variable v, double hi) override { inner_->set_variable_upper(v, hi); }
       VarKind variable_kind(Variable v) const override { return inner_->variable_kind(v); }
       void set_variable_kind(Variable v, VarKind k) override { inner_->set_variable_kind(v, k); }
       void set_progress_callback(ProgressCallback cb) override { inner_->set_progress_callback(std::move(cb)); }
       void set_sensitivity_ranging(bool on) override { inner_->set_sensitivity_ranging(on); }
       void write_model(const std::string& path) const override { inner_->write_model(path); }
       void set_initial_solution(const std::unordered_map<Variable, double>& values) override {
           inner_->set_initial_solution(values);
       }

   private:
       std::unique_ptr<Solver> inner_;
       int solves_ = 0;
   };

Step 2: register it
-------------------

A ``SolverRegistration`` at namespace scope registers a factory when the
program (or the shared library containing it) is loaded:

.. check: file

.. code-block:: cpp

   namespace {
   SolverRegistration counting_registration{
       "counting", [](const Model& model, const SolverOptions& options) {
           return std::make_unique<CountingSolver>(model, options);
       }};
   }  // namespace

Registering a second backend under a name that is already taken
replaces the first.

Step 3: use it
--------------

.. code-block:: cpp

   Model model;
   SAIOPT_VAR(model, x, .bounds = {0.0, 4.0});
   SAIOPT_OBJECTIVE(model, ObjectiveSense::Maximize, x);

   auto solver = create_solver(model, "counting", SolverOptions{}.set_bool("output_flag", false));
   solver->solve();
   solver->set_variable_upper(x, 7.0);
   std::printf("%g\n", solver->solve().objective_value);

.. code-block:: text

   solve #1
   solve #2
   7

Shipping a backend as a library
-------------------------------

If the backend lives in its own library, as ``saiopt_highs`` and
``saiopt_scip`` do, build it as a ``SHARED`` library. A static archive's
registration object can be dropped by the linker, because nothing
refers to it. Programs using it must also link it with
``-Wl,--no-as-needed`` (see :doc:`installation`) so that it's loaded at
all.

Things a backend is expected to handle
--------------------------------------

* Throw ``std::runtime_error`` from the constructor for an option it
  doesn't recognize or a value it rejects.
* Throw ``std::runtime_error`` for a ``Variable`` or ``ConstraintRef``
  that isn't in the model it was built from.
* Set ``SolveResult::has_solution`` whenever a feasible solution came
  back, even if it isn't proven optimal, and fill ``variable_values``
  and ``objective_value`` only then.
* Fill ``duals`` and ``reduced_costs`` for an LP, and the ``*_ranges``
  maps only after ``set_sensitivity_ranging(true)``. A backend that
  can't compute something leaves that map empty.
* Make the setters' changes visible to the next ``solve()`` and to
  ``write_model``.
* Treat ``set_initial_solution`` as a hint: drop a start that can't be
  completed, and throw only for a value outside the variable's current
  bounds, a value that isn't finite, or a variable from another model.
