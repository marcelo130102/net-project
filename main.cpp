#include <iostream>
#include <cstdlib>
#include <string>
#include <vector>
#include <thread>

void execute_script(const std::string& comand) {
	std::cout << "[C++] running: " << comand << std::endl;
	system(comand.c_str());
}

int main() {
	int num_slaves = 10;
	int num_parts_dataset = num_slaves + 1;
	std::string dataset_original = "Dataset of Diabetes.csv";
	std::vector<std::thread> process;

	// dataset split
	std::string cmd_split = "python dataset_splitter.py \"" + dataset_original + "\" " + std::to_string(num_parts_dataset);
	execute_script(cmd_split);

	// push master
	std::string cmd_master = "python basicClasificacion_master.py " + std::to_string(num_slaves) + " diabetes_master.csv";
	process.push_back(std::thread(execute_script, cmd_master));

	// push slaves
	for (int i = 0; i < num_slaves; ++i) {
		int id_file = i + 1;
		std::string file_slave = "diabetes_slave_" + std::to_string(id_file) + ".csv";
		std::string cmd_slave = "python basicClasificacion_slave.py " + std::to_string(i) + " " + file_slave;
		process.push_back(std::thread(execute_script, cmd_slave));
	}

	// joins
	for (auto& p : process) {
		if (p.joinable())
			p.join();
	}

	return 0;
}
