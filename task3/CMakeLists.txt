cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

project(
	clt-svr-model
	VERSION 0.1.0
	DESCRIPTION "TUM Cloud Computing Lab - Client Server model"
	HOMEPAGE_URL "https://github.com/dgiantsidi/client-server-model"
	LANGUAGES CXX
	)

docs_early_return()

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Packages ----

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

find_package(Protobuf REQUIRED)
find_package(fmt REQUIRED)

# ---- Declare library ----

include_directories(SYSTEM ${Protobuf_INCLUDE_DIRS})
include_directories(SYSTEM ${CMAKE_CURRENT_BINARY_DIR})
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/extern/include)
#include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb)
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb/include)
#link_directories(${CMAKE_CURRENT_SOURCE_DIR}/rocksdb)
# link_libraries("-lrocksdb -lz")
#link_libraries("-lz")

set(CMAKE_BUILD_TYPE_COPY "${CMAKE_BUILD_TYPE}")
set(CMAKE_BUILD_TYPE "Release")
set(FAIL_ON_WARNINGS OFF CACHE INTERNAL "Deactivate Werror" FORCE)
add_subdirectory(rocksdb EXCLUDE_FROM_ALL)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE_COPY})


protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS source/kvs.proto source/raft.proto)

add_custom_command(
	OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/message.h
	DEPENDS ${PROTO_HDRS} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/message_hdr.py
	COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/message_hdr.py ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/message.h ${PROTO_HDRS}
	)

add_library(
	clt-svr-proto_lib OBJECT
	${PROTO_HDRS}
	${PROTO_SRCS}
	${CMAKE_CURRENT_BINARY_DIR}/message.h
	)

target_include_directories(
	clt-svr-proto_lib ${warning_guard}
	PUBLIC
	"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/source>"
	)

target_compile_features(clt-svr-proto_lib PUBLIC cxx_std_20)

set_target_properties(
	clt-svr-proto_lib
	PROPERTIES
	CXX_CLANG_TIDY ""
	)

add_library(
	svr_lib OBJECT
	${CMAKE_CURRENT_BINARY_DIR}/message.h
	)

set_target_properties(svr_lib PROPERTIES LINKER_LANGUAGE CXX)
target_compile_features(svr_lib PUBLIC cxx_std_20)

# ---- Declare executables ----

add_executable(svr source/server.cpp ${CMAKE_CURRENT_BINARY_DIR}/message.h)
add_executable(clt-svr::svr ALIAS svr)

set_target_properties(
	svr PROPERTIES
	OUTPUT_NAME svr
	EXPORT_NAME svr
	)

target_compile_features(svr PRIVATE cxx_std_20)

target_link_libraries(svr
	PRIVATE
	clt-svr-proto_lib
	svr_lib
	${Protobuf_LIBRARIES}
	fmt::fmt
	Threads::Threads
	rocksdb
	)

add_executable(clt source/client.cpp ${CMAKE_CURRENT_BINARY_DIR}/message.h)
add_executable(clt-svr::clt ALIAS clt)

set_target_properties(
	clt PROPERTIES
	OUTPUT_NAME clt
	EXPORT_NAME clt
	)

target_compile_features(clt PRIVATE cxx_std_20)

target_link_libraries(clt
	PRIVATE
	clt-svr-proto_lib
	${Protobuf_LIBRARIES}
	fmt::fmt
	Threads::Threads)


# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
	include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT clt-svr-model_DEVELOPER_MODE)
	return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
	message(
		AUTHOR_WARNING
		"Developer mode is intended for developers of clt-svr-model"
		)
endif()

include(cmake/dev-mode.cmake)
