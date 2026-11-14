find_package(Python 3.9 REQUIRED COMPONENTS Interpreter Development.Module)
execute_process(
  COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
  OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT
)
find_package(nanobind CONFIG REQUIRED)

# One extension module holding the bindings, both backends, and (through
# the static saiopt/highs/libscip libraries the top level builds in this
# mode) everything they depend on. The backends' sources are compiled
# straight in, not linked from saiopt_highs/saiopt_scip, so the static
# objects that register "highs" and "scip" are part of the module itself
# and run when it's imported -- and there's exactly one copy of the
# solver registry for them to register into.
nanobind_add_module(_saiopt NB_STATIC
  src/module.cpp
  src/expressions.cpp
  src/blocks.cpp
  src/solvers.cpp
  ${PROJECT_SOURCE_DIR}/src/solvers/highs_solver.cpp
  ${PROJECT_SOURCE_DIR}/src/solvers/scip_solver.cpp
)
target_include_directories(_saiopt PRIVATE
  ${highs_SOURCE_DIR}/highs
  ${highs_BINARY_DIR}
  ${scip_SOURCE_DIR}/src
  ${scip_BINARY_DIR}
)
target_link_libraries(_saiopt PRIVATE saiopt highs libscip)

nanobind_add_stub(_saiopt_stub
  MODULE _saiopt
  OUTPUT _saiopt.pyi
  PYTHON_PATH $<TARGET_FILE_DIR:_saiopt>
  DEPENDS _saiopt
)

install(TARGETS _saiopt LIBRARY DESTINATION saiopt COMPONENT python)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/_saiopt.pyi DESTINATION saiopt COMPONENT python)
