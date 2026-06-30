#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "calculator.hpp"
#include "matrix_calculator.hpp"

namespace py = pybind11;

PYBIND11_MODULE(calculator, m) {
	m.doc() = "Calculadora escalar y de matrices implementada en C++";

	m.def("add", &calculator::add, "Suma dos numeros");
	m.def("subtract", &calculator::subtract, "Resta dos numeros");
	m.def("multiply", &calculator::multiply, "Multiplica dos numeros");
	m.def("divide", &calculator::divide, "Divide dos numeros");

	m.def("matrix_add", &calculator::matrix_add, "Suma elemento a elemento");
	m.def("matrix_subtract", &calculator::matrix_subtract, "Resta elemento a elemento");
	m.def("matrix_multiply", &calculator::matrix_multiply, "Multiplica elemento a elemento");
	m.def("matrix_divide", &calculator::matrix_divide, "Divide elemento a elemento");
	m.def("matrix_scale", &calculator::matrix_scale, "Multiplica una matriz por un escalar");
	m.def("matrix_dot", &calculator::matrix_dot, "Multiplicacion matricial (producto punto)");
}
