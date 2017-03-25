#include <boost/asio/io_service.hpp>
#include <boost/make_shared.hpp>
#include <boost/optional.hpp>
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
    value_ = request->value();
    done->Run();
  }

  void Get(gp::RpcController *controller, const example::Empty *request, example::Double *response,
           gp::Closure *done) {
    std::cout << "Getting the value ..." << std::endl;
    if (value_ == boost::none) {
      controller->SetFailed("Value never set");
    } else {
      response->set_value(*value_);
    }
    done->Run();
  }

  void Append(::google::protobuf::RpcController *controller, const ::example::String *request,
              ::example::String *response, ::google::protobuf::Closure *done) {
    std::cout << "Appending the data ..." << std::endl;
    data_ += request->data();
    response->set_data(data_);
    done->Run();
  }

private:
  boost::optional< double > value_;
  gp::string data_;
};

int main(int argc, char *argv[]) {

  ba::io_service queue;
  pr::Server server(queue, 12345, boost::make_shared< Service >());
  server.start();

  queue.run();

  return 0;
}