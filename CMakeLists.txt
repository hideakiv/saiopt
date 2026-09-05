cmake_minimum_required(VERSION 3.21)
project(saiopt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(FetchContent)

# The core library is solver-agnostic: it only depends on the abstract
# Solver interface (include/saiopt/solver.hpp), never on a concrete
# backend such as HiGHS.
add_library(saiopt
  src/variable.cpp
  src/expression.cpp
  src/constraint.cpp
  src/block.cpp
  src/sparse_lp.cpp
  src/model.cpp
)
target_include_directories(saiopt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

option(SAIOPT_BUILD_EXAMPLES "Build saiopt examples" ON)
option(SAIOPT_BUILD_TESTS "Build saiopt tests" ON)
option(SAIOPT_WITH_MPI "Build saiopt_mpi, for sending Blocks between processes over MPI" ON)
option(SAIOPT_WITH_HIGHS "Build saiopt_highs, a Solver backend implemented with HiGHS" ON)

if(SAIOPT_WITH_HIGHS)
  set(FAST_BUILD ON CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    highs
    GIT_REPOSITORY https://github.com/ERGO-Code/HiGHS.git
    GIT_TAG v1.15.1
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(highs)

  add_library(saiopt_highs
    src/highs_solver.cpp
  )
  target_include_directories(saiopt_highs
    PRIVATE
      ${highs_SOURCE_DIR}/highs
      ${highs_BINARY_DIR}
  )
  target_link_libraries(saiopt_highs PUBLIC saiopt PRIVATE highs)
endif()

if(SAIOPT_WITH_MPI)
  find_package(MPI REQUIRED COMPONENTS CXX)
  add_library(saiopt_mpi
    src/mpi.cpp
  )
  target_link_libraries(saiopt_mpi PUBLIC saiopt MPI::MPI_CXX)
endif()

if(SAIOPT_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(SAIOPT_BUILD_TESTS)
  add_subdirectory(tests)
endif()
