#ifndef PROTO_RPC_CONTROLLER
#define PROTO_RPC_CONTROLLER

#include <iostream>

#include <boost/current_function.hpp>

#include <google/protobuf/service.h> // for RpcConteoller

#include <proto_rpc/namespace.hpp>
#include <proto_rpc/rpc_messages.hpp> // for FailureInfo

namespace proto_rpc {

class Controller : public gp::RpcController, public FailureInfo {
public:
  Controller() { Reset(); }

  virtual ~Controller() {}

  // failure info

  void Reset() {
    FailureInfo::Clear();
    FailureInfo::set_failed(false);
  }

  bool Failed() const { return FailureInfo::failed(); }

  void SetFailed(const gp::string &reason) {
    FailureInfo::set_failed(true);
    FailureInfo::set_error_text(reason);
  }

  gp::string ErrorText() const { return FailureInfo::error_text(); }

  // cancel operations (not supported)

  void StartCancel() {
    std::cerr << "Error: Not implemented\n"
              << "From: " << BOOST_CURRENT_FUNCTION << std::endl;
  }

  bool IsCanceled() const { return false; }

  void NotifyOnCancel(gp::Closure *) {}
};
}

#endif // PROTO_RPC_CONTROLLER