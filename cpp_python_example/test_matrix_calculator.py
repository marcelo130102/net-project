import os
import sys

import numpy as np

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, BASE_DIR)

import calculator


def test_matrix_add():
	a = np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float64)
	b = np.array([[5.0, 6.0], [7.0, 8.0]], dtype=np.float64)
	result = calculator.matrix_add(a, b)
	expected = a + b
	assert np.allclose(result, expected)


def test_matrix_subtract():
	a = np.array([[5.0, 6.0], [7.0, 8.0]], dtype=np.float32)
	b = np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float32)
	result = calculator.matrix_subtract(a, b)
	assert np.allclose(result, a - b)


def test_matrix_multiply():
	a = np.array([[2.0, 3.0], [4.0, 5.0]], dtype=np.float64)
	b = np.array([[1.5, 2.0], [0.5, 1.0]], dtype=np.float64)
	result = calculator.matrix_multiply(a, b)
	assert np.allclose(result, a * b)


def test_matrix_scale():
	a = np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float32)
	result = calculator.matrix_scale(a, 0.5)
	assert np.allclose(result, a * 0.5)


def test_matrix_dot():
	a = np.array([[1.0, 2.0], [3.0, 4.0]], dtype=np.float64)
	b = np.array([[5.0, 6.0], [7.0, 8.0]], dtype=np.float64)
	result = calculator.matrix_dot(a, b)
	assert np.allclose(result, a @ b)


def test_weight_vector_shape():
	weights = np.random.randn(1, 128).astype(np.float32)
	avg = calculator.matrix_scale(
		calculator.matrix_add(weights, weights),
		0.5,
	)
	assert avg.shape == (1, 128)


if __name__ == "__main__":
	test_matrix_add()
	test_matrix_subtract()
	test_matrix_multiply()
	test_matrix_scale()
	test_matrix_dot()
	test_weight_vector_shape()
	print("Todos los tests de matrix_calculator pasaron.")
