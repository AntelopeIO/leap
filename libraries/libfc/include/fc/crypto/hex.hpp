#pragma once
#include <fc/utility.hpp>
#include <string>
#include <vector>

namespace fc {
    uint8_t from_hex( char c );
    std::string to_hex( const char* d, uint32_t s );
    std::string to_hex( const std::vector<char>& data );

    /**
     *  @return the number of bytes decoded
     */
    size_t from_hex( const std::string& hex_str, char* out_data, size_t out_data_len );
} 
