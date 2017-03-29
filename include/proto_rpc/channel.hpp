#ifndef PROTO_RPC_CHANNEL
#define PROTO_RPC_CHANNEL

#include <iostream>
#include <stdexcept>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/ref.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>      // for RpcChannel
#include <google/protobuf/stubs/common.h> // for callbacks

#include <proto_rpc/controller.hpp>
#include <proto_rpc/message_coding.hpp>
#include <proto_rpc/namespace.hpp>
#include <proto_rpc/messages.hpp>

namespace proto_rpc {

class Channel : public gp::RpcChannel {
public:
  enum { DEFAULT_TIMEOUT = 5000 };

public:
  Channel(const ba::ip::address_v4 &address, const unsigned short port,
          const bp::time_duration &timeout = bp::milliseconds(DEFAULT_TIMEOUT))
      : endpoint_(address, port), timeout_(timeout), socket_(queue_), timer_(queue_) {}

  virtual ~Channel() {}

  void CallMethod(const gp::MethodDescriptor *method, gp::RpcController *controller,
                  const gp::Message *request, gp::Message *response, gp::Closure *done) {
    // ensure a controller and a closure exist
    boost::scoped_ptr< gp::RpcController > _controller;
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
        connect();
        std::cout << "Connected to a server at " << endpoint_ << std::endl;

        // send the service description to the sever once connected
        {
          gp::ServiceDescriptorProto descriptor;
          method->service()->CopyTo(&descriptor);
          write(descriptor);
        }

        // receive a match result against a description the server has
        FailureInfo info;
        read(info);

        // check the match result
        if (!info.IsInitialized()) {
          throw std::runtime_error("Uninitialized failure info");
        }
        if (info.failed()) {
          throw std::runtime_error(info.error_text());
        }
      }

      // send the method index and the request
      {
        MethodIndex index;
        index.set_value(method->index());
        write(index);
      }
      write(*request);

      // receive the failure info and the response
      FailureInfo info;
      read(info, *response);

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
  void connect() {
    // set timeout. on timeout, the expiration handler will cancel operations on the socket.
    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Channel::handleTimerEvent, this, _1));

    // start connecting to the endpoint. the connection handler will cancel the timeout operation.
    bs::error_code error;
    socket_.async_connect(endpoint_,
                          boost::bind(&Channel::handleSocketEvent, this, _1, boost::ref(error)));

    // spin the callback queue until all handlers have been dispatched
    queue_.reset();
    queue_.run();

    // throw the error if happened on connecting
    if (error) {
      throw bs::system_error(error);
    }
  }

  void write(const gp::Message &message) {
    // encode the given message
    ba::streambuf buffer;
    encode(message, buffer);

    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Channel::handleTimerEvent, this, _1));

    bs::error_code error;
    ba::async_write(socket_, buffer,
                    boost::bind(&Channel::handleSocketEvent, this, _1, boost::ref(error)));

    queue_.reset();
    queue_.run();

    if (error) {
      throw bs::system_error(error);
    }
  }

  std::size_t read(ba::streambuf &buffer, gp::Message &message) {
    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Channel::handleTimerEvent, this, _1));

    bs::error_code error;
    std::size_t bytes;
    ba::async_read_until(socket_, buffer, Decode(message),
                         boost::bind(&Channel::handleSocketEvent2, this, _1, _2, boost::ref(error),
                                     boost::ref(bytes)));

    queue_.reset();
    queue_.run();

    if (error) {
      throw bs::system_error(error);
    }

    return bytes;
  }

  void read(gp::Message &message) {
    ba::streambuf buffer;
    buffer.consume(read(buffer, message));
  }

  void read(gp::Message &message, gp::Message &message2) {
    ba::streambuf buffer;
    buffer.consume(read(buffer, message));
    buffer.consume(read(buffer, message2));
  }

  void handleSocketEvent(const bs::error_code &error, bs::error_code &error_out) {
    timer_.cancel();
    error_out = error;
  }

  void handleSocketEvent2(const bs::error_code &error, const std::size_t bytes,
                          bs::error_code &error_out, std::size_t &bytes_out) {
    timer_.cancel();
    error_out = error;
    bytes_out = bytes;
  }

  void handleTimerEvent(const bs::error_code &error) {
    if (error == ba::error::operation_aborted) { // canceled by a socket event handler
      return;
    } else if (error) {
      std::cerr << "Error on timer event: " << error.message() << std::endl;
      return;
    }
    socket_.cancel();
  }

private:
  const ba::ip::tcp::endpoint endpoint_;
  const bp::time_duration timeout_;
  ba::io_service queue_;
  ba::ip::tcp::socket socket_;
  ba::deadline_timer timer_;
};
}

#endif // PROTO_RPC_CHANNEL