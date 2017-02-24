#ifndef PROTO_RPC_SERVER
#define PROTO_RPC_SERVER

#include <iostream>

#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/current_function.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/system/error_code.hpp>

#include <google/protobuf/service.h>

#include <proto_rpc/controller.hpp>
#include <proto_rpc/message_coding.hpp>
#include <proto_rpc/namespace.hpp>
#include <proto_rpc/rpc_messages.hpp>

namespace proto_rpc {

class Session : public boost::enable_shared_from_this< Session > {
  friend class Server;

private:
  Session(ba::io_service &queue, const boost::shared_ptr< gp::Service > &service)
      : socket_(queue), service_(service) {}

public:
  virtual ~Session() { std::cout << "Closed the session (" << this << ")" << std::endl; }

  void start() {
    std::cout << "Started a session (" << this << ") with " << socket_.remote_endpoint()
              << std::endl;
    startReadMethodIndex(shared_from_this());
  }

private:
  static void startReadMethodIndex(const boost::shared_ptr< Session > &self) {
    // clear the buffer
    self->buffer_.consume(self->buffer_.size());

    const boost::shared_ptr< MethodIndex > index(new MethodIndex());
    ba::async_read_until(self->socket_, self->buffer_, Decode(*index),
                         boost::bind(&Session::handleReadMethodIndex, self, index, _1, _2));
  }

  static void handleReadMethodIndex(const boost::shared_ptr< Session > &self,
                                    const boost::shared_ptr< MethodIndex > &index,
                                    const bs::error_code &error, const std::size_t bytes) {
    if (error == ba::error::eof) {
      // disconnected by the client
      return;
    } else if (error) {
      std::cerr << "Error: " << error.message() << "\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      return;
    }

    // clear the range corresponding the parsed index
    self->buffer_.consume(bytes);

    // look up the method
    const gp::MethodDescriptor *method(self->service_->GetDescriptor()->method(index->value()));
    if (!method) {
      std::cerr << "Error: Method not found\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      const boost::shared_ptr< FailureInfo > info(new FailureInfo());
      info->set_failed(true);
      info->set_error_text("Method not found on server");
      startWriteResult(self, info, NULL);
      return;
    }

    startReadRequest(self, method);
  }

  static void startReadRequest(const boost::shared_ptr< Session > &self,
                               const gp::MethodDescriptor *method) {
    const boost::shared_ptr< gp::Message > request(
        self->service_->GetRequestPrototype(method).New());
    ba::async_read_until(self->socket_, self->buffer_, Decode(*request),
                         boost::bind(&Session::handleReadRequest, self, method, request, _1, _2));
  }

  static void handleReadRequest(const boost::shared_ptr< Session > &self,
                                const gp::MethodDescriptor *method,
                                const boost::shared_ptr< gp::Message > &request,
                                const bs::error_code &error, const std::size_t bytes) {
    if (error == ba::error::eof) {
      // disconnected by the client
      return;
    } else if (error) {
      std::cerr << "Error: " << error.message() << "\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      return;
    }

    // clear the range corresponding the parsed request
    self->buffer_.consume(bytes);

    if (!request->IsInitialized()) {
      std::cerr << "Error: Uninitialized request\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      const boost::shared_ptr< FailureInfo > info(new FailureInfo());
      info->set_failed(true);
      info->set_error_text("Uninitialized request on server");
      startWriteResult(self, info, NULL);
      return;
    }

    // call the method with the request
    callMethod(self, method, request);
  }

  static void callMethod(const boost::shared_ptr< Session > &self,
                         const gp::MethodDescriptor *method,
                         const boost::shared_ptr< gp::Message > &request) {
    const boost::shared_ptr< Controller > controller(new Controller());
    const boost::shared_ptr< gp::Message > response(
        self->service_->GetResponsePrototype(method).New());
    self->service_->CallMethod(method, controller.get(), request.get(), response.get(),
                               gp::NewCallback(&gp::DoNothing));

    startWriteResult(self, controller, response);
  }

  static void startWriteResult(const boost::shared_ptr< Session > &self,
                               const boost::shared_ptr< FailureInfo > &info,
                               const boost::shared_ptr< gp::Message > &response) {
    // clear the buffer
    self->buffer_.consume(self->buffer_.size());

    // set the failure info if the response is invalid
    if (!info->failed() && !response->IsInitialized()) {
      std::cerr << "Error: Uninitialized response\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      info->set_failed(true);
      info->set_error_text("Uninitialized response on server");
    }

    // encode the result
    encode(*info, self->buffer_);
    if (!info->failed()) {
      encode(*response, self->buffer_);
    }

    // send the result
    ba::async_write(self->socket_, self->buffer_,
                    boost::bind(&Session::handleWriteResult, self, _1));
  }

  static void handleWriteResult(const boost::shared_ptr< Session > &self,
                                const bs::error_code &error) {
    if (error == ba::error::eof) {
      // disconnected by the client
      return;
    } else if (error) {
      std::cerr << "Error: " << error.message() << "\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      return;
    }

    startReadMethodIndex(self);
  }

private:
  ba::ip::tcp::socket socket_;
  ba::streambuf buffer_;
  const boost::shared_ptr< gp::Service > service_;
};

class Server {
public:
  Server(ba::io_service &queue, const unsigned short port,
         const boost::shared_ptr< gp::Service > &service)
      : acceptor_(queue, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), port)), service_(service) {}

  virtual ~Server() {}

  void start() {
    std::cout << "Started a server at " << acceptor_.local_endpoint() << std::endl;
    startAccept();
  }

private:
  void startAccept() {
    const boost::shared_ptr< Session > session(new Session(acceptor_.get_io_service(), service_));
    acceptor_.async_accept(session->socket_, boost::bind(&Server::handleAccept, this, session, _1));
  }

  void handleAccept(const boost::shared_ptr< Session > &session, const bs::error_code &error) {
    if (error) {
      std::cerr << "Error: " << error.message() << "\n"
                << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
      startAccept();
      return;
    }
    session->start();
    startAccept();
  }

private:
  ba::ip::tcp::acceptor acceptor_;
  const boost::shared_ptr< gp::Service > service_;
};
}

#endif // PROTO_RPC_SERVER