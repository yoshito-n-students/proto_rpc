#ifndef PROTO_RPC_MESSAGE_CODING
#define PROTO_RPC_MESSAGE_CODING

#include <iostream>
#include <utility> // for pair

#include <boost/asio/read_until.hpp> // for is_match_condition
#include <boost/asio/streambuf.hpp>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>

#include <proto_rpc/namespace.hpp>

namespace proto_rpc {

static inline void encode(const gp::Message &message, ba::streambuf &buffer) {
  // triple-wrapping of the buffer is required ...
  std::ostream os(&buffer);
  gp::io::OstreamOutputStream oos(&os);
  gp::io::CodedOutputStream output(&oos);

  // write the message length and then the message data to the buffer
  output.WriteVarint32(message.ByteSize());
  message.SerializeWithCachedSizes(&output);
}

class Decode {
public:
  Decode(gp::Message &message) : message_(message) {}

  virtual ~Decode() {}

  template < typename Iterator >
  std::pair< Iterator, bool > operator()(Iterator begin, Iterator end) const {
    // convert the given range to an input stream
    gp::io::CodedInputStream input(reinterpret_cast< const gp::uint8 * >(&(*begin)),
                                   static_cast< int >(end - begin));

    // read the message length
    gp::uint32 message_size;
    if (!input.ReadVarint32(&message_size)) {
      return std::pair< Iterator, bool >(begin, false);
    }

    // check if the buffer length is enough to be read message data
    {
      const void *data;
      int size;
      if (!input.GetDirectBufferPointer(&data, &size)) {
        size = 0;
      }
      if (static_cast< int >(message_size) > size) {
        return std::pair< Iterator, bool >(begin, false);
      }
    }

    // read the message data
    input.PushLimit(message_size);
    message_.ParsePartialFromCodedStream(&input);
    return std::pair< Iterator, bool >(begin + input.CurrentPosition(), true);
  }

private:
  gp::Message &message_;
};
}

// export Decode to boost.asio
namespace boost {
namespace asio {
template <> struct is_match_condition< proto_rpc::Decode > : public boost::true_type {};
}
}

#endif // PROTO_RPC_MESSAGE_CODING