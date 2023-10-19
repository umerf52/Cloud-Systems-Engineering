#include <arpa/inet.h>
#include <cxxopts.hpp>
#include <fmt/format.h>
#include <iostream>

#include <assert.h>
#include "rocksdb/db.h"

#include "kvs.pb.h"
#include "raft.pb.h"

static constexpr auto length_size_field = sizeof(uint32_t);
#define debug_print(...) fmt::print(__VA_ARGS__)

const std::string FOLLOWER = "follower";
const std::string LEADER = "leader";

pthread_t follower_thread;

rocksdb::DB* db;

int clientCommPort = -1;
int my_listening_socket = -1;
int client_socket = -1;
int my_server_id = -1;
std::vector<int> allPorts;
std::map<int, int> port_socket_map;
bool leader = false;

int term = 1;
int leaderID = 1;
int prevLogIndex = 1;
int prevLogTerm = 1;
int leaderCommit = 1;

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
	if (listen(server_fd, 10) < 0) {
		perror("listen");
		return -1;
	}
  return server_fd;
}

int connect_socket(const int port) {
    int sock = 0, opt = 1;
	struct sockaddr_in serv_addr;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		printf("\n Socket creation error \n");
		return -1;
	}

	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
		perror("setsockopt");
		return -1;
	}

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	// Convert IPv4 and IPv6 addresses from text to binary
	// form
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}

	if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		printf("Connection Failed \n");
		return -1;
	}
    return sock;
}

int accept_connection(int sockfd) {
	struct sockaddr_in address;
	int addrlen = sizeof(address), new_socket;

    if ((new_socket = accept(sockfd, (struct sockaddr*)&address, (socklen_t*)&addrlen)) < 0) {
		perror("error: accept");
		return -1;
	}
    return new_socket;
}

static auto read_n(int fd, char *buffer, size_t n) -> size_t {
  size_t bytes_read = 0;
  size_t retries = 0;
  constexpr size_t max_retries = 10000;
  while (bytes_read < n) {
    auto bytes_left = n - bytes_read;
    auto bytes_read_now = recv(fd, buffer + bytes_read, bytes_left, 0);
    // negative return_val means that there are no more data (fine for non
    // blocking socket)
    if (bytes_read_now == 0) {
      if (retries >= max_retries) {
        return bytes_read;
      }
      ++retries;
      continue;
    }
    if (bytes_read_now > 0) {
      bytes_read += bytes_read_now;
      retries = 0;
    }
  }
  return bytes_read;
}

inline auto convert_byte_array_to_int(char *b) noexcept -> uint32_t {
  if constexpr (LITTLE_ENDIAN) {
#if defined(__GNUC__)
    uint32_t res = 0;
    memcpy(&res, b, sizeof(res));
    return __builtin_bswap32(res);
#else  // defined(__GNUC__)
    return (b[0] << 24) | ((b[1] & 0xFF) << 16) | ((b[2] & 0xFF) << 8) |
           (b[3] & 0xFF);
#endif // defined(__GNUC__)
  }
  uint32_t result = 0;
  memcpy(&result, b, sizeof(result));
  return result;
}

inline auto convert_int_to_byte_array(char *dst, uint32_t sz) noexcept -> void {
  if constexpr (LITTLE_ENDIAN) {
#if defined(__GNUC__)
    auto tmp = __builtin_bswap32(sz);
    memcpy(dst, &tmp, sizeof(tmp));
#else      // defined(__GNUC__)
    auto tmp = dst;
    tmp[0] = (sz >> 24) & 0xFF;
    tmp[1] = (sz >> 16) & 0xFF;
    tmp[2] = (sz >> 8) & 0xFF;
    tmp[3] = sz & 0xFF;
#endif     // defined(__GNUC__)
  } else { // BIG_ENDIAN
    memcpy(dst, &sz, sizeof(sz));
  }
}

inline auto destruct_message(char *msg, size_t bytes)
    -> std::optional<uint32_t> {
  if (bytes < 4) {
    return std::nullopt;
  }

  auto actual_msg_size = convert_byte_array_to_int(msg);
  return actual_msg_size;
}

auto secure_recv(int fd) -> std::pair<size_t, std::unique_ptr<char[]>> {
  char dlen[4];

  if (auto byte_read = read_n(fd, dlen, length_size_field);
      byte_read != length_size_field) {
    debug_print("[{}] Length of size field does not match got {} expected {}\n",
                __func__, byte_read, length_size_field);
    return {0, nullptr};
  }

  auto actual_msg_size_opt = destruct_message(dlen, length_size_field);
  if (!actual_msg_size_opt) {
    debug_print("[{}] Could not get a size from message\n", __func__);
    return {0, nullptr};
  }

  auto actual_msg_size = *actual_msg_size_opt;
  auto buf = std::make_unique<char[]>(static_cast<size_t>(actual_msg_size) + 1);
  buf[actual_msg_size] = '\0';
  if (auto byte_read = read_n(fd, buf.get(), actual_msg_size);
      byte_read != actual_msg_size) {
    debug_print("[{}] Length of message is incorrect got {} expected {}\n",
                __func__, byte_read, actual_msg_size);
    return {0, nullptr};
  }

  if (actual_msg_size == 0) {
    debug_print("[{}] wrong .. {} bytes\n", __func__, actual_msg_size);
  }
  return {actual_msg_size, std::move(buf)};
}

auto secure_send(int fd, char *data, size_t len) -> std::optional<size_t> {
  auto bytes = 0LL;
  auto remaining_bytes = len;

  char *tmp = data;

  while (remaining_bytes > 0) {
    bytes = send(fd, tmp, remaining_bytes, 0);
    if (bytes < 0) {
      return std::nullopt;
    }
    remaining_bytes -= bytes;
    tmp += bytes;
  }

  return len;
}

inline void construct_message(char *dst, const char *payload, size_t payload_size) {
  convert_int_to_byte_array(dst, payload_size);
  ::memcpy(dst + length_size_field, payload, payload_size);
}

void sendWrapper(int sock_fd, raft::AppendEntriesRequest m) {
	std::string buffer = "";
	m.SerializeToString(&buffer);
	auto msg_size = buffer.size();
	auto buf = std::make_unique<char[]>(msg_size + length_size_field);
	construct_message(buf.get(), buffer.c_str(), msg_size);
  secure_send(sock_fd, buf.get(), msg_size + length_size_field);
}

void sendWrapper(int sock_fd, kvs::server_response m) {
	std::string buffer = "";
	m.SerializeToString(&buffer);
	auto msg_size = buffer.size();
	auto buf = std::make_unique<char[]>(msg_size + length_size_field);
	construct_message(buf.get(), buffer.c_str(), msg_size);
    secure_send(sock_fd, buf.get(), msg_size + length_size_field);
}

raft::AppendEntriesRequest construct_append_entry_message(kvs::client_msg m) {
    raft::AppendEntriesRequest req;
    req.set_term(term);
    req.set_leaderid(leaderID);
    req.set_prevlogindex(prevLogIndex);
    req.set_prevlogterm(prevLogTerm);
    req.set_leadercommit(leaderCommit);
    raft::Entry *entry = req.add_entries();
    entry->set_id(m.id());
    if (m.type() == kvs::client_msg::GET) {
        entry->set_type(raft::Entry::GET);
    }
    else if (m.type() == kvs::client_msg::PUT) {
        entry->set_type(raft::Entry::PUT);
    }
    else if (m.type() == kvs::client_msg::DIRECT_GET) {
        entry->set_type(raft::Entry::DIRECT_GET);
    }
    entry->set_key(m.key());
    entry->set_value(m.value());
    printf("constructed message\n");
    return req;
}

void handle_get(kvs::client_msg m, int socket) {
  kvs::server_response resp;
  resp.set_id(m.id());
  std::string value;
  bool status = db->Get(rocksdb::ReadOptions(), m.key().c_str(), &value).ok();
  if (status) {
    resp.set_status(kvs::server_response::OK);
    resp.set_value(value);
  } else {
    resp.set_status(kvs::server_response::NO_KEY);
  }
  printf("sending this naya reply\n");
  resp.PrintDebugString();
  sendWrapper(socket, resp);
}

void* follower_thread_func(void* arg) {
  printf("spawned follower thread\n");
  client_socket = listening_socket(clientCommPort);
  printf("accepting on client socket %d\n", client_socket);
  while (true) {
    int temp_client_fd = accept_connection(client_socket);
    std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(temp_client_fd);
    printf("received a message\n");
    kvs::client_msg m;
    m.PrintDebugString();
    m.ParseFromString(res.second.get());
    if (m.type() == kvs::client_msg::DIRECT_GET) {
      handle_get(m, temp_client_fd);
    } else {
      printf("unexpected message type\n");
    }
  }
}

auto main(int argc, char * argv[]) -> int {
  cxxopts::Options options("svr", "server for cloud-lab task 3");
  options.allow_unrecognised_options().add_options()(
      "p,port", "Port of the server", cxxopts::value<in_port_t>())(
      "l,leader", "Leader", cxxopts::value<bool>()->default_value("false"))(
      "r,raft_ports", "Raft ports", cxxopts::value<std::vector<in_port_t>>())(
      "i,server_id",
      "Server id",
      cxxopts::value<unsigned>()->default_value("0"))("h,help", "Print help");

  auto args = options.parse(argc, argv);

  if (args.count("help")) {
    fmt::print("{}\n", options.help());
    return 0;
  }

  if (!args.count("port")) {
    fmt::print(stderr, "The port is required\n{}\n", options.help());
    return -1;
  }

  // TODO: implement!
  leader = args["leader"].as<bool>();
  clientCommPort = args["port"].as<in_port_t>();
  my_server_id = args["server_id"].as<unsigned>();
  for(int i = 0; i < args["raft_ports"].as<std::vector<in_port_t>>().size(); i++) {
      allPorts.push_back(args["raft_ports"].as<std::vector<in_port_t>>()[i]);
  }
  int myPort = allPorts[my_server_id];
  printf("myPort: %d\n", myPort);

  my_listening_socket = listening_socket(myPort);

  rocksdb::Options dbOptions;
	dbOptions.create_if_missing = true;
	std::string path = "/tmp/testdb" + std::to_string(myPort);
	rocksdb::Status status = rocksdb::DB::Open(dbOptions, path, &db);
	assert(status.ok());

  printf("sending join to other servers\n");
  int connected = 0;
  for (int i = 0; i < allPorts.size(); i++) {
    if (allPorts[i] != myPort) {
      int temp_port = allPorts[i];
      printf("connecting to %d\n", temp_port);
      int temp_sock_fd = connect_socket(allPorts[i]);
      port_socket_map[temp_port] = temp_sock_fd;
      if (temp_sock_fd >= 0) {
        printf("Connected to %d\n", i);
        kvs::server_response svr_resp;
        svr_resp.set_id(my_server_id);
        svr_resp.set_status(kvs::server_response::OK);
        sendWrapper(temp_sock_fd, svr_resp);
        connected++;
      }
    }
  }
   for(int i = 0; i < allPorts.size(); i++) {
    printf("%d: %d\n", i, port_socket_map[allPorts[i]]);
  }
  printf("receiving join from other servers\n");
  int need_to_connect = allPorts.size() - connected - 1;
  for (int i=0; i<need_to_connect; i++) {
    int temp_sock_fd = accept_connection(my_listening_socket);
    std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(temp_sock_fd);
    kvs::server_response svr_resp;
    svr_resp.ParseFromString(res.second.get());
    port_socket_map[allPorts[svr_resp.id()]] = temp_sock_fd;
    printf("Connected to %d\n", svr_resp.id());
  }
  ("Connected to all servers. Current map: \n");
  for(int i = 0; i < allPorts.size(); i++) {
    printf("%d: %d\n", i, port_socket_map[allPorts[i]]);
  }

  if (leader) {
    client_socket = listening_socket(clientCommPort);
    while (true) {
      int temp_client_fd = accept_connection(client_socket);
      std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(temp_client_fd);
      kvs::client_msg m;
      m.ParseFromString(res.second.get());
      printf("leader received a message\n");
      m.PrintDebugString();
      if (m.type() == kvs::client_msg::PUT) {
        raft::AppendEntriesRequest req = construct_append_entry_message(m);
        for (int i = 0; i < allPorts.size(); i++) {
          if (allPorts[i] != myPort) {
            int temp_sock_fd = connect_socket(allPorts[i]);
            sendWrapper(temp_sock_fd, req);
          }
        }
        printf("m.key(): %s\n", m.key().c_str());
        printf("m.value(): %s\n", m.value().c_str());
        bool status = db->Put(rocksdb::WriteOptions(), m.key().c_str(), m.value().c_str()).ok();
        kvs::server_response resp;
        if (status) {
          resp.set_status(kvs::server_response::OK);
        } else {
          resp.set_status(kvs::server_response::ERROR);
        }
        resp.set_id(m.id());
        sendWrapper(temp_client_fd, resp);
      }
      else if (m.type() == kvs::client_msg::GET || m.type() == kvs::client_msg::DIRECT_GET) {
        printf("get or direct get\n");
        handle_get(m, temp_client_fd);
      }
    }
  } else {
    pthread_create(&follower_thread, NULL, follower_thread_func, NULL);
    while (true) {
      printf("putting an item\n");
      int temp_fd = accept_connection(my_listening_socket);
      std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(temp_fd);
      raft::AppendEntriesRequest req;
      req.ParseFromString(res.second.get());
      req.PrintDebugString();
      printf("req.entries_size(): %d\n", req.entries_size());
      for (int i=0; i<req.entries_size(); i++) {
        raft::Entry entry = req.entries(i);
        if (entry.type() == raft::Entry::PUT) {
          db->Put(rocksdb::WriteOptions(), entry.key().c_str(), entry.value().c_str()).ok();
        } else {
          printf("unexpected type received\n");
        }
      }

    }
  }

  return 0;
}
