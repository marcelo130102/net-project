#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include "calculator.hpp"
#include "test_net.hpp"

namespace py = pybind11;

PYBIND11_MODULE(modulo, m) {
	m.doc() = "C++ modulo - include calculator and network test";

	py::module_ m_calc = m.def_submodule("calculator", "calculator functions");
	m_calc.def("add", &calculator::add, "Add two numbers");
	m_calc.def("subtract", &calculator::subtract, "Subtract two numbers");
	m_calc.def("multiply", &calculator::multiply, "Multiply two numbers");
	m_calc.def("divide", &calculator::divide, "Divide two numbers");

	py::class_<NetMaster>(m, "NetMaster")
		.def(py::init<int, int>())
		.def("send_matrix", &NetMaster::send_matrix)
		.def("receive_matrix", &NetMaster::receive_matrix);

	py::class_<NetSlave>(m, "NetSlave")
		.def(py::init<const std::string&, int>())
		.def("send_matrix", &NetSlave::send_matrix)
		.def("receive_matrix", &NetSlave::receive_matrix);
}
