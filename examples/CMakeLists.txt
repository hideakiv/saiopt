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
