#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <vector>

namespace py = pybind11;

class NetSlave {
	int sockfd;
	int tx_sequence;
	int my_slave_idx;
	sockaddr_in master_addr;
	std::vector<int> recentlyCompleted;

public:
	NetSlave(const std::string& ip, int port);
	~NetSlave();
	void send_matrix(py::array_t<float> matrix);
	py::array_t<float> receive_matrix();
};
