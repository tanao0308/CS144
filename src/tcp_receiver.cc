#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( message.RST ) {
    reassembler_.reader().set_error();
  }
  if ( message.SYN ) {
    zero_point_ = message.seqno;
  }
  if ( !zero_point_.has_value() ) {
    return;
  }

  uint64_t checkpoint_ = writer().bytes_pushed() + 1 + writer().is_closed();
  Wrap32 seqno = message.seqno;
  uint64_t absolute_seqno = seqno.unwrap( zero_point_.value(), checkpoint_ );
  uint64_t stream_index = ( message.SYN ? 0 : absolute_seqno - 1 );

  reassembler_.insert( stream_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  uint64_t checkpoint_ = writer().bytes_pushed() + 1 + writer().is_closed();
  TCPReceiverMessage msg;
  if ( zero_point_.has_value() ) {
    msg.ackno = Wrap32::wrap( checkpoint_, zero_point_.value() );
  }
  msg.window_size = min( 65535ul, writer().available_capacity() );
  if ( reassembler_.reader().has_error() || reassembler_.writer().has_error() ) {
    msg.RST = true;
  }
  return msg;
}
