cmake_minimum_required(VERSION 2.8.3)

# Name of this project
project(proto_rpc)

# Existing libraries this project uses
find_package(
    Protobuf REQUIRED
)

# Location of include files
include_directories(
    ${PROTOBUF_INCLUDE_DIRS}
)

# RPC (remote procedure call) library for generic serivces of protobuf2
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/include/proto_rpc/messages.hpp messages.pb.cc
    COMMAND protoc --cpp_out=${CMAKE_CURRENT_BINARY_DIR} messages.proto
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/messages.pb.h ${CMAKE_CURRENT_SOURCE_DIR}/include/proto_rpc/messages.hpp
    DEPENDS proto/messages.proto
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/proto
)
add_library(
    proto_rpc
    ${CMAKE_CURRENT_BINARY_DIR}/messages.pb.cc
)

# Library dependences
target_link_libraries(
    proto_rpc
    ${PROTOBUF_LIBRARIES}
)
