#pragma once

#include "byte_stream.hh"
#include "debug.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <functional>

class Timer
{
public:
  explicit Timer( Wrap32 isn_ ) : isn( isn_ ), live_time( 0 ) {}

  void start( uint64_t RTO_ms_ )
  {

    RTO_ms = RTO_ms_;
    is_started = true;
    start_time = live_time;
  }

  bool have_started() const { return is_started; }

  void add_message( uint64_t seqno, const TCPSenderMessage& message, uint64_t RTO_ms_ )
  {
    message_[seqno] = message;
    init_RTO = RTO_ms_;
    if ( !is_started ) {
      start( RTO_ms_ );
    }
  }

  uint64_t remove_ack_msg( uint64_t ackno );

  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit, bool window_full );

  uint64_t get_num() const;

  uint64_t get_retransmission_count() const { return retransmission_count; }

private:
  std::map<uint64_t, TCPSenderMessage> message_ {};
  Wrap32 isn;
  uint64_t RTO_ms = 0;
  uint64_t retransmission_count = 0;
  bool is_started = false;
  uint64_t start_time = 0;
  uint64_t live_time;
  uint64_t init_RTO = 0;
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), timer_( isn )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // For testing: how many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  Timer timer_;
  uint16_t window_size_ = 1; // The size of the window
  uint64_t expect_ackno = 0; // The expected ackno
  bool send_FIN = false;     // Whether to send the last segment
  bool send_SYN = false;     // Whether to send the first segment
};
