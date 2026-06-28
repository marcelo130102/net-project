#pragma once

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <vector>
#include <string>
#include <map>

namespace py = pybind11;

class NetMaster {
	int sockfd;
	int num_slaves;
	int tx_sequence;
	std::vector<sockaddr_in> client_addrs;
	std::vector<int> recentlyCompleted;
	std::map<int, std::string> completed_matrices; // to save tmp slave_dix - serial data

public:
	NetMaster(int port, int num_slaves);
	~NetMaster();
	void send_matrix(int slave_idx, py::array_t<float> matrix);
	py::array_t<float> receive_matrix(int slave_idx);
};
