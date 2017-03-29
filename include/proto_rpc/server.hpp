#ifndef PROTO_RPC_SERVER
#define PROTO_RPC_SERVER

#include <iostream>

#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/make_shared.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/system/error_code.hpp>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

#include <proto_rpc/controller.hpp>
#include <proto_rpc/message_coding.hpp>
#include <proto_rpc/namespace.hpp>
#include <proto_rpc/rpc_messages.hpp>

namespace proto_rpc {

class Session : public boost::enable_shared_from_this< Session > {
  friend class Server;

public:
  Session(ba::io_service &queue, const boost::shared_ptr< gp::Service > &service,
          const bp::time_duration &timeout)
      : socket_(queue), timer_(queue), service_(service), timeout_(timeout) {}

  virtual ~Session() { std::cout << "Session " << this << ": Closed" << std::endl; }

  void start() {
    std::cout << "Session " << this << ": Started with " << socket_.remote_endpoint() << std::endl;
    startReadServiceDescriptor();
  }

private:
  // common elements of the following data types
  struct CommonData {
    CommonData() { info.set_failed(false); }

    virtual ~CommonData() {}

    void setFailed(const gp::string &error_text) {
      info.set_failed(true);
      if (info.has_error_text()) {
        info.set_error_text(info.error_text() + "; " + error_text);
      } else {
        info.set_error_text(error_text);
      }
    }

    FailureInfo info;

    ba::streambuf read_buffer;
    ba::streambuf write_buffer;
  };

  // data used in the initial authorization
  struct AuthorizationData : CommonData {
    AuthorizationData() {}

    virtual ~AuthorizationData() {}

    gp::ServiceDescriptorProto descriptor;
  };

  // data used in a single RPC
  struct RpcData : CommonData {
    RpcData() : method(NULL) {}

    virtual ~RpcData() {}

    const gp::MethodDescriptor *method;
    MethodIndex index;
    boost::scoped_ptr< gp::Message > request;
    boost::scoped_ptr< gp::Message > response;
  };

private:
  /*
  * initial authorization steps
  *   1. read the descriptor of the client-side service
  *   2. write the equality of the service descriptors
  *   3. start the first RPC if the descriptors are equal
  */

  void startReadServiceDescriptor() {
    // starting point of the initial authorization. prepare data for the authorization.
    const boost::shared_ptr< AuthorizationData > data(boost::make_shared< AuthorizationData >());

    // set timeout. on timeout, the expiration handler will cancel operations on the socket.
    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Session::handleExpire, this, _1, shared_from_this()));

    // start reading the socket. the receive handler will cancel the timeout.
    ba::async_read_until(
        socket_, data->read_buffer, Decode(data->descriptor),
        boost::bind(&Session::handleReadServiceDescriptor, this, data, _1, _2, shared_from_this()));
  }

  void handleReadServiceDescriptor(const boost::shared_ptr< AuthorizationData > &data,
                                   const bs::error_code &error, const std::size_t bytes,
                                   const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    // cancel the timer
    timer_.cancel();

    if (error) {
      std::cerr << "Session " << this
                << ": Error on reading service descriptor: " << error.message() << std::endl;
      return;
    }

    // clear the buffer corresponding the received descriptor
    data->read_buffer.consume(bytes);

    // check if the server-side service is valid
    if (!service_) {
      data->setFailed("Null service on server");
      startWriteAuthorizationResult(data);
      return;
    }

    // check if the client-side service descriptor is valid
    if (!data->descriptor.IsInitialized()) {
      data->setFailed("Uninitialized service descriptor on server");
      startWriteAuthorizationResult(data);
      return;
    }

    // check if the service descriptors are equal
    gp::ServiceDescriptorProto this_descriptor;
    service_->GetDescriptor()->CopyTo(&this_descriptor);
    if (this_descriptor.SerializeAsString() != data->descriptor.SerializeAsString()) {
      data->setFailed("Service descriptor mismatch on server");
      startWriteAuthorizationResult(data);
      return;
    }

    startWriteAuthorizationResult(data);
  }

  void startWriteAuthorizationResult(const boost::shared_ptr< AuthorizationData > &data) {
    encode(data->info, data->write_buffer);

    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Session::handleExpire, this, _1, shared_from_this()));

    ba::async_write(
        socket_, data->write_buffer,
        boost::bind(&Session::handleWriteAuthorizationResult, this, data, _1, shared_from_this()));
  }

  void handleWriteAuthorizationResult(const boost::shared_ptr< AuthorizationData > &data,
                                      const bs::error_code &error,
                                      const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    timer_.cancel();

    if (error) {
      std::cerr << "Session " << this
                << ": Error on writing authorization result: " << error.message() << std::endl;
      return;
    }

    // start the first RPC if the authorization is ok
    if (!data->info.failed()) {
      startReadMethodIndex();
    }

    // end of the initial authorization. the authorization data is destructed here.
  }

  /*
  * RPC steps
  *   1. read the index of a method to be called (go 2a if the index is valid, or 2b)
  *   2a. read a request of the method (go 3 if the request is valid, or 4)
  *   2b. consume a request of the method (go 4)
  *   3. call the method with the request
  *   4. write the result of this RPC
  *   5. start the next RPC
  */

  void startReadMethodIndex() {
    // starting point of a RPC. prepare data for this RPC.
    const boost::shared_ptr< RpcData > data(boost::make_shared< RpcData >());

    // wait the first data or disconnection from the client without timeout
    ba::async_read_until(
        socket_, data->read_buffer, Decode(data->index),
        boost::bind(&Session::handleReadMethodIndex, this, data, _1, _2, shared_from_this()));
  }

  void handleReadMethodIndex(const boost::shared_ptr< RpcData > &data, const bs::error_code &error,
                             const std::size_t bytes,
                             const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    if (error == ba::error::eof) { // disconnected by the client
      return;
    } else if (error) {
      std::cerr << "Session " << this << ": Error on reading method index: " << error.message()
                << std::endl;
      return;
    }

    // clear the buffer corresponding the received index
    data->read_buffer.consume(bytes);

    // check if the received method index is valid
    if (!data->index.IsInitialized()) {
      data->setFailed("Uninitialized method index on server");
      startConsumeRequest(data);
      return;
    }

    // check if the method index is in range
    data->method = service_->GetDescriptor()->method(data->index.value());
    if (!data->method) {
      data->setFailed("Method not found on server");
      startConsumeRequest(data);
      return;
    }

    startReadRequest(data);
  }

  void startReadRequest(const boost::shared_ptr< RpcData > &data) {
    data->request.reset(service_->GetRequestPrototype(data->method).New());

    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Session::handleExpire, this, _1, shared_from_this()));

    ba::async_read_until(
        socket_, data->read_buffer, Decode(*data->request),
        boost::bind(&Session::handleReadRequest, this, data, _1, _2, shared_from_this()));
  }

  void handleReadRequest(const boost::shared_ptr< RpcData > &data, const bs::error_code &error,
                         const std::size_t bytes,
                         const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    timer_.cancel();

    if (error) {
      std::cerr << "Session " << this << ": Error on reading request: " << error.message()
                << std::endl;
      return;
    }

    // clear the range corresponding the parsed request
    data->read_buffer.consume(bytes);

    // check if the request is valid
    if (!data->request->IsInitialized()) {
      data->setFailed("Uninitialized request on server");
      startWriteRpcResult(data);
      return;
    }

    callMethod(data);
  }

  void startConsumeRequest(const boost::shared_ptr< RpcData > &data) {
    data->request.reset(new Placeholder());

    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Session::handleExpire, this, _1, shared_from_this()));

    ba::async_read_until(
        socket_, data->read_buffer, Decode(*data->request),
        boost::bind(&Session::handleConsumeRequest, this, data, _1, _2, shared_from_this()));
  }

  void handleConsumeRequest(const boost::shared_ptr< RpcData > &data, const bs::error_code &error,
                            const std::size_t bytes,
                            const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    timer_.cancel();

    if (error) {
      std::cerr << "Session " << this << ": Error on consuming request: " << error.message()
                << std::endl;
      return;
    }

    // clear the range corresponding the received request
    data->read_buffer.consume(bytes);

    startWriteRpcResult(data);
  }

  void callMethod(const boost::shared_ptr< RpcData > &data) {
    // call the method
    Controller controller;
    data->response.reset(service_->GetResponsePrototype(data->method).New());
    service_->CallMethod(data->method, &controller, data->request.get(), data->response.get(),
                         gp::NewCallback(&gp::DoNothing));

    // check if the call is succeeded
    if (controller.Failed()) {
      data->setFailed(controller.ErrorText());
      startWriteRpcResult(data);
      return;
    }

    // check if the call returned a valid response
    if (!data->response->IsInitialized()) {
      data->setFailed("Uninitialized response on server");
      startWriteRpcResult(data);
      return;
    }

    startWriteRpcResult(data);
  }

  void startWriteRpcResult(const boost::shared_ptr< RpcData > &data) {
    // ensure the response exists
    if (!data->response) {
      data->response.reset(new Placeholder());
    }

    // encode the failure info and the response
    encode(data->info, data->write_buffer);
    encode(*data->response, data->write_buffer);

    timer_.expires_from_now(timeout_);
    timer_.async_wait(boost::bind(&Session::handleExpire, this, _1, shared_from_this()));

    ba::async_write(socket_, data->write_buffer, boost::bind(&Session::handleWriteRpcResult, this,
                                                             data, _1, shared_from_this()));
  }

  void handleWriteRpcResult(const boost::shared_ptr< RpcData > &, const bs::error_code &error,
                            const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    timer_.cancel();

    if (error) {
      std::cerr << "Session " << this << ": Error on writing RPC result: " << error.message()
                << std::endl;
      return;
    }

    // start the next RPC
    startReadMethodIndex();

    // end of this RPC. the data is destructed here.
  }

  void handleExpire(const bs::error_code &error,
                    const boost::shared_ptr< Session > & /*tracked_this_ptr*/) {
    if (error == ba::error::operation_aborted) { // timeout is canceled
      return;
    } else if (error) {
      std::cerr << "Session " << this << ": Error on waiting expiration: " << error.message()
                << std::endl;
      return;
    }

    socket_.cancel();
  }

private:
  ba::ip::tcp::socket socket_;
  ba::deadline_timer timer_;
  const boost::shared_ptr< gp::Service > service_;
  const bp::time_duration timeout_;
};

class Server {
public:
  enum { DEFAULT_SESSION_TIMEOUT = 5000 };

public:
  Server(ba::io_service &queue, const unsigned short port,
         const boost::shared_ptr< gp::Service > &service,
         const bp::time_duration &session_timeout = bp::milliseconds(DEFAULT_SESSION_TIMEOUT))
      : acceptor_(queue, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), port)), service_(service),
        session_timeout_(session_timeout) {}

  virtual ~Server() {}

  void start() {
    std::cout << "Started a server at " << acceptor_.local_endpoint() << std::endl;
    startAccept();
  }

private:
  void startAccept() {
    const boost::shared_ptr< Session > session(
        boost::make_shared< Session >(acceptor_.get_io_service(), service_, session_timeout_));
    acceptor_.async_accept(session->socket_, boost::bind(&Server::handleAccept, this, session, _1));
  }

  void handleAccept(const boost::shared_ptr< Session > &session, const bs::error_code &error) {
    if (error) {
      std::cerr << "Error on accepting: " << error.message() << std::endl;
      startAccept();
      return;
    }
    session->start();
    startAccept();
  }

private:
  ba::ip::tcp::acceptor acceptor_;
  const boost::shared_ptr< gp::Service > service_;
  const bp::time_duration session_timeout_;
};
}

#endif // PROTO_RPC_SERVER