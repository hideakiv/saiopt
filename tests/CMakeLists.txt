include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.16.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(saiopt_tests test_lp.cpp test_block.cpp test_macros.cpp test_serialize.cpp)
target_link_libraries(saiopt_tests PRIVATE saiopt saiopt_highs Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(CTest)
include(Catch)
catch_discover_tests(saiopt_tests)
