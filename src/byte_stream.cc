#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity )
  : finish_write_( false ), tot_write_( 0 ), stream_( std::queue<char> {} ), capacity_( capacity )
{}

bool Writer::is_closed() const
{
  return finish_write_;
}

void Writer::push( string data )
{
  for ( char x : data ) {
    if ( stream_.size() == capacity_ ) {
      break;
    }
    stream_.push( x );
    tot_write_++;
  }
  return;
}

void Writer::close()
{
  finish_write_ = true;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - stream_.size();
}

uint64_t Writer::bytes_pushed() const
{
  return tot_write_;
}

bool Reader::is_finished() const
{
  return finish_write_ && stream_.size() == 0;
}

uint64_t Reader::bytes_popped() const
{
  return tot_write_ - stream_.size();
}

string_view Reader::peek() const
{
  if ( is_finished() || stream_.empty() ) {
    return string_view( "" );
  }
  string_view result( &stream_.front(), 1 );
  return result;
}

void Reader::pop( uint64_t len )
{
  while ( len-- ) {
    if ( stream_.empty() ) {
      break;
    }
    stream_.pop();
  }
}

uint64_t Reader::bytes_buffered() const
{
  return stream_.size();
}
