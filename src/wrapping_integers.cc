#include "wrapping_integers.hh"
#include "debug.hh"
#include <stdlib.h>

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  return Wrap32{ static_cast<uint32_t>(n) } + zero_point.raw_value_;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  uint64_t diff = (raw_value_ + (1UL << 32) - zero_point.raw_value_) % (1UL << 32);
  uint64_t result = diff + (1UL << 32) * (checkpoint / (1UL << 32));
  if(checkpoint > result && (checkpoint - result) > (1UL << 31)){
      result += (1UL << 32);
  }
  else if(checkpoint < result && (result - checkpoint) > (1UL << 31)){
      if(result > (1UL << 32))
        result -= (1UL << 32);
  }
  return result;
}
