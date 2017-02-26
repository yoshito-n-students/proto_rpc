#include <iostream>

#include <boost/asio/io_service.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/shared_ptr.hpp>

#include <proto_rpc/channel.hpp>
#include <proto_rpc/controller.hpp>

#include <example_service.pb.h>

namespace ba = boost::asio;
namespace gp = google::protobuf;
namespace pr = proto_rpc;

int main(int argc, char *argv[]) {

  static const double default_value(100.);
  double value(default_value);
  if (argc >= 2) {
    try {
      value = boost::lexical_cast< double >(argv[1]);
    } catch (const boost::bad_lexical_cast &error) {
      std::cerr << error.what() << std::endl;
      value = default_value;
    }
  }

  ba::io_service queue;
  boost::shared_ptr< pr::Channel > channel(new pr::Channel(queue, "127.0.0.1", 12345));
  boost::shared_ptr< example::Service > client(new example::Service::Stub(channel.get()));

  {
    pr::Controller controller;
    example::Empty request;
    example::Double response;
    client->Get(&controller, &request, &response, NULL);
    if (controller.Failed()) {
      std::cout << "Get: NG (" << controller.ErrorText() << ")" << std::endl;
    } else {
      std::cout << "Get: OK (" << response.value() << ")" << std::endl;
    }
  }

  {
    pr::Controller controller;
    example::Double request;
    example::Empty response;
    request.set_value(value);
    client->Set(&controller, &request, &response, NULL);
    if (controller.Failed()) {
      std::cout << "Set: NG (" << controller.ErrorText() << ")" << std::endl;
    } else {
      std::cout << "Set: OK" << std::endl;
    }
  }

  {
    pr::Controller controller;
    example::Empty request;
    example::Double response;
    client->Get(&controller, &request, &response, NULL);
    if (controller.Failed()) {
      std::cout << "Get: NG (" << controller.ErrorText() << ")" << std::endl;
    } else {
      std::cout << "Get: OK (" << response.value() << ")" << std::endl;
    }
  }

  {
    pr::Controller controller;
    example::String request;
    request.set_data(boost::lexical_cast< std::string >(value));
    example::String response;
    client->Append(&controller, &request, &response, NULL);
    if (controller.Failed()) {
      std::cout << "Append: NG (" << controller.ErrorText() << ")" << std::endl;
    } else {
      std::cout << "Append: OK (" << response.data() << ")" << std::endl;
    }
  }

  return 0;
}