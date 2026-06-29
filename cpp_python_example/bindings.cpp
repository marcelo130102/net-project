#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "calculator.hpp"
#include "matrix_calculator.hpp"
#include "net_master.hpp"
#include "net_slave.hpp"
// #include "test_net.hpp"

namespace py = pybind11;

PYBIND11_MODULE(modulo, m) {
	m.doc() = "C++ modulo - include calculator and robust UDP network";

	py::module_ m_calc = m.def_submodule("calculator", "calculator functions");
	m_calc.def("add", &calculator::add, "Add two numbers");
	m_calc.def("subtract", &calculator::subtract, "Subtract two numbers");
	m_calc.def("multiply", &calculator::multiply, "Multiply two numbers");
	m_calc.def("divide", &calculator::divide, "Divide two numbers");
	m_calc.def("matrix_add", &calculator::matrix_add, "Element-wise matrix addition");
	m_calc.def("matrix_subtract", &calculator::matrix_subtract, "Element-wise matrix subtraction");
	m_calc.def("matrix_multiply", &calculator::matrix_multiply, "Element-wise matrix multiplication");
	m_calc.def("matrix_divide", &calculator::matrix_divide, "Element-wise matrix division");
	m_calc.def("matrix_scale", &calculator::matrix_scale, "Scale matrix by scalar");
	m_calc.def("matrix_dot", &calculator::matrix_dot, "Matrix multiplication (dot product)");

	py::class_<NetMaster>(m, "NetMaster")
		.def(py::init<int, int>())
		.def("send_matrix", &NetMaster::send_matrix)
		.def("receive_matrix", &NetMaster::receive_matrix);

	py::class_<NetSlave>(m, "NetSlave")
		.def(py::init<const std::string&, int>())
		.def("send_matrix", &NetSlave::send_matrix)
		.def("receive_matrix", &NetSlave::receive_matrix);
}
