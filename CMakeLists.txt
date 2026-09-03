cmake_minimum_required(VERSION 3.21)
project(saiopt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

set(FAST_BUILD ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  highs
  GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
  GIT_TAG v1.15.1
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(highs)

add_library(saiopt
  src/variable.cpp
  src/expression.cpp
  src/constraint.cpp
  src/block.cpp
  src/sparse_lp.cpp
  src/model.cpp
  src/highs_solver.cpp
)
target_include_directories(saiopt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${highs_SOURCE_DIR}/highs
    ${highs_BINARY_DIR}
)
target_link_libraries(saiopt PUBLIC highs)

option(SAIOPT_BUILD_EXAMPLES "Build saiopt examples" ON)
option(SAIOPT_BUILD_TESTS "Build saiopt tests" ON)

if(SAIOPT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SAIOPT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
