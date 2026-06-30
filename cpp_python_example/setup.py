from setuptools import setup, Extension
import pybind11

common_sources = [
	"calculator.cpp",
	"matrix_calculator.cpp",
]

ext_modules = [
	Extension(
		"modulo",
		common_sources + [
			"protocol.cpp",
			"net_master.cpp",
			"net_slave.cpp",
			"bindings.cpp",
		],
		include_dirs=[pybind11.get_include()],
		language='c++'
	),
	Extension(
		"calculator",
		common_sources + [
			"calculator_bindings.cpp",
		],
		include_dirs=[pybind11.get_include()],
		language='c++'
	),
]

setup(
	name="modulo",
	version="1.0",
	ext_modules=ext_modules,
	install_requires=['pybind11>=2.10.0'],
	python_requires=">=3.6",
)
