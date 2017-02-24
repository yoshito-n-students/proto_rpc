#ifndef PROTO_RPC_CHANNEL
#define PROTO_RPC_CHANNEL

#include <iostream>
#include <stdexcept>
#include <string>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/current_function.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h> // for RpcChannel

#include <proto_rpc/message_coding.hpp>
#include <proto_rpc/namespace.hpp>
#include <proto_rpc/rpc_messages.hpp>

namespace proto_rpc {

class Channel : public gp::RpcChannel {
public:
  Channel(ba::io_service &queue, const std::string &address, const unsigned short port)
      : endpoint_(ba::ip::address_v4::from_string(address), port), socket_(queue) {}

  virtual ~Channel() {}

  void CallMethod(const gp::MethodDescriptor *method, gp::RpcController *controller,
                  const gp::Message *request, gp::Message *response, gp::Closure *done) {
    try {
      // check arguments
      if (!method) {
        throw std::runtime_error("Null method");
      }
      if (!request) {
        throw std::runtime_error("Null request");
      }
      if (!response) {
        throw std::runtime_error("Null response");
      }
      if (!request->IsInitialized()) {
        throw std::runtime_error("Uninitialized request");
      }

      // connect to the server as needed
      if (!socket_.is_open()) {
        socket_.connect(endpoint_);
        std::cout << "Connected to a server at " << endpoint_ << std::endl;
      }

      // clear the buffer
      buffer_.consume(buffer_.size());

      // send the method index and the request
      {
        MethodIndex index;
        index.set_value(method->index());
        encode(index, buffer_);
        encode(*request, buffer_);
      }
      ba::write(socket_, buffer_);

      // receive the result
      {
        std::size_t bytes;
        FailureInfo info;
        bytes = ba::read_until(socket_, buffer_, Decode(info));
        buffer_.consume(bytes);
        if (info.failed()) {
          throw std::runtime_error(info.error_text());
        }
      }

      // receive the response
      {
        std::size_t bytes;
        bytes = ba::read_until(socket_, buffer_, Decode(*response));
        buffer_.consume(bytes);
        if (!response->IsInitialized()) {
          throw std::runtime_error("Uninitialized response");
        }
      }
    } catch (const std::runtime_error &error) {
      std::cerr << "Error: " << error.what() << "\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      if (controller) {
        controller->SetFailed(error.what());
      }
    }
    // finally run the closure
    if (done) {
      done->Run();
    }
  }

private:
  const ba::ip::tcp::endpoint endpoint_;
  ba::ip::tcp::socket socket_;
  ba::streambuf buffer_;
};
}

#endif // PROTO_RPC_CHANNEL