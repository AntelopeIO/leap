#pragma once
#include <fc/utility.hpp>
#include <fc/fwd.hpp>

#include <optional>
#include <string>

namespace fc
{
  int64_t  to_int64( const std::string& );
  uint64_t to_uint64( const std::string& );
  double   to_double( const std::string& );
  std::string to_string( double );
  std::string to_string( uint64_t );
  std::string to_string( int64_t );
  std::string to_string( uint16_t );
  std::string to_pretty_string( int64_t );
  inline std::string to_string( int32_t v ) { return to_string( int64_t(v) ); }
  inline std::string to_string( uint32_t v ){ return to_string( uint64_t(v) ); }
#ifdef __APPLE__
  inline std::string to_string( size_t s) { return to_string(uint64_t(s)); }
#endif

  typedef std::optional<std::string> ostring;
  class variant_object;
  std::string format_string( const std::string&, const variant_object&, bool minimize = false );
  std::string trim( const std::string& );
  std::string to_lower( const std::string& );
  std::string trim_and_normalize_spaces( const std::string& s );
}
