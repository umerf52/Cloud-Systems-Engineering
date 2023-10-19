#include <iostream>
#include <string>
#include <unistd.h>
#include <map>

#include "shared.h"
#include "utils.h"
#include "utils.cpp"

auto main(int argc, char *argv[]) -> int {
	if(argc < 3) {
		printf("Usage: master-svr -p <MASTER_PORT>\n");
		return 1;
	}
	
	int port = std::atoi(argv[2]);
	int num_servers = 0;
	std::map<int, int> server_port_map;
	std::map<int32_t, std::string> kv_map;

	int server_sockfd = listening_socket(port);
    if (server_sockfd < 0) {
        std::cerr << "listening_socket failed\n";
        exit(1);
    }

	for (;;) {
        int sock_fd = accept_connection(server_sockfd);
		std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(sock_fd);
		if (res.first == 0) {
			close(sock_fd);
			continue;
		}
		sockets::msg m;
        m.ParseFromString(res.second.get());
		m.PrintDebugString();
		if (m.type() == sockets::msg::MASTER_PUT) {
			kv_map.insert(std::pair<int32_t, std::string>(m.key(), m.value()));
		}
		else if (m.type() == sockets::msg::SERVER) {
			num_servers++;
			server_port_map.insert(std::pair<int, int>(num_servers, m.port()));
			if (num_servers >= 3) {
				for (auto it = server_port_map.begin(); it != server_port_map.end(); it++) {
					int sock_fd = connect_socket("localhost", it->second);
					if (sock_fd < 0) {
						printf("could not connect to server %d\n", it->second);
						std::cerr << "master_sock failed\n";
						exit(1);
					}
					sockets::msg temp_msg;
					temp_msg.set_type(sockets::msg::REORG);
					sendWrapper(sock_fd, temp_msg);
				}
				for (auto it = kv_map.begin(); it != kv_map.end(); it++) {
					int shard = (it->first % num_servers) + 1;
					int shard_port = server_port_map.find(shard)->second;
					sockets::msg temp_m;
					temp_m.set_type(sockets::msg::REORG_PUT);
					temp_m.set_key(it->first);
					temp_m.set_value(it->second);
					int sock_fd = connect_socket("localhost", shard_port);
					if (sock_fd < 0) {
						std::cerr << "master_sock failed\n";
						exit(1);
					}
					sendWrapper(sock_fd, temp_m);
					close(sock_fd);
				}
			}
		} else if (m.type() == sockets::msg::GET || m.type() == sockets::msg::PUT) {
			int shard = (m.key() % num_servers) + 1;
			int ret_port = server_port_map.find(shard)->second;
			
			sockets::msg temp_m;
			temp_m.set_port(ret_port);
            temp_m.set_type(sockets::msg::REPLY);
			sendWrapper(sock_fd, temp_m);
		}
    }

	close(server_sockfd);
	return 0;
}
