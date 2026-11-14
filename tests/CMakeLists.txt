include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(saiopt_tests test_lp.cpp test_block.cpp test_macros.cpp test_serialize.cpp test_scip.cpp
  test_write_model.cpp test_initial_solution.cpp)
# saiopt_highs/saiopt_scip are only used through their "highs"/"scip"
# self-registration (see saiopt/solver.hpp) -- --no-as-needed keeps the
# linker from dropping them out of NEEDED just because no symbol here
# calls into either directly.
target_link_libraries(saiopt_tests PRIVATE
  saiopt -Wl,--no-as-needed saiopt_highs saiopt_scip -Wl,--as-needed Catch2::Catch2WithMain)
# test_write_model.cpp reads written model files back with HiGHS' own C
# API, independently of saiopt, to check they stand on their own.
target_include_directories(saiopt_tests PRIVATE ${highs_SOURCE_DIR}/highs ${highs_BINARY_DIR})
target_link_libraries(saiopt_tests PRIVATE highs)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(CTest)
include(Catch)
catch_discover_tests(saiopt_tests)
