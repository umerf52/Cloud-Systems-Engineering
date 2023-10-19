#include <iostream>
#include <string>
#include <unistd.h>

#include "utils.h"

int main(int args, char *argv[]) {
    if (args < 7) {
        std::cerr << "usage: ./client <num_threads> <hostname> <port> "
                     "<num_messages> <add> <sub>\n";
        exit(1);
    }

    int numClients = std::atoi(argv[1]);
    std::string hostname = argv[2];
    int port = std::atoi(argv[3]);
    int numMessages = std::atoi(argv[4]);
    int add = std::atoi(argv[5]);
    int sub = std::atoi(argv[6]);

    int sockfd = connect_socket(hostname.c_str(), port);
    for (int i = 0; i < numMessages; i++) {
        if (i % 2 == 0) {
            send_msg(sockfd, OPERATION_ADD, add);
        } else {
            send_msg(sockfd, OPERATION_SUB, sub);
        }
    }
    send_msg(sockfd, OPERATION_TERMINATION, 0);
    int32_t operation_type;
    int64_t argument;
    recv_msg(sockfd, &operation_type, &argument);
    printf("%ld\n", argument);
    close(sockfd);
    return 0;
}
