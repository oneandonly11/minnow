#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ) {}

void Writer::push( string data )
{

  if ( data.empty() ) {
    return;
  }
  if ( closed_ ) {
    return;
  }

  if ( buffer_size_ + data.size() > capacity_ ) {
    data.resize( capacity_ - buffer_size_ );
  }
  buffer_ += data;
  buffer_size_ += data.size();
  bytes_pushed_ += data.size();
}

void Writer::close()
{
  closed_ = true;
  if ( buffer_size_ == 0 ) {
    finished_ = true;
  }
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_size_;
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  return string_view( buffer_ );
}

void Reader::pop( uint64_t len )
{
  if ( len > buffer_size_ ) {
    len = buffer_size_;
  }
  buffer_.erase( 0, len );
  buffer_size_ -= len;
  bytes_popped_ += len;

  if ( buffer_size_ == 0 && closed_ ) {
    finished_ = true;
  }
}

bool Reader::is_finished() const
{
  return finished_;
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_size_;
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
