#include "test_net.hpp"

NetMaster::NetMaster(int port, int num_slaves) {
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
		throw std::runtime_error("Fallo al bindear el puerto master");

	listen(server_fd, num_slaves);

	std::cout<<"[C++] master waiting "<<num_slaves<<" slaves...\n";
	for (int i = 0; i < num_slaves; ++i) {
		int client_fd = accept(server_fd, NULL, NULL);
		if (client_fd < 0)
			throw std::runtime_error("error accepting slave\n");
		client_fds.push_back(client_fd);
		std::cout<<"[C++] slave "<<i + 1<<" connected\n";
	}
}

NetMaster::~NetMaster() {
	for (int fd : client_fds)
		close(fd);
	close(server_fd);
}

void NetMaster::send_matrix(int slave_idx, py::array_t<float> matrix) {
	py::buffer_info buf = matrix.request();
	int rows = buf.shape[0];
	int cols = buf.shape[1];
	float* ptr = static_cast<float*>(buf.ptr);

	int fd = client_fds[slave_idx];
	send(fd, &rows, sizeof(int), 0);
	send(fd, &cols, sizeof(int), 0);
	send(fd, ptr, rows * cols * sizeof(float), 0);
}

py::array_t<float> NetMaster::receive_matrix(int slave_idx) {
	int fd = client_fds[slave_idx];
	int rows = 0, cols = 0;

	recv(fd, &rows, sizeof(int), MSG_WAITALL);
	recv(fd, &cols, sizeof(int), MSG_WAITALL);

	auto result = py::array_t<float>({rows, cols});
	py::buffer_info buf = result.request();
	float* ptr = static_cast<float*>(buf.ptr);

	recv(fd, ptr, rows * cols * sizeof(float), MSG_WAITALL);
	return result;
}

NetSlave::NetSlave(const std::string& ip, int port) {
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &serv_addr.sin_addr);

	std::cout<<"[C++] slave trying connecting master...\n";
	while (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
		sleep(1); // to keep trying
	std::cout<<"[C++] slave connected...\n";
}

NetSlave::~NetSlave() {
	close(sockfd);
}

void NetSlave::send_matrix(py::array_t<float> matrix) {
	py::buffer_info buf = matrix.request();
	int rows = buf.shape[0];
	int cols = buf.shape[1];
	float* ptr = static_cast<float*>(buf.ptr);

	send(sockfd, &rows, sizeof(int), 0);
	send(sockfd, &cols, sizeof(int), 0);
	send(sockfd, ptr, rows * cols * sizeof(float), 0);
}

py::array_t<float> NetSlave::receive_matrix() {
	int rows = 0, cols = 0;
	recv(sockfd, &rows, sizeof(int), MSG_WAITALL);
	recv(sockfd, &cols, sizeof(int), MSG_WAITALL);

	auto result = py::array_t<float>({rows, cols});
	py::buffer_info buf = result.request();
	float* ptr = static_cast<float*>(buf.ptr);

	recv(sockfd, ptr, rows * cols * sizeof(float), MSG_WAITALL);
	return result;
}
