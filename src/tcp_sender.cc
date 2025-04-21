#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

uint64_t Timer::get_num() const
{
  uint64_t sum = 0;
  for ( auto it = message_.begin(); it != message_.end(); ++it ) {
    sum += it->second.sequence_length();
  }
  return sum;
}

uint64_t Timer::remove_ack_msg( uint64_t ackno )
{
  auto it = message_.begin();
  while ( it != message_.end() ) {
    if ( it->first + it->second.sequence_length() <= ackno ) {
      it = message_.erase( it );
      retransmission_count = 0;
      RTO_ms = init_RTO;
      start_time = live_time;
    } else {
      return it->first + it->second.sequence_length();
    }
  }
  if ( message_.empty() ) {
    is_started = false;
    retransmission_count = 0;
    RTO_ms = init_RTO;
  }

  return ackno;
}

void Timer::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit, bool window_full )
{
  live_time += ms_since_last_tick;
  if ( !is_started ) {
    return;
  }
  if ( live_time - start_time >= RTO_ms ) {

    auto it = message_.begin();
    transmit( it->second );
    if ( !window_full ) {
      retransmission_count++;
      RTO_ms *= 2;
    }
    start( RTO_ms );
  }
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return timer_.get_num();
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return timer_.get_retransmission_count();
}

void TCPSender::push( const TransmitFunction& transmit )
{
  std::string_view input = input_.reader().peek();
  if ( input.empty() ) {
    if ( !send_SYN ) {
      TCPSenderMessage message;
      message.seqno = isn_.wrap( 0, isn_ );
      message.SYN = true;
      if ( input_.reader().is_finished() ) {
        message.FIN = true;
        send_FIN = true;
      }
      transmit( message );
      send_SYN = true;
      timer_.add_message( message.seqno.unwrap( isn_, 0 ), message, initial_RTO_ms_ );
      return;
    }
  }
  uint64_t sum = timer_.get_num();
  uint16_t window_size = window_size_;
  if ( window_size == 0 ) {
    window_size++;
  }
  while ( sum < window_size && !input.empty() ) {

    uint32_t len
      = min( min( input.size(), TCPConfig::MAX_PAYLOAD_SIZE ), static_cast<size_t>( window_size - sum ) );
    auto payload = static_cast<string>( input.substr( 0, len ) );
    uint64_t checkpoint = input_.reader().bytes_popped();
    TCPSenderMessage message;
    message.seqno = isn_.wrap( checkpoint + 1, isn_ );
    if ( !send_SYN ) {
      message.SYN = true;
      message.seqno = isn_.wrap( 0, isn_ );
      if ( len == window_size ) {
        len = len - 1;
        payload = static_cast<string>( input.substr( 0, len ) );
      }
      send_SYN = true;
    }
    input_.reader().pop( len );
    message.payload = payload;
    if ( input_.reader().is_finished() && window_size - payload.size() > 0 ) {
      message.FIN = true;
      send_FIN = true;
    }
    if ( input_.has_error() ) {
      message.RST = true;
    }

    transmit( message );
    timer_.add_message( message.seqno.unwrap( isn_, checkpoint ), message, initial_RTO_ms_ );
    sum += message.sequence_length();
    expect_ackno = checkpoint + 1;
    input = input_.reader().peek();
  }
  if ( input_.reader().is_finished() && ( !send_FIN ) && ( window_size > sum ) ) {

    TCPSenderMessage message;
    message.seqno = isn_.wrap( input_.reader().bytes_popped() + 1, isn_ );
    message.FIN = true;
    transmit( message );
    timer_.add_message( message.seqno.unwrap( isn_, input_.reader().bytes_popped() ), message, initial_RTO_ms_ );
    send_FIN = true;
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage message;
  uint64_t n = input_.reader().bytes_popped() + 1;

  if ( send_FIN )
    n++;
  message.seqno = isn_.wrap( n, isn_ );
  if ( input_.has_error() ) {
    message.RST = true;
  }
  return message;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    input_.reader().set_error();
    return;
  }
  window_size_ = msg.window_size;
  if ( msg.ackno ) {
    uint64_t ackno = msg.ackno->unwrap( isn_, expect_ackno );
    if ( ackno > input_.reader().bytes_popped() + 1 ) {
      if ( input_.reader().is_finished() && ackno == input_.reader().bytes_popped() + 2 ) {
        expect_ackno = timer_.remove_ack_msg( ackno );
      }
      return;
    }
    expect_ackno = timer_.remove_ack_msg( ackno );
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  timer_.tick( ms_since_last_tick, transmit, window_size_ == 0 );
}
