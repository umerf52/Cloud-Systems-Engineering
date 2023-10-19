#include <iostream>
#include <string>
#include <unistd.h>
#include <map>
#include <vector>

#include <assert.h>
#include "rocksdb/db.h"

#include "client_message.pb.h"
#include "shared.h"
#include "utils.h"
#include "utils.cpp"

sockets::msg generateMessage(bool status) {
	sockets::msg m;
	m.set_status(status);
	m.set_type(sockets::msg::REPLY);
	return m;
}

auto main(int argc, char *argv[]) -> int {
	if(argc < 5) {
		printf("Usage: svr -p <PORT> -m <MASTER_PORT>\n");
		return 1;
	}
	
	int port = std::atoi(argv[2]);
	int master_port = std::atoi(argv[4]);
	std::vector<std::string> keys;

	rocksdb::DB* db;
	rocksdb::Options dbOptions;
	dbOptions.create_if_missing = true;
	std::string path = "/tmp/testdb" + std::to_string(port);
	rocksdb::Status status = rocksdb::DB::Open(dbOptions, path, &db);
	assert(status.ok());

	int master_sockfd = connect_socket("localhost", master_port);
	if (master_sockfd < 0) {
		std::cerr << "master_sock failed\n";
		exit(1);
	}
	sockets::msg m;
	m.set_type(sockets::msg::SERVER);
	m.set_port(port);
	sendWrapper(master_sockfd, m);
	close(master_sockfd);

	int server_sockfd = listening_socket(port);
    if (server_sockfd < 0) {
        std::cerr << "listening_socket failed\n";
        exit(1);
    }

	for(;;) {
		int sock_fd = accept_connection(server_sockfd);
		std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(sock_fd);
		if (res.first == 0) {
			close(sock_fd);
			continue;
		}
		sockets::msg temp_msg;

        temp_msg.ParseFromString(res.second.get());

		if (temp_msg.type() == sockets::msg::REORG) {
			for (std::string key : keys) {
				rocksdb::Status status = db->Delete(rocksdb::WriteOptions(), key);
				assert(status.ok());
			}
			keys.clear();
		} else if (temp_msg.type() == sockets::msg::REORG_PUT) {
			std::string key = std::to_string(temp_msg.key());
			std::string value = temp_msg.value();
			db->Put(rocksdb::WriteOptions(), key, temp_msg.value().c_str()).ok();
			keys.push_back(key);
		} else if (temp_msg.type() == sockets::msg::PUT) {
			std::string key = std::to_string(temp_msg.key());
			std::string value = temp_msg.value();
			bool status = db->Put(rocksdb::WriteOptions(), key, temp_msg.value().c_str()).ok();
			sockets::msg reply = generateMessage(status);
			sendWrapper(sock_fd, reply);
			if (status) {
				int temp_master_sockfd = connect_socket("localhost", master_port);
				if (temp_master_sockfd < 0) {
					std::cerr << "master_sock failed\n";
					exit(1);
				}
				temp_msg.set_type(sockets::msg::MASTER_PUT);
				sendWrapper(temp_master_sockfd, temp_msg);
				keys.push_back(key);
				close(temp_master_sockfd);
			}
		} else if (temp_msg.type() == sockets::msg::GET) {
			const char key = static_cast<char>(temp_msg.key());
			std::string value;
			bool status = db->Get(rocksdb::ReadOptions(), std::to_string(key), &value).ok();
			sockets::msg reply = generateMessage(status);
			reply.set_value(value);
			sendWrapper(sock_fd, reply);
		}
		close(sock_fd);
	}

	delete db;
	close(master_sockfd);
	close(server_sockfd);
	return 0;
}
