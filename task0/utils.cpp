#include "utils.h"
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "message.pb.h"

#define INT_SIZE 4

extern "C" {

int listening_socket(int port) {
    int server_fd, opt = 1;
	struct sockaddr_in address;

	// Creating socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
		perror("socket failed");
		return -1;
	}

	// Forcefully attaching socket to the port 8080
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
		perror("setsockopt");
		return -1;
	}
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	// Forcefully attaching socket to the port 8080
	if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		perror("bind failed");
		return -1;
	}
	if (listen(server_fd, 3) < 0) {
		perror("listen");
		return -1;
	}
    return server_fd;
}

int connect_socket(const char *hostname, const int port) {
    int sock = 0;
	struct sockaddr_in serv_addr;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	// Convert IPv4 and IPv6 addresses from text to binary
	// form
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)
		<= 0) {
		printf(
			"\nInvalid address/ Address not supported \n");
		return -1;
	}

	if (connect(sock, (struct sockaddr*)&serv_addr,
				sizeof(serv_addr))
		< 0) {
		printf("\nConnection Failed \n");
		return -1;
	}
    return sock;
}

int accept_connection(int sockfd) {
	struct sockaddr_in address;
	int addrlen = sizeof(address), new_socket;

    if ((new_socket
		= accept(sockfd, (struct sockaddr*)&address,
				(socklen_t*)&addrlen))
		< 0) {
		perror("error: accept");
		return -1;
	}
    return new_socket;
}

int recv_msg(int sockfd, int32_t *operation_type, int64_t *argument) {
	std::string size_buffer(INT_SIZE, 0);
	read(sockfd, &size_buffer[0], INT_SIZE);
	int32_t payload_size = atoi(size_buffer.c_str());
    std::string buffer(payload_size, 0);
	int i = 0;
	int bytes_read = 0;
	do {
		// read 1 byte at a time
		bytes_read += read(sockfd, &buffer[i], 1);
		i += 1;
	} while (bytes_read < payload_size);
    sockets::message m;
    m.ParseFromString(buffer);
    *operation_type = (int32_t) m.type();
    *argument = m.argument();
    return 0;
}

int send_msg(int sockfd, int32_t operation_type, int64_t argument) {
    sockets::message m;
    m.set_argument(argument);
    m.set_type((sockets::message_OperationType) operation_type);
    std::string buffer;
    m.SerializeToString(&buffer);
	// append payload size to the front
	uint32_t msg_size = buffer.size();
	std::string temp_string = std::to_string(msg_size);
	int offset = INT_SIZE - temp_string.size();
	for (int i = 0; i < offset; i++) {
		temp_string = "0" + temp_string;
	}
	const char* size_char_array = temp_string.c_str();
	std::string payload = size_char_array + buffer;
    send(sockfd, payload.c_str(), payload.size(), 0);
    return 0;
}

}