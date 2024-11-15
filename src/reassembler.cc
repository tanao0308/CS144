#include "reassembler.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  uint64_t index = first_index;
  if ( is_last_substring ) {
    last_index_ = first_index + data.length();
  }

  for ( char c : data ) {
    // 当前 index 在此范围内才能被加入
    if ( index < next_index_ || index >= next_index_ + output_.writer().available_capacity() ) {
      index++;
      continue;
    }
    internal_bytes[index] = c;
    index++;
  }

  index = next_index_;
  string msg;
  while ( internal_bytes.count( next_index_ ) ) {
    msg.push_back( internal_bytes[next_index_] );
    internal_bytes.erase( next_index_ );
    next_index_++;

    if ( next_index_ == last_index_ ) {
      break;
    }
  }
  writer().push( msg );
  if ( next_index_ == last_index_ ) {
    writer().close();
  }
}

uint64_t Reassembler::bytes_pending() const
{
  return internal_bytes.size();
}
