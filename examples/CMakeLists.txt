foreach(example
    diet_problem
    blocks_basics
    indexed_macros
    hierarchical_facilities
    transportation
)
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE saiopt)
endforeach()

if(TARGET saiopt_mpi)
  add_executable(mpi_parallel_build mpi_parallel_build.cpp)
  target_link_libraries(mpi_parallel_build PRIVATE saiopt_mpi)
endif()
