#include <iostream>
#include <string>
#include <unistd.h>
#include <map>

#include "client_message.pb.h"
#include "shared.h"
#include "utils.h"
#include "utils.cpp"

auto main(int argc, char *argv[]) -> int {
    if(argc < 11) {
		printf("Usage: clt -p <PORT> -o <OPERATION> -k <KEY> -v <VALUE> -m <MASTER_PORT> -d <DIRECT>\n");
		return 1;
	}

    int port = std::atoi(argv[2]);
    std::string operation = argv[4];
    int32_t key = std::atoi(argv[6]);
    std::string value = argv[8];
    int master_port = std::atoi(argv[10]);
    // direct = std::atoi(argv[12]);
    bool direct = false;

    if (direct) {
        int shard_sockfd = connect_socket("localhost", master_port);
        if (shard_sockfd < 0) {
            std::cerr << "shard_sockfd failed";
            exit(1);
        }

        sockets::msg m1;
        m1.set_key(key);
        if (operation == "GET") {
            m1.set_type(sockets::msg::GET);
        } else if (operation == "PUT") {
            m1.set_type(sockets::msg::PUT);
            m1.set_value(value);
        }

        sendWrapper(shard_sockfd, m1);

        std::pair<size_t, std::unique_ptr<char[]>> res2 = secure_recv(shard_sockfd);
        close(shard_sockfd);
        if (res2.first == 0) {
            close(shard_sockfd);
            return 2;
        }
        sockets::msg m2;
        m2.ParseFromString(res2.second.get());
        if (m2.status()) {
            return 0;
        } else {
            return 2;
        }
    } else {
        int master_sockfd = connect_socket("localhost", master_port);
        if (master_sockfd < 0) {
            std::cerr << "master_sock failed\n";
            exit(1);
        }

        sockets::msg m1;
        m1.set_type(sockets::msg::GET);
        m1.set_key(key);
        sendWrapper(master_sockfd, m1);

        std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(master_sockfd);
        close(master_sockfd);
        if (res.first == 0) {
            printf("No server found\n");
            close(master_sockfd);
            return 2;
        }
        sockets::msg m2;
        m2.ParseFromString(res.second.get());

        int shard_port = m2.port();
        int shard_sockfd = connect_socket("localhost", shard_port);
        if (shard_sockfd < 0) {
            std::cerr << "shard_sockfd failed\n";
            exit(1);
        }

        sockets::msg m3;
        m3.set_key(key);
        if (operation == "GET") {
            m3.set_type(sockets::msg::GET);
        } else if (operation == "PUT") {
            m3.set_type(sockets::msg::PUT);
            m3.set_value(value);
        }
        sendWrapper(shard_sockfd, m3);

        std::pair<size_t, std::unique_ptr<char[]>> res2 = secure_recv(shard_sockfd);
        close(shard_sockfd);
        if (res2.first <= 0) {
            printf("Shard recv failed\n");
            close(shard_sockfd);
            return 2;
        }

        sockets::msg m4;
        m4.ParseFromString(res2.second.get());
        if (m4.status()) {
            return 0;
        } else {
            return 2;
        }
    }

}
