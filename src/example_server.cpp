#include <boost/asio/io_service.hpp>
#include <boost/shared_ptr.hpp>

#include <proto_rpc/server.hpp>

#include <example_service.pb.h>

namespace ba = boost::asio;
namespace gp = google::protobuf;
namespace pr = proto_rpc;

class Service : public example::Service {
public:
  Service() {}

  void Set(gp::RpcController *controller, const example::Double *request, example::Empty *response,
           gp::Closure *done) {
    std::cout << "Setting the value to " << request->value() << " ..." << std::endl;
    if (!value_) {
      value_.reset(new double(request->value()));
    } else {
      *value_ = request->value();
    }
    done->Run();
  }

  void Get(gp::RpcController *controller, const example::Empty *request, example::Double *response,
           gp::Closure *done) {
    std::cout << "Getting the value ..." << std::endl;
    if (!value_) {
      controller->SetFailed("Value never set");
    } else {
      response->set_value(*value_);
    }
    done->Run();
  }

private:
  boost::shared_ptr< double > value_;
};

int main(int argc, char *argv[]) {

  ba::io_service queue;
  boost::shared_ptr< Service > service(new Service());
  pr::Server server(queue, 12345, service);
  server.start();

  queue.run();

  return 0;
}