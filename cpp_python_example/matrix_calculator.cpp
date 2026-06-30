#include "matrix_calculator.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace calculator {
namespace {

void validate_same_shape(const py::buffer_info& a, const py::buffer_info& b) {
	if (a.ndim != b.ndim) {
		throw std::invalid_argument("Las matrices deben tener la misma cantidad de dimensiones");
	}

	for (py::ssize_t i = 0; i < a.ndim; ++i) {
		if (a.shape[i] != b.shape[i]) {
			throw std::invalid_argument("Las matrices deben tener la misma forma");
		}
	}
}

py::ssize_t total_elements(const py::buffer_info& buf) {
	py::ssize_t count = 1;
	for (py::ssize_t i = 0; i < buf.ndim; ++i) {
		count *= buf.shape[i];
	}
	return count;
}

template <typename T>
py::array_t<T> typed_array(const py::array& array) {
	return py::array_t<T>(array);
}

template <typename T>
py::array element_wise_binary(
	const py::array& a,
	const py::array& b,
	T (*op)(T, T)
) {
	py::array_t<T> a_arr = typed_array<T>(a);
	py::array_t<T> b_arr = typed_array<T>(b);

	py::buffer_info buf_a = a_arr.request();
	py::buffer_info buf_b = b_arr.request();
	validate_same_shape(buf_a, buf_b);

	auto result = py::array_t<T>(buf_a.shape);
	py::buffer_info buf_out = result.request();

	const T* ptr_a = static_cast<const T*>(buf_a.ptr);
	const T* ptr_b = static_cast<const T*>(buf_b.ptr);
	T* ptr_out = static_cast<T*>(buf_out.ptr);

	const py::ssize_t size = total_elements(buf_a);
	for (py::ssize_t i = 0; i < size; ++i) {
		ptr_out[i] = op(ptr_a[i], ptr_b[i]);
	}

	return result;
}

template <typename T>
py::array element_wise_scale(const py::array& a, double scalar) {
	py::array_t<T> a_arr = typed_array<T>(a);
	py::buffer_info buf_a = a_arr.request();

	auto result = py::array_t<T>(buf_a.shape);
	py::buffer_info buf_out = result.request();

	const T* ptr_a = static_cast<const T*>(buf_a.ptr);
	T* ptr_out = static_cast<T*>(buf_out.ptr);

	const py::ssize_t size = total_elements(buf_a);
	const T factor = static_cast<T>(scalar);
	for (py::ssize_t i = 0; i < size; ++i) {
		ptr_out[i] = ptr_a[i] * factor;
	}

	return result;
}

template <typename T>
py::array matrix_dot_typed(const py::array_t<T>& a_arr, const py::array_t<T>& b_arr) {
	py::buffer_info buf_a = a_arr.request();
	py::buffer_info buf_b = b_arr.request();

	if (buf_a.ndim != 2 || buf_b.ndim != 2) {
		throw std::invalid_argument("matrix_dot requiere matrices bidimensionales");
	}

	const py::ssize_t rows_a = buf_a.shape[0];
	const py::ssize_t cols_a = buf_a.shape[1];
	const py::ssize_t rows_b = buf_b.shape[0];
	const py::ssize_t cols_b = buf_b.shape[1];

	if (cols_a != rows_b) {
		throw std::invalid_argument(
			"Dimensiones incompatibles para multiplicacion matricial: "
			+ std::to_string(rows_a) + "x" + std::to_string(cols_a)
			+ " y " + std::to_string(rows_b) + "x" + std::to_string(cols_b)
		);
	}

	std::vector<py::ssize_t> out_shape = {rows_a, cols_b};
	auto result = py::array_t<T>(out_shape);
	py::buffer_info buf_out = result.request();

	const T* ptr_a = static_cast<const T*>(buf_a.ptr);
	const T* ptr_b = static_cast<const T*>(buf_b.ptr);
	T* ptr_out = static_cast<T*>(buf_out.ptr);

	for (py::ssize_t i = 0; i < rows_a; ++i) {
		for (py::ssize_t j = 0; j < cols_b; ++j) {
			T sum = 0;
			for (py::ssize_t k = 0; k < cols_a; ++k) {
				sum += ptr_a[i * cols_a + k] * ptr_b[k * cols_b + j];
			}
			ptr_out[i * cols_b + j] = sum;
		}
	}

	return result;
}

py::array dispatch_binary(
	const py::array& a,
	const py::array& b,
	float (*op_f)(float, float),
	double (*op_d)(double, double)
) {
	const auto dtype = py::dtype(a.dtype());
	if (dtype.is(py::dtype::of<float>())) {
		return element_wise_binary<float>(a, b, op_f);
	}
	if (dtype.is(py::dtype::of<double>())) {
		return element_wise_binary<double>(a, b, op_d);
	}

	throw std::invalid_argument("Tipo no soportado. Use float32 o float64");
}

py::array dispatch_scale(const py::array& a, double scalar) {
	const auto dtype = py::dtype(a.dtype());
	if (dtype.is(py::dtype::of<float>())) {
		return element_wise_scale<float>(a, scalar);
	}
	if (dtype.is(py::dtype::of<double>())) {
		return element_wise_scale<double>(a, scalar);
	}

	throw std::invalid_argument("Tipo no soportado. Use float32 o float64");
}

float add_f(float x, float y) { return x + y; }
double add_d(double x, double y) { return x + y; }

float sub_f(float x, float y) { return x - y; }
double sub_d(double x, double y) { return x - y; }

float mul_f(float x, float y) { return x * y; }
double mul_d(double x, double y) { return x * y; }

float div_f(float x, float y) {
	if (y == 0.0f) {
		throw std::invalid_argument("Division por cero");
	}
	return x / y;
}

double div_d(double x, double y) {
	if (y == 0.0) {
		throw std::invalid_argument("Division por cero");
	}
	return x / y;
}

}  // namespace

py::array matrix_add(const py::array& a, const py::array& b) {
	return dispatch_binary(a, b, add_f, add_d);
}

py::array matrix_subtract(const py::array& a, const py::array& b) {
	return dispatch_binary(a, b, sub_f, sub_d);
}

py::array matrix_multiply(const py::array& a, const py::array& b) {
	return dispatch_binary(a, b, mul_f, mul_d);
}

py::array matrix_divide(const py::array& a, const py::array& b) {
	return dispatch_binary(a, b, div_f, div_d);
}

py::array matrix_scale(const py::array& a, double scalar) {
	return dispatch_scale(a, scalar);
}

py::array matrix_dot(const py::array& a, const py::array& b) {
	const auto dtype = py::dtype(a.dtype());
	if (!dtype.is(py::dtype::of<float>()) && !dtype.is(py::dtype::of<double>())) {
		throw std::invalid_argument("Tipo no soportado. Use float32 o float64");
	}

	if (!py::dtype(b.dtype()).is(dtype)) {
		throw std::invalid_argument("Ambas matrices deben tener el mismo tipo");
	}

	if (dtype.is(py::dtype::of<float>())) {
		return matrix_dot_typed<float>(typed_array<float>(a), typed_array<float>(b));
	}

	return matrix_dot_typed<double>(typed_array<double>(a), typed_array<double>(b));
}

}  // namespace calculator
