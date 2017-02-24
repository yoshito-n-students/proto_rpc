#include <boost/asio/io_service.hpp>
#include <boost/shared_ptr.hpp>

#include <proto_rpc/channel.hpp>
#include <proto_rpc/controller.hpp>

#include <example_service.pb.h>

namespace ba = boost::asio;
namespace gp = google::protobuf;
namespace pr = proto_rpc;

int main(int argc, char *argv[]) {

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
    request.set_value(199.);
    client->Set(&controller, &request, &response, NULL);
    std::cout << "Set: " << (controller.failed() ? "NG" : "OK") << std::endl;
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

  return 0;
}