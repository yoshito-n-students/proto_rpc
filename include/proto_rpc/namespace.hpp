#ifndef PROTO_RPC_NAMESPACE
#define PROTO_RPC_NAMESPACE

namespace boost {
namespace asio {}
namespace posix_time {}
namespace system {}
}

namespace google {
namespace protobuf {}
}

namespace proto_rpc {
namespace ba = boost::asio;
namespace bp = boost::posix_time;
namespace bs = boost::system;
namespace gp = google::protobuf;
}

#endif // PROTO_RPC_NAMESPACE