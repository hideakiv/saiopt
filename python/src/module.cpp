#include "bindings.hpp"

NB_MODULE(_saiopt, m) {
    m.doc() = "Python bindings for saiopt's C++ library.";
    saiopt::python::bind_expressions(m);
    saiopt::python::bind_blocks(m);
    saiopt::python::bind_solvers(m);
}
