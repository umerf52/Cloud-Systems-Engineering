#include <iostream>

#include <arpa/inet.h>
#include <cxxopts.hpp>
#include <fmt/format.h>
#include "kvs.pb.h"
#include <unistd.h>
#include <iostream>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

static constexpr auto length_size_field = sizeof(uint32_t);
#define debug_print(...) fmt::print(__VA_ARGS__)

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
		printf("\nConnection Failed \n");
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
      // @dimitra: the socket is in non-blocking mode; select() should be also
      // applied
      //             return -1;
      //
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

void sendWrapper(int sock_fd, kvs::client_msg m) {
	std::string buffer = "";
	m.SerializeToString(&buffer);
	auto msg_size = buffer.size();
	auto buf = std::make_unique<char[]>(msg_size + length_size_field);
	construct_message(buf.get(), buffer.c_str(), msg_size);
  secure_send(sock_fd, buf.get(), msg_size + length_size_field);
}


auto main(int argc, char * argv[]) -> int {
  cxxopts::Options options(argv[0], "Client for the task 3");
  options.allow_unrecognised_options().add_options()(
      "p,port", "Port of the server", cxxopts::value<in_port_t>())(
      "o,operation",
      "Specify either a GET, PUT or DIRECT_GET operation",
      cxxopts::value<std::string>())(
      "k,key", "Specify key for an operation", cxxopts::value<std::string>())(
      "v,value",
      "Specify value for a PUT operation",
      cxxopts::value<std::string>())("h,help", "Print help");

  auto args = options.parse(argc, argv);
  if (args.count("help")) {
    fmt::print("{}\n", options.help());
    return 0;
  }

  if (!args.count("port")) {
    fmt::print(stderr, "The port is required\n{}\n", options.help());
    return 1;
  }

  if (!args.count("operation")) {
    fmt::print(stderr, "The operation is required\n{}\n", options.help());
    return 1;
  }
  auto operation = args["operation"].as<std::string>();
  if (operation != "GET" && operation != "PUT" && operation != "DIRECT_GET") {
    fmt::print(stderr, "The invalid operation: {}\n", operation);
    return 1;
  }

  if (!args.count("key")) {
    fmt::print(stderr, "The key is required\n{}\n", options.help());
    return 1;
  }

  std::string value;
  if (!args.count("value")) {
    if (operation == "PUT") {
      fmt::print(stderr, "The value is required\n{}\n", options.help());
      return 1;
    }
  } else {
    value = args["value"].as<std::string>();
  }

  auto port = args["port"].as<in_port_t>();
  auto key = args["key"].as<std::string>();

  // TODO: implement!
  auto ret = 0;

  // Exit code
  // 0 -> OK
  // 1 -> Client error
  // 2 -> Not leader
  // 3 -> No key
  // 4 -> Server error

  kvs::client_msg req;
  if (operation == "GET") {
    req.set_type(kvs::client_msg::GET);
  } else if (operation == "PUT") {
    req.set_type(kvs::client_msg::PUT);
    req.set_value(value);
  } else if (operation == "DIRECT_GET") {
    req.set_type(kvs::client_msg::DIRECT_GET);
  }
  req.set_id(1);
  req.set_key(key);
  int sock_fd = connect_socket(port);
  sendWrapper(sock_fd, req);

  std::pair<size_t, std::unique_ptr<char[]>> res = secure_recv(sock_fd);
  if (res.first == 0) {
        ret = 4;
  }
  kvs::server_response resp;
  resp.ParseFromString(res.second.get());
  if (resp.status() == kvs::server_response::NOT_LEADER) {
    ret = 2;
  } else if (resp.status() == kvs::server_response::NO_KEY) {
    ret = 3;
  } else if (resp.status() == kvs::server_response::OK) {
  }

  if (operation == "GET" || operation == "DIRECT_GET") {
    printf("%s\n", resp.value().c_str());
  }
  
  return ret;
}
