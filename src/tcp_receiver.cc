#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{

  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }
  if ( message.SYN ) {
    ISN = message.seqno.unwrap( Wrap32 { 0 }, 0 );
    ISN_received = true;
  }
  if ( !ISN_received ) {
    return;
  }
  uint64_t seqno = message.seqno.unwrap( Wrap32 { ISN }, reassembler_.writer().bytes_pushed() + 1 );
  if ( message.SYN )
    seqno++;

  reassembler_.insert( seqno - 1, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage message;
  if ( ISN_received ) {
    message.ackno = Wrap32 { ISN + static_cast<uint32_t>( reassembler_.writer().bytes_pushed() ) + 1 };
    if ( reassembler_.writer().is_closed() ) {
      message.ackno = message.ackno.value() + 1;
    }
  } else {
    message.ackno = std::nullopt;
  }
  message.window_size = reassembler_.writer().available_capacity() > UINT16_MAX
                          ? UINT16_MAX
                          : reassembler_.writer().available_capacity();
  message.RST = reassembler_.writer().has_error();
  return message;
}
