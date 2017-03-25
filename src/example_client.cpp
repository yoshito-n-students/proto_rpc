#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/scoped_ptr.hpp>

#include <proto_rpc/channel.hpp>
#include <proto_rpc/controller.hpp>

#include <example_service.pb.h>

namespace ba = boost::asio;
namespace bp = boost::program_options;
namespace gp = google::protobuf;
namespace pr = proto_rpc;

int main(int argc, char *argv[]) {

  // read parameters from the command line
  ba::ip::address_v4 address;
  unsigned short port;
  double value;
  try {
    // define available options
    bp::options_description options;
    options.add(boost::make_shared< bp::option_description >("help", bp::bool_switch()));
    options.add(boost::make_shared< bp::option_description >(
        "address", bp::value< std::string >()->default_value("127.0.0.1")));
    options.add(boost::make_shared< bp::option_description >(
        "port", bp::value< unsigned short >()->default_value(12345)));
    options.add(boost::make_shared< bp::option_description >(
        "value", bp::value< double >()->default_value(100.)));
    // parse the command line
    bp::variables_map args;
    bp::store(bp::parse_command_line(argc, argv, options), args);
    if (args["help"].as< bool >()) {
      std::cout << "Available options:\n" << options << std::endl;
      return 0;
    }
    // convert the parsing result
    address = ba::ip::address_v4::from_string(args["address"].as< std::string >());
    port = args["port"].as< unsigned short >();
    value = args["value"].as< double >();
  } catch (const std::exception &error) {
    std::cerr << "Error: " << error.what() << std::endl;
    return 1;
  }

  // construct a RPC client
  const boost::scoped_ptr< pr::Channel > channel(new pr::Channel(address, port));
  const boost::scoped_ptr< example::Service > client(new example::Service::Stub(channel.get()));

  // call Get(). this shall return an error if Set() never called
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

  // call Set()
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

  // call Get() again. this shall return the value set by the recent call of Set().
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

  // call Append().
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