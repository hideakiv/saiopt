foreach(example
    diet_problem
    blocks_basics
    indexed_macros
    hierarchical_facilities
    transportation
    solver_mutation
    jagged_index
)
  add_executable(${example} ${example}.cpp)
  # saiopt_highs is only ever used through its "highs" self-registration
  # (see saiopt/solver.hpp) -- no symbol here calls into it directly, so
  # --no-as-needed keeps the linker's --as-needed default from dropping
  # it out of the binary's NEEDED list, which would silently skip that
  # registration at load time.
  target_link_libraries(${example} PRIVATE saiopt -Wl,--no-as-needed saiopt_highs -Wl,--as-needed)
endforeach()

if(TARGET saiopt_mpi AND TARGET saiopt_highs)
  add_executable(mpi_parallel_build mpi_parallel_build.cpp)
  target_link_libraries(mpi_parallel_build PRIVATE saiopt_mpi -Wl,--no-as-needed saiopt_highs -Wl,--as-needed)
endif()
