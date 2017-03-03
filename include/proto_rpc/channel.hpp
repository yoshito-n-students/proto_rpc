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
#include <boost/shared_ptr.hpp>
#include <boost/system/system_error.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>      // for RpcChannel
#include <google/protobuf/stubs/common.h> // for callbacks

#include <proto_rpc/controller.hpp>
#include <proto_rpc/message_coding.hpp>
#include <proto_rpc/namespace.hpp>
#include <proto_rpc/rpc_messages.hpp>

namespace proto_rpc {

class Channel : public gp::RpcChannel {
public:
  Channel(ba::io_service &queue, const ba::ip::address_v4 &address, const unsigned short port)
      : endpoint_(address, port), socket_(queue) {}

  virtual ~Channel() {}

  void CallMethod(const gp::MethodDescriptor *method, gp::RpcController *controller,
                  const gp::Message *request, gp::Message *response, gp::Closure *done) {
    // ensure a controller and a closure exist
    boost::shared_ptr< gp::RpcController > _controller;
    if (!controller) {
      _controller.reset(new Controller());
      controller = _controller.get();
    }
    if (!done) {
      // Note: this closure deletes itself when Run() is called
      done = gp::NewCallback(&gp::DoNothing);
    }

    try {
      // check inputs
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

      // connect to the server if not connected
      if (!socket_.is_open()) {
        socket_.connect(endpoint_);
        std::cout << "Connected to a server at " << endpoint_ << std::endl;

        // send the service description to the sever once connected
        {
          gp::ServiceDescriptorProto descriptor;
          method->service()->CopyTo(&descriptor);
          ba::streambuf buffer;
          encode(descriptor, buffer);
          ba::write(socket_, buffer);
        }

        std::cout << "!!" << std::endl;

        // receive a match result against a description the server has
        FailureInfo info;
        {
          ba::streambuf buffer;
          buffer.consume(ba::read_until(socket_, buffer, Decode(info)));
        }

        // check the match result
        if (!info.IsInitialized()) {
          throw std::runtime_error("Uninitialized failure info");
        }
        if (info.failed()) {
          throw std::runtime_error(info.error_text());
        }
      }

      std::cout << "!!" << std::endl;

      // send the method index and the request
      {
        MethodIndex index;
        index.set_value(method->index());
        ba::streambuf buffer;
        encode(index, buffer);
        encode(*request, buffer);
        ba::write(socket_, buffer);
      }

      std::cout << "!!" << std::endl;

      // receive the failure info and the response
      FailureInfo info;
      {
        ba::streambuf buffer;
        buffer.consume(ba::read_until(socket_, buffer, Decode(info)));
        buffer.consume(ba::read_until(socket_, buffer, Decode(*response)));
      }

      std::cout << "!!" << std::endl;

      // check outputs
      if (!info.IsInitialized()) {
        throw std::runtime_error("Uninitialized failure info");
      }
      if (info.failed()) {
        throw std::runtime_error(info.error_text());
      }
      if (!response->IsInitialized()) {
        throw std::runtime_error("Uninitialized response");
      }
    } catch (const bs::system_error &error) {
      // a network error
      socket_.close();
      controller->SetFailed(error.what());
      done->Run();
      return;
    } catch (const std::runtime_error &error) {
      // a rpc failure not by network reasons
      controller->SetFailed(error.what());
      done->Run();
      return;
    }

    done->Run();
  }

private:
  ba::ip::tcp::socket socket_;
  const ba::ip::tcp::endpoint endpoint_;
};
}

#endif // PROTO_RPC_CHANNEL