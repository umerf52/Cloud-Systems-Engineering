#include <cxxopts.hpp>
#include <iostream>
#include <string>
#include <unistd.h>

#include <assert.h>
#include "rocksdb/db.h"

#include "client_message.pb.h"
#include "server_message.pb.h"
#include "shared.h"
#include "utils.h"
#include "utils.cpp"

#define THREAD_POOL_SIZE 4

pthread_t threadPool[THREAD_POOL_SIZE];
pthread_mutex_t myMutex = PTHREAD_MUTEX_INITIALIZER;
rocksdb::DB* db;
std::string hostname = "";

struct node {
    struct node* next;
    int* socket;
};
typedef struct node MyNode;

MyNode* head = NULL;
MyNode* tail = NULL;

void* client_handler(void* pointer_client_fd) {
    int client_recv_sockfd = *((int*)pointer_client_fd);
	sockets::client_msg msg;
	std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(client_recv_sockfd);
	if (res.first == 0) {
		close(client_recv_sockfd);
		return NULL;
	}
	msg.ParseFromString(res.second.get());
	const sockets::client_msg::OperationData& od = msg.ops(0);
	int client_port = od.port();
	int client_send_fd = connect_socket("localhost", client_port);

	for(;;) {
		msg.Clear();
        res = secure_recv(client_recv_sockfd);
		if (res.first == 0) {
			break;
		}
		const sockets::client_msg::OperationData& od = msg.ops(0);
        if (od.type() == sockets::client_msg::PUT) {
			std::string key = std::to_string(od.key());
			std::string value = od.value();
			bool status = db->Put(rocksdb::WriteOptions(), key, value).ok();
			server::server_response_reply reply;
			reply.set_success(status);
			reply.set_op_id(od.op_id());
			std::string buffer = "";
    		reply.SerializeToString(&buffer);
			auto msg_size = buffer.size();
			auto buf = std::make_unique<char[]>(msg_size + length_size_field);
			construct_message(buf.get(), buffer.c_str(), msg_size);
			secure_send(client_send_fd, buf.get(), msg_size + length_size_field);
		} else if (od.type() == sockets::client_msg::GET) {
			const char key = static_cast<char>(od.key());
			std::string value = "";
			bool status = db->Get(rocksdb::ReadOptions(), std::to_string(key), &value).ok();
			server::server_response_reply reply;
			reply.set_success(status);
			reply.set_value(value);
			reply.set_op_id(od.op_id());
			std::string buffer = "";
    		reply.SerializeToString(&buffer);
			auto msg_size = buffer.size();
			auto buf = std::make_unique<char[]>(msg_size + length_size_field);
			construct_message(buf.get(), buffer.c_str(), msg_size);
			secure_send(client_send_fd, buf.get(), msg_size + length_size_field);
		}	
	}
	
	close(client_recv_sockfd);
    close(client_send_fd);
	return NULL;
}

void addToList(int* s) {
    MyNode* n = new MyNode;
    n->socket = s;
    n->next = NULL;
    if (tail == NULL) {
        head = n;
    } else {
        tail->next = n;
    }
    tail = n;
}

int* getFromList() {
    if (head == NULL) {
        return NULL;
    } else {
        int* ret = head->socket;
        MyNode* temp = head;
        head = head->next;
        if (head == NULL) {
			tail = NULL;
			}
        return ret;
    }
}

void* threadRunner(void* arg) {
    for (;;) {
        int* pointer_client_fd;
        pthread_mutex_lock(&myMutex);
        pointer_client_fd = getFromList();
        pthread_mutex_unlock(&myMutex); 
        
        if (pointer_client_fd != NULL) {
            client_handler(pointer_client_fd);
        } 
    }
}

auto main(int argc, char *argv[]) -> int {
	cxxopts::Options options(argv[0]);
    options.add_options()("c,no_clients", "Number of clients", cxxopts::value<int>())
	("s,hostname", "Hostname", cxxopts::value<std::string>())
	("p,port", "Port of the server", cxxopts::value<size_t>())
	("m,num_messages", "Number of messages", cxxopts::value<size_t>())
	("t,trace", "Trace file to use", cxxopts::value<std::string>())
	("n,num_threads","Number of threads",cxxopts::value<int>());
	
	auto args = options.parse(argc, argv);
	if(argc < 4) {
		std::cout << options.help() << std::endl;
		return 1;
	}

	rocksdb::Options dbOptions;
	dbOptions.create_if_missing = true;
	rocksdb::Status status = rocksdb::DB::Open(dbOptions, "/tmp/testdb", &db);
	assert(status.ok());
	
    int numThreads = std::atoi(argv[2]);
    hostname = argv[4];
	int port = std::atoi(argv[6]);
	int numClients = std::atoi(argv[8]);

	for (int i=0; i < numThreads; i++) {
        pthread_create(&threadPool[i], NULL, threadRunner, NULL);
    }

	int server_sockfd = listening_socket(port);
    if (server_sockfd < 0) {
        std::cerr << "listening_socket failed\n";
        exit(1);
    }

	for (;;) {
        int client_socket = accept_connection(server_sockfd);
        int* pointer = new int;
		*pointer = client_socket;
        pthread_mutex_lock(&myMutex); 
        addToList(pointer);
        pthread_mutex_unlock(&myMutex);
    }

	delete db;
	close(server_sockfd);
	return 0;
}
