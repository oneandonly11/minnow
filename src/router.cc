#include "router.hh"
#include "debug.hh"

#include <iostream>

using namespace std;

// route_prefix: The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
// prefix_length: For this route to be applicable, how many high-order (most-significant) bits of
//    the route_prefix will need to match the corresponding bits of the datagram's destination address?
// next_hop: The IP address of the next hop. Will be empty if the network is directly attached to the router (in
//    which case, the next hop address should be the datagram's final destination).
// interface_num: The index of the interface to send the datagram out on.
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: adding route " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(direct)" )
       << " on interface " << interface_num << "\n";

  // Check if the interface number is valid
  if ( interface_num >= interfaces_.size() ) {
    throw runtime_error( "Invalid interface number" );
  }
  forwarding_table_.push_back( { route_prefix, prefix_length, next_hop, interface_num } );
}

// Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
void Router::route()
{
  // For each interface
  for ( size_t i = 0; i < interfaces_.size(); ++i ) {
    auto& interface = interfaces_[i];

    // For each datagram in the interface's queue
    while ( !interface->datagrams_received().empty() ) {
      auto dgram = interface->datagrams_received().front();
      interface->datagrams_received().pop();

      // Check if the datagram's TTL is zero
      if ( dgram.header.ttl <= 1 ) {
        continue; // Drop the datagram if TTL is zero
      } else {
        dgram.header.ttl--;              // Decrement the TTL
        dgram.header.compute_checksum(); // Recompute the checksum
      }

      // Find the longest prefix match in the forwarding table
      optional<ForwardingRule> best_match;
      for ( const auto& rule : forwarding_table_ ) {
        if ( ( rule.route_prefix == 0
               || rule.route_prefix >> ( 32 - rule.prefix_length )
                    == dgram.header.dst >> ( 32 - rule.prefix_length ) )
             && ( best_match == nullopt || rule.prefix_length > best_match->prefix_length ) ) {
          best_match = rule;
        }
      }

      // If a match is found, send the datagram to the appropriate interface
      if ( best_match.has_value() ) {
        auto& next_interface = interfaces_[best_match->interface_num];
        if ( best_match->next_hop.has_value() ) {
          next_interface->send_datagram( dgram, best_match->next_hop.value() );
        } else {
          next_interface->send_datagram( dgram, Address::from_ipv4_numeric( dgram.header.dst ) );
        }
      }
    }
  }
}
