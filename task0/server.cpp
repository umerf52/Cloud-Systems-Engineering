#include <atomic>
#include <iostream>
#include <string>
#include <unistd.h>
#include "utils.h"

std::atomic<int64_t> number{0};

int main(int args, char *argv[]) {
    if (args < 3) {
        std::cerr << "usage: ./server <numThreads> <port>\n";
        exit(1);
    }

    int numThreads = std::atoi(argv[1]);
    int port = std::atoi(argv[2]);

    int sockfd = listening_socket(port);
    if (sockfd < 0) {
        std::cerr << "listening_socket failed\n";
        exit(1);
    }
    while (true) {
        int new_sockfd = accept_connection(sockfd);
        while (true) {
            int32_t operation_type;
            int64_t argument;
            recv_msg(new_sockfd, &operation_type, &argument);
            if (operation_type == OPERATION_ADD) {
                number += argument;
            } else if (operation_type == OPERATION_SUB) {
                number -= argument;
            } else if (operation_type == OPERATION_TERMINATION) {
                send_msg(new_sockfd, OPERATION_COUNTER, number);
                printf("%ld\n", number.load());
                fflush(stdout);
                close(new_sockfd);
                break;
            }
        }
        sleep(1);
    }
    close(sockfd);
    
    return 0;
}
