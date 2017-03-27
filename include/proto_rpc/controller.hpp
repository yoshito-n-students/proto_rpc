#ifndef PROTO_RPC_CONTROLLER
#define PROTO_RPC_CONTROLLER

#include <google/protobuf/service.h> // for RpcConteoller

#include <proto_rpc/namespace.hpp>

namespace proto_rpc {

class Controller : public gp::RpcController {
public:
  Controller() { Reset(); }

  virtual ~Controller() {}

  // failure info

  void Reset() {
    failed_ = false;
    error_text_.clear();
  }

  bool Failed() const { return failed_; }

  void SetFailed(const gp::string &reason) {
    failed_ = true;
    error_text_ = reason;
  }

  gp::string ErrorText() const { return error_text_; }

  // cancel operations (not supported)

  void StartCancel() {}

  bool IsCanceled() const { return false; }

  void NotifyOnCancel(gp::Closure *) {}

private:
  bool failed_;
  gp::string error_text_;
};
}

#endif // PROTO_RPC_CONTROLLER