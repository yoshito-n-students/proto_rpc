cmake_minimum_required(VERSION 3.7)

# Name of this project
project(proto_rpc)

# Existing libraries this project uses
find_package(
    Boost REQUIRED
    system
)
find_library(
    protobuf
    NAMES libprotobuf
    PATHS $ENV{PROTOBUF_LIBRARYDIR}
)

# Location of include files
include_directories(
    include
    ${Boost_INCLUDE_DIRS}
    $ENV{PROTOBUF_INCLUDEDIR}
)

# Location of library files
link_directories(
    ${Boost_LIBRARY_DIRS}
    $ENV{PROTOBUF_LIBRARYDIR}
)

# RPC (remote procedure call) library for generic serivces of protobuf2
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/proto_rpc/rpc_messages.hpp rpc_messages.pb.cc
    COMMAND protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR} rpc_messages.proto
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/rpc_messages.pb.h ${CMAKE_CURRENT_SOURCE_DIR}/include/proto_rpc/rpc_messages.hpp
    DEPENDS proto/rpc_messages.proto
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/proto
)
add_library(
    proto_rpc
    ${CMAKE_CURRENT_BINARY_DIR}/rpc_messages.pb.cc
)

# Library dependences
target_link_libraries(
    proto_rpc
    ${protobuf}
)
