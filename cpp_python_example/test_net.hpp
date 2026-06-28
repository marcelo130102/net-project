#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <iostream>
#include <vector>
#include <cstring>
#include <string>
#include <stdexcept>

namespace py = pybind11;

class NetMaster {
	int server_fd;
	std::vector<int> client_fds;

public:
	NetMaster(int port, int num_slaves);
	~NetMaster();
	void send_matrix(int slave_idx, py::array_t<float> matrix);
	py::array_t<float> receive_matrix(int slave_idx);
};

class NetSlave {
	int sockfd;

public:
	NetSlave(const std::string& ip, int port);
	~NetSlave();
	void send_matrix(py::array_t<float> matrix);
	py::array_t<float> receive_matrix();
};
