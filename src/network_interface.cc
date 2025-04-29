#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  // Check if the next hop is in the ARP cache
  auto it = arp_cache_.find( next_hop.ipv4_numeric() );
  if ( it != arp_cache_.end() ) {
    // If found, create an Ethernet frame and send it
    EthernetFrame frame;
    frame.header.dst = it->second.first;           // Destination MAC address from ARP cache
    frame.header.src = ethernet_address_;          // Source MAC address of this interface
    frame.header.type = EthernetHeader::TYPE_IPv4; // Type for IPv4

    // Serialize the datagram and add it to the frame payload
    frame.payload = serialize( dgram );

    // Transmit the frame
    transmit( frame );
  } else {

    auto it1 = arp_requests_.find( next_hop.ipv4_numeric() );
    if ( it1 != arp_requests_.end() ) {
      // If an ARP request has already been sent for this IP address, just return
      // debug("last_tick_ - it1->second = {}",last_tick_ - it1->second);
      if ( last_tick_ - it1->second < 5000 ) {
        // Store the datagram in a queue for later processing
        // auto it2 = datagrams_in_.begin();
        // for( ; it2 != datagrams_in_.end(); ++it2 ) {
        //   if ( it2->first.header.dst == dgram.header.dst ) {
        //     break; // Don't send another request if the last one was sent less than 5 seconds ago
        //   }
        // }
        // if( it2 == datagrams_in_.end() ) {
        // If the datagram is not already in the queue, add it
        // debug("Adding datagram to datagrams_in_");
        datagrams_in_.emplace_back( dgram, next_hop );
        // }
        return; // Don't send another request if the last one was sent less than 5 seconds ago
      }
    }

    ARPMessage arp_request;
    arp_request.opcode = ARPMessage::OPCODE_REQUEST;
    arp_request.sender_ethernet_address = ethernet_address_;
    arp_request.sender_ip_address = ip_address_.ipv4_numeric();
    arp_request.target_ip_address = next_hop.ipv4_numeric();
    arp_request.target_ethernet_address = EthernetAddress {}; // Set to zero for requests
    arp_request.target_ip_address = next_hop.ipv4_numeric();
    // Create an Ethernet frame for the ARP request
    EthernetFrame frame;
    frame.header.dst = ETHERNET_BROADCAST;        // Broadcast address for ARP requests
    frame.header.src = ethernet_address_;         // Source MAC address of this interface
    frame.header.type = EthernetHeader::TYPE_ARP; // Type for ARP
    // Serialize the ARP message and add it to the frame payload
    frame.payload = serialize( arp_request );

    // Store the datagram in a queue for later processing
    datagrams_in_.emplace_back( dgram, next_hop );

    // Send the ARP request
    transmit( frame );

    // Add the ARP request to the arp_requests_ map
    arp_requests_[next_hop.ipv4_numeric()] = last_tick_;
  }
}

//! \param[in] frame the incoming Ethernet frame
void NetworkInterface::recv_frame( EthernetFrame frame )
{
  if ( frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST ) {
    // If the destination address is not for this interface, ignore the frame
    return;
  }
  // Check the type of the frame
  if ( frame.header.type == EthernetHeader::TYPE_IPv4 ) {
    // If the type is IPv4, parse the datagram and push it to the datagrams_received queue
    InternetDatagram dgram;
    if ( parse( dgram, frame.payload ) ) {
      datagrams_received_.push( dgram );
    }
  } else if ( frame.header.type == EthernetHeader::TYPE_ARP ) {
    // If the type is ARP, parse the ARP message
    ARPMessage arp_message;
    if ( parse( arp_message, frame.payload ) ) {
      // Handle ARP request or reply

      // remember the sender's Ethernet address and IP address
      arp_cache_[arp_message.sender_ip_address]
        = std::pair<EthernetAddress, size_t>( arp_message.sender_ethernet_address, last_tick_ );

      if ( arp_requests_.find( arp_message.sender_ip_address ) != arp_requests_.end() ) {
        arp_requests_.erase( arp_message.sender_ip_address );
      }

      auto it = datagrams_in_.begin();
      while ( it != datagrams_in_.end() ) {
        if ( it->second.ipv4_numeric() == arp_message.sender_ip_address ) {
          // Send the datagram now that we have the MAC address
          send_datagram( it->first, it->second );
          it = datagrams_in_.erase( it ); // Remove from queue after sending
        } else {
          ++it;
        }
      }

      if ( arp_message.opcode == ARPMessage::OPCODE_REQUEST ) {
        // If it's a request, check if the target IP address matches this interface's IP address
        if ( arp_message.target_ip_address != ip_address_.ipv4_numeric() ) {
          // If not, ignore the request
          return;
        }
        // If it matches, send an ARP reply
        // If it's a request, send an ARP reply
        ARPMessage arp_reply;
        arp_reply.opcode = ARPMessage::OPCODE_REPLY;
        arp_reply.sender_ethernet_address = ethernet_address_;
        arp_reply.sender_ip_address = ip_address_.ipv4_numeric();
        arp_reply.target_ethernet_address = arp_message.sender_ethernet_address;
        arp_reply.target_ip_address = arp_message.sender_ip_address;

        // Create an Ethernet frame for the ARP reply
        EthernetFrame reply_frame;
        reply_frame.header.dst = arp_message.sender_ethernet_address; // Destination MAC address from request
        reply_frame.header.src = ethernet_address_;                   // Source MAC address of this interface
        reply_frame.header.type = EthernetHeader::TYPE_ARP;           // Type for ARP

        // Serialize the ARP message and add it to the frame payload
        reply_frame.payload = serialize( arp_reply );

        // Transmit the frame
        transmit( reply_frame );
      }
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  for ( auto it = arp_requests_.begin(); it != arp_requests_.end(); ) {
    if ( ms_since_last_tick + last_tick_ - it->second > 5000 ) {

      // Pending datagrams dropped when pending request expires
      auto it2 = datagrams_in_.begin();
      while ( it2 != datagrams_in_.end() ) {
        if ( it2->second.ipv4_numeric() == it->first ) {
          // Remove the datagram from the queue
          it2 = datagrams_in_.erase( it2 );
        } else {
          ++it2;
        }
      }

      // Remove the entry if it's older than 5 seconds
      it = arp_requests_.erase( it );
    } else {
      ++it;
    }
  }

  for ( auto it = arp_cache_.begin(); it != arp_cache_.end(); ) {
    if ( ms_since_last_tick + last_tick_ - it->second.second > 30000 ) {
      // Remove the entry if it's older than 5 seconds
      it = arp_cache_.erase( it );
    } else {
      ++it;
    }
  }
  last_tick_ += ms_since_last_tick;
}
