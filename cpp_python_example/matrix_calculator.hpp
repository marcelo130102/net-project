#pragma once

#include <pybind11/numpy.h>

namespace py = pybind11;

namespace calculator {

py::array matrix_add(const py::array& a, const py::array& b);
py::array matrix_subtract(const py::array& a, const py::array& b);
py::array matrix_multiply(const py::array& a, const py::array& b);
py::array matrix_divide(const py::array& a, const py::array& b);
py::array matrix_scale(const py::array& a, double scalar);
py::array matrix_dot(const py::array& a, const py::array& b);

}  // namespace calculator
