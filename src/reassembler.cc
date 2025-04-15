#include "reassembler.hh"
#include "debug.hh"

using namespace std;

void merge( std::map<uint64_t, std::string>& buffer_data )
{
  std::map<uint64_t, std::string>::iterator it = buffer_data.begin();
  while ( it != buffer_data.end() ) {
    auto next_it = std::next( it );
    if ( next_it != buffer_data.end() ) {
      if ( it->first + it->second.size() >= next_it->first ) {
        if ( it->first + it->second.size() < next_it->first + next_it->second.size() ) {
          it->second += next_it->second.substr( it->first + it->second.size() - next_it->first );
        }
        buffer_data.erase( next_it );
      } else {
        ++it;
      }
    } else {
      break;
    }
  }
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( is_last_substring ) {
    data_.last_index = first_index + data.size();
  }
  uint64_t available_capacity = output_.writer().available_capacity();
  if ( output_.writer().is_closed() || available_capacity == 0 ) {
    return; // Ignore data if the stream is already closed
  }

  if ( first_index <= data_.now_index ) {
    if ( first_index + data.size() <= data_.now_index ) {
      if ( is_last_substring ) {
        output_.writer().close();
        data_.now_index = 0;
        data_.buffer_data.clear();
      }
      return; // Ignore data that is completely before the current index
    }
    // If the first index is before the current index, we need to handle overlapping
    size_t unoverlap_start = data_.now_index - first_index;
    size_t unoverlap_end = min( data.size(), available_capacity + unoverlap_start );
    string unoverlap_data = data.substr( unoverlap_start, unoverlap_end );
    output_.writer().push( unoverlap_data );
    data_.now_index += unoverlap_data.size();

    for ( std::map<uint64_t, std::string>::iterator it = data_.buffer_data.begin();
          it != data_.buffer_data.end(); ) {
      if ( it->first + it->second.size() <= data_.now_index ) {
        it = data_.buffer_data.erase( it ); // Remove the data that has been written
      } else if ( it->first + it->second.size() > data_.now_index && it->first <= data_.now_index ) {
        // If the data is partially overlapping, we need to push the non-overlapping part
        unoverlap_data = it->second.substr( data_.now_index - it->first, it->second.size() );
        output_.writer().push( unoverlap_data );
        data_.now_index += unoverlap_data.size();
        it = data_.buffer_data.erase( it );
      } else {
        break;
      }
    }
    if ( data_.now_index == data_.last_index ) {
      output_.writer().close();
      data_.now_index = 0;
      data_.last_index = 0;
      data_.buffer_data.clear();
    }

  } else if ( first_index > data_.now_index ) {
    if ( first_index >= data_.now_index + available_capacity ) {
      return; // Ignore data that is completely after the current index
    }
    if ( data_.buffer_data.find( first_index ) != data_.buffer_data.end() ) {
      if ( data_.buffer_data[first_index].size() >= data.size() ) {
        return; // Ignore data that is already stored
      }
    }
    uint64_t end_index = min( data.size(), data_.now_index + available_capacity - first_index );
    string unoverlap_data = data.substr( 0, end_index );
    data_.buffer_data[first_index] = unoverlap_data;

    merge( data_.buffer_data );
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  // debug( "unimplemented count_bytes_pending() called" );
  // return {};
  uint64_t count = 0;
  for ( const auto& internal_data : data_.buffer_data ) {
    count += internal_data.second.size();
  }
  return count;
}
