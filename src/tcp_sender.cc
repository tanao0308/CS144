#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <algorithm>

using namespace std;

void TCPSender::push( const TransmitFunction& transmit )
{
  Reader& bytes_reader = input_.reader();
  const size_t window_size = wnd_size_ == 0 ? 1 : wnd_size_;
  // 不断组装并发送分组数据报，且在 FIN 发出后不再尝试组装报文
  for ( string payload {}; !sent_fin_; payload.clear() ) {
    // 从流中读取数据并组装报文，直到达到报文长度限制或窗口上限
    const uint64_t max_payload_size
      = min( TCPConfig::MAX_PAYLOAD_SIZE, window_size - ( num_bytes_in_flight_ + ( !sent_syn_ ) ) );
    read( bytes_reader, min( max_payload_size, bytes_reader.bytes_buffered() ), payload );
    // 判断 FIN 能否在此刻发出去
    if ( payload.length() + num_bytes_in_flight_ + ( !sent_syn_ ) + 1 <= window_size ) {
      sent_fin_ |= bytes_reader.is_finished();
    }
    // 组装 message
    auto msg = make_message( next_seqno_, move( payload ), !sent_syn_, sent_fin_ );
    if ( msg.sequence_length() == 0 ) {
      break;
    }
    outstanding_bytes_.push( msg );
    num_bytes_in_flight_ += msg.sequence_length();
    next_seqno_ += msg.sequence_length();
    sent_syn_ = true;
    transmit( msg );
    timer_.active();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message( next_seqno_, {}, false );
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  wnd_size_ = msg.window_size;
  if ( !msg.ackno.has_value() ) {
    if ( msg.window_size == 0 )
      input_.set_error();
    return;
  }
  // 对方所期待的下一个字节序号
  const uint64_t excepting_seqno = msg.ackno->unwrap( isn_, next_seqno_ );
  if ( excepting_seqno > next_seqno_ ) // 收到了没发出去的字节的确认
    return;                            // 不接受这个确认报文

  bool is_acknowledged = false; // 用于判断确认是否发生
  while ( !outstanding_bytes_.empty() ) {
    auto& buffered_msg = outstanding_bytes_.front();
    // 对方期待的下一字节不大于队首的字节序号，或者队首分组只有部分字节被确认
    const uint64_t final_seqno = acked_seqno_ + buffered_msg.sequence_length() - buffered_msg.SYN;
    if ( excepting_seqno <= acked_seqno_ || excepting_seqno < final_seqno ) {
      break; // 这种情况下不会更改缓冲队列
    }

    is_acknowledged = true; // 表示有字节被确认
    num_bytes_in_flight_ -= buffered_msg.sequence_length() - syn_flag_;
    acked_seqno_ += buffered_msg.sequence_length() - syn_flag_;
    // 最后检查 syn 是否被确认
    syn_flag_ = sent_syn_ ? syn_flag_ : excepting_seqno <= next_seqno_;
    outstanding_bytes_.pop();
  }

  if ( is_acknowledged ) {
    // 如果全部分组都被确认，那就停止计时器
    timer_.restart();
    if ( !outstanding_bytes_.empty() ) {
      timer_.active();
    }
    timer_.reset_retransmit();
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  timer_.tick( ms_since_last_tick );
  if ( timer_.is_expired() ) {
    transmit( outstanding_bytes_.front() ); // 只传递队首元素
    timer_.reset();
    if ( wnd_size_ != 0 )
      timer_.timeout();
    timer_.add_retransmit();
  }
}

TCPSenderMessage TCPSender::make_message( uint64_t seqno, string payload, bool SYN, bool FIN ) const
{
  return { .seqno = Wrap32::wrap( seqno, isn_ ),
           .SYN = SYN,
           .payload = move( payload ),
           .FIN = FIN,
           .RST = input_.reader().has_error() };
}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return num_bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return timer_.cnt_retransmit();
}
